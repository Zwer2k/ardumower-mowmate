#include "ota_http_server.h"
#include "log.h"
#include <Update.h>
#include <AsyncJson.h>
#include <SPIFFS.h>
#include <esp_task_wdt.h>
#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <time.h>
#include "trust.h"
#include "git_version.h"

using namespace ArduMower::Modem::Ota;
using namespace std::placeholders;

volatile size_t ArduMower::Modem::Ota::otaFlashProgress = 0;
volatile size_t ArduMower::Modem::Ota::otaFlashTotal = 0;
volatile bool ArduMower::Modem::Ota::otaFlashForceSend = false;

const char *resultToString(Http::Result r);

static const char *GITHUB_HOST = "github.com";
static const char *GITHUB_API_HOST = "api.github.com";
static const char *GITHUB_LATEST_RELEASE_URL =
  "https://api.github.com/repos/Zwer2k/ardumower-mowmate/releases/latest";

// Der Hintergrund-Check läuft selten: eine Firmware erscheint nicht stündlich,
// und jeder Lauf kostet einen TLS-Handshake (~45KB Heap).
static const uint32_t GITHUB_CHECK_INTERVAL_MS = 6 * 60 * 60 * 1000UL;
// Nach einem Fehlversuch früher erneut probieren – direkt nach dem Boot steht
// oft weder DNS noch die Uhrzeit, ohne die kein Zertifikat validiert.
static const uint32_t GITHUB_CHECK_RETRY_MS = 15 * 60 * 1000UL;
// Abstand zwischen "Modem ist betriebsbereit" und dem ersten Lauf. Die Frist
// startet erst, wenn WiFi, Uhr und Heap stimmen – nie ab dem Bootzeitpunkt.
static const uint32_t GITHUB_CHECK_READY_DELAY_MS = 60 * 1000UL;
// Vor 2021-01-01 ist die Uhr offensichtlich nicht gestellt – dann scheitert
// jede Zertifikatsprüfung, der Versuch lohnt nicht.
static const time_t GITHUB_MIN_VALID_EPOCH = 1609459200;
// Bewusst sehr niedrig: Der Riegel soll nur verhindern, dass der automatische
// Check ein ohnehin taumelndes Gerät umstösst – wifi_health startet bei
// maxAlloc < 2048 neu. Für den Download läuft derselbe TLS-Stack ganz ohne
// Schranke, der Check darf also nicht strenger sein als das, was er ankündigt.
static const uint32_t GITHUB_CHECK_MIN_MAX_ALLOC = 20 * 1024UL;

HttpServer::HttpServer(Settings::Settings &settings, AsyncWebServer &server, MowerUpdater &mowerUpdater)
    : ArduMower::Modem::Http::Common(settings), _server(server), _mowerUpdater(mowerUpdater),
  _active(false), _failed(false), _restart(false), _restartTime(0), _flashSession(NULL),
  _githubUpdateActive(false), _githubUpdateSucceeded(false), _githubUpdateErrorLogged(false),
  _githubUpdateBuffered(false), _githubDownloadProgress(0), _githubDownloadTotal(0),
  _githubFlashProgress(0), _githubFlashTotal(0),
  _githubUpdateError{},
  _githubReachable(false), _githubCheckActive(false), _githubCheckDone(false),
  _githubCheckArmed(false),
  _githubCheckPublishPending(false), _githubUpdateAvailable(false), _githubNextCheckAt(0),
  _githubCheckError{}, _githubLatestVersion{} {}

void HttpServer::begin()
{
  auto uploadRequestHandler = std::bind(&HttpServer::handleUploadRequest, this, _1);
  auto uploadHandler = std::bind(&HttpServer::handleUpload, this, _1, _2, _3, _4, _5, _6);

  _server.on("/api/modem/ota/upload", HTTP_POST, uploadRequestHandler, uploadHandler);
  _server.on("/api/modem/ota/github", HTTP_POST, [this](AsyncWebServerRequest *request) {
    handleGithubUpdateRequest(request);
  });
  _server.on("/api/modem/ota/github/status", HTTP_GET, [this](AsyncWebServerRequest *request) {
    handleGithubUpdateStatus(request);
  });

  auto postRequestHandler = std::bind(&HttpServer::handlePostRequest, this, _1);
  auto bodyHandler = std::bind(&HttpServer::handleBody, this, _1, _2, _3, _4, _5);

  _server.on("/api/modem/ota/post", HTTP_POST, postRequestHandler, uploadHandler, bodyHandler);
}

static bool isValidReleaseVersion(const String &version)
{
  if (version.length() < 6 || version[0] != 'v') return false;

  int dots = 0;
  bool digitSinceSeparator = false;
  for (size_t i = 1; i < version.length(); i++)
  {
    const char c = version[i];
    if (c >= '0' && c <= '9')
    {
      digitSinceSeparator = true;
      continue;
    }
    if (c != '.' || !digitSinceSeparator || dots >= 2) return false;
    dots++;
    digitSinceSeparator = false;
  }
  return dots == 2 && digitSinceSeparator;
}

struct GithubUpdateContext
{
  HttpServer *server;
  String version;
};

