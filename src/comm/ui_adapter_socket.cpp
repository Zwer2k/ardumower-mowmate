#include <functional>
#include <time.h>
#include "ui_adapter_socket.h"
#include "schedule.h"
#ifdef ENABLE_MAP
#include "path_planner.h"
#endif
#include "json.h"
#include "log.h"
#include "logToUi.h"
#include "terminal.h"

#define _LOG_ "UiSocket::"

uint32_t clientPingInterval = 10000;

// Upper bound for reassembling fragmented WebSocket text messages. Anything
// larger than this could not be processed on the ESP anyway (String::concat
// doubles the buffer while growing), so cap it instead of running out of heap.
static const size_t maxFrameBufferBytes = 64 * 1024;
uint32_t defaultVersionRequestInterval = 10000;
uint32_t defaultStateUpdateInterval = 5000;

using namespace ArduMower::Modem::Http;

// Strict RFC-3629 UTF-8 validation.  Rejects overlong encodings (C0/C1),
// surrogate halves (ED A0..BF), and code points > U+10FFFF (F4 90+).
// Invalid bytes are replaced with '?' so the resulting string is always
// valid UTF-8 — preventing browser-side "Could not decode a text frame
// as UTF-8" WebSocket disconnects.
static void sanitizeUtf8InPlace(String& str) {
  const size_t oldLen = str.length();
  // Fast path: serializeJson() output is plain ASCII in practically all cases.
  // Only allocate and rebuild the string if a control byte or a non-ASCII byte
  // is actually present (avoids one heap alloc + copy per WebSocket send).
  {
    const char *p = str.c_str();
    bool clean = true;
    for (size_t i = 0; i < oldLen; i++) {
      unsigned char c = (unsigned char)p[i];
      if (c >= 0x80 || (c < 0x20 && c != '\r' && c != '\n' && c != '\t')) { clean = false; break; }
    }
    if (clean) return;
  }
  String out;
  out.reserve(oldLen);
  for (size_t i = 0; i < oldLen; i++) {
    unsigned char c = (unsigned char)str[i];
    if (c < 0x20 && c != '\r' && c != '\n' && c != '\t') {
      out += '?';
      continue;
    }
    if (c < 0x80) {
      out += (char)c;
      continue;
    }
    size_t seqLen = 0;
    uint32_t cp = 0;
    bool valid = false;
    if (c >= 0xC2 && c <= 0xDF) {
      seqLen = 2; cp = c & 0x1F;
    } else if ((c & 0xF0) == 0xE0) {
      seqLen = 3; cp = c & 0x0F;
    } else if ((c & 0xF8) == 0xF0) {
      seqLen = 4; cp = c & 0x07;
    }
    if (seqLen == 0 || i + seqLen > oldLen) {
      out += '?';
      continue;
    }
    valid = true;
    for (size_t j = 1; j < seqLen; j++) {
      unsigned char b = (unsigned char)str[i + j];
      if ((b & 0xC0) != 0x80) { valid = false; break; }
      cp = (cp << 6) | (b & 0x3F);
    }
    // RFC 3629 rejects: overlong, surrogates, > U+10FFFF
    if (valid) {
      if (seqLen == 2 && cp < 0x80) valid = false;           // overlong
      else if (seqLen == 3 && cp < 0x800) valid = false;     // overlong
      else if (seqLen == 4 && cp < 0x10000) valid = false;   // overlong
      else if (cp >= 0xD800 && cp <= 0xDFFF) valid = false;  // surrogate
      else if (cp > 0x10FFFF) valid = false;                  // beyond Unicode
    }
    if (valid) {
      for (size_t j = 0; j < seqLen; j++) out += str[i + j];
      i += seqLen - 1;
    } else {
      out += '?';
    }
  }
  str = out;
}

// Cheap check for the frontend heartbeat frame `{"type":<requestPing>,...}`.
// Runs in the async_tcp task, so it must not allocate or parse JSON.
static bool isPingRequest(const char *data, size_t len) {
  char needle[24];
  int n = snprintf(needle, sizeof(needle), "\"type\":%d", (int)ArduMower::Modem::Http::RequestDataType::requestPing);
  if (n <= 0 || (size_t)n >= sizeof(needle) || len < (size_t)n) return false;
  for (size_t i = 0; i + (size_t)n <= len; i++) {
    if (memcmp(data + i, needle, (size_t)n) == 0) {
      // Reject longer numbers (e.g. "type":230 when looking for "type":23)
      const char next = (i + (size_t)n < len) ? data[i + n] : '\0';
      if (next < '0' || next > '9') return true;
    }
  }
  return false;
}

// Conservative heap guard: leave enough headroom so that the underlying TCP
// stack and ESPAsyncWebServer can safely allocate frame headers and queue
// nodes.  Queuing many messages when heap is low causes frame corruption
// (observed as "Invalid frame header" / 0x81 prefix bytes inside payloads).
static bool broadcastHeapOk(size_t payloadLen) {
  const size_t minHeap = payloadLen * 2 + 8192;
  if (ESP.getFreeHeap() < minHeap) {
    Log(WARN, "%s broadcast heap too low (%u free) for %u byte payload, skipping", _LOG_, ESP.getFreeHeap(), (unsigned)payloadLen);
    return false;
  }
  return true;
}

UiSocketItem::UiSocketItem(
  UiSocketHandler *socketHandler,
  uint32_t clientId,
  ArduMower::Domain::Robot::StateSource &source) 
  : _socketHandler(socketHandler), _clientId(clientId), _source(source) 
{
  _socketHandler->sendData(ResponseDataType::mowerState, this, true);
  yield();
  _socketHandler->sendData(ResponseDataType::desiredState, this, true);
  yield();
  _socketHandler->sendMapList(this);
  yield();
  _socketHandler->sendSchedule(this);
  yield();
  _socketHandler->sendClock(this);
  yield();
  _socketHandler->sendBufferedLogTo(this, 2);
  yield();
  _socketHandler->sendDrivenTrack(this);
  yield();
  if (_source.obstacles().timestamp > 0) {
    _socketHandler->sendData(ResponseDataType::obstacles, this, true);
  }
  // Defer map chunks until the TCP/WS queues have drained.  Sending a large
  // map snapshot immediately after connect causes frame corruption in
  // ESPAsyncWebServer (observed as 0x81 prefix bytes inside payloads).
  _socketHandler->_mapSendPendingUntil = millis() + UiSocketHandler::mapSendDelayMs;

#if defined(ENABLE_LIVE_MAP) || defined(ENABLE_GPS_DASHBOARD)
  // Send cached UBX data to new client if available
  if (_source.ubxResponse().timestamp > 0) {
    _socketHandler->sendData(ResponseDataType::ubxResponse, this, true);
  }
  if (_source.gpsDetails().timestamp > 0) {
    _socketHandler->sendData(ResponseDataType::gpsDetails, this, true);
  }
#endif
}

