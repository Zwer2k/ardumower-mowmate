#include "wifi_adapter.h"
#include "log.h"
#include "schedule.h"
#include <WiFi.h>
#include <WiFiUdp.h>
#include <inttypes.h>
#include <time.h>
#include <sys/time.h>
#include <esp_sntp.h>
#include <errno.h>

using namespace ArduMower::Modem::Wifi;

static const uint32_t staAbortTimeout = 1 * 60 * 1000;
static const uint32_t offStopTimeout = 5 * 60 * 1000;

static uint64_t ntpToUnixMicros(uint8_t buf[48])
{
  uint32_t seconds = ((uint32_t)buf[40] << 24) | ((uint32_t)buf[41] << 16) | ((uint32_t)buf[42] << 8) | (uint32_t)buf[43];
  uint32_t fraction = ((uint32_t)buf[44] << 24) | ((uint32_t)buf[45] << 16) | ((uint32_t)buf[46] << 8) | (uint32_t)buf[47];
  uint64_t micros = ((uint64_t)fraction * 1000000ULL) >> 32;
  const uint32_t NTP_UNIX_OFFSET = 2208988800UL;
  return ((uint64_t)(seconds - NTP_UNIX_OFFSET) * 1000000ULL) + micros;
}

static bool fetchNtp(const String &host, IPAddress &outIp, uint64_t &outMicros)
{
  Log(INFO, "WiFi::Adapter::STA::RTC NTP request(host=%s)", host.c_str());
  WiFiUDP udp;
  if (!udp.begin(12345)) {
    Log(WARN, "WiFi::Adapter::STA::RTC NTP udp-bind-failed(port=12345)");
    return false;
  }

  IPAddress ip;
  if (!WiFi.hostByName(host.c_str(), ip)) {
    Log(WARN, "WiFi::Adapter::STA::RTC NTP dns-failed(host=%s)", host.c_str());
    udp.stop();
    return false;
  }
  Log(INFO, "WiFi::Adapter::STA::RTC NTP resolved(host=%s ip=%s)",
      host.c_str(), ip.toString().c_str());

  uint8_t packet[48] = {0};
  packet[0] = 0xE3; // LI=3, VN=4, Mode=3 (client)
  if (udp.beginPacket(ip, 123) != 1 || udp.write(packet, 48) != 48 || udp.endPacket() != 1) {
    Log(WARN, "WiFi::Adapter::STA::RTC NTP send-failed(ip=%s)", ip.toString().c_str());
    udp.stop();
    return false;
  }

  unsigned long start = millis();
  while (millis() - start < 3000) {
    int len = udp.parsePacket();
    if (len >= 48) {
      uint8_t buf[48];
      udp.read(buf, 48);
      const uint8_t leap = buf[0] >> 6;
      const uint8_t version = (buf[0] >> 3) & 0x07;
      const uint8_t mode = buf[0] & 0x07;
      const uint8_t stratum = buf[1];
      outIp = ip;
      outMicros = ntpToUnixMicros(buf);
      Log(INFO, "WiFi::Adapter::STA::RTC NTP reply(ip=%s len=%d li=%u version=%u mode=%u stratum=%u epoch=%llu)",
          ip.toString().c_str(), len, leap, version, mode, stratum,
          (unsigned long long)(outMicros / 1000000ULL));
      udp.stop();
      return true;
    }
    delay(10);
  }

  Log(WARN, "WiFi::Adapter::STA::RTC NTP timeout(host=%s ip=%s timeout=3000ms)",
      host.c_str(), ip.toString().c_str());
  udp.stop();
  return false;
}

void Adapter::reconnect()
{
  if (_mode != Mode::STA)
    return;

  Log(INFO, "WiFi::Adapter::reconnect");
  WiFi.reconnect();
}

void Adapter::fullReconnect()
{
  Log(WARN, "WiFi::Adapter::fullReconnect");
  // disconnect(false,true) statt disconnect(true,true):
  // wifioff=true triggert WiFi-Deinit/Reinit, das auf ESP32-S3+OPI-PSRAM
  // fehlschlägt ("create wifi task: failed", esp_wifi_init 257 = ESP_ERR_NO_MEM)
  WiFi.disconnect(false, true);

  if (_settings.wifi.staSettingsValid()) {
    beginSta();
  }
}