void HttpServer::handleGithubUpdateRequest(AsyncWebServerRequest *request)
{
  if (!auth(request)) return;
  if (!request->hasParam("version"))
  {
    reject(request, 400, "github-update", "missing-version");
    return;
  }

  const String version = request->getParam("version")->value();
  Log(INFO, "Ota::HttpServer::github-update::request(version=%s)", version.c_str());
  if (!isValidReleaseVersion(version))
  {
    Log(WARN, "Ota::HttpServer::github-update::invalid-version(%s)", version.c_str());
    reject(request, 400, "github-update", "invalid-version");
    return;
  }
  if (_githubUpdateActive || _flashSession || _githubCheckActive)
  {
    Log(WARN, "Ota::HttpServer::github-update::already-active(github=%d upload=%d check=%d)",
        _githubUpdateActive, _flashSession != NULL, _githubCheckActive);
    reject(request, 409, "github-update", _githubCheckActive ? "check-active" : "update-active");
    return;
  }

  auto context = new GithubUpdateContext{this, version};
  _active = true;
  _githubUpdateActive = true;
  _githubUpdateSucceeded = false;
  _githubUpdateErrorLogged = false;
  _githubDownloadProgress = 0;
  _githubDownloadTotal = 0;
  _githubFlashProgress = 0;
  _githubFlashTotal = 0;
#ifdef CONFIG_IDF_TARGET_ESP32S3
  _githubUpdateBuffered = true;
#else
  _githubUpdateBuffered = false;
#endif
  _githubUpdateError[0] = '\0';
  otaFlashProgress = 0;
  otaFlashTotal = 0;

  if (xTaskCreate(githubUpdateTask, "github-ota", 8192, context, 1, NULL) != pdPASS)
  {
    delete context;
    _active = false;
    _githubUpdateActive = false;
    reject(request, 500, "github-update", "task-create-failed");
    return;
  }

  request->send(202, "application/json", "{\"success\":true,\"result\":\"started\"}");
}

void HttpServer::handleGithubUpdateStatus(AsyncWebServerRequest *request)
{
  if (!auth(request)) return;

  if (!_githubUpdateActive && _githubUpdateError[0] != '\0' && !_githubUpdateErrorLogged)
  {
    _githubUpdateErrorLogged = true;
    Log(ERR, "Ota::HttpServer::github-update::status(error=%s)", _githubUpdateError);
  }

  AsyncJsonResponse *response = new AsyncJsonResponse();
  JsonObject root = response->getRoot();
  root["active"] = _githubUpdateActive;
  root["success"] = _githubUpdateSucceeded;
  root["buffered"] = _githubUpdateBuffered;
  root["downloadProgress"] = _githubDownloadProgress;
  root["downloadTotal"] = _githubDownloadTotal;
  root["flashProgress"] = _githubFlashProgress;
  root["flashTotal"] = _githubFlashTotal;
  if (_githubUpdateError[0] != '\0') root["error"] = _githubUpdateError;
  response->setLength();
  request->send(response);
}

void HttpServer::githubUpdateTask(void *parameter)
{
  auto context = static_cast<GithubUpdateContext *>(parameter);
  context->server->runGithubUpdate(context->version);
  delete context;
  vTaskDelete(NULL);
}

static const char *firmwareTarget()
{
#ifdef CONFIG_IDF_TARGET_ESP32S3
  return "esp32-s3";
#else
  return "esp32";
#endif
}

// Nur stabile Versionen der Form vX.Y.Z. Alles andere (Vorabversionen,
// Datums-Tags) wird nicht zum Vergleich herangezogen.
static bool parseVersion(const char *version, int parts[3])
{
  if (!version) return false;
  if (*version == 'v' || *version == 'V') version++;

  for (int i = 0; i < 3; i++)
  {
    if (*version < '0' || *version > '9') return false;
    parts[i] = 0;
    while (*version >= '0' && *version <= '9')
    {
      parts[i] = parts[i] * 10 + (*version - '0');
      version++;
    }
    if (i < 2 && *version++ != '.') return false;
  }
  return *version == '\0';
}

static bool isNewerVersion(const char *candidate, const char *current)
{
  int left[3], right[3];
  if (!parseVersion(candidate, left) || !parseVersion(current, right)) return false;

  for (int i = 0; i < 3; i++)
  {
    if (left[i] != right[i]) return left[i] > right[i];
  }
  return false;
}

void HttpServer::publishFirmwareStatus(bool checking)
{
  if (!onFirmwareStatus) return;

  const FirmwareStatus status = {
    _githubReachable,
    checking,
    _githubUpdateAvailable,
    _githubCheckDone,
    git_tag[0] ? git_tag : NULL,
    _githubLatestVersion[0] ? _githubLatestVersion : NULL,
    _githubCheckError[0] ? _githubCheckError : NULL,
  };
  onFirmwareStatus(status);
}

void HttpServer::scheduleGithubCheck(uint32_t delayMs)
{
  _githubNextCheckAt = millis() + delayMs;
}

// Was einen Check gerade unmöglich macht, oder NULL wenn alles bereit ist.
// userRequested = jemand hat im Dialog auf "Check again" geklickt.
const char *HttpServer::githubCheckBlocker(bool userRequested) const
{
  if (WiFi.status() != WL_CONNECTED) return "wifi-disconnected";
  if (time(NULL) < GITHUB_MIN_VALID_EPOCH) return "clock-not-synced";
  // Während eines laufenden Updates keinen zweiten TLS-Client aufmachen.
  if (_githubUpdateActive || _flashSession) return "update-active";
  // Wer ausdrücklich prüfen lässt, bekommt keinen Heap-Riegel vorgesetzt: Der
  // Firmware-Download macht denselben Handshake völlig ungeprüft.
  if (!userRequested && ESP.getMaxAllocHeap() < GITHUB_CHECK_MIN_MAX_ALLOC) return "low-memory";
  return NULL;
}