void UiSocketItem::handleData(RequestDataType dataType, JsonDocument &jsonData)
{
  Log(DBG, "%s handle data type %d", _LOG_, dataType);
  switch (dataType)
  {
  case RequestDataType::modemLogSettings:
    logToUi.modemLogLevel = jsonData["logLevel"];
    Log(INFO, "%s set modem log level to %d", _LOG_, logToUi.modemLogLevel);
    break;

  case RequestDataType::mowerConsoleRequest:
    _socketHandler->cmdToMower(jsonData["cmd"]);
    break;

#if defined(ENABLE_LIVE_MAP) || defined(ENABLE_GPS_DASHBOARD)
  case RequestDataType::requestGpsDetails:
    _gpsDetailsRefs++;
    _socketHandler->gpsDetailsRefCount++;
    _socketHandler->gpsDetailsActive = true;
    _socketHandler->ubxResponseActive = true;
    // Only reset polling timer on first activation (ref was 0)
    if (_socketHandler->gpsDetailsRefCount == 1) {
      _socketHandler->resetRequestTimestamp(ResponseDataType::gpsDetails);
      _socketHandler->ubxPollSequence = 0;
      _socketHandler->ubxConfigIndex = 0;
    }
    Log(INFO, "%s GPS details polling activated (ref=%d)", _LOG_, _socketHandler->gpsDetailsRefCount);
    break;

  case RequestDataType::stopGpsDetails:
    if (_gpsDetailsRefs > 0) _gpsDetailsRefs--;
    if (_socketHandler->gpsDetailsRefCount > 0) {
      _socketHandler->gpsDetailsRefCount--;
    }
    if (_socketHandler->gpsDetailsRefCount == 0) {
      _socketHandler->gpsDetailsActive = false;
      _socketHandler->ubxResponseActive = false;
    }
    Log(INFO, "%s GPS details polling deactivated (ref=%d)", _LOG_, _socketHandler->gpsDetailsRefCount);
    break;
#endif

  case RequestDataType::requestSensorSummary:
    _sensorSummaryRefs++;
    _socketHandler->sensorSummaryRefCount++;
    _socketHandler->sensorSummaryActive = true;
    // Only reset polling timer on first activation (ref was 0)
    if (_socketHandler->sensorSummaryRefCount == 1) {
      _socketHandler->resetRequestTimestamp(ResponseDataType::sensorSummary);
    }
    Log(INFO, "%s Sensor summary polling activated (ref=%d)", _LOG_, _socketHandler->sensorSummaryRefCount);
    break;

  case RequestDataType::stopSensorSummary:
    if (_sensorSummaryRefs > 0) _sensorSummaryRefs--;
    if (_socketHandler->sensorSummaryRefCount > 0) {
      _socketHandler->sensorSummaryRefCount--;
    }
    if (_socketHandler->sensorSummaryRefCount == 0) {
      _socketHandler->sensorSummaryActive = false;
    }
    Log(INFO, "%s Sensor summary polling deactivated (ref=%d)", _LOG_, _socketHandler->sensorSummaryRefCount);
    break;

#if defined(ENABLE_LIVE_MAP) || defined(ENABLE_GPS_DASHBOARD)
  case RequestDataType::requestUbx:
    {
      String hexCmd = jsonData["hex"] | "";
      if (hexCmd.length() > 0) {
        _socketHandler->sendUbx(hexCmd);
        _socketHandler->ubxResponseActive = true;
        _socketHandler->resetRequestTimestamp(ResponseDataType::ubxResponse);
        Log(INFO, "%s UBX command sent", _LOG_);
      }
    }
    break;
#endif

  case RequestDataType::joystickMove:
    {
      float linear = jsonData["linear"] | 0.0f;
      float angular = jsonData["angular"] | 0.0f;
      _socketHandler->joystickMove(linear, angular);
    }
    break;

  case RequestDataType::navigateTo:
    {
      float x = jsonData["x"] | 0.0f;
      float y = jsonData["y"] | 0.0f;
      _socketHandler->navigateTo(x, y);
    }
    break;

  case RequestDataType::uploadMap: {
    const String requestedMapId = jsonData["mapId"] | "";
    if (requestedMapId == _source.currentMapId() && requestedMapId.length() > 0) {
      _socketHandler->uploadMapToMower();
    } else {
      Log(WARN, "%s uploadMap: request for map %s rejected; current map is %s", _LOG_,
          requestedMapId.c_str(), _source.currentMapId().c_str());
    }
    break;
  }

  case RequestDataType::robotCommand:
    {
      String action = jsonData["action"] | "";
      if (action == "start")
        _socketHandler->cmdStart();
      else if (action == "stop")
        _socketHandler->cmdStop();
      else if (action == "dock")
        _socketHandler->cmdDock();
      else if (action == "skipWaypoint")
        _socketHandler->cmdSkipWaypoint();
      else if (action == "reboot")
        _socketHandler->cmdReboot();
      else if (action == "poweroff")
        _socketHandler->cmdPowerOff();
      else if (action == "mowerEnabled") {
        auto enabled = jsonData["enabled"];
        if (enabled.isNull())
          _socketHandler->cmdMowerAuto();
        else
          _socketHandler->cmdMowerEnabled(enabled.as<bool>());
      }
      else
        Log(WARN, "%s robotCommand: unknown action %s", _LOG_, action.c_str());
    }
    break;

   case RequestDataType::setMap:
    {
      using namespace ArduMower::Domain::Robot;
      const uint32_t syncId = jsonData["syncId"] | 0;
      const String requestedMapId = jsonData["mapId"] | "";
      MowerMap map;
      Log(DBG, "%s setMap: parsing perimeter, exclusions, dockpoints, search wire, waypoints", _LOG_);
      auto readDouble = [](JsonObject p, const char* key1, const char* key2) -> double {
        if (p[key1].is<JsonVariant>()) return p[key1];
        if (p[key2].is<JsonVariant>()) return p[key2];
        return 0.0;
      };
      auto readPoint = [readDouble](JsonObject p) -> MapPoint {
        MapPoint pt{readDouble(p, "X", "x"), -readDouble(p, "Y", "y")};
        if (p["delta"].is<JsonVariant>()) pt.delta = p["delta"];
        if (p["timestamp"].is<JsonVariant>()) pt.timestamp = p["timestamp"].as<String>();
        if (p["sol"].is<JsonVariant>()) pt.sol = p["sol"];
        if (p["tag"].is<JsonVariant>()) pt.tag = p["tag"];
        if (p["conn"].is<JsonVariant>()) pt.isConnector = p["conn"].as<bool>();
        return pt;
      };
      if (jsonData["dateTime"].is<JsonVariant>()) map.dateTime = jsonData["dateTime"].as<String>();
      if (jsonData["source"].is<JsonVariant>()) map.source = jsonData["source"].as<String>();
      JsonArray perimeter = jsonData["perimeter"];
      for (JsonObject p : perimeter) {
        map.perimeter.push_back(readPoint(p));
      }
      JsonArray exclusions = jsonData["exclusions"];
      for (JsonArray ex : exclusions) {
        std::vector<MapPoint> excl;
        for (JsonObject p : ex) {
          excl.push_back(readPoint(p));
        }
        map.exclusions.push_back(excl);
      }
      JsonArray dockpoints = jsonData["dockpoints"];
      for (JsonObject p : dockpoints) {
        map.dockpoints.push_back(readPoint(p));
      }
      JsonArray searchWire = jsonData["searchWire"];
      for (JsonObject p : searchWire) {
        map.searchWire.push_back(readPoint(p));
      }
      JsonArray waypoints = jsonData["waypoints"];
      for (JsonObject p : waypoints) {
        map.waypoints.push_back(readPoint(p));
      }
      map.rotation = jsonData["rotation"] | 0.0;
      const MowSettings settings = _source.mowSettings();
      map.pattern = settings.pattern;
      map.mowOfs = settings.width;
      map.patternAngle = settings.angle;
      map.distanceToBorder = settings.distanceToBorder;
      map.borderLaps = settings.borderLaps;
      map.mowBorderCcw = settings.mowBorderCcw;
      map.doMowArea = settings.doMowArea;
      map.doMowPerimeter = settings.doMowPerimeter;
      map.doMowBorder = settings.doMowBorder;
      map.doMowExclusions = settings.doMowExclusions;
      map.doMowExclusionBorder = settings.doMowExclusionBorder;
        Log(DBG, "%s setMap: parsed perimeter=%d exclusions=%d dockpoints=%d searchWire=%d waypoints=%d rotation=%.1f", _LOG_,
          map.perimeter.size(), map.exclusions.size(), map.dockpoints.size(), map.searchWire.size(), map.waypoints.size(), map.rotation);
      const bool mapIdMatches = requestedMapId.length() > 0 && requestedMapId == _source.currentMapId();
      const bool accepted = mapIdMatches && !_source.isMowerMapReading();
      if (accepted) {
        _socketHandler->setMap(map);
      } else if (!mapIdMatches) {
        Log(WARN, "%s setMap: sync %u for map %s rejected; current map is %s", _LOG_, syncId,
            requestedMapId.c_str(), _source.currentMapId().c_str());
      }
      _socketHandler->sendMapAck(this, syncId, accepted);
    }
    break;

   case RequestDataType::setMowSettings:
    {
      using namespace ArduMower::Domain::Robot;
      const String requestedMapId = jsonData["mapId"] | "";
      if (requestedMapId.length() == 0 || requestedMapId != _source.currentMapId()) {
        Log(WARN, "%s setMowSettings: request for map %s rejected; current map is %s", _LOG_,
            requestedMapId.c_str(), _source.currentMapId().c_str());
        break;
      }
      MowSettings s = _source.mowSettings();
      if (!jsonData["pattern"].isNull()) s.pattern = jsonData["pattern"];
      if (!jsonData["width"].isNull()) s.width = jsonData["width"];
      if (!jsonData["angle"].isNull()) s.angle = jsonData["angle"];
      if (!jsonData["distanceToBorder"].isNull()) s.distanceToBorder = jsonData["distanceToBorder"];
      if (!jsonData["borderLaps"].isNull()) s.borderLaps = jsonData["borderLaps"];
      if (!jsonData["mowBorderCcw"].isNull()) s.mowBorderCcw = jsonData["mowBorderCcw"];
      if (!jsonData["doMowArea"].isNull()) s.doMowArea = jsonData["doMowArea"];
      if (!jsonData["doMowPerimeter"].isNull()) s.doMowPerimeter = jsonData["doMowPerimeter"];
      if (!jsonData["doMowBorder"].isNull()) s.doMowBorder = jsonData["doMowBorder"];
      if (!jsonData["doMowExclusions"].isNull()) s.doMowExclusions = jsonData["doMowExclusions"];
      if (!jsonData["doMowExclusionBorder"].isNull()) s.doMowExclusionBorder = jsonData["doMowExclusionBorder"];
      Log(INFO, "%s setMowSettings received: pattern=%d width=%.2f angle=%d distToBorder=%d laps=%d doMowArea=%d doMowPerimeter=%d doMowBorder=%d doMowExclusions=%d doMowExclusionBorder=%d",
          _LOG_, s.pattern, s.width, s.angle, s.distanceToBorder, s.borderLaps,
          s.doMowArea, s.doMowPerimeter, s.doMowBorder, s.doMowExclusions, s.doMowExclusionBorder);
      _socketHandler->setMowSettings(s);
      _socketHandler->sendData(ResponseDataType::mowSettings, NULL, true);
    }
    break;

   case RequestDataType::requestMowSettings:
    _socketHandler->sendData(ResponseDataType::mowSettings, this, true);
    break;

   case RequestDataType::clearWaypoints:
    _socketHandler->clearWaypoints();
    break;

   case RequestDataType::calculateWaypoints:
    _socketHandler->calculateWaypoints();
    break;

   case RequestDataType::listMaps:
    _socketHandler->sendMapList(this);
    break;

   case RequestDataType::createMap: {
    String name = jsonData["name"] | "";
    if (_source.createMap(name)) {
      _socketHandler->abortMapChunkSend();
      yield();
      _socketHandler->sendData(ResponseDataType::mowSettings, NULL, true);
      _socketHandler->sendData(ResponseDataType::map, NULL, true);
      _socketHandler->sendMapList(NULL);
    }
    break;
  }

   case RequestDataType::copyMap: {
    String name = jsonData["name"] | "";
    if (_source.copyMap(name)) {
      _socketHandler->abortMapChunkSend();
      yield();
      _socketHandler->sendData(ResponseDataType::mowSettings, NULL, true);
      _socketHandler->sendData(ResponseDataType::map, NULL, true);
      _socketHandler->sendMapList(NULL);
    }
    break;
  }

   case RequestDataType::loadMap: {
    String id = jsonData["id"] | "";
    bool discardCurrent = jsonData["discardCurrent"] | false;
    bool readyToLoad = !discardCurrent || _source.discardMap();
    if (readyToLoad && _source.loadMap(id)) {
      _socketHandler->abortMapChunkSend();
      yield();
      _socketHandler->sendData(ResponseDataType::mowSettings, NULL, true);
      _socketHandler->sendData(ResponseDataType::map, NULL, true);
      _socketHandler->sendMapList(NULL);
    } else {
      // Die aktuelle Karte bei einem Ladefehler behalten. Die Map-Liste
      // liefert dem Frontend wieder die weiterhin autoritative currentId.
      _socketHandler->abortMapChunkSend();
      yield();
      _socketHandler->sendMapList(NULL);
    }
    break;
  }

   case RequestDataType::saveMap: {
    String name = jsonData["name"] | "";
    double rotation = jsonData["rotation"] | 0.0;
    // After a save the backend may send an updated map (hash changes). Abort
    // any ongoing chunk transfer so the new map snapshot can be sent cleanly.
    _socketHandler->abortMapChunkSend();
    yield();
    if (_source.saveMap(name, rotation).length() > 0) {
      _socketHandler->sendMapList(NULL);
    }
    break;
  }

   case RequestDataType::renameMap: {
    String id = jsonData["id"] | "";
    String name = jsonData["name"] | "";
    _socketHandler->abortMapChunkSend();
    yield();
    if (_source.renameMap(id, name)) {
      _socketHandler->sendMapList(NULL);
    }
    break;
  }

   case RequestDataType::deleteMap: {
    String id = jsonData["id"] | "";
    if (_source.deleteMap(id)) {
      _socketHandler->abortMapChunkSend();
      yield();
      _socketHandler->sendData(ResponseDataType::mowSettings, NULL, true);
      _socketHandler->sendData(ResponseDataType::map, NULL, true);
      _socketHandler->sendMapList(NULL);
    }
    break;
  }

   case RequestDataType::discardMap: {
    if (_source.discardMap()) {
      _socketHandler->abortMapChunkSend();
      yield();
      _socketHandler->sendData(ResponseDataType::mowSettings, NULL, true);
      _socketHandler->sendData(ResponseDataType::map, NULL, true);
      _socketHandler->sendMapList(NULL);
    }
    break;
  }

   case RequestDataType::setActiveMap: {
    String id = jsonData["id"] | "";
    if (_source.setActiveMap(id)) {
      _socketHandler->sendMapList(NULL);
    }
    break;
  }

   case RequestDataType::importMap: {
    String json = jsonData["json"] | "";
    String name = jsonData["name"] | "";
    double rotation = jsonData["rotation"] | 0.0;
    ArduMower::Domain::Robot::MowerMap imported;
    if (_source.importMowerMap(json, imported)) {
      imported.rotation = rotation;
      if (_source.createMap(name)) {
        _socketHandler->setMap(imported);
        _socketHandler->abortMapChunkSend();
        yield();
        _socketHandler->sendData(ResponseDataType::mowSettings, NULL, true);
        _socketHandler->sendData(ResponseDataType::map, NULL, true);
        _socketHandler->sendMapList(NULL);
      }
    }
    break;
  }

   case RequestDataType::exportMap: {
    ArduMower::Domain::Robot::MowerMap map = _source.mowerMap();
    String json = _source.exportMowerMap(map);
    if (json.length() > 0) {
      JsonDocument resp;
      resp["type"] = ResponseDataType::mowerMap;
      resp["data"]["json"] = json;
      String out;
      serializeJson(resp, out);
      this->sendText(out);
    }
    break;
  }

   case RequestDataType::setSchedule: {
    bool enabled = jsonData["enabled"] | true;
    JsonArray entriesArr = jsonData["entries"];
    std::vector<ArduMower::Modem::Schedule::Entry> entries;
    if (entriesArr) {
      for (JsonObject obj : entriesArr) {
        ArduMower::Modem::Schedule::Entry e;
        if (e.unmarshal(obj)) {
          // Assign IDs sequentially for new entries without ID.
          if (e.id == 0) e.id = entries.size() + 1;
          entries.push_back(e);
        }
      }
    }
    if (_socketHandler->setSchedule(enabled, entries)) {
      _socketHandler->sendSchedule(NULL);
    }
    break;
  }

   case RequestDataType::saveSchedule: {
    _socketHandler->saveSchedule();
    _socketHandler->sendSchedule(NULL);
    break;
  }

   case RequestDataType::requestSchedule: {
    _socketHandler->sendSchedule(this);
    break;
  }

   case RequestDataType::requestClock: {
    _socketHandler->sendClock(this);
    break;
  }

  case RequestDataType::requestPing: {
    char response[24];
    snprintf(response, sizeof(response), "{\"type\":%d}", (int)ResponseDataType::responsePong);
    sendTextRaw(response);
    break;
  }

  default:
    break;
  }
}

UiSocketItem::~UiSocketItem()
{
}

bool UiSocketItem::sendText(String text)
{
  if (_socketHandler->isClientReceivingChunk(_clientId))
    return false;

  return sendTextRaw(text);
}

bool UiSocketItem::sendTextRaw(String text)
{
  sanitizeUtf8InPlace(text);
  return _socketHandler->sendTextToId(_clientId, text.c_str(), text.length());
}

void UiSocketItem::ping()
{
  if (!_socketHandler->lockSendMutex())
    return;
  _socketHandler->_ws->ping(_clientId);
  _socketHandler->unlockSendMutex();
}

AwsClientStatus UiSocketItem::status()
{
  return _socketHandler->findClient(_clientId) ? WS_CONNECTED : WS_DISCONNECTED;
}


#ifdef MOWER_TERMINAL
UiSocketHandler::UiSocketHandler(
  Terminal &terminal,
  AsyncWebServer &server,
  ArduMower::Domain::Robot::StateSource &source,
  ArduMower::Domain::Robot::CommandExecutor &cmd,
  Ota::MowerUpdater &mowerUpdater,
  ArduMower::Modem::Schedule::Manager &scheduleManager
) 
  : _terminal(terminal), _server(server), _source(source), _cmd(cmd), _mowerUpdater(mowerUpdater), _scheduleManager(scheduleManager)
{
  _ws = new AsyncWebSocket("/ws");
  _sendMutex = xSemaphoreCreateMutex();
  _clientsMutex = xSemaphoreCreateMutex();
  _wsEvtMutex = xSemaphoreCreateMutex();
  _helloMutex = xSemaphoreCreateMutex();

  for (int i=0; i < ResponseDataType::responseDataTypeLength; i++) {
    oldDataTimestamp[i] = 0;
    lastDataRequestTimestamp[i] = defaultStateUpdateInterval;
    lastSentTimestamp[i] = 0;
  }

  _terminal.addRxHandler(std::bind(&UiSocketHandler::sendTerminalLine, this, std::placeholders::_1));
  _mowerUpdater.addStatusHandler(std::bind(&UiSocketHandler::uploadStatusHandler, this, std::placeholders::_1));
}