void Adapter::begin()
{
  _mode = mode();

  switch (_mode)
  {
  case Mode::STA:
    beginSta();
    break;

  case Mode::AP:
    beginAp();
    break;

  default:
    beginOff();
    break;
  }
}

void Adapter::loop()
{
  switch (_mode)
  {
  case Mode::STA:
    loopSta();
    break;

  case Mode::AP:
    loopAp();
    break;

  default:
    loopOff();
    break;
  }
}

void Adapter::loopSta()
{
  static bool wasEverConnected = false;
  static bool wasConnected = false;
  static uint32_t disconnectedSince = 0;
  const bool connected = WiFi.isConnected();

  if (!wasEverConnected && !connected && millis() - _staTimeoutStart > staAbortTimeout)
  {
    Log(INFO, "WiFi::Adapter::loopSta::no-connection");

    stop();
    _mode = Mode::OFF;
    beginOff();
    return;
  }

  if (connected == wasConnected)
  {
    // Prolonged disconnection after initial connect – force reconnect
    if (!connected && wasEverConnected && disconnectedSince != 0 &&
        millis() - disconnectedSince > 15000)
    {
      Log(INFO, "WiFi::Adapter::loopSta::reconnecting (disconnected %ums)", millis() - disconnectedSince);
      WiFi.reconnect();
      disconnectedSince = millis();
    }

    // Retry NTP if not yet synced, or refresh once per hour to avoid drift.
    if (connected && (!_ntpSynced || millis() - _ntpLastAttempt >= 3600000UL))
    {
      trySyncTime();
    }
    return;
  }

  wasConnected = connected;

  if (connected)
  {
    wasEverConnected = true;
    disconnectedSince = 0;
    auto ip = WiFi.localIP().toString();
    Log(INFO, "WiFi::Adapter::STA::IP(%s)", ip.c_str());
    trySyncTime();
  }
  else
  {
    disconnectedSince = millis();
    _ntpSynced = false;
    _ntpAttempted = false;
    Log(INFO, "WiFi::Adapter::STA::disconnected");
  }
}

void Adapter::trySyncTime()
{
  if (_ntpSynced) return;

  // Don't hammer configTime: retry at most every 10 seconds.
  if (_ntpAttempted && millis() - _ntpLastAttempt < 10000) return;

  String tz = _settings.time.tz;
  if (tz.length() == 0) tz = "CET-1CEST,M3.5.0,M10.5.0/3";
  setenv("TZ", tz.c_str(), 1);
  tzset();

  String ntp1 = _settings.time.ntp_server1.length() > 0 ? _settings.time.ntp_server1 : "pool.ntp.org";
  String ntp2 = _settings.time.ntp_server2.length() > 0 ? _settings.time.ntp_server2 : "time.nist.gov";
  Log(INFO, "WiFi::Adapter::STA::RTC sync-start(epoch=%lld tz=%s primary=%s fallback=%s)",
      (long long)time(NULL), tz.c_str(), ntp1.c_str(), ntp2.c_str());

  // Keep persistent copies: esp_sntp_setservername only stores the pointer,
  // not the string content. If a temporary String is destroyed, the pointer
  // becomes dangling and lwip sends garbage server names.
  _ntpServer1 = ntp1;
  _ntpServer2 = ntp2;

  // Use IDF SNTP API. On some ESP32-S3 builds configTime/getLocalTime
  // do not sync reliably; explicit sntp init is more robust.
  if (esp_sntp_enabled()) {
    Log(DBG, "WiFi::Adapter::STA::NTP stopping existing sntp");
    esp_sntp_stop();
  }

  bool ok1 = false, ok2 = false;
  IPAddress ip1, ip2;
  uint64_t micros1 = 0, micros2 = 0;
  if (_ntpServer1.length() > 0) ok1 = fetchNtp(_ntpServer1, ip1, micros1);
  if (!ok1 && _ntpServer2.length() > 0) ok2 = fetchNtp(_ntpServer2, ip2, micros2);
  if (!ok1 && !ok2) {
    Log(WARN, "WiFi::Adapter::STA::RTC sync-failed(no-reachable-server)");
    _ntpAttempted = true;
    _ntpLastAttempt = millis();
    return;
  }

  IPAddress ip = ok1 ? ip1 : ip2;
  uint64_t micros = ok1 ? micros1 : micros2;
  struct timeval tv = { (time_t)(micros / 1000000ULL), (suseconds_t)(micros % 1000000ULL) };
  if (settimeofday(&tv, nullptr) != 0) {
    Log(ERR, "WiFi::Adapter::STA::RTC settimeofday-failed(errno=%d)", errno);
    _ntpAttempted = true;
    _ntpLastAttempt = millis();
    return;
  }

  _ntpSynced = true;
  _ntpAttempted = true;
  _ntpLastAttempt = millis();
  const time_t now = time(NULL);
  struct tm localTime;
  char localTimeText[32] = {};
  localtime_r(&now, &localTime);
  strftime(localTimeText, sizeof(localTimeText), "%Y-%m-%dT%H:%M:%S%z", &localTime);
  Log(INFO, "WiFi::Adapter::STA::RTC sync-success(server=%s ip=%s epoch=%lld local=%s)",
      ok1 ? _ntpServer1.c_str() : _ntpServer2.c_str(), ip.toString().c_str(),
      (long long)now, localTimeText);
  _schedule.computeNextRun();
}
void Adapter::loopAp()
{
}