// Startet den Check-Task, wenn gerade nichts dagegen spricht. Gibt false
// zurück, wenn nicht geprüft werden konnte – _githubCheckError sagt warum.
bool HttpServer::startGithubCheck(bool userRequested)
{
  if (_githubCheckActive) return true;

  const char *blocker = githubCheckBlocker(userRequested);
  if (blocker)
  {
    _githubReachable = false;
    snprintf(_githubCheckError, sizeof(_githubCheckError), "%s", blocker);
    Log(INFO, "Ota::HttpServer::github-check::skipped(%s free=%u max=%u)",
      blocker, (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMaxAllocHeap());
    return false;
  }

  _githubCheckActive = true;
  // TLS-Handshake und gefiltertes JSON-Parsen laufen im selben Task – 8KB Stack
  // sind dafür zu knapp bemessen.
  if (xTaskCreate(githubCheckTask, "github-check", 12288, this, 1, NULL) != pdPASS)
  {
    _githubCheckActive = false;
    _githubReachable = false;
    snprintf(_githubCheckError, sizeof(_githubCheckError), "check-task-create-failed");
    Log(ERR, "Ota::HttpServer::github-check::task-create-failed");
    return false;
  }

  // Auch ein manuell ausgelöster Lauf verschiebt den nächsten Hintergrund-Check.
  scheduleGithubCheck(GITHUB_CHECK_INTERVAL_MS);
  return true;
}

void HttpServer::requestFirmwareStatus(bool force)
{
  if (_githubCheckActive)
  {
    publishFirmwareStatus(true);
    return;
  }

  if (force && startGithubCheck(true))
  {
    publishFirmwareStatus(true);
    return;
  }

  publishFirmwareStatus(false);
}

// Läuft im loopTask: startet fällige Hintergrund-Checks und verschickt deren
// Ergebnis. Der Check-Task selbst fasst den WebSocket bewusst nicht an.
void HttpServer::loopGithubCheck()
{
  if (_githubCheckPublishPending)
  {
    _githubCheckPublishPending = false;
    publishFirmwareStatus(false);
  }

  if (_githubCheckActive) return;

  // Erster Durchlauf nach dem Boot: nur die Frist setzen, nie prüfen. Der
  // Konstruktor kann das nicht, dort ist millis() noch nichts wert.
  if (!_githubCheckArmed)
  {
    _githubCheckArmed = true;
    scheduleGithubCheck(GITHUB_CHECK_READY_DELAY_MS);
    return;
  }

  // Solange etwas blockiert, läuft die Wartezeit gar nicht erst los. Damit
  // hängt der erste Check am Betriebszustand des Modems und nicht am
  // Bootzeitpunkt – der Start selbst bleibt davon vollständig unberührt.
  const char *blocker = githubCheckBlocker(false);
  if (blocker)
  {
    scheduleGithubCheck(GITHUB_CHECK_READY_DELAY_MS);
    _githubReachable = false;
    // Nur bei Wechsel senden – diese Schleife läuft mit der Loop-Frequenz.
    if (strcmp(_githubCheckError, blocker) != 0)
    {
      snprintf(_githubCheckError, sizeof(_githubCheckError), "%s", blocker);
      publishFirmwareStatus(false);
    }
    return;
  }

  if ((int32_t)(millis() - _githubNextCheckAt) < 0) return;

  if (!startGithubCheck(false))
  {
    scheduleGithubCheck(GITHUB_CHECK_RETRY_MS);
    publishFirmwareStatus(false);
  }
}

void HttpServer::githubCheckTask(void *parameter)
{
  static_cast<HttpServer *>(parameter)->runGithubCheck();
  vTaskDelete(NULL);
}

void HttpServer::runGithubCheck()
{
  const char *error = NULL;
  char errorDetail[64] = {};
  char latest[sizeof(_githubLatestVersion)] = {};
  bool assetFound = false;

  IPAddress apiIp;
  if (WiFi.hostByName(GITHUB_API_HOST, apiIp) != 1)
  {
    error = "dns-failed";
    Log(WARN, "Ota::HttpServer::github-check::dns-failed(host=%s)", GITHUB_API_HOST);
  }

  HTTPClient http;
  WiFiClientSecure secureClient;
  bool httpBegun = false;

  if (!error)
  {
    secureClient.setCACert(tls_ca_trust);
    secureClient.setHandshakeTimeout(15);
    http.setConnectTimeout(15000);
    http.setTimeout(15000);
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);

    if (!http.begin(secureClient, GITHUB_LATEST_RELEASE_URL))
    {
      error = "api-init-failed";
    }
    else
    {
      httpBegun = true;
      http.addHeader("Accept", "application/vnd.github+json");
      // Die GitHub-API weist Anfragen ohne User-Agent ab.
      http.addHeader("User-Agent", "ardumower-mowmate");

      const int httpCode = http.GET();
      if (httpCode != HTTP_CODE_OK)
      {
        if (httpCode < 0)
        {
          const String httpError = HTTPClient::errorToString(httpCode);
          char tlsError[48] = {};
          const int tlsErrorCode = secureClient.lastError(tlsError, sizeof(tlsError));
          snprintf(errorDetail, sizeof(errorDetail), "api-transport-%d:%.16s tls=%d",
            httpCode, httpError.c_str(), tlsErrorCode);
          Log(WARN, "Ota::HttpServer::github-check::transport-error(code=%d message=%s tls=%d tls-message=%s)",
            httpCode, httpError.c_str(), tlsErrorCode, tlsError[0] ? tlsError : "none");
        }
        else
        {
          snprintf(errorDetail, sizeof(errorDetail), "api-http-%d", httpCode);
          Log(WARN, "Ota::HttpServer::github-check::http-error(status=%d)", httpCode);
        }
        error = errorDetail;
      }
    }
  }

  if (!error)
  {
    // Die Release-Antwort ist mehrere KB gross. Der Filter lässt ArduinoJson
    // nur die zwei Felder behalten, die uns interessieren – der Rest wird beim
    // Streamen verworfen und belegt keinen Heap.
    JsonDocument filter;
    filter["tag_name"] = true;
    filter["assets"][0]["name"] = true;

    JsonDocument doc;
    const DeserializationError jsonError =
      deserializeJson(doc, http.getStream(), DeserializationOption::Filter(filter));
    if (jsonError)
    {
      snprintf(errorDetail, sizeof(errorDetail), "api-parse-failed:%.24s", jsonError.c_str());
      error = errorDetail;
      Log(WARN, "Ota::HttpServer::github-check::parse-failed(%s)", jsonError.c_str());
    }
    else
    {
      const char *tag = doc["tag_name"] | "";
      snprintf(latest, sizeof(latest), "%s", tag);

      char assetName[32];
      snprintf(assetName, sizeof(assetName), "%s-firmware.bin", firmwareTarget());
      for (JsonObject asset : doc["assets"].as<JsonArray>())
      {
        if (strcmp(asset["name"] | "", assetName) == 0)
        {
          assetFound = true;
          break;
        }
      }

      if (latest[0] == '\0') error = "api-no-tag";
      else if (!assetFound) error = "no-firmware-asset";
    }
  }

  if (httpBegun) http.end();

  if (error)
  {
    snprintf(_githubCheckError, sizeof(_githubCheckError), "%s", error);
  }
  else
  {
    _githubCheckError[0] = '\0';
    snprintf(_githubLatestVersion, sizeof(_githubLatestVersion), "%s", latest);
  }

  // Ein fehlendes Asset heisst: GitHub war erreichbar, dieses Release taugt nur
  // nicht für dieses Board. Das ist kein Erreichbarkeitsproblem.
  _githubReachable = error == NULL || strcmp(error, "no-firmware-asset") == 0;
  _githubCheckDone = true;
  _githubUpdateAvailable = error == NULL && isNewerVersion(_githubLatestVersion, git_tag);
  _githubCheckActive = false;
  _githubCheckPublishPending = true;

  Log(INFO, "Ota::HttpServer::github-check::result(reachable=%d latest=%s current=%s update=%d error=%s free=%u)",
    _githubReachable, _githubLatestVersion[0] ? _githubLatestVersion : "none",
    git_tag[0] ? git_tag : "none", _githubUpdateAvailable,
    _githubCheckError[0] ? _githubCheckError : "none", (unsigned)ESP.getFreeHeap());
}