void UiSocketHandler::sendTerminalLine(String line) 
{
  auto message = TerminalMessage(line);
  this->sendData(ResponseDataType::mowerConsole, NULL, message, false);
}
#else
UiSocketHandler::UiSocketHandler(
  AsyncWebServer &server,
  ArduMower::Domain::Robot::StateSource &source,
  ArduMower::Domain::Robot::CommandExecutor &cmd,
  Ota::MowerUpdater &mowerUpdater,
  ArduMower::Modem::Schedule::Manager &scheduleManager
) 
  : _server(server), _source(source), _cmd(cmd), _mowerUpdater(mowerUpdater), _scheduleManager(scheduleManager)
{
  _ws = new AsyncWebSocket("/ws");
  _sendMutex = xSemaphoreCreateMutex();
  _clientsMutex = xSemaphoreCreateMutex();
  _wsEvtMutex = xSemaphoreCreateMutex();
  _helloMutex = xSemaphoreCreateMutex();

  for (int i=0; i < ResponseDataType::responseDataTypeLength; i++) {
    oldDataTimestamp[i] = 0;
    lastDataRequestTimestamp[i] = defaultStateUpdateInterval;
    lastSentTimestamp[i] = 0;
  }

  _mowerUpdater.addStatusHandler(std::bind(&UiSocketHandler::uploadStatusHandler, this, std::placeholders::_1));
}
#endif

void UiSocketHandler::uploadStatusHandler(byte progress) 
{
  auto message = Ota::StatusMessage(progress);
  this->sendData(ResponseDataType::mowerConsole, NULL, message, false);
}

UiSocketHandler::~UiSocketHandler() 
{   
  // Drain and free any remaining queued events
  if (_wsEvtMutex) {
    xSemaphoreTake(_wsEvtMutex, portMAX_DELAY);
    for (auto &e : _wsEvtQueue) { if (e.data) free(e.data); }
    _wsEvtQueue.clear();
    xSemaphoreGive(_wsEvtMutex);
  }
  if (_helloMutex) {
    xSemaphoreTake(_helloMutex, portMAX_DELAY);
    _pendingHellos.clear();
    xSemaphoreGive(_helloMutex);
  }
  vSemaphoreDelete(_wsEvtMutex);
  vSemaphoreDelete(_helloMutex);
  vSemaphoreDelete(_clientsMutex);
  if (_sendMutex != NULL) {
    vSemaphoreDelete(_sendMutex);
    _sendMutex = NULL;
  }
  delete _ws;
}

void UiSocketHandler::enqueueWsEvent(WsEvent &&evt) {
  if (xSemaphoreTake(_wsEvtMutex, pdMS_TO_TICKS(50)) != pdTRUE) {
    if (evt.data) free(evt.data);
    Log(WARN, "%s enqueueWsEvent: mutex timeout, event dropped", _LOG_);
    return;
  }
  if (_wsEvtQueue.size() < 64) {
    _wsEvtQueue.push_back(std::move(evt));
  } else {
    // Queue full: drop oldest non-critical events to make room
    while (!_wsEvtQueue.empty() && _wsEvtQueue.front().type == WsEvtType::DATA_CONTINUE) {
      if (_wsEvtQueue.front().data) free(_wsEvtQueue.front().data);
      _wsEvtQueue.pop_front();
    }
    // CONNECT/DISCONNECT must never be dropped: losing a DISCONNECT leaks the
    // UiSocketItem and its _frameBuffer entry forever, losing a CONNECT leaves
    // a client that the modem can never answer. Evict data frames instead —
    // a truncated request is recoverable, a lost lifecycle event is not.
    const bool critical = (evt.type == WsEvtType::CONNECT || evt.type == WsEvtType::DISCONNECT);
    if (critical) {
      for (auto it = _wsEvtQueue.begin(); it != _wsEvtQueue.end() && _wsEvtQueue.size() >= 64; ) {
        if (it->type == WsEvtType::CONNECT || it->type == WsEvtType::DISCONNECT) {
          ++it;
          continue;
        }
        if (it->data) free(it->data);
        it = _wsEvtQueue.erase(it);
      }
    }
    if (_wsEvtQueue.size() < 64 || critical) {
      _wsEvtQueue.push_back(std::move(evt));
    } else {
      if (evt.data) free(evt.data);
      Log(WARN, "%s enqueueWsEvent: queue full, event dropped", _LOG_);
    }
  }
  xSemaphoreGive(_wsEvtMutex);
}

// Thread-safe itemMap lookup.  Caller must NOT hold _clientsMutex when
// calling this — it acquires the lock internally.
UiSocketItem* UiSocketHandler::findClient(uint32_t clientId) {
  lockClients();
  auto it = itemMap.find(clientId);
  UiSocketItem *item = (it != itemMap.end()) ? it->second : nullptr;
  unlockClients();
  return item;
}

// Send text to a specific client using the library's thread-safe ID-based API.
// Handles heap guard, sendMutex, and activity marking.
bool UiSocketHandler::sendTextToId(uint32_t clientId, const char* data, size_t len) {
  size_t textLen = len;
  if (textLen > 512) {
    size_t minFree = textLen * 2 + 4096;
    if (ESP.getFreeHeap() < minFree) {
      Log(WARN, "%s sendTextToId heap too low (%u free) for %u byte payload, skipping", _LOG_, ESP.getFreeHeap(), (unsigned)textLen);
      return false;
    }
  }

  if (!lockSendMutex())
    return false;

  bool ok = _ws->text(clientId, data, len);
  if (ok) markClientActivity();
  unlockSendMutex();
  return ok;
}

// Drain the event queue and process all pending ws events in loopTask.
// This is called once per loop() iteration.
void UiSocketHandler::processWsEvents() {
  // Process pending hellos first (sent from CONNECT events).
  // The mutex is released while the (potentially slow) send runs, so track
  // ownership explicitly: giving a mutex that is currently held by ANOTHER
  // task would release that task's lock and corrupt the deque.
  bool helloHeld = (xSemaphoreTake(_helloMutex, 0) == pdTRUE);
  while (helloHeld && !_pendingHellos.empty()) {
    // Send hello using the library's locked API.
    // If a chunked map transfer is active, defer the hello to avoid
    // interleaving a text frame with fragmented frames (WebSocket spec
    // violation → "Invalid frame header" / "Could not decode a text frame").
    if (mapChunkSendState.active) break;

    auto hello = std::move(_pendingHellos.front());
    _pendingHellos.pop_front();
    xSemaphoreGive(_helloMutex);
    helloHeld = false;

    _ws->text(hello.clientId, hello.json.c_str(), hello.json.length());
    markClientActivity();

    helloHeld = (xSemaphoreTake(_helloMutex, 0) == pdTRUE);
  }
  if (helloHeld) xSemaphoreGive(_helloMutex);

  // Process deferred ws events
  if (xSemaphoreTake(_wsEvtMutex, 0) != pdTRUE) return;
  bool evtHeld = true;

  while (evtHeld && !_wsEvtQueue.empty()) {
    WsEvent evt = std::move(_wsEvtQueue.front());
    _wsEvtQueue.pop_front();
    xSemaphoreGive(_wsEvtMutex);
    evtHeld = false;

    switch (evt.type) {
      case WsEvtType::CONNECT: {
        uint32_t cid = evt.clientId;
        // Lock ordering: _clientsMutex must be released before _sendMutex
        // is acquired (by UiSocketItem ctor → sendTextToId). This prevents
        // a lock-ordering inversion with sendTextAllWithRetry which takes
        // _sendMutex first, then _clientsMutex. Without this, the async_tcp
        // task (terminal handler) and loopTask can deadlock, causing silent
        // send failures during the connect burst — the browser never receives
        // initialization data, leading to WebSocket protocol errors.
        UiSocketItem *item = nullptr;
        {
          lockClients();
          auto it = itemMap.find(cid);
          if (it != itemMap.end()) {
            delete it->second;
            itemMap.erase(it);
          }
          _frameBuffer.erase(cid);
          unlockClients();
        }
        // Create UiSocketItem OUTSIDE the lock — its constructor sends data
        // which takes _sendMutex, and holding _clientsMutex during that
        // would invert lock ordering with sendTextAllWithRetry.
        item = new UiSocketItem(this, cid, _source);
        lockClients();
        itemMap[cid] = item;
        unlockClients();
        break;
      }

      case WsEvtType::DISCONNECT: {
        uint32_t cid = evt.clientId;
        bool abortTargetedTransfer = false;
        lockClients();
        _frameBuffer.erase(cid);
        auto it = itemMap.find(cid);
        if (it != itemMap.end()) {
          // A transfer addressed to this client cannot complete any more.
          // Previously it was silently turned into a broadcast, which fed
          // the other clients a partial transfer. Abort it instead — after
          // the lock is released, because finishing flushes broadcasts.
          if (mapChunkSendState.active && mapChunkSendState.clientId == cid)
            abortTargetedTransfer = true;
          // Release this client's share of the polling subscriptions. A tab
          // that is closed never sends stop*, and the counters were only
          // reset once the LAST client left — so a single remaining client
          // kept GPS/sensor polling running forever.
          UiSocketItem *gone = it->second;
#if defined(ENABLE_LIVE_MAP) || defined(ENABLE_GPS_DASHBOARD)
          if (gone->_gpsDetailsRefs > 0) {
            gpsDetailsRefCount = (gpsDetailsRefCount > gone->_gpsDetailsRefs) ? gpsDetailsRefCount - gone->_gpsDetailsRefs : 0;
            if (gpsDetailsRefCount == 0) {
              gpsDetailsActive = false;
              ubxResponseActive = false;
            }
          }
#endif
          if (gone->_sensorSummaryRefs > 0) {
            sensorSummaryRefCount = (sensorSummaryRefCount > gone->_sensorSummaryRefs) ? sensorSummaryRefCount - gone->_sensorSummaryRefs : 0;
            if (sensorSummaryRefCount == 0) sensorSummaryActive = false;
          }
          delete gone;
          itemMap.erase(it);
          if (itemMap.empty()) {
#if defined(ENABLE_LIVE_MAP) || defined(ENABLE_GPS_DASHBOARD)
            gpsDetailsRefCount = 0;
            gpsDetailsActive = false;
            ubxResponseActive = false;
#endif
            sensorSummaryRefCount = 0;
            sensorSummaryActive = false;
            Log(INFO, "%s last client disconnected, resetting all ref counts", _LOG_);
          }
        }
        unlockClients();
        if (abortTargetedTransfer) {
          Log(DBG, "%s client %u disconnected during targeted map transfer, aborting", _LOG_, cid);
          finishMapChunkSend();
        }
        if (evt.data) free(evt.data);
        break;
      }

      case WsEvtType::DATA_CONTINUE: {
        uint32_t cid = evt.clientId;
        if (evt.data && evt.len > 0) {
          lockClients();
          auto it = _frameBuffer.find(cid);
          if (it != _frameBuffer.end()) {
            // Bound the reassembly buffer: a client that never sends the final
            // fragment (or a malicious one) would otherwise grow it until the
            // heap is exhausted. concat() returning false means the allocation
            // failed — drop the partial message instead of keeping a truncated
            // one around.
            if (it->second.length() + evt.len > maxFrameBufferBytes) {
              Log(WARN, "%s frame buffer overflow for client %u (%u bytes), dropping message",
                  _LOG_, cid, (unsigned)(it->second.length() + evt.len));
              _frameBuffer.erase(it);
            } else if (!it->second.concat((const char*)evt.data, evt.len)) {
              Log(WARN, "%s frame buffer alloc failed for client %u, dropping message", _LOG_, cid);
              _frameBuffer.erase(it);
            }
          }
          unlockClients();
        }
        if (evt.data) free(evt.data);
        break;
      }

      case WsEvtType::DATA_START: {
        uint32_t cid = evt.clientId;
        AwsFrameInfo *info = (AwsFrameInfo*)&evt.info;
        if (info->message_opcode == WS_TEXT) {
          String frag((const char*)evt.data, evt.len);
          lockClients();
          _frameBuffer[cid] = std::move(frag);
          unlockClients();
        }
        if (evt.data) free(evt.data);
        break;
      }

      case WsEvtType::DATA_FINAL: {
        uint32_t cid = evt.clientId;
        AwsFrameInfo *info = (AwsFrameInfo*)&evt.info;
        if (info->message_opcode == WS_TEXT) {
          if (info->index == 0 && info->len == (size_t)evt.len) {
            // Single complete frame
            char *buf = (char*)evt.data;
            if (buf) buf[evt.len] = '\0';
            handleData(cid, buf);
          } else {
            // Final fragment of multi-frame message:
            // Extract accumulated data under lock, then process outside lock.
            String payload;
            lockClients();
            auto it = _frameBuffer.find(cid);
            if (it != _frameBuffer.end()) {
              if (evt.data && evt.len > 0) it->second.concat((const char*)evt.data, evt.len);
              payload = std::move(it->second);
              _frameBuffer.erase(it);
            }
            unlockClients();
            if (payload.length() > 0) {
              handleData(cid, (char*)payload.c_str());
            }
          }
        }
        if (evt.data) free(evt.data);
        break;
      }
    }

    evtHeld = (xSemaphoreTake(_wsEvtMutex, 0) == pdTRUE);
  }

  // Only release the mutex if this task still owns it.
  if (evtHeld) xSemaphoreGive(_wsEvtMutex);
}