void Adapter::loopOff()
{
  static bool stopped = false;
  static uint32_t stopFor = 0;

  if (stopFor != _offTimeoutStart)
  {
    stopped = false;
    stopFor = _offTimeoutStart;
  }

  if (stopped)
    return;

  if (millis() - _offTimeoutStart < offStopTimeout)
    return;

  stop();
  stopped = true;
  if (_settings.wifi.mode == Mode::STA && _settings.wifi.staSettingsValid())
  {
    Log(INFO, "WiFi::Adapter::loopOff::switch-to-sta");
    _mode = Mode::STA;
    beginSta();
    return;
  }

  Log(INFO, "WiFi::Adapter::loopOff::stop");
}

void Adapter::beginSta()
{
  Log(DBG, "WiFi::Adapter::beginSta");
  _staTimeoutStart = millis();
  WiFi.setHostname(_settings.general.name.c_str());
  WiFi.mode(WIFI_STA);

  if (_settings.wifi.sta_ip_mode == 1 && 
      _settings.wifi.sta_ip != "" && 
      _settings.wifi.sta_gateway != "" && 
      _settings.wifi.sta_subnet != "")
  {
    IPAddress ip, gateway, subnet, dns;
    if (ip.fromString(_settings.wifi.sta_ip.c_str()) && 
        gateway.fromString(_settings.wifi.sta_gateway.c_str()) && 
        subnet.fromString(_settings.wifi.sta_subnet.c_str()))
    {
      if (_settings.wifi.sta_dns != "" && dns.fromString(_settings.wifi.sta_dns.c_str()))
      {
        WiFi.config(ip, gateway, subnet, dns);
      }
      else
      {
        WiFi.config(ip, gateway, subnet);
      }
      Log(INFO, "WiFi::Adapter::STA::static-ip(%s)", _settings.wifi.sta_ip.c_str());
    }
    else
    {
      Log(WARN, "WiFi::Adapter::STA::invalid-static-ip");
    }
  }

  WiFi.begin(_settings.wifi.sta_ssid.c_str(), _settings.wifi.sta_psk.c_str());
  WiFi.setAutoReconnect(true);
}

void Adapter::beginAp()
{
  Log(DBG, "WiFi::Adapter::beginAp");
  WiFi.setHostname(_settings.general.name.c_str());
  WiFi.mode(WIFI_AP);
  if (_settings.wifi.apSettingsValid())
    WiFi.softAP(_settings.wifi.ap_ssid.c_str(), _settings.wifi.ap_psk.c_str());
  else
    WiFi.softAP(_settings.wifi.default_ap_ssid, _settings.wifi.default_ap_psk);

  auto ip = WiFi.softAPIP().toString();
  Log(INFO, "WiFi::Adapter::AP::IP(%s)", ip.c_str());
}

void Adapter::beginOff()
{
  Log(DBG, "WiFi::Adapter::beginOff");

  _offTimeoutStart = millis();
  beginAp();
}

void Adapter::stop()
{
  WiFi.disconnect(true, true);
  WiFi.mode(WIFI_OFF);
}

Mode Adapter::mode()
{
  if (_settings.wifi.mode == Mode::STA)
  {
    if (_settings.wifi.staSettingsValid())
      return Mode::STA;

    Log(DBG, "WiFi::Adapter::mode::sta::settings-invalid")
  }

  if (_settings.wifi.mode == Mode::AP)
    return Mode::AP;

  return Mode::OFF;
}