void HttpServer::runGithubUpdate(const String &version)
{
  static const size_t MAX_GITHUB_OTA_SIZE = 0x300000;
  const char *target = firmwareTarget();
  const String url = "https://github.com/Zwer2k/ardumower-mowmate/releases/download/" +
    version + "/" + target + "-firmware.bin";

  const time_t currentTime = time(NULL);
  Log(INFO, "Ota::HttpServer::github-update::start(version=%s target=%s epoch=%lld wifi=%d rssi=%d free=%u max=%u)",
      version.c_str(), target, (long long)currentTime, (int)WiFi.status(), WiFi.RSSI(),
      (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMaxAllocHeap());
  Log(DBG, "Ota::HttpServer::github-update::url(%s)", url.c_str());

  HTTPClient http;
  WiFiClientSecure secureClient;
  secureClient.setCACert(tls_ca_trust);
  secureClient.setHandshakeTimeout(15);
  http.setConnectTimeout(15000);
  http.setTimeout(15000);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);

  bool updateStarted = false;
  uint8_t *downloadBuffer = NULL;
  uint8_t *dramBuffer = NULL;
  const char *error = NULL;
  char errorDetail[128] = {};
  int httpCode = 0;

  IPAddress githubIp;
  if (WiFi.hostByName(GITHUB_HOST, githubIp) != 1)
  {
    error = "download-dns-failed:github.com";
    Log(ERR, "Ota::HttpServer::github-update::dns-failed(host=%s)", GITHUB_HOST);
  }
  else
  {
    Log(INFO, "Ota::HttpServer::github-update::dns(host=%s ip=%s)", GITHUB_HOST, githubIp.toString().c_str());
  }

  if (!error && !http.begin(secureClient, url))
  {
    error = "download-init-failed";
  }
  else if (!error)
  {
    Log(INFO, "Ota::HttpServer::github-update::http-get");
    httpCode = http.GET();
    if (httpCode != HTTP_CODE_OK)
    {
      if (httpCode < 0)
      {
        const String httpError = HTTPClient::errorToString(httpCode);
        char tlsError[96] = {};
        const int tlsErrorCode = secureClient.lastError(tlsError, sizeof(tlsError));
        snprintf(errorDetail, sizeof(errorDetail), "download-transport-%d:%.24s tls=%d:%.64s",
          httpCode, httpError.c_str(), tlsErrorCode, tlsError[0] ? tlsError : "none");
        Log(ERR, "Ota::HttpServer::github-update::transport-error(code=%d message=%s tls=%d tls-message=%s epoch=%lld)",
          httpCode, httpError.c_str(), tlsErrorCode, tlsError[0] ? tlsError : "none", (long long)time(NULL));
      }
      else
      {
        snprintf(errorDetail, sizeof(errorDetail), "download-http-%d", httpCode);
        Log(ERR, "Ota::HttpServer::github-update::http-error(status=%d location=%s)",
            httpCode, http.getLocation().c_str());
      }
      error = errorDetail;
    }
  }

  const int total = error ? 0 : http.getSize();
  if (!error)
  {
    Log(INFO, "Ota::HttpServer::github-update::response(status=%d size=%d)", httpCode, total);
  }
  if (!error && total <= 0)
  {
    error = "invalid-content-length";
  }
#ifdef CONFIG_IDF_TARGET_ESP32S3
  if (!error && (size_t)total > MAX_GITHUB_OTA_SIZE)
  {
    error = "firmware-too-large";
    Log(ERR, "Ota::HttpServer::github-update::firmware-too-large(size=%u max=%u)",
        (unsigned)total, (unsigned)MAX_GITHUB_OTA_SIZE);
  }
  if (!error)
  {
    Log(INFO, "Ota::HttpServer::github-update::psram-alloc(size=%u free=%u max=%u)",
        (unsigned)total, (unsigned)ESP.getFreePsram(), (unsigned)ESP.getMaxAllocPsram());
    downloadBuffer = (uint8_t *)ps_malloc((size_t)total);
    if (!downloadBuffer) error = "psram-allocation-failed";
  }
#else
  if (!error && !Update.begin((size_t)total))
  {
    error = "update-begin-failed";
  }
  else if (!error)
  {
    updateStarted = true;
  }
  if (!error)
  {
    dramBuffer = (uint8_t *)malloc(4096);
    if (!dramBuffer) error = "buffer-allocation-failed";
  }
#endif

  size_t downloaded = 0;
  unsigned long lastDataAt = millis();
  WiFiClient *stream = error ? NULL : http.getStreamPtr();
  _githubDownloadTotal = total;
  _githubFlashTotal = total;
  otaFlashProgress = 0;
  otaFlashTotal = total;

  while (!error && downloaded < (size_t)total)
  {
    const int available = stream->available();
    if (available <= 0)
    {
      if (!http.connected() || millis() - lastDataAt > 15000)
      {
        error = "download-interrupted";
        Log(ERR, "Ota::HttpServer::github-update::stream-interrupted(downloaded=%u total=%u connected=%d idle=%ums)",
          (unsigned)downloaded, (unsigned)total, http.connected(), (unsigned)(millis() - lastDataAt));
        break;
      }
      delay(1);
      continue;
    }

    const size_t requested = std::min(
      std::min((size_t)available, (size_t)4096),
      (size_t)total - downloaded);
    uint8_t *targetBuffer = downloadBuffer ? downloadBuffer + downloaded : dramBuffer;
    const size_t received = stream->readBytes(targetBuffer, requested);
    if (received == 0)
    {
      error = "download-read-failed";
      Log(ERR, "Ota::HttpServer::github-update::read-failed(downloaded=%u total=%u available=%d)",
          (unsigned)downloaded, (unsigned)total, available);
      break;
    }
    if (downloaded == 0 && targetBuffer[0] != 0xe9)
    {
      error = "invalid-firmware-header";
      Log(ERR, "Ota::HttpServer::github-update::invalid-header(first=0x%02x)", targetBuffer[0]);
      break;
    }
#ifndef CONFIG_IDF_TARGET_ESP32S3
    if (Update.write(targetBuffer, received) != received)
    {
      error = "update-write-failed";
      Log(ERR, "Ota::HttpServer::github-update::write-failed(written=%u chunk=%u error=%s)",
          (unsigned)downloaded, (unsigned)received, Update.errorString());
      break;
    }
#endif

    downloaded += received;
    _githubDownloadProgress = downloaded;
#ifndef CONFIG_IDF_TARGET_ESP32S3
    _githubFlashProgress = downloaded;
    otaFlashProgress = downloaded;
#endif
    lastDataAt = millis();
    yield();
  }

#ifdef CONFIG_IDF_TARGET_ESP32S3
  http.end();
  if (!error)
  {
    Log(INFO, "Ota::HttpServer::github-update::download-success(size=%u free=%u max=%u)",
        (unsigned)downloaded, (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMaxAllocHeap());
    dramBuffer = (uint8_t *)malloc(4096);
    if (!dramBuffer) error = "buffer-allocation-failed";
  }
  if (!error && !Update.begin((size_t)total))
  {
    error = "update-begin-failed";
  }
  else if (!error)
  {
    updateStarted = true;
  }

  size_t flashed = 0;
  while (!error && flashed < (size_t)total)
  {
    const size_t chunk = std::min((size_t)4096, (size_t)total - flashed);
    memcpy(dramBuffer, downloadBuffer + flashed, chunk);
    if (Update.write(dramBuffer, chunk) != chunk)
    {
      error = "update-write-failed";
      Log(ERR, "Ota::HttpServer::github-update::write-failed(written=%u chunk=%u error=%s)",
          (unsigned)flashed, (unsigned)chunk, Update.errorString());
      break;
    }
    flashed += chunk;
    _githubFlashProgress = flashed;
    otaFlashProgress = flashed;
    esp_task_wdt_reset();
    yield();
  }
#else
  const size_t flashed = downloaded;
#endif

  if (!error && !Update.end(true))
  {
    error = "update-end-failed";
    Log(ERR, "Ota::HttpServer::github-update::end-failed(error=%s)", Update.errorString());
  }
  if (error && updateStarted) Update.abort();
  if (dramBuffer) free(dramBuffer);
  if (downloadBuffer) free(downloadBuffer);
#ifndef CONFIG_IDF_TARGET_ESP32S3
  http.end();
#endif

  if (error)
  {
    snprintf(_githubUpdateError, sizeof(_githubUpdateError), "%s", error);
    Log(ERR, "Ota::HttpServer::github-update::%s", error);
  }
  else
  {
    otaFlashProgress = otaFlashTotal;
    otaFlashForceSend = true;
    _githubUpdateSucceeded = true;
    Log(INFO, "Ota::HttpServer::github-update::success(downloaded=%u flashed=%u)",
      (unsigned)downloaded, (unsigned)flashed);
  }

  _githubUpdateActive = false;
  _active = false;
  if (!error) requestRestart(2000);
}

void HttpServer::loop()
{
  loopFlash();
  loopGithubCheck();
  loopRestart();
}

void HttpServer::queueFlash(Http::ModemUploadSession *session)
{
  _flashSession = session;
}

void HttpServer::loopFlash()
{
  if (!_flashSession) return;

  auto s = _flashSession;

  if (s->isFlashPending())
  {
    if (!s->beginFlash())
    {
      delete s;
      _flashSession = NULL;
      Log(WARN, "Ota::HttpServer::loopFlash::begin-failed – restarting");
      requestRestart();
      return;
    }
  }

  if (s->flashProgress() >= s->flashTotal())
  {
    if (s->endFlash()) {
      if (onFlashProgress) onFlashProgress(s->flashTotal(), s->flashTotal());
      requestRestart();
    } else {
      Log(WARN, "Ota::HttpServer::loopFlash::end-failed – restarting");
      Update.abort();
      requestRestart();
    }
    delete s;
    _flashSession = NULL;
    return;
  }

  s->loopFlashWrite();
}

void HttpServer::handleUploadRequest(AsyncWebServerRequest *request)
{
  Http::UploadSession *session = (Http::UploadSession*)request->_tempObject;
  if (session == NULL)
  {
    Log(ERR, "Http::UploadSession::handleRequest::session-null");
    reject(request, 400, "upload", "unknown-request");
    return;
  }

  session->respond(request);
}

FirmwareUploadType HttpServer::getUploadType(AsyncWebServerRequest *request) {
  FirmwareUploadType uploadType = FirmwareUploadType::modem;
  if (request->hasParam("type")) {
    if (request->getParam("type")->value() == "mower") {
      uploadType = FirmwareUploadType::mower;
    }
  }

  Log(INFO, "Http::UploadSession::getUploadType type=%d", uploadType);

  return uploadType;
}

void HttpServer::handleUpload(AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final)
{
  if (index == 0) {
    FirmwareUploadType uploadType = getUploadType(request);
    if (uploadType == FirmwareUploadType::modem) {
      beginModemUpdate(request, index, data, len, final);
    } else {
      beginMowerUpdate(request, filename, index, data, len, final);
    }
  } else {
    continueUpdate(request, index, data, len, final);
  }
}

void HttpServer::handlePostRequest(AsyncWebServerRequest *request)
{
  Http::UploadSession *session = (Http::UploadSession *)request->_tempObject;
  if (session == NULL)
  {
    Log(ERR, "Http::UploadSession::handlePostRequest::session-null");
    reject(request, 400, "upload", "unknown-request");
    return;
  }

  session->respond(request);
}

void HttpServer::handleBody(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total)
{
  if (index == 0)
    beginModemUpdate(request, index, data, len, len == total);
  else
    continueUpdate(request, index, data, len, (index + len) == total);
}

void HttpServer::beginModemUpdate(AsyncWebServerRequest *request, size_t index, uint8_t *data, size_t len, bool final)
{
  if (!auth(request))
    return;

  auto session = new Http::ModemUploadSession(this);
  request->_tempObject = session;
  session->handle(index, data, len, final);
}

void HttpServer::beginMowerUpdate(AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final)
{
  if (!auth(request))
    return;

  auto session = new Http::MowerUploadSession(this, filename, _mowerUpdater);
  request->_tempObject = session;
  session->handle(index, data, len, final);
}

void HttpServer::continueUpdate(AsyncWebServerRequest *request, size_t index, uint8_t *data, size_t len, bool final)
{
  Http::UploadSession *session = (Http::UploadSession *)request->_tempObject;
  if (session == NULL)
    return;

  session->handle(index, data, len, final);
}

void HttpServer::requestRestart(uint32_t delayMs)
{
  _restartTime = millis() + delayMs;
  _restart = true;
}

void HttpServer::loopRestart()
{
  if (!_restart)
    return;

  if (millis() < _restartTime)
    return;

  ESP.restart();
}

// Http::ModemUploadSession

Http::ModemUploadSession::ModemUploadSession(HttpServer *_s)
  : s(_s), result(Result::PENDING), _buffer(NULL), _bufferPos(0), _streaming(false),
    _flashWritten(0), _dramBuf(NULL)
{
  _buffer = (uint8_t*)ps_malloc(MAX_OTA_SIZE);
  if (_buffer)
  {
    Log(INFO, "Ota::Http::ModemUploadSession::mode=buffered(size=%u)", MAX_OTA_SIZE);
  }
  else
  {
    Log(WARN, "Ota::Http::ModemUploadSession::mode=streaming (ps_malloc failed)");
    _streaming = true;
  }
}

Http::ModemUploadSession::~ModemUploadSession()
{
  if (_dramBuf) free(_dramBuf);
  if (_buffer) free(_buffer);
}

void Http::ModemUploadSession::handle(size_t index, uint8_t *data, size_t len, bool final)
{
  if (!(result == Result::PENDING || result == Result::STARTED))
    return;

  if (index == 0)
  {
    if (!verifyHeader(data, len))
    {
      result = Result::VERIFY_HEADER_FAILED;
      return;
    }

    if (_streaming)
    {
      if (!Update.begin(UPDATE_SIZE_UNKNOWN))
      {
        Log(ERR, "Ota::Http::ModemUploadSession::handle::update-begin-error(%s)", Update.errorString());
        result = Result::UPDATE_BEGIN_FAILED;
        return;
      }
      Log(INFO, "Ota::Http::ModemUploadSession::handle::streaming-begin");
    }
    else
    {
      Log(INFO, "Ota::Http::ModemUploadSession::handle::buffer-begin");
    }

    result = Result::STARTED;
  }

  if (_streaming)
  {
    if (Update.write(data, len) != len)
    {
      Log(ERR, "Ota::Http::ModemUploadSession::handle::write-error(%s)", Update.errorString());
      result = Result::SHORT_WRITE_ERROR;
      return;
    }
    esp_task_wdt_reset();

    if (!final) return;

    if (!Update.end(true))
    {
      Log(ERR, "Ota::Http::ModemUploadSession::handle::update-end-error(%s)", Update.errorString());
      result = Result::UPDATE_END_FAILED;
      return;
    }
    Log(INFO, "Ota::Http::ModemUploadSession::handle::streaming-end");
    result = Result::SUCCESS;
  }
  else
  {
    if (_bufferPos + len > MAX_OTA_SIZE)
    {
      Log(ERR, "Ota::Http::ModemUploadSession::handle::buffer-overflow(%u+%u > %u)", _bufferPos, len, MAX_OTA_SIZE);
      result = Result::ERROR;
      return;
    }

    memcpy(_buffer + _bufferPos, data, len);
    _bufferPos += len;

    if (!final) return;

    Log(INFO, "Ota::Http::ModemUploadSession::handle::buffer-end(size=%u)", _bufferPos);
    result = Result::FLASH_PENDING;
  }
}

void Http::ModemUploadSession::respond(AsyncWebServerRequest *request)
{
  auto res = new AsyncJsonResponse();
  auto o = res->getRoot();

  bool success = (result == Result::FLASH_PENDING || result == Result::SUCCESS);

  if (_streaming && result == Result::SUCCESS)
    o["md5"] = Update.md5String();

  o["success"] = success;
  o["result"] = resultToString(result);

  Log(INFO, "Ota::Http::ModemUploadSession::respond(%d / %s)", (int)result, resultToString(result));
  res->setLength();
  request->send(res);

  if (result == Result::FLASH_PENDING)
  {
    s->queueFlash(this);
    request->_tempObject = NULL;
  }
  else if (result == Result::SUCCESS)
  {
    s->requestRestart();
    request->_tempObject = NULL;
    delete this;
  }
  else {
    Log(WARN, "Ota::Http::ModemUploadSession::respond::error(%s) – restarting", resultToString(result));
    request->_tempObject = NULL;
    if (_buffer) free(_buffer);
    _buffer = NULL;
    s->requestRestart();
    delete this;
  }
}

bool Http::ModemUploadSession::beginFlash()
{
  Log(INFO, "Ota::Http::ModemUploadSession::flash-start(size=%u)", _bufferPos);

  if (!_buffer || _bufferPos == 0)
  {
    Log(ERR, "Ota::Http::ModemUploadSession::flash::no-data");
    return false;
  }

  _dramBuf = (uint8_t*)malloc(4096);
  if (!_dramBuf)
  {
    Log(ERR, "Ota::Http::ModemUploadSession::flash::dram-alloc-failed");
    return false;
  }

  if (!Update.begin(_bufferPos))
  {
    Log(ERR, "Ota::Http::ModemUploadSession::flash::update-begin-error(%s)", Update.errorString());
    free(_dramBuf);
    _dramBuf = NULL;
    return false;
  }

  _flashWritten = 0;
  otaFlashProgress = 0;
  otaFlashTotal = _bufferPos;
  result = Result::FLASHING;
  return true;
}

bool Http::ModemUploadSession::loopFlashWrite()
{
  if (_flashWritten >= _bufferPos) return true;
  if (!_buffer || !_dramBuf) return false;

  size_t chunk = _bufferPos - _flashWritten;
  if (chunk > 4096) chunk = 4096;

  memcpy(_dramBuf, _buffer + _flashWritten, chunk);

  auto n = Update.write(_dramBuf, chunk);
  if (n != chunk)
  {
    Log(ERR, "Ota::Http::ModemUploadSession::flash::write-error(%s)", Update.errorString());
    Update.abort();
    return false;
  }

  _flashWritten += chunk;
  otaFlashProgress = _flashWritten;
  esp_task_wdt_reset();
  yield();
  if (_flashWritten % (4096 * 10) < chunk)
    Log(INFO, "Ota::Http::ModemUploadSession::flash::progress(%u/%u)", _flashWritten, _bufferPos);
  return _flashWritten >= _bufferPos;
}

bool Http::ModemUploadSession::endFlash()
{
  if (!Update.end(true))
  {
    Log(ERR, "Ota::Http::ModemUploadSession::flash::update-end-error(%s)", Update.errorString());
    return false;
  }

  otaFlashProgress = otaFlashTotal;
  otaFlashForceSend = true;
  Log(INFO, "Ota::Http::ModemUploadSession::flash::success");
  return true;
}

bool Http::ModemUploadSession::verifyHeader(uint8_t *data, size_t len)
{
  if (len < 1)
  // return false;
  {
    Log(INFO, "Ota::Http::ModemUploadSession::verifyHeader::error::length");
    return false;
  }
  if (data[0] != 0xe9)
  // return false;
  {
    Log(INFO, "Ota::Http::ModemUploadSession::verifyHeader::error::magic");
    return false;
  }
  // -00000000  e9 06 02 2f f0 4a 08 40  ee 00 00 00 00 00 00 00  |.../.J.@........|
  // +00000000  e9 06 02 2f 04 48 08 40  ee 00 00 00 00 00 00 00  |.../.H.@........|

  // -00000010  00 00 00 00 00 00 00 01  20 00 40 3f e0 59 05 00  |........ .@?.Y..|
  // +00000010  00 00 00 00 00 00 00 01  20 00 40 3f 3c 5f 05 00  |........ .@?<_..|
  // +00000010  00 00 00 00 00 00 00 01  20 00 40 3f f0 52 03 00  |........ .@?.R..|

  // if (len < )
  return true;
}

static const char *result_success = "success";
static const char *result_started = "started";
static const char *result_flash_file = "flash_file";
static const char *result_incomplete = "incomplete";
static const char *result_error = "error";
static const char *result_index_mismatch = "index_mismatch";
static const char *result_update_begin_failed = "update_begin_failed";
static const char *result_verify_header_failed = "verify_header_failed";
static const char *result_short_write_error = "short_write_error";
static const char *result_update_end_failed = "update_end_failed";
static const char *result_flash_pending = "flash_pending";
static const char *result_flashing = "flashing";
static const char *result_unknown = "unknown";

const char *resultToString(Http::Result r)
{
  switch (r)
  {
  case Http::Result::SUCCESS:
    return result_success;

  case Http::Result::STARTED:
    return result_started;

  case Http::Result::FLASH_FILE:
    return result_flash_file;

  case Http::Result::INCOMPLETE:
    return result_incomplete;

  case Http::Result::ERROR:
    return result_error;

  case Http::Result::INDEX_MISMATCH:
    return result_index_mismatch;

  case Http::Result::UPDATE_BEGIN_FAILED:
    return result_update_begin_failed;

  case Http::Result::VERIFY_HEADER_FAILED:
    return result_verify_header_failed;

  case Http::Result::SHORT_WRITE_ERROR:
    return result_short_write_error;

  case Http::Result::UPDATE_END_FAILED:
    return result_update_end_failed;

  case Http::Result::FLASH_PENDING:
    return result_flash_pending;

  case Http::Result::FLASHING:
    return result_flashing;

  default:
    return result_unknown;
  }
}

// Http::MowerUploadSession

Http::MowerUploadSession::MowerUploadSession(HttpServer *server, String filename, MowerUpdater &mowerUpdater) : 
  _server(server), _filename(filename), _mowerUpdater(mowerUpdater), result(Result::PENDING), _index(0) 
{
  if(!SPIFFS.begin(true)){
    Log(ERR, "Http::MowerUploadSession::MowerUploadSession can't init SPIFFS");
    return;
  }

  if (!_filename.startsWith("/")) _filename = "/" + _filename;
}

void Http::MowerUploadSession::respond(AsyncWebServerRequest *request)
{
  auto res = new AsyncJsonResponse();
  auto o = res->getRoot();

  bool success = (result == Result::SUCCESS || result == Result::FLASH_FILE);

  o["success"] = success;
  o["result"] = resultToString(result);

  Log(INFO, "Ota::Http::MowerUploadSession::respond(%d / %s)", (int)result, resultToString(result));
  res->setLength();
  request->send(res);
  request->_tempObject = NULL;
}

void Http::MowerUploadSession::handle(size_t index, uint8_t *data, size_t len, bool final)
{
  if (!(result == Result::PENDING || result == Result::STARTED))
    return;
    
  if (_index != index)
  {
    Log(ERR, "Ota::Http::MowerUploadSession::handle::index-mismatch(expect=%u is=%u)", _index, index);
    result = Result::INDEX_MISMATCH;
    return;
  }

  if (index == 0)
  {
    handleListFiles();

    if (SPIFFS.exists(_filename)) {
      Log(DBG, "Ota::Http::MowerUploadSession::handle remove file");
      SPIFFS.remove(_filename);
    }

    fsUploadFile = SPIFFS.open(_filename, "w");
    
    if (!verifyHeader(data, len))
    {
      result = Result::VERIFY_HEADER_FAILED;
      return;
    }

    Log(DBG, "Ota::Http::MowerUploadSession::handle::upload-begin %s", _filename.c_str());
    result = Result::STARTED;
  }

  if (!fsUploadFile) 
  {
    Log(ERR, "Ota::Http::MowerUploadSession::handle::upload-write-error(no file handler)");
    result = Result::UPDATE_BEGIN_FAILED;
    return;
  }
  

  auto n = fsUploadFile.write(data, len);
  if (n != len)
  {
    Log(ERR, "Ota::Http::MowerUploadSession::handle::upload-write-error(len=%d written=%d)", len, n);
    result = Result::SHORT_WRITE_ERROR;
    return;
  }
  _index += len;

  if (!final)
    return;

  fsUploadFile.close();
  result = Result::FLASH_FILE;
  
  Log(DBG, "Ota::Http::MowerUploadSession::handle written %u", _index);
  
  _mowerUpdater.startUpdate(_filename, [this](String updateResult) {
    if (updateResult == "") {
      Log(INFO, "Ota::Http::MowerUploadSession::handle success");
    } else {
      Log(ERR, "Ota::Http::MowerUploadSession::handle faild with error %s", updateResult.c_str());
    }
    result = updateResult == "" ? Result::SUCCESS : Result::UPDATE_END_FAILED;
  });
}

bool Http::MowerUploadSession::verifyHeader(uint8_t *data, size_t len)
{
  if (len < 8)
  {
    Log(INFO, "Ota::Http::MowerUploadSession::verifyHeader::error::length (need at least 8 bytes, got %d)", len);
    return false;
  }
  
  // Check for STM32 firmware patterns:
  // 1. ARM Cortex-M vector table - first 4 bytes should be stack pointer (typically in RAM range)
  // 2. Second 4 bytes should be reset vector (typically in flash range)
  
  uint32_t stackPointer = (data[3] << 24) | (data[2] << 16) | (data[1] << 8) | data[0];
  uint32_t resetVector = (data[7] << 24) | (data[6] << 16) | (data[5] << 8) | data[4];
  
  // STM32 stack pointer should be in RAM range (typically 0x20000000 - 0x20040000 for most STM32)
  // Stack Pointer: 0x20000000–0x200FFFFF (STM32 RAM, bis 1MB)
  if (stackPointer < 0x20000000 || stackPointer > 0x200FFFFF) {
      Log(INFO, "Ota::Http::MowerUploadSession::verifyHeader::error::invalid-stack-pointer (0x%08x)", stackPointer);
      return false;
  }
  
  // Reset vector should be in flash range (typically 0x08000000+ for STM32) and should be odd (Thumb mode)
  // Reset Vector: 0x08000001–0x080FFFFF (STM32 Flash, Thumb Mode)
  if (resetVector < 0x08000001 || resetVector > 0x080FFFFF || (resetVector & 0x1) == 0) {
      Log(INFO, "Ota::Http::MowerUploadSession::verifyHeader::error::invalid-reset-vector (0x%08x)", resetVector);
      return false;
  }
  
  // Additional check: file should have reasonable size for STM32 firmware
  if (len > 0 && len < 1024)
  {
    Log(WARN, "Ota::Http::MowerUploadSession::verifyHeader::warning::small-file-size (%d bytes)", len);
    // Don't fail here, just warn - it might be the first chunk
  }
  
  Log(INFO, "Ota::Http::MowerUploadSession::verifyHeader::success (SP: 0x%08x, Reset: 0x%08x)", stackPointer, resetVector);
  return true;
}

void Http::MowerUploadSession::handleListFiles()
{
  String fileList = "File list: ";
  String Listcode;
  File dir = SPIFFS.open("/");
  File file = dir.openNextFile();
  while (file)
  {
    String fileName = file.name();
    File f = SPIFFS.open(("/" + fileName).c_str());
    String fileSize = String(f.size());
    fileList +=  " " + fileName + "   Size: " + fileSize;
    file = dir.openNextFile();
  }
  Log(INFO, fileList.c_str());
}