void UiSocketHandler::begin()
{
  _ws->onEvent(std::bind(&ArduMower::Modem::Http::UiSocketHandler::wsEvent, this, std::placeholders::_1, 
    std::placeholders::_2, std::placeholders::_3, std::placeholders::_4, std::placeholders::_5, std::placeholders::_6));
  _server.addHandler(_ws);

  // HTTP endpoint for CSV log export
  _server.on("/api/log/export", HTTP_GET, [this](AsyncWebServerRequest *request) {
    String csv;
    logToUi.exportAll(csv);
    
    // Build filename with current date/time or fallback to millis
    char filename[64];
    time_t now = time(nullptr);
    if (now > 1609459200) { // After 2021-01-01, assume valid time
      struct tm* ti = localtime(&now);
      snprintf(filename, sizeof(filename), 
        "ardumower-log-%04d-%02d-%02d-%02d-%02d-%02d.csv",
        ti->tm_year + 1900, ti->tm_mon + 1, ti->tm_mday,
        ti->tm_hour, ti->tm_min, ti->tm_sec);
    } else {
      snprintf(filename, sizeof(filename), "ardumower-log-%lu.csv", millis());
    }
    
    AsyncWebServerResponse *response = request->beginResponse(200, "text/csv; charset=utf-8", csv);
    response->addHeader("Content-Disposition", String("attachment; filename=\"") + filename + "\"");
    request->send(response);
  });
}

void UiSocketHandler::loop()
{
  // Process deferred ws events from async_tcp task FIRST.
  // This moves all itemMap / handleData work into loopTask.
  processWsEvents();

  _ws->cleanupClients();

  // Kartenliste bei Änderungen broadcasten, sobald mindestens ein Client verbunden ist
  if (_source.mapListDirty() && countConnectedClients() > 0) {
    _source.clearMapListDirty();
    sendMapList(NULL);
  }

  if (_scheduleManager.dirty() && _scheduleDirty && countConnectedClients() > 0) {
    _scheduleDirty = false;
    sendSchedule(NULL);
  }

  // Send current time to all connected clients once per minute.
  {
    static unsigned long lastClockSend = 0;
    if (millis() - lastClockSend >= 60000) {
      lastClockSend = millis();
      sendClock(NULL);
    }
  }

  if (countConnectedClients() == 0)
    return;

  // Start the deferred initial map chunk send once the connect burst has had
  // time to drain.  _mapSendPendingUntil is armed in the CONNECT path.
  // If a transfer is still running when the deadline passes, keep the send
  // pending instead of dropping it: startMapChunkSend() returns silently
  // while active, and a client that connected during another client's
  // transfer would otherwise never receive the map.
  if (_mapSendPendingUntil != 0 && (int32_t)(millis() - _mapSendPendingUntil) >= 0) {
    if (!mapChunkSendState.active) {
      _mapSendPendingUntil = 0;
      sendData(ResponseDataType::map, NULL, true);
    }
  }

#if defined(ENABLE_LIVE_MAP) || defined(ENABLE_GPS_DASHBOARD)
  ubxLoop();
#endif

  static byte loopCase = 0;
    switch (loopCase)
    {
    case 0:
      versionRequestLoop();
      break;
    case 1:
      stateRequestLoop();
      break;
    case 2:
      sendData(ResponseDataType::mowerState);
      break;
    case 3:
      sendData(ResponseDataType::mowerStats);
      break;
    case 4:
      sendData(ResponseDataType::desiredState);
      break;
    case 5:
      processMapChunkSend();
      break;
    case 6:
      processCalculateWaypoints();
      break;
    case 7:
      sendData(ResponseDataType::map);
      break;
    case 8:
      logToUiLoop();
      break;
    case 9:
      sensorRequestLoop();
      break;
    case 10:
      if (_source.sensorSummary().timestamp > 0) {
        sendData(ResponseDataType::sensorSummary);
      }
      break;
#if defined(ENABLE_LIVE_MAP) || defined(ENABLE_GPS_DASHBOARD)
    case 11:
      gpsRequestLoop();
      if (gpsDetailsActive) ubxPollLoop();
      break;
    case 12:
      if (_source.gpsDetails().timestamp > 0) {
        sendData(ResponseDataType::gpsDetails);
      }
      break;
    case 13:
      // Send UBX response immediately when available (no deduplication – frontend handles that)
      if (ubxResponseActive && _source.ubxResponse().timestamp > 0) {
        sendData(ResponseDataType::ubxResponse, NULL, true); // force=true: bypass oldDataTimestamp
        _source.ubxResponseP()->timestamp = 0; // Mark as consumed
        _lastSentUbxTimestamp = 0; // Allow next poll to advance immediately
      }
      break;
#endif
    case 14:
      processUploadToMower();
      break;
    case 15:
      processScheduleTrigger();
      processScheduleTriggerStateMachine();
      break;

    case 16: {
      static unsigned long lastTrackSend = 0;
      if (millis() - lastTrackSend >= 10000) {
        lastTrackSend = millis();
        sendDrivenTrack(NULL);
      }
      break;
    }
    case 17:
      // Hindernis-Polling: alle 10s AT+S2 an den Mower (wie CaSSAndRA)
      _cmd.requestObstacles();
      break;
    case 18:
      if (_source.obstacles().timestamp > 0) {
        sendData(ResponseDataType::obstacles);
      }
      break;
    }
    loopCase++;
    if (loopCase > 18) loopCase = 0;
  yield();
  
  //pingClients();
}

void UiSocketHandler::versionRequestLoop()
{
  auto props = _source.props();
  if (props.timestamp != 0)
    return;

  if (millis() - lastVersionRequestTimestamp > defaultVersionRequestInterval) {
    _cmd.requestVersion();
    lastVersionRequestTimestamp = millis();
  }
}

void UiSocketHandler::stateRequestLoop()
{
  auto state = _source.state();

  if ((millis() - state.timestamp) > defaultStateUpdateInterval) {
    if ((lastDataRequestTimestamp[ResponseDataType::mowerState] > 0) && ((millis() - lastDataRequestTimestamp[ResponseDataType::mowerState]) < defaultStateUpdateInterval)) 
      return;
    _cmd.requestStatus();
    lastDataRequestTimestamp[ResponseDataType::mowerState] = millis();
  } 
}

void UiSocketHandler::requestStats()
{
  _cmd.requestStats();
}

void UiSocketHandler::requestStatsNow()
{
  _cmd.requestStatsNow();
}

void UiSocketHandler::sensorRequestLoop()
{
  if ((lastDataRequestTimestamp[ResponseDataType::sensorSummary] > 0) && ((millis() - lastDataRequestTimestamp[ResponseDataType::sensorSummary]) < 1000))
    return;
  _cmd.requestSensorSummary();
  lastDataRequestTimestamp[ResponseDataType::sensorSummary] = millis();
}

#if defined(ENABLE_LIVE_MAP) || defined(ENABLE_GPS_DASHBOARD)
void UiSocketHandler::gpsRequestLoop()
{
  static uint32_t lastAttempt = 0;
  if (millis() - lastAttempt < 200) return;
  lastAttempt = millis();

  if ((lastDataRequestTimestamp[ResponseDataType::gpsDetails] > 0) && ((millis() - lastDataRequestTimestamp[ResponseDataType::gpsDetails]) < 20000))
    return;
  if (_cmd.requestGpsDetails()) {
    lastDataRequestTimestamp[ResponseDataType::gpsDetails] = millis();
  }
}

void UiSocketHandler::ubxPollLoop()
{
  // Don't overwrite a pending command that hasn't been sent yet
  if (pendingUbxCmd.length() > 0) return;

  // Wait until the previous response was consumed by case 12
  if (ubxResponseActive && _source.ubxResponse().timestamp > 0) return;

  // Safety: advance even without response after timeout
  if (_lastSentUbxTimestamp > 0 && millis() - _lastSentUbxTimestamp < 5000) return;

  // Fast commands: polled every cycle for responsive Simple Dashboard
  static const char* fastCmds[] = {
    "B562010700000819",                   // 0: NAV-PVT
    "B5620135000036A3",                   // 1: NAV-SAT
  };

  // Slow config commands: one per cycle, cycled through.
  // Hex strings computed with the ubxPoll() helper (layer=7 for CFG-VALGET).
  static const char* slowCmds[] = {
    "B562010400000510",                   // 0: NAV-DOP
    "B5620A0400000E34",                   // 1: MON-VER
    "B5620A0900001343",                   // 2: MON-HW
    "B5620A38000042D0",                   // 3: MON-RF
    "B5620A36000040CA",                   // 4: MON-COMMS
    "B56201030000040D",                   // 5: NAV-STATUS
    "B562062400002A84",                   // 6: CFG-NAV5
    "B562060800000E30",                   // 7: CFG-RATE
    "B562068B080000010000010070101B8C",   // 8: CFG-VALGET-PORT1 (UART1 baud)
    "B562068B1C0000070000010073100200731003007310010074100200741004007410D6C0", // 9: CFG-VALGET-UART1-PROTO
    "B562068B1400000700001F0031102500311021003110220031103766",   // 10: CFG-VALGET-GNSS
    "B562068B08000001000001003A10E520",   // 11: CFG-VALGET-SBAS
    "B562068B080000010000010076102198",   // 12: CFG-VALGET-RTCM
    "B562068B0C0000070000010021300200213049B2",   // 13: CFG-VALGET-RATE
  };
  const uint8_t slowCount = sizeof(slowCmds) / sizeof(slowCmds[0]);

  // Interleave: fast[0], fast[1], slow[n], fast[0], fast[1], slow[n+1], ...
  switch (ubxPollSequence) {
    case 0:
      pendingUbxCmd = fastCmds[0];  // NAV-PVT
      ubxPollSequence = 1;
      break;
    case 1:
      pendingUbxCmd = fastCmds[1];  // NAV-SAT
      ubxPollSequence = 2;
      break;
    case 2:
      pendingUbxCmd = slowCmds[ubxConfigIndex];
      ubxConfigIndex = (ubxConfigIndex + 1) % slowCount;
      ubxPollSequence = 0;
      break;
  }

  _source.ubxResponseP()->timestamp = 0;
  Log(DBG, "%subxPollLoop seq=%d configIdx=%d", _LOG_, ubxPollSequence, ubxConfigIndex);
}

bool UiSocketHandler::sendUbx(const String &hexCmd)
{
  pendingUbxCmd = hexCmd;
  _source.ubxResponseP()->timestamp = 0; // Discard stale/periodic frames
  _lastSentUbxTimestamp = 0;
  return true;
}
#endif

void UiSocketHandler::resetRequestTimestamp(ResponseDataType dataType)
{
  lastDataRequestTimestamp[dataType] = 0;
}

#if defined(ENABLE_LIVE_MAP) || defined(ENABLE_GPS_DASHBOARD)
void UiSocketHandler::ubxLoop()
{
  if (pendingUbxCmd.length() == 0) return;

  static uint32_t lastAttempt = 0;
  if (millis() - lastAttempt < 200) return; // Throttle: ~5 retries/s max
  lastAttempt = millis();

  if (_cmd.sendUbx(pendingUbxCmd)) {
    Log(DBG, "%subxLoop sent pending cmd", _LOG_);
    pendingUbxCmd = "";
    _lastSentUbxTimestamp = millis();
  }
}
#endif

void UiSocketHandler::sendData(ResponseDataType dataType, UiSocketItem *sendTo, bool force)
{
  // Serialize all non-map traffic while a chunked map transfer is in progress.
  // ESPAsyncWebServer interleaves other frames with fragmented text frames,
  // which violates the WebSocket spec and causes "previous message is unfinished".
  if (dataType != ResponseDataType::map && mapChunkSendState.active) {
    return;
  }
  switch (dataType) {
    case ResponseDataType::mowerState: {
      auto state = _source.state();
      // Position nur überschreiben, wenn der Mäher tatsächlich angedockt ist
      // (job == charge) oder gerade neu gestartet wurde und noch keine GPS-Position hat.
      // Nur amps < 0 ist zu unzuverlässig, da es zu oft vorkommt.
      bool dockedCharging = (state.job == 2);
      bool recentlyRestarted = (state.timestamp > 0 && state.timestamp < 30000);
      bool noPosition = (state.position.x == 0.0f && state.position.y == 0.0f);
      auto map = _source.mowerMap();
      bool overrideActive = (dockedCharging || (recentlyRestarted && noPosition)) && map.dockpoints.size() > 0;
      if (overrideActive) {
        const auto &home = map.dockpoints.back();
        state.position.x = home.X;
        state.position.y = home.Y;
        // Orientierung aus der Dock-Linie ableiten (Vektor vom vorletzten zum letzten Punkt)
        if (map.dockpoints.size() >= 2) {
          const auto &prev = map.dockpoints[map.dockpoints.size() - 2];
          state.position.delta = atan2(home.Y - prev.Y, home.X - prev.X);
        }
        // Nur beim Übergang in den Override-Zustand loggen, nicht dauernd währenddessen
        if (!_dockOverrideActive) {
          Log(DBG, "%s sendData: docked/restarted (job=%d ts=%u amps=%.2f), overriding position with home/dock (%.2f, %.2f)",
              _LOG_, state.job, state.timestamp, state.amps, state.position.x, state.position.y);
          _dockOverrideActive = true;
        }
      } else {
        if (_dockOverrideActive) {
          Log(DBG, "%s sendData: left dock/restart window (job=%d ts=%u amps=%.2f)",
              _LOG_, state.job, state.timestamp, state.amps);
          _dockOverrideActive = false;
        }
      }
      sendData(dataType, sendTo, state, force);
      if (state.job == 1 || state.job == 0) {
        pushDrivenTrackPoint(state.position.x, state.position.y, state.timestamp);
      }
      break;
    }
    case ResponseDataType::mowerStats:
      sendData(dataType, sendTo, _source.stats(), force);
      break;
    case ResponseDataType::desiredState:
      sendData(dataType, sendTo, _source.desiredState(), force);
      break;
    case ResponseDataType::map:
      // Starte asynchronen Map-Chunk-Versand
      startMapChunkSend(sendTo, force);
      break;
    case ResponseDataType::sensorSummary:
      sendData(dataType, sendTo, _source.sensorSummary(), force);
      break;
#if defined(ENABLE_LIVE_MAP) || defined(ENABLE_GPS_DASHBOARD)
    case ResponseDataType::gpsDetails:
      sendData(dataType, sendTo, _source.gpsDetails(), force);
      break;
    case ResponseDataType::ubxResponse:
      sendData(dataType, sendTo, _source.ubxResponse(), force);
      break;
#endif
    case ResponseDataType::mowSettings:
      sendData(dataType, sendTo, _source.mowSettings(), force);
      break;
    case ResponseDataType::obstacles:
      sendData(dataType, sendTo, _source.obstacles(), force);
      break;
    default:
      break;
  }
}

// Starte asynchronen Map-Chunk-Versand
void UiSocketHandler::startMapChunkSend(UiSocketItem* sendTo, bool force) {
  auto map = _source.mowerMap();
  if (mapChunkSendState.active || (!force && (map.timestamp == 0 || map.timestamp == oldDataTimestamp[ResponseDataType::map]))) {
    return;
  }
  // reading-Flag auf der echten Map setzen: schützt _source vor gleichzeitigen setMap()-Aufrufen
  _source.beginMowerMapRead();
  // Snapshot einmalig speichern - verhindert Race Condition waehrend Chunk-Versand
  mapChunkSendState.snapshot = map;
  // Eindeutige Transfer-ID für diesen Map-Transfer; Frontend kann damit verspätete
  // Chunks aus vorherigen Transfers eindeutig erkennen und verwerfen.
  mapChunkSendState.transferId++;
  if (mapChunkSendState.transferId == 0) mapChunkSendState.transferId = 1;
  // Metadaten des aktuellen Karteninhalts für alle Chunks merken
  mapChunkSendState.metaHash = _source.currentMapHash();
  mapChunkSendState.metaCrc = _source.currentMapCrc();
  mapChunkSendState.metaArea = _source.currentMapArea();
  mapChunkSendState.metaRotation = _source.currentMapRotation();
  lastDataRequestTimestamp[ResponseDataType::map] = 0;
  mapChunkSendState.active = true;
  mapChunkSendState.clientId = sendTo ? sendTo->clientId() : 0;
  mapChunkSendState.timestamp = map.timestamp;
  mapChunkSendState.phase = 0;
  mapChunkSendState.exclusionIdx = 0;
  mapChunkSendState.idx = 0;
  mapChunkSendState.lastRetryMs = 0;
  mapChunkSendState.lastProgressMs = millis();
}

// Pro loop() einen Chunk versenden – Snapshot aus mapChunkSendState verwenden
void UiSocketHandler::processMapChunkSend() {
  if (!mapChunkSendState.active) return;
  // Stall-Watchdog: ein Transfer, der längere Zeit keinen einzigen Chunk
  // loswird (Client-Queue voll, Client hängt in WS_DISCONNECTING, ...),
  // blockiert sonst dauerhaft ALLE anderen Nachrichten (sendData() kehrt bei
  // aktivem Transfer sofort zurück) und lässt jedes setMap() abprallen.
  if (mapChunkSendState.lastProgressMs &&
      millis() - mapChunkSendState.lastProgressMs > mapChunkSendTimeoutMs) {
    Log(WARN, "%s processMapChunkSend: transfer %u stalled for %u ms (phase=%d client=%u), aborting",
        _LOG_, mapChunkSendState.transferId, (unsigned)(millis() - mapChunkSendState.lastProgressMs),
        mapChunkSendState.phase, mapChunkSendState.clientId);
    finishMapChunkSend();
    // Retry later; by then cleanupClients()/the TCP ACK timeout have usually
    // removed the client that caused the stall.
    _mapSendPendingUntil = millis() + 5000;
    return;
  }
  // Retry-Delay: bei fehlgeschlagenem Senden 100ms warten
  if (mapChunkSendState.lastRetryMs && millis() - mapChunkSendState.lastRetryMs < 100) return;
  // Keine Clients → Chunk-Versand abbrechen
  if (countConnectedClients() == 0) {
    Log(DBG, "%s processMapChunkSend: no connected clients, aborting", _LOG_);
    mapChunkSendState.active = false;
    _source.endMowerMapRead();
    return;
  }
  // Snapshot verwenden: einmalig beim Start gespeichert, bleibt konsistent
  auto& map = mapChunkSendState.snapshot;
  const size_t blockSize = 30;
  bool chunkSent = false;
  bool stalled = false;
  size_t nextIdx = 0;

  switch (mapChunkSendState.phase) {
    case 0: // Perimeter
      if (mapChunkSendState.idx == 0) {
        if (!sendMapChunk(MapPointType::Perimeter, map.perimeter, mapChunkSendState.timestamp, mapChunkSendState.clientId, -1, 0, 0, true, nextIdx)) {
          mapChunkSendState.lastRetryMs = millis(); stalled = true;
          break;
        }
      }
      if (map.perimeter.size() > 0 && mapChunkSendState.idx < map.perimeter.size()) {
        chunkSent = sendMapChunk(MapPointType::Perimeter, map.perimeter, mapChunkSendState.timestamp, mapChunkSendState.clientId, -1, mapChunkSendState.idx, blockSize, false, nextIdx);
        if (!chunkSent) {
          mapChunkSendState.lastRetryMs = millis(); stalled = true;
          break;
        }
        mapChunkSendState.idx = nextIdx;
        if (mapChunkSendState.idx >= map.perimeter.size()) { mapChunkSendState.phase = 1; mapChunkSendState.idx = 0; }
        break;
      } else { mapChunkSendState.phase = 1; mapChunkSendState.idx = 0; }
      // fallthrough
    case 1: // Exclusions
      if (mapChunkSendState.exclusionIdx == 0 && mapChunkSendState.idx == 0) {
        static const std::vector<ArduMower::Domain::Robot::MapPoint> emptyExcl;
        if (!sendMapChunk(MapPointType::Exclusion, emptyExcl, mapChunkSendState.timestamp, mapChunkSendState.clientId, -1, 0, 0, true, nextIdx)) {
          mapChunkSendState.lastRetryMs = millis(); stalled = true;
          break;
        }
      }
      if (map.exclusions.size() > 0 && mapChunkSendState.exclusionIdx < map.exclusions.size()) {
        const auto& excl = map.exclusions[mapChunkSendState.exclusionIdx];
        if (excl.size() > 0 && mapChunkSendState.idx < excl.size()) {
          chunkSent = sendMapChunk(MapPointType::Exclusion, excl, mapChunkSendState.timestamp, mapChunkSendState.clientId, (int)mapChunkSendState.exclusionIdx, mapChunkSendState.idx, blockSize, false, nextIdx);
          if (!chunkSent) {
            mapChunkSendState.lastRetryMs = millis(); stalled = true;
            break;
          }
          mapChunkSendState.idx = nextIdx;
          if (mapChunkSendState.idx >= excl.size()) { mapChunkSendState.exclusionIdx++; mapChunkSendState.idx = 0; }
          break;
        } else { mapChunkSendState.exclusionIdx++; mapChunkSendState.idx = 0; }
        break;
      } else { mapChunkSendState.phase = 2; mapChunkSendState.exclusionIdx = 0; mapChunkSendState.idx = 0; }
      // fallthrough
    case 2: // Dockpoints
      if (mapChunkSendState.idx == 0) {
        static const std::vector<ArduMower::Domain::Robot::MapPoint> emptyDockpoints;
        if (!sendMapChunk(MapPointType::Dockpoints, emptyDockpoints, mapChunkSendState.timestamp, mapChunkSendState.clientId, -1, 0, 0, true, nextIdx)) {
          mapChunkSendState.lastRetryMs = millis(); stalled = true;
          break;
        }
      }
      if (map.dockpoints.size() > 0 && mapChunkSendState.idx < map.dockpoints.size()) {
        chunkSent = sendMapChunk(MapPointType::Dockpoints, map.dockpoints, mapChunkSendState.timestamp, mapChunkSendState.clientId, -1, mapChunkSendState.idx, blockSize, false, nextIdx);
        if (!chunkSent) {
          mapChunkSendState.lastRetryMs = millis(); stalled = true;
          break;
        }
        mapChunkSendState.idx = nextIdx;
        if (mapChunkSendState.idx >= map.dockpoints.size()) { mapChunkSendState.phase = 3; mapChunkSendState.idx = 0; }
        break;
      } else { mapChunkSendState.phase = 3; mapChunkSendState.idx = 0; }
      // fallthrough
    case 3: // Search Wire
      if (mapChunkSendState.idx == 0) {
        static const std::vector<ArduMower::Domain::Robot::MapPoint> emptySearchWire;
        if (!sendMapChunk(MapPointType::SearchWire, emptySearchWire, mapChunkSendState.timestamp, mapChunkSendState.clientId, -1, 0, 0, true, nextIdx)) {
          mapChunkSendState.lastRetryMs = millis(); stalled = true;
          break;
        }
      }
      if (map.searchWire.size() > 0 && mapChunkSendState.idx < map.searchWire.size()) {
        chunkSent = sendMapChunk(MapPointType::SearchWire, map.searchWire, mapChunkSendState.timestamp, mapChunkSendState.clientId, -1, mapChunkSendState.idx, blockSize, false, nextIdx);
        if (!chunkSent) {
          mapChunkSendState.lastRetryMs = millis(); stalled = true;
          break;
        }
        mapChunkSendState.idx = nextIdx;
        if (mapChunkSendState.idx >= map.searchWire.size()) { mapChunkSendState.phase = 4; mapChunkSendState.idx = 0; }
        break;
      } else { mapChunkSendState.phase = 4; mapChunkSendState.idx = 0; }
      // fallthrough
    case 4: // Waypoints
      if (mapChunkSendState.idx == 0) {
        static const std::vector<ArduMower::Domain::Robot::MapPoint> emptyWaypoints;
        if (!sendMapChunk(MapPointType::Waypoints, emptyWaypoints, mapChunkSendState.timestamp, mapChunkSendState.clientId, -1, 0, 0, true, nextIdx)) {
          mapChunkSendState.lastRetryMs = millis(); stalled = true;
          break;
        }
      }
      if (map.waypoints.size() > 0 && mapChunkSendState.idx < map.waypoints.size()) {
        chunkSent = sendMapChunk(MapPointType::Waypoints, map.waypoints, mapChunkSendState.timestamp, mapChunkSendState.clientId, -1, mapChunkSendState.idx, blockSize, false, nextIdx);
        if (!chunkSent) {
          mapChunkSendState.lastRetryMs = millis(); stalled = true;
          break;
        }
        mapChunkSendState.idx = nextIdx;
        if (mapChunkSendState.idx >= map.waypoints.size()) { mapChunkSendState.phase = 5; mapChunkSendState.idx = 0; }
        break;
      } else {
        sendMapChunk(MapPointType::Waypoints, map.waypoints, mapChunkSendState.timestamp, mapChunkSendState.clientId, -1, 0, 0, false, nextIdx);
        mapChunkSendState.phase = 5; mapChunkSendState.idx = 0;
      }
      // fallthrough
    case 5:
      {
        JsonDocument doc;
        doc["type"] = ResponseDataType::map;
        doc["timestamp"] = mapChunkSendState.timestamp;
        doc["transferId"] = mapChunkSendState.transferId;
        doc["data"]["complete"] = true;
        String json;
        serializeJson(doc, json);
        if (!sendMapChunkText(mapChunkSendState.clientId, json)) {
          mapChunkSendState.lastRetryMs = millis(); stalled = true;
          break;
        }
      }
      oldDataTimestamp[ResponseDataType::map] = map.timestamp;
      finishMapChunkSend();
      break;
  }
  if (mapChunkSendState.active && !stalled) {
    mapChunkSendState.lastProgressMs = millis();
  }
}

// End the current chunk transfer (completed or aborted) and flush the
// non-map messages that were held back while it was running, so the frame
// stream stays unfragmented. Must NOT be called while holding _clientsMutex:
// the flushed broadcasts take _sendMutex and then _clientsMutex.
void UiSocketHandler::finishMapChunkSend() {
  if (!mapChunkSendState.active) return;
  mapChunkSendState.active = false;
  mapChunkSendState.clientId = 0;
  mapChunkSendState.lastRetryMs = 0;
  mapChunkSendState.lastProgressMs = 0;
  _source.endMowerMapRead();
  if (_mapListPending) {
    _mapListPending = false;
    sendMapList(NULL);
  }
  if (_drivenTrackPending) {
    _drivenTrackPending = false;
    sendDrivenTrack(NULL);
  }
  if (_flashProgressPending) {
    _flashProgressPending = false;
    JsonDocument doc;
    auto status = doc["status"].to<JsonObject>();
    status["progress"] = _flashProgressPct;
    String json;
    serializeJson(doc, json);
    sendTextAllWithRetry(json);
  }
}

// Hilfsfunktion: Sende einen Chunk eines MapPoint-Vektors
bool UiSocketHandler::sendMapChunk(MapPointType pointType, const std::vector<ArduMower::Domain::Robot::MapPoint>& points, uint32_t timestamp, uint32_t clientId, int exclusionIdx, size_t startIdx, size_t blockSize, bool reset, size_t &nextIdx) {
  const size_t maxJsonSize = 1024;
  size_t total = points.size();
  nextIdx = 0;
  if (!reset && (startIdx > total || (startIdx == total && total > 0))) return false;
  // Vor Serialisierung prüfen ob Ziel-Client sendefähig ist
  if (!clientCanSend(clientId)) return false;
  JsonDocument doc;
  doc["type"] = ResponseDataType::map;
  doc["timestamp"] = timestamp;
  doc["transferId"] = mapChunkSendState.transferId;
  auto dataObj = doc["data"].to<JsonObject>();
  size_t transferTotal = mapChunkSendState.snapshot.perimeter.size()
    + mapChunkSendState.snapshot.dockpoints.size()
    + mapChunkSendState.snapshot.searchWire.size()
    + mapChunkSendState.snapshot.waypoints.size();
  for (const auto &exclusion : mapChunkSendState.snapshot.exclusions) {
    transferTotal += exclusion.size();
  }
  dataObj["transferTotal"] = transferTotal;
  if (reset) {
    dataObj["reset"] = true;
    dataObj["pointType"] = static_cast<int>(pointType);
  } else {
    dataObj["startIndex"] = (int)startIdx;
    dataObj["total"] = (int)total;
    dataObj["pointType"] = static_cast<int>(pointType);
    if (pointType == MapPointType::Exclusion) {
      dataObj["exclusionIdx"] = exclusionIdx;
    }
    if (mapChunkSendState.metaHash.length() > 0) {
      auto metaObj = dataObj["meta"].to<JsonObject>();
      metaObj["hash"] = mapChunkSendState.metaHash;
      metaObj["crc"] = mapChunkSendState.metaCrc;
      metaObj["area"] = mapChunkSendState.metaArea;
      metaObj["rotation"] = mapChunkSendState.metaRotation;
    }
    auto arr = dataObj["points"].to<JsonArray>();
    size_t measured = measureJson(doc);
    size_t pointsAdded = 0;
    size_t idx = startIdx;
    while (pointsAdded < blockSize && idx < total) {
      JsonObject obj = arr.add<JsonObject>();
      bool marshalOk = true;
      try {
        points[idx].marshalFull(obj);
      } catch (...) {
        Log(ERR, "%s marshal exception at pointType=%d idx=%u", _LOG_, (int)pointType, (unsigned)idx);
        marshalOk = false;
      }
      if (!marshalOk) {
        arr.remove(arr.size() - 1);
        ++idx;
        continue;
      }
      size_t newMeasured = measureJson(doc);
      if (newMeasured >= maxJsonSize - 128) {
        arr.remove(arr.size() - 1);
        Log(DBG, "%s remove point", _LOG_);
        break;
      }
      measured = newMeasured;
      ++idx;
      ++pointsAdded;
    }
    nextIdx = idx;
    Log(DBG, "%s MapChunk type=%d exclIdx=%d: measured=%u, points=%u, nextIdx=%u, pointsAdded=%u", _LOG_, (int)pointType, exclusionIdx, (unsigned)measured, (unsigned)arr.size(), (unsigned)nextIdx, (unsigned)pointsAdded);
  }
  String stateStr;
  try {
    serializeJson(doc, stateStr);
    sanitizeUtf8InPlace(stateStr);
  } catch (...) {
    Log(ERR, "%s serializeJson failed", _LOG_);
    return false;
  }
  if (clientId > 0) {
    if (findClient(clientId) == nullptr) {
      // Target client is gone. Do NOT fall back to broadcasting: the other
      // clients would receive the tail of a transfer they never saw the
      // reset frames for. The DISCONNECT event aborts the transfer; until
      // then just report failure.
      Log(DBG, "%s sendMapChunk: client %u gone", _LOG_, clientId);
      return false;
    }
    if (!sendMapChunkText(clientId, stateStr)) {
      Log(ERR, "%s sendMapChunk to client %u failed", _LOG_, clientId);
      _ws->cleanupClients();
      return false;
    }
  } else {
    if (!sendMapChunkText(0, stateStr)) {
      Log(ERR, "%s sendMapChunk broadcast failed", _LOG_);
      _ws->cleanupClients();
      return false;
    }
  }
  return true;
}

void UiSocketHandler::logToUiLoop()
{
  if (!logToUi.hasData()) return;
  uint32_t now = millis();
  if (now - _lastLogSend < 100) return;

  if (countConnectedClients() == 0) return;
  if (!_ws->availableForWriteAll())
    return;

  _lastLogSend = now;
  // force=true: rate limit and "is there anything new" are already handled
  // above. The generic timestamp check cannot be used, because a burst goes
  // out in several updates that all carry the timestamp of the newest line -
  // the remaining lines would never be sent.
  // Advance the send cursor only once the data actually left the modem,
  // otherwise a failed send would silently skip those lines.
  if (sendData(ResponseDataType::modemLog, NULL, logToUi, true))
    logToUi.commitSent();
}

void UiSocketHandler::broadcastFlashProgress(size_t current, size_t total)
{
  int pct = (total > 0) ? (current * 100 / total) : 0;
  int clients = countConnectedClients();
  Log(DBG, "UiSocket::broadcastFlashProgress(pct=%d, clients=%d)", pct, clients);
  if (clients == 0) {
    _flashProgressPending = false;
    return;
  }
  if (mapChunkSendState.active) {
    _flashProgressPending = true;
    _flashProgressPct = pct;
    return;
  }
  _flashProgressPending = false;
  JsonDocument doc;
  auto status = doc["status"].to<JsonObject>();
  status["progress"] = pct;
  String json;
  serializeJson(doc, json);
  sendTextAllWithRetry(json);
}

#ifdef MOWER_TERMINAL
bool UiSocketHandler::cmdToMower(String cmd) {
  Log(DBG, "%s mower console cmd %s", _LOG_, cmd.c_str());
  bool success = _terminal.sendWithoutResponse(cmd);
  if (success) {
    Log(DBG, "%s sends cmd success", _LOG_);
  }
  return success;
}
#else
bool UiSocketHandler::cmdToMower(String) {
  return false;
}
#endif

void UiSocketHandler::cmdStart() {
  _cmd.start();
}

void UiSocketHandler::cmdStop() {
  _cmd.stop();
}

void UiSocketHandler::cmdDock() {
  _cmd.dock();
}

void UiSocketHandler::cmdSkipWaypoint() {
  _cmd.skipWaypoint();
}

void UiSocketHandler::cmdReboot() {
  _cmd.reboot();
}

void UiSocketHandler::cmdPowerOff() {
  _cmd.powerOff();
}

void UiSocketHandler::cmdMowerEnabled(bool enabled) {
  _cmd.mowerEnabled(enabled);
}

void UiSocketHandler::cmdMowerAuto() {
  _cmd.mowerAuto();
}

void UiSocketHandler::joystickMove(float linear, float angular) {
  _cmd.manualDrive(linear, angular);
  Log(DBG, "%s joystickMove(%.2f, %.2f)", _LOG_, linear, angular);
}

void UiSocketHandler::navigateTo(float x, float y) {
  _cmd.navigateTo(x, y);
  Log(DBG, "%s navigateTo(%.2f, %.2f)", _LOG_, x, y);
}

void UiSocketHandler::sendProgress(String operation, int progress, String message) {
  _progressOp = operation;
  _progressPct = progress;
  _progressMsg = message;
}

void UiSocketHandler::uploadMapToMower() {
  if (_uploadToMowerPending) {
    Log(INFO, "%s uploadMapToMower: upload already in progress", _LOG_);
    return;
  }
  if (!_cmd.uploadMapToMower()) {
    Log(WARN, "%s uploadMapToMower: could not start upload", _LOG_);
    return;
  }
  _uploadToMowerPending = true;
  sendProgress("upload", 0, "Uploading map");
  sendData(ResponseDataType::mowerState, NULL, true);
  Log(INFO, "%s uploadMapToMower: started", _LOG_);
}

void UiSocketHandler::processUploadToMower() {
  if (!_uploadToMowerPending) return;
  if (_cmd.uploadMapToMowerActive()) {
    auto p = _cmd.uploadProgress();
    if (p.label.length() > 0) {
      String msg = p.label;
      if (p.total > 0) msg += " " + String(p.done) + "/" + String(p.total);
      if (p.totalTotal > 0) {
        if (p.total > 0) msg += " (" + String(p.totalDone) + "/" + String(p.totalTotal) + ")";
        else msg += " " + String(p.totalDone) + "/" + String(p.totalTotal);
      }
      sendProgress("upload", p.pct, msg);
    }
    return;
  }

  _uploadToMowerPending = false;
  if (_cmd.uploadMapToMowerSuccess()) {
    sendProgress("upload", 100, "Upload complete");
    Log(INFO, "%s processUploadToMower: upload complete, refreshing mower state", _LOG_);
  } else {
    sendProgress("upload", 100, "Upload failed");
    Log(WARN, "%s processUploadToMower: upload failed", _LOG_);
  }
  sendData(ResponseDataType::mowerState, NULL, true);
  _cmd.requestStatusNow(); // refresh state/crc immediately after upload
}

void UiSocketHandler::setMowSettings(const ArduMower::Domain::Robot::MowSettings &s) {
  _source.setMowSettings(s);
}

void UiSocketHandler::sendWaypointsDirect(const std::vector<ArduMower::Domain::Robot::MapPoint> &waypoints, uint32_t timestamp) {
  JsonDocument doc;
  doc["type"] = ResponseDataType::map;
  doc["timestamp"] = timestamp;
  auto data = doc["data"].to<JsonObject>();
  data["startIndex"] = 0;
  data["total"] = (int)waypoints.size();
  data["pointType"] = static_cast<int>(MapPointType::Waypoints);
  auto arr = data["points"].to<JsonArray>();
  for (const auto &wp : waypoints) {
    JsonObject obj = arr.add<JsonObject>();
    wp.marshal(obj);
  }
  String json;
  serializeJson(doc, json);
  sanitizeUtf8InPlace(json);
  sendTextAllWithRetry(json);
}

void UiSocketHandler::clearWaypoints() {
  sendProgress("clear", 0, "Clearing waypoints...");
  sendData(ResponseDataType::mowerState, NULL, true);
  using namespace ArduMower::Domain::Robot;
  auto map = _source.mowerMap();
  uint32_t ts = millis();
  map.waypoints.clear();
  _source.setMap(map);
  abortMapChunkSend();
  yield();
  sendWaypointsDirect(map.waypoints, ts);
  sendData(ResponseDataType::map, NULL, true);
  sendMapList(NULL);
  Log(INFO, "%s clearWaypoints: waypoints cleared, map broadcast", _LOG_);
  sendProgress("clear", 100, "Complete");
  sendData(ResponseDataType::mowerState, NULL, true);
}

void UiSocketHandler::calculateWaypoints() {
  if (_calculateWaypointsPending || _calculateWaypointsRunning) {
    Log(INFO, "%s calculateWaypoints: already queued or running", _LOG_);
    sendProgress("calculate", 0, "Calculation already in progress");
    return;
  }
  _calculateWaypointsPending = true;
  _calculateWaypointsTimestamp = millis();
  _calculateWaypointsMap = _source.mowerMap();
  _calculateWaypointsSettings = _source.mowSettings();
  sendProgress("calculate", 0, "Calculating waypoints...");
  sendData(ResponseDataType::mowerState, NULL, true);
  Log(INFO, "%s calculateWaypoints: queued", _LOG_);
}

void UiSocketHandler::processCalculateWaypoints() {
  if (!_calculateWaypointsPending || _calculateWaypointsRunning)
    return;

  _calculateWaypointsRunning = true;
  _calculateWaypointsPending = false;

  using namespace ArduMower::Domain::Robot;
  auto map = _calculateWaypointsMap;

  map.waypoints.clear();
#ifdef ENABLE_MAP
  auto settings = _calculateWaypointsSettings;
  Log(INFO, "%s processCalculateWaypoints: settings pattern=%d width=%.2f angle=%d distToBorder=%d laps=%d doMowArea=%d doMowBorder=%d doMowExclusionBorder=%d",
      _LOG_, settings.pattern, settings.width, settings.angle, settings.distanceToBorder, settings.borderLaps,
      settings.doMowArea, settings.doMowBorder, settings.doMowExclusionBorder);
  auto state = _source.state();
  decltype(ArduMower::Modem::PathPlanner::calculateWaypoints(map, settings, &state)) waypoints;
  try {
    waypoints = ArduMower::Modem::PathPlanner::calculateWaypoints(map, settings, &state);
  } catch (...) {
    Log(ERR, "%s processCalculateWaypoints: exception during calculation", _LOG_);
    sendProgress("calculate", 100, "Failed");
    _calculateWaypointsRunning = false;
    return;
  }
  for (const auto &wp : waypoints)
    map.waypoints.push_back(wp);
#endif
  _source.setMap(map);
  abortMapChunkSend();
  yield();
  sendData(ResponseDataType::map, NULL, true);
  sendMapList(NULL);
  Log(INFO, "%s processCalculateWaypoints: %d waypoints generated, map broadcast", _LOG_, map.waypoints.size());
  sendProgress("calculate", 100, "Complete");
  sendData(ResponseDataType::mowerState, NULL, true);
  _calculateWaypointsRunning = false;
}

void UiSocketHandler::setMap(const ArduMower::Domain::Robot::MowerMap &map) {
  _source.setMap(map);
}

void UiSocketHandler::abortMapChunkSend() {
  if (mapChunkSendState.active) {
    mapChunkSendState.active = false;
    mapChunkSendState.clientId = 0;
    mapChunkSendState.lastRetryMs = 0;
    mapChunkSendState.lastProgressMs = 0;
    // Release the read lock that was acquired in startMapChunkSend().
    // Otherwise subsequent map operations can deadlock.
    _source.endMowerMapRead();
    // Reset pending flags so we don't send stale data that could overlap
    // with the fresh map transfer triggered after the abort.
    _mapListPending = false;
    _drivenTrackPending = false;
    _flashProgressPending = false;
    Log(DBG, "%s abortMapChunkSend: ongoing chunk send aborted", _LOG_);
  }
}

static bool sendJsonDoc(UiSocketItem *item, JsonDocument &doc)
{
  String stateStr;
  serializeJson(doc, stateStr);
  if (stateStr.length() == 0) return false;
  sanitizeUtf8InPlace(stateStr);
  return item->sendText(stateStr);
}

void UiSocketHandler::sendBufferedLogTo(UiSocketItem* item, uint16_t maxChunks)
{
  uint16_t chunks = 0;
  uint16_t offset = 0;
  const uint16_t chunkSize = 20;
  while (chunks < maxChunks) {
    JsonDocument doc;
    doc["type"] = ResponseDataType::modemLog;
    uint16_t sent = logToUi.marshalBatch(doc["data"].to<JsonObject>(), offset, chunkSize);
    if (sent == 0 || doc.overflowed()) break;
    if (!sendJsonDoc(item, doc)) break;
    offset += sent;
    chunks++;
    yield();
  }
  oldDataTimestamp[ResponseDataType::modemLog] = logToUi.timestamp;
}

#ifdef MOWER_TERMINAL
void UiSocketHandler::sendBufferedTerminalTo(UiSocketItem* item, uint16_t maxChunks)
{
  uint16_t chunks = 0;
  uint16_t offset = 0;
  const uint16_t chunkSize = 20;
  while (chunks < maxChunks) {
    JsonDocument doc;
    doc["type"] = ResponseDataType::mowerConsole;
    uint16_t sent = _terminal.marshalBatch(doc["data"].to<JsonObject>(), offset, chunkSize);
    if (sent == 0 || doc.overflowed()) break;
    if (!sendJsonDoc(item, doc)) break;
    offset += sent;
    chunks++;
    yield();
  }
}
#endif

template<typename T>
bool UiSocketHandler::sendData(ResponseDataType dataType, UiSocketItem *sendTo, T &&data, bool force)
{
  if (!force && (data.timestamp == 0 || data.timestamp == oldDataTimestamp[dataType])) {
    return false;
  }

  // Rate limit sends per data type (skip if sent too recently)
  if (!force) {
    uint32_t now = millis();
    uint32_t minInterval = 0;
    switch (dataType) {
      case ResponseDataType::mowerState:     minInterval = 1000;  break;
      case ResponseDataType::mowerStats:     minInterval = 5000;  break;
      case ResponseDataType::desiredState:   minInterval = 2000;  break;
      case ResponseDataType::sensorSummary:  minInterval = 1000;  break;
      case ResponseDataType::modemLog:       minInterval = 100;   break;
      default:                               minInterval = 0;     break;
    }
    if (minInterval > 0 && (now - lastSentTimestamp[dataType]) < minInterval)
      return false;
    lastSentTimestamp[dataType] = now;
  }

  oldDataTimestamp[dataType] = data.timestamp;
  // Don't reset request timestamp for types with their own polling loop,
  // otherwise they would re-request immediately after sending data
  switch (dataType) {
    case ResponseDataType::mowerState:
#if defined(ENABLE_LIVE_MAP) || defined(ENABLE_GPS_DASHBOARD)
    case ResponseDataType::gpsDetails:
#endif
    case ResponseDataType::sensorSummary:
      break;
    default:
      lastDataRequestTimestamp[dataType] = 0;
      break;
  }

  JsonDocument doc;
  doc["type"] = dataType;
  // Forwarding reference, not by value: marshal() may record on the source
  // object how much it produced (see LogToUi), and a copy would discard that.
  // Temporaries from the 3-argument overload still bind.
  auto _j = doc["data"].to<JsonObject>(); data.marshal(_j);
  if (dataType == ResponseDataType::mowerState) {
    _j["uploaded_map_id"] = _source.lastUploadedMapId();
    _j["uploaded_map_crc"] = _source.lastUploadedMapCrc();
  }
  if (dataType == ResponseDataType::mowSettings) {
    _j["mapId"] = _source.currentMapId();
  }

  if (dataType == ResponseDataType::mowerState && !_progressOp.isEmpty()) {
    doc["progressPct"] = _progressPct;
    doc["progressMsg"] = _progressMsg;
    doc["progressOp"] = _progressOp;
    if (_progressPct >= 100) {
      _progressPct = 0;
      _progressOp = "";
      _progressMsg = "";
    }
  }

  String stateStr;
  serializeJson(doc, stateStr);
  sanitizeUtf8InPlace(stateStr);

  if (sendTo != NULL) {
    return sendTo->sendText(stateStr);
  }
  if (countConnectedClients() == 0) return false;
  if (!broadcastHeapOk(stateStr.length()) || !sendTextAllWithRetry(stateStr)) {
    _ws->cleanupClients();
    return false;
  }
  return true;
}

void UiSocketHandler::sendMapList(UiSocketItem *sendTo)
{
  // Do not interleave map-list broadcasts with an ongoing fragmented map
  // chunk transfer. A text frame in the middle of a fragmented message
  // violates the WebSocket framing and causes browser-side disconnects
  // ("previous message is unfinished"). Defer the broadcast until the
  // chunked transfer is complete.
  if (sendTo == NULL && mapChunkSendState.active) {
    _mapListPending = true;
    return;
  }

  auto list = _source.mapList();
  String activeId = _source.activeMapId();
  String currentId = _source.currentMapId();

  JsonDocument doc;
  doc["type"] = ResponseDataType::mapList;
  doc["timestamp"] = millis();
  auto dataObj = doc["data"].to<JsonObject>();
  dataObj["activeId"] = activeId;
  dataObj["currentId"] = currentId;
  auto mapsArr = dataObj["maps"].to<JsonArray>();
  for (const auto &m : list) {
    JsonObject obj = mapsArr.add<JsonObject>();
    obj["id"] = m.id;
    obj["name"] = m.name;
    obj["area"] = m.area;
    obj["hash"] = m.hash;
    obj["crc"] = m.crc;
    obj["rotation"] = m.rotation;
    obj["timestamp"] = m.timestamp;
    obj["unsaved"] = m.unsaved;
    obj["requiresRename"] = m.requiresRename;
  }

  String json;
  serializeJson(doc, json);
  sanitizeUtf8InPlace(json);

  if (sendTo != NULL) {
    sendTo->sendText(json);
  } else {
    if (countConnectedClients() == 0) return;
    if (!broadcastHeapOk(json.length()) || !sendTextAllWithRetry(json)) {
      _ws->cleanupClients();
    }
  }
}

void UiSocketHandler::sendMapAck(UiSocketItem *sendTo, uint32_t syncId, bool accepted)
{
  const auto map = _source.mowerMap();
  if (accepted) {
    oldDataTimestamp[ResponseDataType::map] = map.timestamp;
  }

  JsonDocument doc;
  doc["type"] = ResponseDataType::mapAck;
  doc["timestamp"] = map.timestamp;
  auto data = doc["data"].to<JsonObject>();
  data["hash"] = _source.currentMapHash();
  data["crc"] = _source.currentMapCrc();
  data["area"] = _source.currentMapArea();
  data["rotation"] = _source.currentMapRotation();
  data["unsaved"] = true;
  data["syncId"] = syncId;
  data["mapId"] = _source.currentMapId();
  data["accepted"] = accepted;

  String json;
  serializeJson(doc, json);
  sanitizeUtf8InPlace(json);
  sendTo->sendText(json);
}

bool UiSocketHandler::setSchedule(bool enabled, const std::vector<ArduMower::Modem::Schedule::Entry> &entries)
{
  bool ok = _scheduleManager.setConfig(enabled, entries);
  if (ok) {
    _scheduleDirty = true;
  }
  return ok;
}

bool UiSocketHandler::saveSchedule()
{
  bool ok = _scheduleManager.save();
  return ok;
}

void UiSocketHandler::sendSchedule(UiSocketItem *sendTo)
{
  JsonDocument doc;
  doc["type"] = ResponseDataType::schedule;
  doc["timestamp"] = millis();
  auto dataObj = doc["data"].to<JsonObject>();
  _scheduleManager.marshal(dataObj);
  dataObj["nextRun"] = _scheduleManager.nextRun();
  dataObj["nextRunEntryId"] = _scheduleManager.nextRunEntryId();
  dataObj["nextRunMapName"] = _scheduleManager.nextRunMapName();
  dataObj["dirty"] = _scheduleManager.dirty();

  String json;
  serializeJson(doc, json);
  sanitizeUtf8InPlace(json);

  if (sendTo != NULL) {
    sendTo->sendText(json);
  } else {
    if (countConnectedClients() == 0) return;
    if (!broadcastHeapOk(json.length()) || !sendTextAllWithRetry(json)) {
      _ws->cleanupClients();
    }
  }
}

void UiSocketHandler::sendClock(UiSocketItem *sendTo)
{
  JsonDocument doc;
  doc["type"] = ResponseDataType::clock;
  doc["timestamp"] = millis();
  auto dataObj = doc["data"].to<JsonObject>();
  dataObj["epoch"] = (uint32_t)time(nullptr);
  dataObj["ntpSynced"] = time(nullptr) >= 1609459200;

  String json;
  serializeJson(doc, json);
  sanitizeUtf8InPlace(json);

  if (sendTo != NULL) {
    sendTo->sendText(json);
  } else {
    if (countConnectedClients() == 0) return;
    if (!broadcastHeapOk(json.length()) || !sendTextAllWithRetry(json)) {
      _ws->cleanupClients();
    }
  }
}

void UiSocketHandler::processScheduleTrigger()
{
  // Time must be valid and schedule must be enabled.
  if (!_scheduleManager.enabled()) return;
  if (_scheduleManager.entries().empty()) return;

  time_t now = time(nullptr);
  if (now < 1609459200) return;

  uint32_t nr = _scheduleManager.nextRun();
  if (nr == 0 || now < (time_t)nr) return;

  int entryId = _scheduleManager.nextRunEntryId();
  String mapId;
  String mapName;
  for (const auto &e : _scheduleManager.entries()) {
    if (e.id == entryId) {
      mapId = e.mapId;
      mapName = e.mapName;
      break;
    }
  }

  if (mapId.length() == 0) {
    Log(WARN, "%s processScheduleTrigger: entry %d has no map", _LOG_, entryId);
    _scheduleManager.computeNextRun();
    _scheduleDirty = true;
    return;
  }

  // If already mowing, skip this run and compute the next one.
  auto state = _source.state();
  if (state.job == 1) { // MOW
    Log(INFO, "%s processScheduleTrigger: already mowing, skipping entry %d", _LOG_, entryId);
    _scheduleManager.computeNextRun();
    _scheduleDirty = true;
    return;
  }

  if (_scheduleTriggerPending) {
    return;
  }

  Log(INFO, "%s processScheduleTrigger: entry %d map=%s", _LOG_, entryId, mapName.c_str());
  _scheduleTriggerPending = true;
  _scheduleTriggerPhase = 0;
  _scheduleTriggerMapId = mapId;
}

void UiSocketHandler::processScheduleTriggerStateMachine()
{
  if (!_scheduleTriggerPending) return;

  switch (_scheduleTriggerPhase) {
    case 0:
      // Upload the persisted version of the scheduled map directly.
      // Previously the map was made current via loadMap(), which preferred
      // the RAM draft — a schedule mowed with unsaved edits — and switched
      // the map away under an open editor. uploadSavedMapToMower() reads the
      // SPIFFS copy and leaves the current map alone.
      if (!_cmd.uploadSavedMapToMower(_scheduleTriggerMapId)) {
        Log(WARN, "%s processScheduleTriggerStateMachine: upload of saved map %s could not be started", _LOG_, _scheduleTriggerMapId.c_str());
        _scheduleTriggerPending = false;
        _scheduleManager.computeNextRun();
        _scheduleDirty = true;
        return;
      }
      _scheduleTriggerPhase = 3;
      sendProgress("upload", 0, "Uploading scheduled map");
      break;

    case 3:
      // Wait for upload completion.
      if (_cmd.uploadMapToMowerActive()) {
        auto p = _cmd.uploadProgress();
        if (p.label.length() > 0) {
          String msg = p.label;
          if (p.total > 0) msg += " " + String(p.done) + "/" + String(p.total);
          sendProgress("upload", p.pct, msg);
        }
        return;
      }
      if (_cmd.uploadMapToMowerSuccess()) {
        sendProgress("upload", 100, "Upload complete");
        _scheduleTriggerPhase = 4;
      } else {
        sendProgress("upload", 100, "Upload failed");
        Log(WARN, "%s processScheduleTriggerStateMachine: upload failed", _LOG_);
        _scheduleTriggerPending = false;
        _scheduleManager.computeNextRun();
        _scheduleDirty = true;
      }
      break;

    case 4:
      // Start mowing.
      Log(INFO, "%s processScheduleTriggerStateMachine: starting mower", _LOG_);
      _cmd.start();
      _scheduleTriggerPending = false;
      _scheduleManager.computeNextRun();
      _scheduleDirty = true;
      break;
  }
}

void UiSocketHandler::pushDrivenTrackPoint(float x, float y, uint32_t timestamp) {
  _track.push(x, y, timestamp);
}

void UiSocketHandler::sendDrivenTrack(UiSocketItem *sendTo)
{
  if (countConnectedClients() == 0 && sendTo == NULL) return;
  if (_track.size() == 0) return;

  if (sendTo == NULL && mapChunkSendState.active) {
    _drivenTrackPending = true;
    return;
  }

  if (sendTo == NULL) {
    std::vector<uint32_t> clientIds;
    lockClients();
    clientIds.reserve(itemMap.size());
    for (const auto &entry : itemMap) clientIds.push_back(entry.first);
    unlockClients();
    for (uint32_t clientId : clientIds) {
      UiSocketItem *item = findClient(clientId);
      if (item != NULL && item->drivenTrackSequence() < _track.latestSequence()) {
        sendDrivenTrack(item);
      }
    }
    return;
  }

  const bool full = sendTo->drivenTrackSequence() == 0;
  JsonDocument doc;
  doc["type"] = ResponseDataType::drivenTrack;
  doc["timestamp"] = millis();
  auto dataObj = doc["data"].to<JsonObject>();
  _track.marshal(dataObj, sendTo->drivenTrackSequence(), full);

  String json;
  serializeJson(doc, json);
  sanitizeUtf8InPlace(json);

  if (sendTo->sendText(json)) {
    sendTo->setDrivenTrackSequence(_track.latestSequence());
  }
}

bool UiSocketHandler::isClientReceivingChunk(uint32_t clientId) const {
  if (!mapChunkSendState.active) return false;
  // Broadcast chunks (clientId==0) block all clients; targeted chunks only
  // block the receiving client.
  return (mapChunkSendState.clientId == 0) || (mapChunkSendState.clientId == clientId);
}

// Direct send used by map chunks so they are not blocked by the
// per-client serialization guard. Must not be used for non-map traffic.
bool UiSocketHandler::sendMapChunkText(uint32_t clientId, const String& text) {
  size_t textLen = text.length();
  if (textLen > 512) {
    size_t minFree = textLen * 2 + 4096;
    if (ESP.getFreeHeap() < minFree) {
      Log(WARN, "%s sendMapChunkText heap too low (%u free) for %u byte payload, skipping", _LOG_, ESP.getFreeHeap(), (unsigned)textLen);
      return false;
    }
  }

  if (!lockSendMutex())
    return false;

  const char *data = text.c_str();
  size_t len = text.length();
  bool anySent = false;

  if (clientId > 0) {
    if (_ws->availableForWrite(clientId) && _ws->text(clientId, data, len)) {
      anySent = true;
    }
  } else {
    // Broadcast: send to all clients that can accept data
    if (_ws->availableForWriteAll()) {
      auto status = _ws->textAll(data, len);
      anySent = (status != AsyncWebSocket::DISCARDED);
    }
  }

  if (anySent) markClientActivity();
  unlockSendMutex();
  return anySent;
}

bool UiSocketHandler::sendTextAllWithRetry(const String &text)
{
  const size_t textLen = text.length();
  if (textLen > 512) {
    size_t minFree = textLen * 2 + 4096;
    if (ESP.getFreeHeap() < minFree) {
      Log(WARN, "%s heap too low (%u free) for %u byte WS broadcast, skipping", _LOG_, ESP.getFreeHeap(), (unsigned)textLen);
      return false;
    }
  }

  if (!lockSendMutex())
    return false;

  String sanitized = text;
  sanitizeUtf8InPlace(sanitized);
  const char *data = sanitized.c_str();
  size_t len = sanitized.length();

  _ws->cleanupClients();

  bool anySent = false;
  bool anyConnected = false;

  // NEVER iterate _ws->getClients() directly: that returns the library's raw
  // client list without holding _ws_clients_lock, while the async_tcp task
  // inserts (_newClient) and erases (_handleDisconnect) entries concurrently.
  // The resulting iterator invalidation / use-after-free showed up as random
  // reboots and corrupted WebSocket frames. Always go through the library's
  // locked API instead.
  if (!mapChunkSendState.active) {
    // Fast path: textAll() iterates the client list under _ws_clients_lock and
    // shares a single buffer between all clients. It also reaches clients whose
    // CONNECT event has not been processed by processWsEvents() yet.
    anyConnected = _ws->count() > 0;
    if (anyConnected) {
      anySent = (_ws->textAll(data, len) != AsyncWebSocket::DISCARDED);
    }
  } else {
    // A chunked map transfer is running: the receiving client(s) must be
    // skipped so that generic text frames are not interleaved with the chunk
    // stream. Snapshot the ids under _clientsMutex (lock order _sendMutex →
    // _clientsMutex, same as the CONNECT path) and send via the locked per-id
    // API.
    std::vector<uint32_t> ids;
    lockClients();
    ids.reserve(itemMap.size());
    for (const auto &entry : itemMap) ids.push_back(entry.first);
    unlockClients();

    for (uint32_t id : ids) {
      anyConnected = true;
      if (isClientReceivingChunk(id)) continue;
      if (!_ws->availableForWrite(id)) continue;
      if (_ws->text(id, data, len)) anySent = true;
    }
  }

  unlockSendMutex();

  if (anySent) markClientActivity();
  if (!anyConnected) return true;
  return anySent;
}

bool UiSocketHandler::clientCanSend(uint32_t clientId) {
  if (clientId == 0) {
    return _ws->availableForWriteAll();
  }
  return _ws->availableForWrite(clientId);
}

size_t UiSocketHandler::countConnectedClients()
{
  return _ws->count();
}

void UiSocketHandler::pingClients()
{
  if (millis() - lastclientPing < clientPingInterval)
    return;

  if (mapChunkSendState.active) {
    lastclientPing = millis();
    return;
  }

  // Iterate itemMap under lock. Use library's locked ping(id) API.
  // Dead entries are removed by processWsEvents on DISCONNECT.
  lockClients();
  for (auto it = itemMap.begin(); it != itemMap.end(); ) {
    _ws->ping(it->first);
    ++it;
  }
  unlockClients();

  lastclientPing = millis();
}

void UiSocketHandler::wsEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void * arg, uint8_t *data, size_t len)
{
  if(type == WS_EVT_CONNECT){
    markWsConnectionEvent();
    Log(INFO, "%s ws[%s][%u] connect", _LOG_, server->url(), client->id());

    client->setCloseClientOnQueueFull(false);
    client->keepAlivePeriod(0);

    // Build hello JSON here (cheap); the heavy burst-send runs in loopTask.
    JsonDocument doc;
    doc["type"] = ResponseDataType::responseHello;
    doc["client"] = client->id();
    JsonObject valueDescriptions = doc["data"].to<JsonObject>();
    JsonObject newObj = valueDescriptions["job"].to<JsonObject>();
    int i = 0;
    for (const char *item: ArduMower::Domain::Robot::State::State::jobDesc) {
      newObj[String(i++)] = item;
    }
    newObj = valueDescriptions["posSolution"].to<JsonObject>();
    i = 0;
    for (const char *item: ArduMower::Domain::Robot::State::State::posSolutionDesc) {
      newObj[String(i++)] = item;
    }
    valueDescriptions["logLevel"] = logToUi.modemLogLevel;
#ifdef ENABLE_MAP
    valueDescriptions["mapEnabled"] = true;
#endif
#ifdef ENABLE_LIVE_MAP
    valueDescriptions["liveMapEnabled"] = true;
#endif
#ifdef ENABLE_GPS_DASHBOARD
    valueDescriptions["gpsDashboardEnabled"] = true;
#endif

    String dataStr;
    serializeJson(doc, dataStr);
    sanitizeUtf8InPlace(dataStr);

    // Queue hello for sending in loopTask (avoids blocking async_tcp)
    if (xSemaphoreTake(_helloMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
      _pendingHellos.push_back({client->id(), std::move(dataStr)});
      xSemaphoreGive(_helloMutex);
    }

    // Queue CONNECT — UiSocketItem ctor (burst send) runs in loopTask
    enqueueWsEvent(WsEvent{WsEvtType::CONNECT, client->id(), nullptr, 0, {}});

    _ws->cleanupClients();

  } else if(type == WS_EVT_DISCONNECT){
    markWsConnectionEvent();
    Log(INFO, "%s ws[%s][%u] disconnect", _LOG_, server->url(), client->id());
    enqueueWsEvent(WsEvent{WsEvtType::DISCONNECT, client->id(), nullptr, 0, {}});

  } else if(type == WS_EVT_ERROR){
    markWsConnectionEvent();
    Log(ERR, "%s ws[%s][%u] error(%u): %s", _LOG_, server->url(), client->id(),
        arg ? *((uint16_t*)arg) : 0, data ? (char*)data : "");
    enqueueWsEvent(WsEvent{WsEvtType::DISCONNECT, client->id(), nullptr, 0, {}});

  } else if(type == WS_EVT_PONG){
    markClientActivity();

  } else if(type == WS_EVT_DATA){
    markClientActivity();
    AwsFrameInfo *info = (AwsFrameInfo*)arg;

    if(info->final && info->index == 0 && info->len == len){
      // Answer heartbeat pings right here in the async_tcp task instead of
      // deferring them to processWsEvents(). UiSocketHandler::loop() — and
      // with it the event queue — is suspended while the mower firmware is
      // being flashed and during long map operations. Without an immediate
      // pong the browser's heartbeat times out and tears down the connection
      // exactly while the user must not be disturbed.
      if (info->opcode == WS_TEXT && data && len > 0 && len < 64 &&
          isPingRequest((const char*)data, len)) {
        char response[24];
        snprintf(response, sizeof(response), "{\"type\":%d}", (int)ResponseDataType::responsePong);
        _ws->text(client->id(), response, strlen(response));
        return;
      }

      // Single complete frame — copy data and queue as DATA_FINAL
      uint8_t *copy = nullptr;
      if(info->opcode == WS_TEXT && data) {
        copy = (uint8_t*)malloc(len + 1);
        if (copy) { memcpy(copy, data, len); copy[len] = '\0'; }
      }
      enqueueWsEvent(WsEvent{WsEvtType::DATA_FINAL, client->id(), copy, len, *info});
    } else {
      // Multi-frame message
      bool isText = (info->message_opcode == WS_TEXT);
      uint8_t *copy = nullptr;
      size_t copyLen = 0;
      if (isText && data && len > 0) {
        copy = (uint8_t*)malloc(len + 1);
        if (copy) { memcpy(copy, data, len); copy[len] = '\0'; copyLen = len; }
      }

      if(info->index == 0){
        enqueueWsEvent(WsEvent{WsEvtType::DATA_START, client->id(), copy, copyLen, *info});
      } else {
        enqueueWsEvent(WsEvent{WsEvtType::DATA_CONTINUE, client->id(), copy, copyLen, *info});
      }

      // Final fragment of multi-frame message
      if((info->index + len) == info->len && info->final){
        uint8_t *copyFinal = nullptr;
        size_t copyFinalLen = 0;
        if (isText && data) {
          copyFinal = (uint8_t*)malloc(len + 1);
          if (copyFinal) { memcpy(copyFinal, data, len); copyFinal[len] = '\0'; copyFinalLen = len; }
        }
        enqueueWsEvent(WsEvent{WsEvtType::DATA_FINAL, client->id(), copyFinal, copyFinalLen, *info});
      }
    }
  }
}

void UiSocketHandler::handleData(uint32_t clientId, char *data)
{
  if (!data) return;
  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, data);
  if (error) {
    Log(ERR, "%s handleData: deserializeJson failed: %s (rawLen=%u)", _LOG_, error.f_str(), (unsigned)strlen(data));
    return;
  }

  lockClients();
  auto it = itemMap.find(clientId);
  if (it != itemMap.end())
  {
    const size_t rawLen = strlen(data);
    Log(DBG, "%s handleData type=%d rawLen=%u", _LOG_, (int)doc["type"], (unsigned)rawLen);
    JsonDocument jsonData;
    if (!jsonData.set(doc["data"])) {
      Log(ERR, "%s handleData: jsonData.set() failed", _LOG_);
      unlockClients();
      return;
    }
    UiSocketItem *item = it->second;
    unlockClients();
    item->handleData(doc["type"], jsonData);
  }
  else
  {
    unlockClients();
    Log(ERR, "%s client with id %u doesn't exist", _LOG_, clientId);
  }
}
