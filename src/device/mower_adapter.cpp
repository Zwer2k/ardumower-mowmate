#include <sstream>
#include <math.h>
#include "mower_adapter.h"
#include "checksum.h"
#include "log.h"
#include "prometheus_util.h"
#include "settings.h"
#include "mower_map.h"
#include "path_planner.h"
#include <SPIFFS.h>

#define _LOG_ "MowerAdapter::"
#define _LOG_CMD_ "MowerAdapter::command::"

using namespace ArduMower::Modem;

ArduMower::Domain::Robot::UploadProgress MowerAdapter::uploadProgress() {
  ArduMower::Domain::Robot::UploadProgress p;
  if (!_mapUploadState.active) return p;

  auto &st = _mapUploadState;
  auto &snap = st.snapshot;

  int exclusionTotal = 0;
  for (const auto &ex : snap.exclusions) exclusionTotal += ex.size();
  p.totalTotal = (int)snap.perimeter.size() + exclusionTotal + (int)snap.dockpoints.size() + (int)snap.waypoints.size();

  int previousDone = 0;
  switch (st.phase) {
    case MapUploadState::start:
      p.label = "Start upload";
      p.total = 0;
      break;
    case MapUploadState::perimeter:
      p.label = "Perimeter";
      p.done = (int)st.pointIdx;
      p.total = (int)snap.perimeter.size();
      break;
    case MapUploadState::exclusions: {
      p.label = "Exclusion " + String(st.polygonIdx + 1);
      p.done = (int)st.pointIdx;
      p.total = (st.polygonIdx < snap.exclusions.size()) ? (int)snap.exclusions[st.polygonIdx].size() : 0;
      previousDone = (int)snap.perimeter.size();
      for (size_t i = 0; i < st.polygonIdx && i < snap.exclusions.size(); i++) previousDone += (int)snap.exclusions[i].size();
      break;
    }
    case MapUploadState::dockpoints:
      p.label = "Dockpoints";
      p.done = (int)st.pointIdx;
      p.total = (int)snap.dockpoints.size();
      previousDone = (int)snap.perimeter.size() + exclusionTotal;
      break;
    case MapUploadState::waypoints:
      p.label = "Waypoints";
      p.done = (int)st.pointIdx;
      p.total = (int)snap.waypoints.size();
      previousDone = (int)snap.perimeter.size() + exclusionTotal + (int)snap.dockpoints.size();
      break;
    case MapUploadState::counts:
      p.label = "Counts";
      p.total = 0;
      previousDone = p.totalTotal;
      break;
    case MapUploadState::exclusionSizes:
      p.label = "Exclusion sizes";
      p.done = (int)st.polygonIdx;
      p.total = (int)snap.exclusions.size();
      previousDone = p.totalTotal;
      break;
    case MapUploadState::finalizing:
      p.label = "Finalizing";
      p.total = 0;
      previousDone = p.totalTotal;
      break;
    default:
      p.label = "Uploading map";
      p.total = 0;
      break;
  }
  p.totalDone = previousDone + p.done;
  if (p.totalDone > p.totalTotal) p.totalDone = p.totalTotal;

  if (p.total > 0) {
    p.pct = (int)(p.done * 100L / p.total);
    if (p.pct >= 100) p.pct = 99; // 100% reserved for done/failed
  } else {
    p.pct = 0;
  }
  return p;
}

void processCSVResponse(const char* res, std::function<void(int, const char*, size_t)> fn);
void processCSVResponse(const String& res, std::function<void(int, const char*, size_t)> fn);

MowerAdapter::MowerAdapter(Settings::Settings &_settings, Router &_router)
    : settings(_settings), router(_router), sendIsInitialized(false) {}

void MowerAdapter::setMap(const ArduMower::Domain::Robot::MowerMap &map) {
  // Warte kurz, bis kein Lesevorgang auf _map läuft (max 100ms)
  int waitCount = 0;
  while (_map.isReading() && waitCount < 10) {
    vTaskDelay(10 / portTICK_PERIOD_MS);
    waitCount++;
  }
  if (_map.isReading()) {
    Log(WARN, "%ssetMap: Map-Lesevorgang läuft noch, ignoriere", _LOG_);
    return;
  }
  _map = map;
  _map.timestamp = millis();
  _lastUploadedMapId = "";
  _lastUploadedMapCrc = 0;
  _currentMapUnsaved = true;
  _mapListDirty = true;
  updateCurrentMapMeta();
  if (_currentMapId.startsWith("__t_")) {
    updateTransientMapMeta(_currentMapId, _map, _map.rotation);
  } else {
    storeCurrentMapDraft();
  }
  Log(INFO, "%ssetMap: perimeter=%d exclusions=%d dockpoints=%d waypoints=%d crc=%d area=%.1f",
      _LOG_, _map.perimeter.size(), _map.exclusions.size(), _map.dockpoints.size(), _map.waypoints.size(),
      _currentMapCrc, _currentMapArea);
}

void MowerAdapter::updateCurrentMapMeta() {
  // CRC und Fläche sind billig. Der MD5-Hash über das serialisierte
  // Geometrie-JSON (JsonDocument + String der ganzen Karte) ist es nicht und
  // wurde bei jedem setMap() – also mehrmals pro Sekunde beim Editieren –
  // berechnet. Er wird jetzt nur noch markiert und in currentMapHash()
  // bei Bedarf nachgeholt (Speichern, Intercept, Kartenliste).
  _currentMapHashDirty = true;
  _currentMapCrc = _mapManager.computeCrc(_map);
  _currentMapArea = _mapManager.computeArea(_map);
  Log(DBG, "%s CRC %d aus Geometrie (pts=%d/%d/%d/%d)",
      _LOG_, _currentMapCrc,
      _map.perimeter.size(), _map.exclusions.size(), _map.dockpoints.size(), _map.waypoints.size());
}

void MowerAdapter::setMowSettings(const ArduMower::Domain::Robot::MowSettings &s) {
  _mowSettings = s;
  _mowSettings.timestamp = millis();
  _lastUploadedMapId = "";
  _lastUploadedMapCrc = 0;
  // Mäh-Einstellungen als Teil der aktuellen Karte speichern, damit sie beim
  // Speichern/Laden/Export der Map erhalten bleiben.
  _map.pattern = _mowSettings.pattern;
  _map.mowOfs = _mowSettings.width;
  _map.patternAngle = _mowSettings.angle;
  _map.distanceToBorder = _mowSettings.distanceToBorder;
  _map.borderLaps = _mowSettings.borderLaps;
  _map.mowBorderCcw = _mowSettings.mowBorderCcw;
  _map.doMowArea = _mowSettings.doMowArea;
  _map.doMowPerimeter = _mowSettings.doMowPerimeter;
  _map.doMowBorder = _mowSettings.doMowBorder;
  _map.doMowExclusions = _mowSettings.doMowExclusions;
  _map.doMowExclusionBorder = _mowSettings.doMowExclusionBorder;
  _map.timestamp = millis();
  _currentMapUnsaved = true;
  _mapListDirty = true;

  if (_currentMapId.startsWith("__t_")) {
    updateTransientMapMeta(_currentMapId, _map, _map.rotation);
  } else {
    storeCurrentMapDraft();
  }

  Log(INFO, "%ssetMowSettings: pattern=%d width=%.2f angle=%d distToBorder=%d laps=%d doMowArea=%d doMowPerimeter=%d doMowBorder=%d doMowExclusionBorder=%d",
      _LOG_, _mowSettings.pattern, _mowSettings.width, _mowSettings.angle, _mowSettings.distanceToBorder, _mowSettings.borderLaps,
      _mowSettings.doMowArea, _mowSettings.doMowPerimeter, _mowSettings.doMowBorder, _mowSettings.doMowExclusionBorder);
}

void MowerAdapter::syncMowSettingsFromMap() {
  _mowSettings.pattern = _map.pattern;
  _mowSettings.width = _map.mowOfs;
  _mowSettings.angle = _map.patternAngle;
  _mowSettings.distanceToBorder = _map.distanceToBorder;
  _mowSettings.borderLaps = _map.borderLaps;
  _mowSettings.mowBorderCcw = _map.mowBorderCcw;
  _mowSettings.doMowArea = _map.doMowArea;
  _mowSettings.doMowPerimeter = _map.doMowPerimeter;
  _mowSettings.doMowBorder = _map.doMowBorder;
  _mowSettings.doMowExclusions = _map.doMowExclusions;
  _mowSettings.doMowExclusionBorder = _map.doMowExclusionBorder;
  _mowSettings.timestamp = millis();
  Log(INFO, "%ssyncMowSettingsFromMap: pattern=%d width=%.2f angle=%d doMowArea=%d doMowPerimeter=%d doMowBorder=%d doMowExclusionBorder=%d",
      _LOG_, _mowSettings.pattern, _mowSettings.width, _mowSettings.angle,
      _mowSettings.doMowArea, _mowSettings.doMowPerimeter, _mowSettings.doMowBorder, _mowSettings.doMowExclusionBorder);
}

std::vector<ArduMower::Domain::Robot::MapInfo> MowerAdapter::mapList() {
  std::vector<ArduMower::Domain::Robot::MapInfo> result;
  if (!_mapManager.begin()) return result;
  for (const auto &m : _mapManager.list()) {
    ArduMower::Domain::Robot::MapInfo info;
    info.id = m.id;
    info.name = m.name;
    info.area = m.area;
    info.hash = m.hash;
    info.crc = m.crc;
    info.rotation = m.rotation;
    info.timestamp = m.timestamp;
    if (_currentMapUnsaved && info.id == _currentMapId) {
      info.unsaved = true;
      if (_pendingRenameId == _currentMapId && _pendingRenameName.length() > 0) {
        info.name = _pendingRenameName;
      }
    }
    if (findMapDraft(info.id) != nullptr) info.unsaved = true;
    result.push_back(info);
  }
  // Transiente (RAM-only) Karten anzeigen, z. B. abgefangene Karten, die noch
  // nicht in SPIFFS gespeichert wurden.
  for (const auto &t : _transientMaps) {
    if (t.id.length() == 0) continue;
    ArduMower::Domain::Robot::MapInfo info;
    info.id = t.id;
    info.name = t.name;
    info.area = t.area;
    info.hash = t.hash;
    info.crc = t.crc;
    info.rotation = t.rotation;
    info.timestamp = t.timestamp;
    info.unsaved = true;
    info.requiresRename = t.requiresRename;
    if (_currentMapUnsaved && info.id == _currentMapId && _pendingRenameId == _currentMapId && _pendingRenameName.length() > 0) {
      info.name = _pendingRenameName;
    }
    result.push_back(info);
  }
  return result;
}

bool MowerAdapter::mapListDirty() {
  return _mapListDirty;
}

void MowerAdapter::clearMapListDirty() {
  _mapListDirty = false;
}

bool MowerAdapter::createMap(const String &name) {
  if (_map.isReading()) {
    Log(WARN, "%screateMap: Map-Lesevorgang läuft, anlegen abgelehnt", _LOG_);
    return false;
  }

  String mapName = name;
  if (mapName.length() == 0) mapName = _mapManager.generateDefaultName();
  if (isNameUsed(mapName)) {
    Log(WARN, "%screateMap: Name '%s' ist bereits vergeben", _LOG_, mapName.c_str());
    return false;
  }

  _map = ArduMower::Domain::Robot::MowerMap();
  _map.timestamp = millis();
  _currentMapId = allocateTransientId();
  _currentMapUnsaved = true;
  _pendingRenameId = "";
  _pendingRenameName = "";
  updateCurrentMapMeta();

  TransientMap transient;
  transient.id = _currentMapId;
  transient.name = mapName;
  transient.area = _currentMapArea;
  transient.hash = currentMapHash();
  transient.crc = _currentMapCrc;
  transient.rotation = _map.rotation;
  transient.timestamp = _map.timestamp;
  transient.requiresRename = false;
  transient.map = _map;
  _transientMaps.push_back(transient);
  _mapListDirty = true;
  Log(INFO, "%screateMap: transiente Karte %s als '%s' angelegt", _LOG_, _currentMapId.c_str(), mapName.c_str());
  return true;
}

bool MowerAdapter::copyMap(const String &name) {
  if (_map.isReading() || _map.perimeter.size() < 3) return false;

  String baseName = name.length() > 0 ? name : "Map";
  String mapName = baseName;
  for (int suffix = 2; isNameUsed(mapName); suffix++) {
    mapName = baseName + " " + String(suffix);
  }

  _currentMapId = allocateTransientId();
  _currentMapUnsaved = true;
  _pendingRenameId = "";
  _pendingRenameName = "";
  _map.timestamp = millis();
  updateCurrentMapMeta();

  TransientMap transient;
  transient.id = _currentMapId;
  transient.name = mapName;
  transient.area = _currentMapArea;
  transient.hash = currentMapHash();
  transient.crc = _currentMapCrc;
  transient.rotation = _map.rotation;
  transient.timestamp = _map.timestamp;
  transient.requiresRename = false;
  transient.map = _map;
  _transientMaps.push_back(transient);
  _mapListDirty = true;
  Log(INFO, "%scopyMap: transiente Kopie %s als '%s' angelegt", _LOG_, _currentMapId.c_str(), mapName.c_str());
  return true;
}

String MowerAdapter::saveMap(const String &name, double rotation) {
  if (_map.isReading()) {
    Log(WARN, "%ssaveMap: Map-Lesevorgang läuft, speichern abgelehnt", _LOG_);
    return "";
  }
  _map.rotation = rotation;
  String saveName = name;
  if (saveName.length() == 0 && _pendingRenameId == _currentMapId && _pendingRenameName.length() > 0) {
    saveName = _pendingRenameName;
  }
  String saveId = _currentMapId;
  if (saveId.startsWith("__t_")) {
    // Transiente Karte wird beim ersten Speichern in SPIFFS überführt.
    saveId = "";
  }
  String effectiveName = saveName.length() > 0 ? saveName : _pendingRenameName;
  if (effectiveName.length() > 0 && isNameUsed(effectiveName, _currentMapId)) {
    Log(WARN, "%ssaveMap: Name '%s' ist bereits vergeben, speichern abgelehnt", _LOG_, effectiveName.c_str());
    return "";
  }
  String id = _mapManager.save(_map, saveName, saveId, rotation);
  if (id.length() > 0) {
    if (_currentMapId.startsWith("__t_")) {
      removeTransientMap(_currentMapId);
    }
    removeMapDraft(id);
    _currentMapId = id;
    _currentMapUnsaved = false;
    _pendingRenameId = "";
    _pendingRenameName = "";
    _mapListDirty = true;
    Log(INFO, "%ssaveMap: Karte gespeichert als %s", _LOG_, id.c_str());
  }
  return id;
}

bool MowerAdapter::loadMap(const String &id) {
  // Warte kurz, bis kein Lesevorgang auf _map läuft (max 100ms)
  int waitCount = 0;
  while (_map.isReading() && waitCount < 10) {
    vTaskDelay(10 / portTICK_PERIOD_MS);
    waitCount++;
  }
  if (_map.isReading()) {
    Log(WARN, "%sloadMap: Map-Lesevorgang läuft noch, laden abgelehnt", _LOG_);
    return false;
  }
  // Transiente (RAM-only) Karten aus dem lokalen Vektor laden.
  if (id.startsWith("__t_")) {
    const auto *t = findTransientMap(id);
    if (!t) {
      Log(WARN, "%sloadMap: transiente Karte %s nicht gefunden", _LOG_, id.c_str());
      return false;
    }
    _currentMapId = id;
    _map = t->map;
    _currentMapHash = t->hash;
    _currentMapHashDirty = false;
    _currentMapArea = t->area;
    _currentMapCrc = t->crc;
    _currentMapUnsaved = true;
    _pendingRenameId = "";
    _pendingRenameName = "";
    syncMowSettingsFromMap();
    Log(INFO, "%sloadMap: transiente Karte %s geladen (unsaved)", _LOG_, id.c_str());
    _mapListDirty = true;
    return true;
  }
  if (const MapDraft *draft = findMapDraft(id)) {
    _currentMapId = id;
    _map = draft->map;
    updateCurrentMapMeta();
    _currentMapUnsaved = true;
    _pendingRenameId = "";
    _pendingRenameName = "";
    syncMowSettingsFromMap();
    Log(INFO, "%sloadMap: RAM-Entwurf %s geladen", _LOG_, id.c_str());
    _mapListDirty = true;
    return true;
  }
  ArduMower::Domain::Robot::MowerMap loaded;
  if (!_mapManager.load(id, loaded)) return false;
  _currentMapId = id;
  _map = loaded;
  _currentMapHash = _mapManager.computeHash(_map);
  _currentMapHashDirty = false;
  _currentMapArea = _mapManager.computeArea(_map);
  _currentMapCrc = _mapManager.getCrc(id);
  _currentMapUnsaved = false;
  _pendingRenameId = "";
  _pendingRenameName = "";
  syncMowSettingsFromMap();
  Log(INFO, "%sloadMap: Karte %s geladen, CRC %d (aus SPIFFS)", _LOG_, id.c_str(), _currentMapCrc);
  _mapListDirty = true;
  return true;
}

bool MowerAdapter::renameMap(const String &id, const String &name) {
  if (id != _currentMapId) return false;
  String newName = name;
  if (newName.length() == 0) newName = _mapManager.generateDefaultName();
  if (isNameUsed(newName, id)) {
    Log(WARN, "%srenameMap: Name '%s' ist bereits vergeben, Umbenennung abgelehnt", _LOG_, newName.c_str());
    return false;
  }
  if (id.startsWith("__t_")) {
    for (auto &t : _transientMaps) {
      if (t.id == id) {
        t.name = newName;
        t.timestamp = millis();
        t.requiresRename = false;
        _mapListDirty = true;
        Log(INFO, "%srenameMap: transiente Karte %s in '%s' umbenannt", _LOG_, id.c_str(), newName.c_str());
        return true;
      }
    }
    return false;
  }

  if (!_mapManager.rename(id, newName)) return false;
  _mapListDirty = true;
  Log(INFO, "%srenameMap: Karte %s in '%s' umbenannt", _LOG_, id.c_str(), newName.c_str());
  return true;
}

bool MowerAdapter::deleteMap(const String &id) {
  if (id.startsWith("__t_")) {
    // Transiente (RAM-only) Karte direkt aus dem RAM entfernen.
    if (!removeTransientMap(id)) {
      Log(WARN, "%sdeleteMap: transiente Karte %s nicht gefunden", _LOG_, id.c_str());
      return false;
    }
    if (_currentMapId == id) {
      // Gelöschte Karte war gerade geladen: auf die erste gespeicherte Karte
      // umschalten, falls vorhanden, sonst auf leere Karte zurücksetzen.
      const auto &maps = _mapManager.list();
      if (!maps.empty()) {
        _currentMapId = maps.front().id;
        if (!_mapManager.load(_currentMapId, _map)) {
          _currentMapId = "";
          _map = ArduMower::Domain::Robot::MowerMap();
          _map.timestamp = millis();
        }
      } else {
        _currentMapId = "";
        _map = ArduMower::Domain::Robot::MowerMap();
        _map.timestamp = millis();
      }
      _currentMapUnsaved = false;
      _pendingRenameId = "";
      _pendingRenameName = "";
      updateCurrentMapMeta();
    }
    _mapListDirty = true;
    Log(INFO, "%sdeleteMap: transiente Karte %s gelöscht", _LOG_, id.c_str());
    return true;
  }

  if (!_mapManager.remove(id)) return false;
  removeMapDraft(id);
  if (_currentMapId == id) {
    // Gelöschte Karte war gerade geladen: aktiv gespeicherte Karte wieder
    // herstellen, falls möglich, sonst auf leere Karte zurücksetzen.
    ArduMower::Domain::Robot::MowerMap loaded;
    if (_mapManager.loadActive(loaded)) {
      _currentMapId = _mapManager.activeId();
      _map = loaded;
      _currentMapCrc = _mapManager.getCrc(_currentMapId);
      syncMowSettingsFromMap();
    } else {
      _currentMapId = "";
      _map = ArduMower::Domain::Robot::MowerMap();
      _map.timestamp = millis();
    }
    // Der Unsaved-/Rename-Zustand gehörte zur gelöschten Karte; sonst wurde
    // die Ersatzkarte als "unsaved" angezeigt.
    _currentMapUnsaved = false;
    _pendingRenameId = "";
    _pendingRenameName = "";
    updateCurrentMapMeta();
  }
  _mapListDirty = true;
  Log(INFO, "%sdeleteMap: Karte %s gelöscht", _LOG_, id.c_str());
  return true;
}

bool MowerAdapter::discardMap() {
  if (_map.isReading()) {
    Log(WARN, "%sdiscardMap: Map-Lesevorgang läuft, verwerfen abgelehnt", _LOG_);
    return false;
  }
  // Transiente Karte beim Verwerfen aus dem RAM entfernen.
  if (_currentMapId.startsWith("__t_")) {
    removeTransientMap(_currentMapId);
  } else if (_currentMapId.length() > 0) {
    String currentId = _currentMapId;
    removeMapDraft(currentId);
    return loadMap(currentId);
  }
  // Unsaved/abgefangene Karte im RAM durch eine leere Karte ersetzen. Damit
  // bleibt Frontend und Backend konsistent, wenn der Benutzer "New Map"
  // auswählt und eine neue Karte zeichnet.
  _currentMapId = "";
  _map = ArduMower::Domain::Robot::MowerMap();
  _map.timestamp = millis();
  _currentMapUnsaved = true;
  _pendingRenameId = "";
  _pendingRenameName = "";
  updateCurrentMapMeta();
  _mapListDirty = true;
  Log(INFO, "%sdiscardMap: aktuelle Karte verworfen, neue leere Karte im RAM", _LOG_);
  return true;
}

bool MowerAdapter::setActiveMap(const String &id) {
  if (!_mapManager.setActive(id)) return false;
  _mapListDirty = true;
  Log(INFO, "%ssetActiveMap: Karte %s als aktiv gesetzt", _LOG_, id.c_str());
  return true;
}

String MowerAdapter::currentMapHash() {
  if (_currentMapHashDirty) {
    _currentMapHash = _mapManager.computeHash(_map);
    _currentMapHashDirty = false;
  }
  return _currentMapHash;
}

int MowerAdapter::currentMapCrc() {
  return _currentMapCrc;
}

double MowerAdapter::currentMapArea() {
  return _currentMapArea;
}

bool MowerAdapter::importMowerMap(const String &json, ArduMower::Domain::Robot::MowerMap &outMap) {
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, json);
  if (err) {
    Log(WARN, "%simportMowerMap: JSON parse failed: %s", _LOG_, err.c_str());
    return false;
  }
  if (!outMap.fromJson(doc.as<JsonObject>())) {
    Log(WARN, "%simportMowerMap: invalid map geometry", _LOG_);
    return false;
  }
  // Importierte Mäh-Einstellungen in den aktuellen Settings übernehmen.
  _mowSettings.pattern = outMap.pattern;
  _mowSettings.width = outMap.mowOfs;
  _mowSettings.angle = outMap.patternAngle;
  _mowSettings.distanceToBorder = outMap.distanceToBorder;
  _mowSettings.borderLaps = outMap.borderLaps;
  _mowSettings.doMowArea = outMap.doMowArea;
  _mowSettings.doMowPerimeter = outMap.doMowPerimeter;
  _mowSettings.doMowBorder = outMap.doMowBorder;
  _mowSettings.doMowExclusions = outMap.doMowExclusions;
  _mowSettings.doMowExclusionBorder = outMap.doMowExclusionBorder;
  _mowSettings.mowBorderCcw = outMap.mowBorderCcw;
  _mowSettings.timestamp = millis();
  return true;
}

String MowerAdapter::exportMowerMap(const ArduMower::Domain::Robot::MowerMap &map) {
  JsonDocument doc;
  JsonObject obj = doc.to<JsonObject>();
  map.toJson(obj);
  String out;
  serializeJsonPretty(doc, out);
  return out;
}

// ===== SPIFFS-Persistenz (vorbereitet, aber noch nicht aktiv) =====
// static const char *mapStorageFile = "/mower_map";
//
// void MowerAdapter::saveMap() { ... }
// void MowerAdapter::loadMap() { ... }

void MowerAdapter::begin()
{
  enc.setOn(settings.general.encryption);
  enc.setPassword(settings.general.password);
  router.sniffRx(this);
  router.sniffTx(this);

  // Initialize desiredState from persisted mower settings
  _desiredState.speed = settings.mower.mowSpeed;
  _desiredState.fixTimeout = settings.mower.fixTimeout;
  _desiredState.finishAndRestart = settings.mower.finishAndRestart;

  // Position settings werden erst nach dem ersten erfolgreichen Versions-Austausch
  // (parseVersionResponse) an den Mower gesendet. Hier wäre die Verbindung noch nicht bereit.

  // Persistierte Kartenverwaltung initialisieren und aktive Karte laden
  if (_mapManager.begin()) {
    ArduMower::Domain::Robot::MowerMap loaded;
    if (_mapManager.loadActive(loaded)) {
      _currentMapId = _mapManager.activeId();
      _map = loaded;
      _currentMapHash = _mapManager.computeHash(_map);
      _currentMapHashDirty = false;
      _currentMapArea = _mapManager.computeArea(_map);
      _currentMapCrc = _mapManager.getCrc(_mapManager.activeId());
      syncMowSettingsFromMap();
      Log(INFO, "%s begin: aktive Karte '%s' aus SPIFFS geladen, CRC %d (%.1f m²)",
          _LOG_, _mapManager.activeId().c_str(), _currentMapCrc, _currentMapArea);
    } else if (_mapManager.activeId().length() > 0) {
      // Aktive ID verweist auf nicht mehr vorhandene Datei -> Index bereinigen
      _mapManager.setActive("");
    }
    _mapListDirty = true;
  }
}

void MowerAdapter::drainRx(const char* line, bool &stop)
{
  parseArduMowerResponse(line);
}

void MowerAdapter::drainTx(const char* line, bool &stop)
{
  parseArduMowerCommand(line);
}

void MowerAdapter::parseArduMowerResponse(const char* line)
{
  Log(COMM, "<< %s", line);

  int len = strlen(line);
  if (len < 2 + 4)
  {
    Log(DBG, "%sparseArduMowerResponse::guard::length(%d)", _LOG_, len);
    return;
  }

  if (line[1] != ',' && !(line[0] == 'S' && (line[1] == '2' || line[1] == '3' || line[1] == '4')) && !(line[0] == 'U') && !(line[0] == '+' && line[1] == 'U'))
  {
    Log(DBG, "%sparseArduMowerResponse::guard::second-char(%c)", _LOG_, line[1]);
    return;
  }

  // Checksum-Präfix ",0x" per Pointer-Check statt substring-Allokation
  if (line[len-5] != ',' || line[len-4] != '0' || line[len-3] != 'x')
  {
    Log(DBG, "%sparseArduMowerResponse::guard::checksum-prefix", _LOG_);
    return;
  }

  const char* payload = line;

#if defined(ENABLE_LIVE_MAP) || defined(ENABLE_GPS_DASHBOARD)
  if (strncmp(payload, "U,", 2) == 0 || strncmp(payload, "+U,", 3) == 0)
    parseUbxResponse(payload);
  else if (strncmp(payload, "S4,", 3) == 0)
    { strncpy(_cachedRawGpsDetails, line, sizeof(_cachedRawGpsDetails) - 1); _cachedRawGpsDetails[sizeof(_cachedRawGpsDetails) - 1] = '\0'; parseGpsDetailsResponse(payload); }
  else if (strncmp(payload, "S2,", 3) == 0)
    parseObstaclesResponse(payload);
  else if (strncmp(payload, "S3,", 3) == 0)
#else
  if (strncmp(payload, "S2,", 3) == 0)
    parseObstaclesResponse(payload);
  else if (strncmp(payload, "S3,", 3) == 0)
#endif
    { strncpy(_cachedRawSensorSummary, line, sizeof(_cachedRawSensorSummary) - 1); _cachedRawSensorSummary[sizeof(_cachedRawSensorSummary) - 1] = '\0'; parseSensorSummaryResponse(payload); }
  else if (strncmp(payload, "S,", 2) == 0)
    { strncpy(_cachedRawState, line, sizeof(_cachedRawState) - 1); _cachedRawState[sizeof(_cachedRawState) - 1] = '\0'; parseStateResponse(payload); }
  else if (strncmp(payload, "V,", 2) == 0)
    parseVersionResponse(payload);
  else if (strncmp(payload, "T,", 2) == 0)
    { strncpy(_cachedRawStats, line, sizeof(_cachedRawStats) - 1); _cachedRawStats[sizeof(_cachedRawStats) - 1] = '\0'; parseStatisticsResponse(payload); }
  else
    Log(DBG, "%sparseArduMowerResponse::payload-unknown(%s)", _LOG_, payload);
}

void MowerAdapter::parseArduMowerCommand(const char* line)
{
  char mutableBuf[512];
  size_t len = strlen(line);
  if (len >= sizeof(mutableBuf)) len = sizeof(mutableBuf) - 1;
  memcpy(mutableBuf, line, len);
  mutableBuf[len] = '\0';

  if (strncmp(mutableBuf, "AT+", 3) != 0)
  {
    enc.decrypt(mutableBuf, len);
  }

  if (strncmp(mutableBuf, "AT+", 3) == 0) {
    for (size_t i = 0; i < len; i++) {
      unsigned char c = (unsigned char)mutableBuf[i];
      if (c < 0x20 && c != '\r' && c != '\n' && c != '\t')
        mutableBuf[i] = '?';
      else if (c >= 0x80)
        mutableBuf[i] = '?';
    }

    int badChar = containsNonUTF8(mutableBuf);
    if (badChar == -1) {
      Log(COMM, ">> %s", mutableBuf);
    } else {
      char save = mutableBuf[badChar];
      mutableBuf[badChar] = '\0';
      String hexString = bytesToHexString(String(mutableBuf + badChar));
      Log(COMM, ">> %s(%s)", mutableBuf,  hexString.c_str());
      mutableBuf[badChar] = save;
    }

    if (strncmp(mutableBuf, "AT+C", 4) == 0) parseATCCommand(mutableBuf);
    if (strncmp(mutableBuf, "AT+W", 4) == 0) parseATWCommand(mutableBuf);
    if (strncmp(mutableBuf, "AT+N", 4) == 0) parseATNCommand(mutableBuf);
    if (strncmp(mutableBuf, "AT+X", 4) == 0) parseATXCommand(mutableBuf);
  } else {
    String hexString = bytesToHexString(String(mutableBuf));
    Log(COMM, ">> (%s)", hexString.c_str());
  }
}

// AT-N Kommando: AT-N,#perimeter,#exclusionPoints,#dockpoints,#waypoints,#free
// Die Exclusion-Polygone werden über anschließende AT+X-Befehle mitgeteilt.
void MowerAdapter::parseATNCommand(const char* line) {
  Log(DBG, "%sparseATNCommand (map counts)", _LOG_);
  int waitCount = 0;
  while (_map.isReading() && waitCount < 10) {
    vTaskDelay(10 / portTICK_PERIOD_MS);
    waitCount++;
  }
  if (_map.isReading()) {
    Log(WARN, "%sparseATNCommand: Map-Lesevorgang läuft noch, ignoriere AT-N", _LOG_);
    return;
  }
  const char* p = line;
  if (strncmp(p, "AT+N,", 5) == 0) p += 5;
  std::vector<int> counts;
  while (*p) {
    counts.push_back(atoi(p));
    while (*p && *p != ',') p++;
    if (*p == ',') p++;
  }
  if (counts.size() < 5) {
    Log(ERR, "%sparseATNCommand: zu wenig Felder", _LOG_);
    return;
  }
  tempNPerimeter = counts[0];
  tempNExclusions = counts[1];
  tempNDockpoints = counts[2];
  tempNWaypoints = counts[3];
  tempMapCountsReceived = true;
  tempExclusionSizes.clear();

  if (tempNExclusions == 0) {
    finalizeInterceptedMap();
  }
}

// AT+X Kommando: AT+X,<startIdx>,<len1>,<len2>,...
void MowerAdapter::parseATXCommand(const char* line) {
  Log(DBG, "%sparseATXCommand (exclusion sizes)", _LOG_);
  int waitCount = 0;
  while (_map.isReading() && waitCount < 10) {
    vTaskDelay(10 / portTICK_PERIOD_MS);
    waitCount++;
  }
  if (_map.isReading()) {
    Log(WARN, "%sparseATXCommand: Map-Lesevorgang läuft noch, ignoriere AT-X", _LOG_);
    return;
  }
  const char* p = line;
  if (strncmp(p, "AT+X,", 5) == 0) p += 5;
  std::vector<int> values;
  while (*p) {
    values.push_back(atoi(p));
    while (*p && *p != ',') p++;
    if (*p == ',') p++;
  }
  if (values.size() < 2) {
    Log(ERR, "%sparseATXCommand: zu wenig Felder", _LOG_);
    return;
  }
  // values[0] = Startindex, danach folgen die Polygonlängen
  for (size_t i = 1; i < values.size(); i++) {
    tempExclusionSizes.push_back(values[i]);
  }

  if (tempMapCountsReceived) {
    int sum = 0;
    for (int s : tempExclusionSizes) sum += s;
    if (sum == tempNExclusions) {
      finalizeInterceptedMap();
    }
  }
}

void MowerAdapter::finalizeInterceptedMap() {
  using namespace ArduMower::Domain::Robot;
  Log(DBG, "%sfinalizeInterceptedMap: perimeter=%d exclusions=%d dock=%d waypoints=%d",
      _LOG_, tempNPerimeter, tempNExclusions, tempNDockpoints, tempNWaypoints);

  _map.perimeter.clear();
  _map.exclusions.clear();
  _map.dockpoints.clear();
  _map.waypoints.clear();

  int idx = 0;
  // Perimeter
  for (int i = 0; i < tempNPerimeter && idx < (int)tempWaypointsBuffer.size(); i++, idx++) {
    _map.perimeter.push_back(tempWaypointsBuffer[idx]);
  }
  // Exclusions anhand der AT+X-Längen aufteilen
  for (size_t ex = 0; ex < tempExclusionSizes.size(); ex++) {
    std::vector<MapPoint> excl;
    for (int j = 0; j < tempExclusionSizes[ex] && idx < (int)tempWaypointsBuffer.size(); j++, idx++) {
      excl.push_back(tempWaypointsBuffer[idx]);
    }
    _map.exclusions.push_back(excl);
  }
  // Dockpoints
  for (int i = 0; i < tempNDockpoints && idx < (int)tempWaypointsBuffer.size(); i++, idx++) {
    _map.dockpoints.push_back(tempWaypointsBuffer[idx]);
  }
  // Waypoints
  for (int i = 0; i < tempNWaypoints && idx < (int)tempWaypointsBuffer.size(); i++, idx++) {
    _map.waypoints.push_back(tempWaypointsBuffer[idx]);
  }

  // Summen prüfen
  bool ok = true;
  int totalExclPoints = 0;
  for (const auto &ex : _map.exclusions) totalExclPoints += ex.size();
  if ((int)_map.perimeter.size() != tempNPerimeter) {
    Log(ERR, "%sfinalizeInterceptedMap: perimeter count mismatch %d != %d", _LOG_, _map.perimeter.size(), tempNPerimeter);
    ok = false;
  }
  if (totalExclPoints != tempNExclusions) {
    Log(ERR, "%sfinalizeInterceptedMap: exclusion point count mismatch %d != %d", _LOG_, totalExclPoints, tempNExclusions);
    ok = false;
  }
  if ((int)_map.exclusions.size() != (int)tempExclusionSizes.size()) {
    Log(ERR, "%sfinalizeInterceptedMap: exclusion polygon count mismatch %d != %d", _LOG_, _map.exclusions.size(), tempExclusionSizes.size());
    ok = false;
  }
  if ((int)_map.dockpoints.size() != tempNDockpoints) {
    Log(ERR, "%sfinalizeInterceptedMap: dockpoints count mismatch %d != %d", _LOG_, _map.dockpoints.size(), tempNDockpoints);
    ok = false;
  }
  if ((int)_map.waypoints.size() != tempNWaypoints) {
    Log(ERR, "%sfinalizeInterceptedMap: waypoints count mismatch %d != %d", _LOG_, _map.waypoints.size(), tempNWaypoints);
    ok = false;
  }
  if (!ok) {
    Log(ERR, "%sfinalizeInterceptedMap: Map-Übertragung fehlerhaft, Abbruch", _LOG_);
    tempMapCountsReceived = false;
    tempExclusionSizes.clear();
    return;
  }
  Log(DBG, "%sfinalizeInterceptedMap: Map vollständig, sende an Client", _LOG_);
  _map.timestamp = millis();
  _mapListDirty = true;
  updateCurrentMapMeta();
  _currentMapUnsaved = true;

  // Vergleiche die abgefangene Geometrie mit den gespeicherten Karten. Wenn
  // eine identische Karte bereits existiert, laden wir deren Meta-Daten
  // (Name, Rotation) und verwenden deren ID, damit der UI-Zustand konsistent
  // bleibt und keine Dubletten entstehen.
  String hash = currentMapHash();
  String existingId = _mapManager.findByHash(hash);
  if (existingId.length() > 0) {
    Log(INFO, "%sfinalizeInterceptedMap: Karte bereits bekannt (%s), lade gespeicherte Version", _LOG_, existingId.c_str());
    loadMap(existingId);
  } else {
    Log(INFO, "%sfinalizeInterceptedMap: neue Karte im RAM, noch nicht gespeichert", _LOG_);
    // Neue abgefangene Karte als transiente (RAM-only) Karte anlegen, damit
    // sie im Dropdown erscheint und umbenannt werden kann, ohne sofort in
    // SPIFFS geschrieben zu werden.
    TransientMap t;
    t.id = allocateTransientId();
    t.name = _mapManager.generateDefaultName();
    t.area = _currentMapArea;
    t.hash = hash;
    t.crc = _currentMapCrc;
    t.rotation = _map.rotation;
    t.timestamp = _map.timestamp;
    t.map = _map;
    _transientMaps.push_back(t);
    _currentMapId = t.id;
  }

  // Abgefangene Karte nur im RAM übernehmen, NICHT automatisch in SPIFFS
  // speichern oder als Default setzen. Das verhindert versehentliche
  // Flash-Schreibvorgänge bei jedem Kartentransfer. Persistierung erfolgt
  // nur noch über den expliziten Save-Button in der UI.
  _mapListDirty = true;
  tempWaypointsBuffer.clear();
  tempExclusionSizes.clear();
  tempMapCountsReceived = false;
}

// start mowing
bool MowerAdapter::start()
{
  Log(DBG, "%sstart", _LOG_CMD_); 
  //    AT+C,   -1,                   -1,    -1,          -1,                 -1,      0.32,             -1,   -1
  // Command, Mow , start / stop / Dock , Speed, fix timeout, finish and restart, Waypoints, skip waypoint ,sonar
  return sendCommand("AT+C,-1,1,0.2,100,0,-1,-1,1");
}

// stop mowing
bool MowerAdapter::stop()
{
  Log(DBG, "%sstop", _LOG_CMD_);
  return sendCommand("AT+C,-1,0,-1,-1,-1,-1,-1,-1");
}

// dock mower to station
bool MowerAdapter::dock()
{
  Log(DBG, "%sdock", _LOG_CMD_);
  return sendCommand("AT+C,-1,4,-1,-1,-1,-1,-1,1");
}

// skip one Waypoint
bool MowerAdapter::skipWaypoint()
{
  Log(DBG, "%sskipWaypoint", _LOG_CMD_);
  return sendCommand("AT+C,-1,-1,-1,-1,-1,-1,1,-1");
}

// set to a waypoint as percent of maximum waypoint
bool MowerAdapter::setWaypoint(float waypoint)
{
  Log(DBG, "%ssetWaypoint(%.2f)", _LOG_CMD_, waypoint);
  char buffer[40];
  snprintf(buffer, sizeof(buffer), "AT+C,-1,-1,-1,-1,-1,%.2f,-1,-1", waypoint);
  String command(buffer);
  return sendCommand(command);
}

// change mower movement speed
bool MowerAdapter::changeSpeed(float speed)
{
  Log(DBG, "%schangeSpeed(%.2f)", _LOG_CMD_, speed);
  char buffer[40];
  snprintf(buffer, sizeof(buffer), "AT+C,-1,-1,%.2f,-1,-1,-1,-1,-1", speed);
  String command(buffer);
  return sendCommand(command);
}

// change way percentage (path coverage / overlap)
bool MowerAdapter::changeWayPerc(float perc)
{
  Log(DBG, "%schangeWayPerc(%.2f)", _LOG_CMD_, perc);
  char buffer[48];
  snprintf(buffer, sizeof(buffer), "AT+C,-1,-1,-1,-1,-1,%.2f,-1,-1", perc);
  String command(buffer);
  return sendCommand(command);
}

// change mow height (AT+S2)
bool MowerAdapter::changeMowHeight(int height)
{
  Log(DBG, "%schangeMowHeight(%d)", _LOG_CMD_, height);
  char buffer[32];
  snprintf(buffer, sizeof(buffer), "AT+S2,%d", height);
  String command(buffer);
  return sendCommand(command);
}

// change tune parameter (AT+CT,<index>,<value>)
bool MowerAdapter::tuneParam(int index, float value)
{
  Log(DBG, "%stuneParam(%d, %.4f)", _LOG_CMD_, index, value);
  char buffer[48];
  snprintf(buffer, sizeof(buffer), "AT+CT,%d,%.4f", index, value);
  String command(buffer);
  return sendCommand(command);
}

// set fix Timeout
bool MowerAdapter::setFixTimeout(int timeout)
{
  Log(DBG, "%ssetFixTimeout(%d)", _LOG_CMD_, timeout);
  String command = "AT+C,-1,-1,-1," + String(timeout) + ",-1,-1,-1,-1";
  return sendCommand(command);
}

// activate and deactivate mowMotor
bool MowerAdapter::mowerEnabled(bool enabled)
{
  Log(DBG, "%smowerEnabled(%d)", _LOG_CMD_, enabled);
  String command = "AT+C," + String(enabled ? "1" : "0") + ",-1,-1,-1,-1,-1,-1,-1";

  return sendCommand(command);
}

// leave mow motor state to mowing operation (AT+C,-1,...)
bool MowerAdapter::mowerAuto()
{
  Log(DBG, "%smowerAuto", _LOG_CMD_);
  return sendCommand("AT+C,-1,-1,-1,-1,-1,-1,-1,-1");
}

// activate and deactivate finish and restart
bool MowerAdapter::finishAndRestartEnabled(bool enabled)
{
  Log(DBG, "%sfinishAndRestartEnabled(%d)", _LOG_CMD_, enabled);
  String command = "AT+C,-1,-1,-1,-1," + String(enabled ? "1" : "0") + ",-1,-1,-1";

  return sendCommand(command);
}

// activate and deactivate sonar
bool MowerAdapter::sonarEnabled(bool enabled)
{
  Log(DBG, "%sfinishAndRestartEnabled(%d)", _LOG_CMD_, enabled);
  String command = "AT+C,-1,-1,-1,-1,-1,-1,-1," + String(enabled ? "1" : "0");

  return sendCommand(command);
}


bool MowerAdapter::requestVersion()
{
  Log(DBG, "%srequestVersion", _LOG_);
  return sendCommand("AT+V", false);
}

bool MowerAdapter::applyPositionSettings()
{
  const bool absolute = settings.position.mode == "absolute";
  const double lon = absolute ? settings.position.lon : 0.0;
  const double lat = absolute ? settings.position.lat : 0.0;

  // Skip if already applied with identical values (avoids AT+P spam after every AT+V)
  if (_lastPosApplied && _lastPosAbsolute == absolute &&
      fabs(_lastPosLon - lon) < 1e-9 && fabs(_lastPosLat - lat) < 1e-9) {
    Log(DBG, "%sapplyPositionSettings: unchanged, skipping", _LOG_);
    return true;
  }

  char command[96];
  snprintf(command, sizeof(command), "AT+P,%d,%.8f,%.8f",
           absolute ? 1 : 0, lon, lat);
  Log(INFO, "%sapplyPositionSettings: mode=%s lon=%.8f lat=%.8f", _LOG_,
      absolute ? "absolute" : "relative", lon, lat);

  bool ok = sendCommand(command, true);
  if (ok) {
    _lastPosApplied = true;
    _lastPosAbsolute = absolute;
    _lastPosLon = lon;
    _lastPosLat = lat;
  }
  return ok;
}

bool MowerAdapter::requestStatus()
{
  uint32_t now = millis();
  if (_lastStateRequest != 0 && now - _lastStateRequest < 5000) return true;
  _lastStateRequest = now ? now : 1;
  Log(DBG, "%srequestStatus", _LOG_);
  if (!assertSendIsInitialized())
    return false;
  return sendCommand("AT+S", true);
}

bool MowerAdapter::requestStats()
{
  uint32_t now = millis();
  if (_lastStatsRequest != 0 && now - _lastStatsRequest < 5000) return true;
  _lastStatsRequest = now ? now : 1;
  Log(DBG, "%srequestStats", _LOG_);
  if (!assertSendIsInitialized())
    return false;
  return sendCommand("AT+T", true);
}

bool MowerAdapter::requestStatusNow()
{
  _lastStateRequest = 0;
  return requestStatus();
}

bool MowerAdapter::requestStatsNow()
{
  _lastStatsRequest = 0;
  return requestStats();
}

bool MowerAdapter::requestSensorSummary()
{
  uint32_t now = millis();
  if (_lastSensorSummaryRequest != 0 && now - _lastSensorSummaryRequest < 5000) return true;
  _lastSensorSummaryRequest = now ? now : 1;
  //Log(DBG, "%srequestSensorSummary", _LOG_);
  if (!assertSendIsInitialized())
    return false;
  return sendCommand("AT+S3", true);
}

bool MowerAdapter::requestControl()
{
  uint32_t now = millis();
  if (_lastControlRequest != 0 && now - _lastControlRequest < 5000) return true;
  _lastControlRequest = now ? now : 1;
  Log(DBG, "%srequestControl", _LOG_);
  if (!assertSendIsInitialized())
    return false;
  return sendCommand("AT+C", true);
}

bool MowerAdapter::requestObstacles()
{
  uint32_t now = millis();
  if (_lastObstaclesRequest != 0 && now - _lastObstaclesRequest < 5000) return true;
  _lastObstaclesRequest = now ? now : 1;
  Log(DBG, "%srequestObstacles", _LOG_);
  if (!assertSendIsInitialized())
    return false;
  return sendCommand("AT+S2", true);
}

#if defined(ENABLE_LIVE_MAP) || defined(ENABLE_GPS_DASHBOARD)
bool MowerAdapter::requestGpsDetails()
{
  uint32_t now = millis();
  if (_lastGpsDetailsRequest != 0 && now - _lastGpsDetailsRequest < 20000) return true;
  _lastGpsDetailsRequest = now ? now : 1;
  Log(DBG, "%srequestGpsDetails", _LOG_);
  if (!assertSendIsInitialized())
    return false;
  return sendCommand("AT+S4", true);
}

bool MowerAdapter::sendUbx(const String &hexCmd)
{
  Log(DBG, "%ssendUbx(%s)", _LOG_, hexCmd.c_str());
  if (!assertSendIsInitialized())
    return false;
  return sendCommand("AT+UBX," + hexCmd, true);
}

void MowerAdapter::parseUbxResponse(const char* line)
{
  Log(DBG, "%sparseUbxResponse", _LOG_);
  const auto now = millis();

  const char* comma = strchr(line, ',');
  if (comma != NULL) {
    // Sunray antwortet mit "U,<hex>,0x<crc>\r\n". Wir speichern nur den
    // Hex-Teil und entfernen die abschließende AT-CRC.
    _ubxResponse.hexData = String(comma + 1);
    int crcIdx = _ubxResponse.hexData.indexOf(",0x");
    if (crcIdx >= 0) {
      _ubxResponse.hexData = _ubxResponse.hexData.substring(0, crcIdx);
    }
  } else {
    _ubxResponse.hexData = "";
  }
  _ubxResponse.timestamp = now;
}
#endif

// linear: m/s
// angular: rad/s
bool MowerAdapter::manualDrive(float linear, float angular)
{
  Log(DBG, "%smanualDrive(%.2f, %.2f)", _LOG_, linear, angular);
  char buffer[40];
  snprintf(buffer, sizeof(buffer), "AT+M,%.2f, %.2f", linear, angular);
  String command(buffer);
  return sendCommand(command);
}

// navigate to point (autonomous, firmware handles navigation)
bool MowerAdapter::navigateTo(float x, float y)
{
  Log(DBG, "%snavigateTo(%.2f, %.2f)", _LOG_CMD_, x, y);
  if (_map.timestamp == 0 || _map.perimeter.size() == 0) {
    Log(WARN, "%snavigateTo rejected: no map loaded", _LOG_);
    return false;
  }
  char buffer[50];
  snprintf(buffer, sizeof(buffer), "AT+R,%.2f,%.2f", x, y);
  String command(buffer);
  return sendCommand(command);
}

// reboot the mower
bool MowerAdapter::reboot()
{
  Log(DBG, "%sreboot()", _LOG_);
  return sendCommand("AT+Y");
}

// reboot GPS
bool MowerAdapter::rebootGPS()
{
  Log(DBG, "%srebootGPS()", _LOG_);
  return sendCommand("AT+Y2");
}

// power off the mower
bool MowerAdapter::powerOff()
{
  Log(DBG, "%spowerOff()", _LOG_);
  return sendCommand("AT+Y3");
}

// custom command
bool MowerAdapter::customCmd(String cmd)
{
  if(cmd.indexOf("AT+")){
    Log(ERR, "%scustom command %s is invalid", _LOG_, cmd.c_str());
    return false;
  }
  else {
    Log(DBG, "%scustom command %s is valid and forwarded", _LOG_, cmd.c_str());
    return sendCommand(cmd);
  }
}

void MowerAdapter::parseStatisticsResponse(const char* line)
{
  Log(DBG, "%sparseStatisticsResponse", _LOG_);
  const auto now = millis();

  processCSVResponse(line, [&](int index, const char* val, size_t len)
                     {
                       (void)len;
                       switch (index)
                       {
                       case 1:
                         _stats.durations.idle = atoi(val);
                         break;
                       case 2:
                         _stats.durations.charge = atoi(val);
                         break;
                       case 3:
                         _stats.durations.mow = atoi(val);
                         break;
                       case 4:
                         _stats.durations.mowFloat = atoi(val);
                         break;
                       case 5:
                         _stats.durations.mowFix = atoi(val);
                         break;

                       case 6:
                         _stats.recoveries.mowFloatToFix = atoi(val);
                         break;

                        case 7:
                          _stats.mowDistanceTraveled = atof(val);
                          break;
                        case 8:
                          _stats.mowMaxDgpsAge = atof(val);
                          break;

                        case 9:
                          _stats.recoveries.imu = atoi(val);
                          break;

                        case 10:
                          _stats.tempMin = atof(val);
                          break;
                        case 11:
                          _stats.tempMax = atof(val);
                          break;

                        case 12:
                          _stats.gpsChecksumErrors = atoi(val);
                          break;
                        case 13:
                          _stats.dgpsChecksumErrors = atoi(val);
                          break;
                        case 14:
                          _stats.maxMotorControlCycleTime = atof(val);
                          break;
                        case 15:
                          _stats.serialBufferSize = atoi(val);
                          break;
                        case 16:
                          _stats.durations.mowInvalid = atoi(val);
                          break;
                        case 17:
                          _stats.recoveries.mowInvalid = atoi(val);
                          break;
                        case 18:
                          _stats.obstacles.count = atoi(val);
                          break;
                        case 19:
                          _stats.freeMemory = atoi(val);
                          break;
                        case 20:
                          _stats.resetCause = atoi(val);
                          break;
                        case 21:
                          _stats.gpsJumps = atoi(val);
                          break;
                        case 22:
                          _stats.obstacles.sonar = atoi(val);
                          break;
                        case 23:
                          _stats.obstacles.bumper = atoi(val);
                          break;
                        case 24:
                          _stats.obstacles.gpsMotionLow = atoi(val);
                          break;
                       }
                     });

  _stats.timestamp = now;
}

void MowerAdapter::parseSensorSummaryResponse(const char* line)
{
  //Log(DBG, "%sparseSensorSummaryResponse", _LOG_);
  const auto now = millis();

  processCSVResponse(line, [&](int index, const char* val, size_t len)
                     {
                       (void)len;
                       // S3,<left>,<center>,<right>,<sonarObs>,<sonarNear>,<bumpL>,<bumpR>,<bumpObs>,<bumpNear>,<lidarObs>,<lidarNear>,<lift>,<rain>
                       switch (index)
                       {
                       case 1:
                         _sensorSummary.sonarLeft = atof(val);
                         break;
                       case 2:
                         _sensorSummary.sonarCenter = atof(val);
                         break;
                       case 3:
                         _sensorSummary.sonarRight = atof(val);
                         break;
                       case 4:
                         _sensorSummary.sonarObstacle = atoi(val) == 1;
                         break;
                       case 5:
                         _sensorSummary.sonarNearObstacle = atoi(val) == 1;
                         break;
                       case 6:
                         _sensorSummary.bumperLeft = atoi(val) == 1;
                         break;
                       case 7:
                         _sensorSummary.bumperRight = atoi(val) == 1;
                         break;
                       case 8:
                         _sensorSummary.bumperObstacle = atoi(val) == 1;
                         break;
                       case 9:
                         _sensorSummary.bumperNearObstacle = atoi(val) == 1;
                         break;
                       case 10:
                         _sensorSummary.lidarObstacle = atoi(val) == 1;
                         break;
                       case 11:
                         _sensorSummary.lidarNearObstacle = atoi(val) == 1;
                         break;
                       case 12:
                         _sensorSummary.liftTriggered = atoi(val) == 1;
                         break;
                       case 13:
                         _sensorSummary.rainTriggered = atoi(val) == 1;
                         break;
                       }
                     });

  _sensorSummary.timestamp = now;
}

// AT+S2 Antwort: S2,<numPolygons>[,<r>,<g>,<b>,<numPoints>,<x1>,<y1>,...]
// Die RGB-Felder werden wie in CaSSAndRA geparsed, aber ignoriert (UI rendert immer orange).
void MowerAdapter::parseObstaclesResponse(const char* line)
{
  Log(DBG, "%sparseObstaclesResponse", _LOG_);
  const auto now = millis();

  // Felder zählen: [0]=S2, [1]=numPolygons, dann pro Polygon [r,g,b,numPoints,x1,y1,...]
  int numPolygons = 0;
  bool firstFieldDone = false;
  bool changed = false;

  // Erster Durchlauf: Anzahl Polygone ermitteln
  processCSVResponse(line, [&](int index, const char* val, size_t len)
                     {
                       (void)len;
                       if (index == 1 && !firstFieldDone) {
                         numPolygons = atoi(val);
                         firstFieldDone = true;
                       }
                     });

  if (numPolygons == 0) {
    // Firmware meldet keine Hindernisse mehr → lokalen Cache leeren (Sync-Modus)
    if (!_obstacles.empty()) {
      _obstacles.clear();
      _obstacles.timestamp = now;
    }
    return;
  }

  // Zweiter Durchlauf: Polygone parsen
  // Feld-Layout nach "S2,<numPolygons>": pro Polygon: r,g,b,numPoints,x1,y1,...
  // phase: 0=warte auf r, 1=warte auf g, 2=warte auf b, 3=warte auf numPoints, 4=lese Koordinaten
  int phase = 0;
  int fieldsLeft = 0;
  bool xPending = false;
  float pendingX = 0.0f;
  ArduMower::Domain::Robot::ObstaclePolygon building;

  auto finishBuilding = [&]() {
    if (building.points.empty()) return;
    int32_t crc = 0;
    for (const auto &p : building.points) {
      crc += (int32_t)(p.first * 100.0f) + (int32_t)(p.second * 100.0f);
    }
    building.crc = crc;
    if (_obstacles.addIfNew(std::move(building))) changed = true;
    building = ArduMower::Domain::Robot::ObstaclePolygon();
  };

  processCSVResponse(line, [&](int index, const char* val, size_t len)
                     {
                       (void)len;
                       if (index <= 1) return; // "S2" und numPolygons schon verarbeitet

                       switch (phase) {
                       case 0: // r
                         finishBuilding(); // vorheriges Polygon abschließen
                         phase = 1;
                         break;
                       case 1: // g
                         phase = 2;
                         break;
                       case 2: // b
                         phase = 3;
                         break;
                       case 3: // numPoints
                         fieldsLeft = atoi(val) * 2;
                         phase = 4;
                         xPending = false;
                         break;
                       case 4: // Koordinaten
                         fieldsLeft--;
                         if (!xPending) {
                           pendingX = atof(val);
                           xPending = true;
                         } else {
                           building.points.push_back({pendingX, atof(val)});
                           xPending = false;
                         }
                         if (fieldsLeft == 0) phase = 0; // Polygon fertig
                         break;
                       }
                     });

  // Letztes Polygon abschließen
  finishBuilding();

  if (changed) {
    _obstacles.timestamp = now;
    Log(INFO, "%sparseObstaclesResponse: %d obstacles cached", _LOG_, (int)_obstacles.polygons.size());
  }
}

#if defined(ENABLE_LIVE_MAP) || defined(ENABLE_GPS_DASHBOARD)
void MowerAdapter::parseGpsDetailsResponse(const char* line)
{
  Log(DBG, "%sparseGpsDetailsResponse", _LOG_);
  const auto now = millis();

  _gpsDetails.timestamp = now;

  int satCount = 0;
  int fieldIdx = -1;

  // Erster Durchlauf: Header-Felder parsen und Gesamtzahl der Token ermitteln
  processCSVResponse(line, [&](int index, const char* val, size_t len)
                     {
                       (void)len;
                       fieldIdx = index;
                       switch (index)
                       {
                       case 1:
                         _gpsDetails.numSV = atoi(val);
                         break;
                       case 2:
                         _gpsDetails.numSVdgps = atoi(val);
                         break;
                       case 3:
                         _gpsDetails.solution = atoi(val);
                         break;
                       case 4:
                         _gpsDetails.hAccuracy = atof(val);
                         break;
                       case 5:
                         _gpsDetails.vAccuracy = atof(val);
                         break;
                       case 6:
                         _gpsDetails.dgpsAge = atoi(val);
                         break;
                       case 7:
                         satCount = atoi(val);
                         break;
                       }
                     });

  // Format anhand der Gesamtfeldzahl bestimmen.
  // processCSVResponse zählt das abschließende CRC-Feld (0xHH) mit,
  // daher müssen wir es hier wieder abziehen.
  int totalFields = fieldIdx + 1;
  int dataFields = totalFields - 8 - 1;
  int fieldsPerSat = (satCount > 0) ? dataFields / satCount : 8;

  if (fieldsPerSat != 10 && fieldsPerSat != 8) {
    Log(WARN, "%sparseGpsDetailsResponse unexpected fieldsPerSat=%d sats=%d total=%d",
        _LOG_, fieldsPerSat, satCount, totalFields);
    fieldsPerSat = 8;
  }

  // Zweiter Durchlauf: Satellitenfelder on-the-fly parsen (kein Vector!)
  _gpsDetails.satellites.clear();
  processCSVResponse(line, [&](int index, const char* val, size_t len)
                     {
                       (void)len;
                       if (index < 8) return;

                       int satField = index - 8;
                       int satIdx = satField / fieldsPerSat;
                       int col = satField % fieldsPerSat;

                       if (satIdx >= satCount || satIdx >= 40) return;

                       if ((size_t)satIdx >= _gpsDetails.satellites.size())
                         _gpsDetails.satellites.resize(satIdx + 1);

                       auto& sat = _gpsDetails.satellites[satIdx];
                       switch (col) {
                       case 0: sat.gnssId = atoi(val); break;
                       case 1: sat.svId = atoi(val); break;
                       case 2: sat.sigId = atoi(val); break;
                       case 3: sat.cno = atoi(val); break;
                       case 4: sat.qualityInd = atoi(val); break;
                       case 5: sat.prUsed = atoi(val) == 1; break;
                       case 6: sat.crCorrUsed = atoi(val) == 1; break;
                       case 7: sat.prRes = atof(val); break;
                       case 8: sat.elevation = (int8_t)atoi(val); break;
                       case 9: sat.azimuth = (int8_t)atoi(val); break;
                       }
                     });

  Log(DBG, "%sparseGpsDetailsResponse done sats=%d/%d fields=%d fps=%d",
      _LOG_, (int)_gpsDetails.satellites.size(), satCount, fieldIdx, fieldsPerSat);
}
#endif

void MowerAdapter::parseVersionResponse(const char* line)
{
  Log(DBG, "%sparseVersionResponse", _LOG_);
  const auto now = millis();

  processCSVResponse(line, [&](int index, const char* val, size_t len)
                     {
                       // V,Ardumower Sunray,1.0.189,1,73,0x58\r
                       switch (index)
                       {
                       case 1:
                         _props.firmware = String(val, len);
                         break;
                       case 2:
                         _props.version = String(val, len);
                         break;
                       case 3:
                         enc.setOn(atoi(val) == 1);
                         break;
                       case 4:
                         enc.setChallenge(atoi(val));
                         break;
                       }
                     });

  _props.timestamp = now;
  sendIsInitialized = true;

  // Positionseinstellungen an den Mower senden, sobald die Kommunikation bereit ist
  applyPositionSettings();
}

void MowerAdapter::parseStateResponse(const char* line)
{
  Log(DBG, "%sparseStateResponse", _LOG_);
  const auto now = millis();

  processCSVResponse(line, [&](int index, const char* val, size_t len)
                     {
                       (void)len;
                       switch (index)
                       {
                       case 1:
                         _state.batteryVoltage = atof(val);
                         break;
                       case 2:
                         _state.position.x = atof(val);
                         break;
                       case 3:
                         _state.position.y = atof(val);
                         break;
                       case 4:
                         _state.position.delta = atof(val);
                         break;
                       case 5:
                         _state.position.solution = atoi(val);
                         break;
                       case 6:
                         _state.job = atoi(val);
                         break;
                       case 7:
                         _state.position.mowPointIndex = atoi(val);
                         break;
                       case 8:
                         _state.position.age = atof(val);
                         break;
                       case 9:
                         _state.sensor = atoi(val);
                         break;

                       case 10:
                         _state.target.x = atof(val);
                         break;
                       case 11:
                         _state.target.y = atof(val);
                         break;

                       case 12:
                         _state.position.accuracy = atof(val);
                         break;
                       case 13:
                         _state.position.visibleSatellites = atoi(val);
                         break;
                       case 14:
                         _state.amps = atof(val);
                         break;
                       case 15:
                         _state.position.visibleSatellitesDgps = atoi(val);
                         break;
                       case 16:
                         _state.mapCrc = atoi(val);
                         break;
                       
                       // incompatible to sunray upstream
                       case 20:
                         _state.chargingMah = atof(val);
                         break;
                       case 21:
                         _state.motorLeftMah = atof(val);
                         break;
                       case 22:
                         _state.motorRightMah = atof(val);
                         break;
                       case 23:
                         _state.motorMowMah = atof(val);
                         break;
                       case 24:
                         _state.temperature = atof(val);
                         break;
                       }
                     });

  _state.timestamp = now;
}

void MowerAdapter::parseATWCommand(const char* line)
{
  Log(DBG, "%sparseATWCommand (waypoint-list)", _LOG_);
  if (_map.isReading()) {
    Log(DBG, "%sparseATWCommand: Map-Lesevorgang läuft, ignoriere", _LOG_);
    return;
  }
  const char* p = line;
  if (strncmp(p, "AT+W,", 5) == 0) p += 5;
  std::vector<float> values;
  while (*p) {
    values.push_back(atof(p));
    while (*p && *p != ',') p++;
    if (*p == ',') p++;
  }

  if (values.size() < 3) return; // Mindestens Index, x, y

  int widx = (int)values[0];
  size_t dataStart = 1;

  using namespace ArduMower::Domain::Robot;
  // Nur beim ersten Block (widx==0) die Waypoint-Liste leeren
  if (widx == 0) {
    tempWaypointsBuffer.clear();
  }
  // Schreibe die empfangenen Punkte ab Startindex (widx) in die Waypoint-Liste
  int writeIdx = widx;

  bool overwriteLogged = false;
  for (size_t i = dataStart; i + 1 < values.size(); i += 2, writeIdx++) {
    float x = values[i];
    float y = values[i + 1];
    if ((int)tempWaypointsBuffer.size() < writeIdx) {
      Log(ERR, "%sparseATWCommand: Indexsprung! writeIdx=%d, BufferSize=%d", _LOG_, writeIdx, (int)tempWaypointsBuffer.size());
    }
    if ((int)tempWaypointsBuffer.size() <= writeIdx) {
      tempWaypointsBuffer.resize(writeIdx + 1);
    } else {
      if (!overwriteLogged) {
        Log(DBG, "%sparseATWCommand: Überschreibe bestehenden Punkt an writeIdx=%d", _LOG_, writeIdx);
        overwriteLogged = true;
      }
    }
    tempWaypointsBuffer[writeIdx] = MapPoint{x, y};
  }
  
  Log(DBG, "%sparseATWCommand done widx=%d, waypoints.size()=%d", _LOG_, widx, tempWaypointsBuffer.size());
}

void MowerAdapter::parseATCCommand(const char* line)
{
  Log(DBG, "%sparseATCCommand", _LOG_);
  const auto now = millis();

  processCSVResponse(
      line,
      [&](int index, const char* val, size_t len)
      {
        (void)len;
        if (atoi(val) == -1)
          return;

        switch (index)
        {
        case 1:
          _desiredState.mowerMotorEnabled = atoi(val) == 1;
          break;
        case 2:
          _desiredState.op = atoi(val);
          break;
        case 3:
          _desiredState.speed = atof(val);
          break;
        case 4:
          _desiredState.fixTimeout = atoi(val);
          break;
        case 5:
          _desiredState.finishAndRestart = atoi(val) == 1;
          break;
        case 6:
          // setMowingPointPercent
          break;
        case 7:
          // skipNextMowingPoint
          break;
        case 8:
          // sonarEnabled
          break;
        }
      });

  _desiredState.timestamp = now;
}

bool MowerAdapter::assertSendIsInitialized()
{
  if (sendIsInitialized) {
    uint32_t age = _state.timestamp > 0
        ? (millis() - _state.timestamp)
        : (millis() - _props.timestamp);
    if (age > 30000) {
      static uint32_t lastStaleWarn = 0;
      uint32_t now = millis();
      if (now - lastStaleWarn >= 10000) {
        lastStaleWarn = now;
        Log(WARN, "%sstate stale for 30s, STM32 may have rebooted - requesting version", _LOG_);
      }
      _state.timestamp = 0;
      sendIsInitialized = false;
      _lastPosApplied = false;  // Force re-send of AT+P after Sunray reboot
    } else {
      return true;
    }
  }

  const uint32_t now = millis();
  static uint32_t next_time = 0;
  if (now < next_time)
    return false;
  next_time = now + 1000;

  Log(DBG, "%sassertSendIsInitialized::request-version", _LOG_);

  if (!requestVersion()) {
    next_time = now + 5000;
    return false;
  }

  Log(DBG, "%sassertSendIsInitialized::version-requested", _LOG_);

  return false;
}

int MowerAdapter::containsNonUTF8(const String& input) {
  const uint8_t* data = (const uint8_t*)input.c_str();
  size_t len = input.length();
  int i = 0;
  while (i < len) {
    if ((data[i] & 0x80) == 0x00) { // 0xxxxxxx (single byte)
      i++;
    } else if ((data[i] & 0xE0) == 0xC0) { // 110xxxxx (two bytes)
      if (i + 1 >= len || (data[i + 1] & 0xC0) != 0x80) return i;
      i += 2;
    } else if ((data[i] & 0xF0) == 0xE0) { // 1110xxxx (three bytes)
      if (i + 2 >= len || (data[i + 1] & 0xC0) != 0x80 || (data[i + 2] & 0xC0) != 0x80) return i;
      i += 3;
    } else if ((data[i] & 0xF8) == 0xF0) { // 11110xxx (four bytes)
      if (i + 3 >= len || (data[i + 1] & 0xC0) != 0x80 || (data[i + 2] & 0xC0) != 0x80 || (data[i + 3] & 0xC0) != 0x80) return i;
      i += 4;
    } else { // Invalid starting byte
      return i;
    }
  }
  return -1;
}

String MowerAdapter::bytesToHexString(const String& byteString) {
  String hexString = "";
  for (size_t i = 0; i < byteString.length(); ++i) {
    byte currentByte = byteString.charAt(i);
    char hexBuffer[3]; // Platz für zwei Hex-Ziffern und das Nullterminierungszeichen
    sprintf(hexBuffer, "%02X", currentByte); // %02X formatiert als zweistellige Hex-Zahl mit führender Null
    hexString += String(hexBuffer);
  }
  return hexString;
}

bool MowerAdapter::uploadSavedMapToMower(const String &id)
{
  if (_mapUploadState.active || _mapUploadPending) {
    Log(WARN, "%suploadSavedMapToMower: upload already in progress", _LOG_);
    return false;
  }
  if (id.length() == 0 || id.startsWith("__t_")) {
    Log(WARN, "%suploadSavedMapToMower: map %s is not persisted", _LOG_, id.c_str());
    return false;
  }
  // Immer die SPIFFS-Version: RAM-Entwürfe (ungespeicherte Änderungen) werden
  // bewusst nicht hochgeladen.
  ArduMower::Domain::Robot::MowerMap loaded;
  if (!_mapManager.load(id, loaded)) {
    Log(WARN, "%suploadSavedMapToMower: map %s could not be loaded", _LOG_, id.c_str());
    return false;
  }
  _mapUploadOverride = loaded;
  _mapUploadOverrideId = id;
  _mapUploadOverridePending = true;
  // Ergebnis des vorherigen Uploads verwerfen: der Upload startet erst im
  // nächsten loop()-Durchlauf, bis dahin meldete uploadMapToMowerSuccess()
  // noch "done" vom letzten Mal – der Aufrufer hätte den Mäher gestartet,
  // bevor die Karte übertragen war.
  _mapUploadState.phase = MapUploadState::idle;
  _mapUploadPending = true;
  Log(INFO, "%suploadSavedMapToMower: queued saved map %s", _LOG_, id.c_str());
  return true;
}

ArduMower::Domain::Robot::MowSettings MowerAdapter::settingsFromMap(const ArduMower::Domain::Robot::MowerMap &map)
{
  ArduMower::Domain::Robot::MowSettings s;
  s.pattern = map.pattern;
  s.width = map.mowOfs;
  s.angle = map.patternAngle;
  s.distanceToBorder = map.distanceToBorder;
  s.borderLaps = map.borderLaps;
  s.mowBorderCcw = map.mowBorderCcw;
  s.doMowArea = map.doMowArea;
  s.doMowPerimeter = map.doMowPerimeter;
  s.doMowBorder = map.doMowBorder;
  s.doMowExclusions = map.doMowExclusions;
  s.doMowExclusionBorder = map.doMowExclusionBorder;
  s.timestamp = millis();
  return s;
}

bool MowerAdapter::uploadMapToMower()
{
  if (_mapUploadState.active || _mapUploadPending) {
    Log(WARN, "%suploadMapToMower: upload already in progress", _LOG_);
    return false;
  }

  _mapUploadState.phase = MapUploadState::idle;
  _mapUploadPending = true;
  Log(INFO, "%suploadMapToMower: queued", _LOG_);
  return true;
}

void MowerAdapter::startMapUploadFromLoop()
{
  if (!_mapUploadPending) return;
  _mapUploadPending = false;

  if (_mapUploadState.active) {
    Log(WARN, "%sstartMapUploadFromLoop: upload already active", _LOG_);
    return;
  }

  const bool useOverride = _mapUploadOverridePending;
  _mapUploadOverridePending = false;
  if (useOverride) {
    // Scheduler: gespeicherte Karte hochladen. _map bleibt unangetastet und
    // wird nicht gesperrt, damit ein offener Editor weiterarbeiten kann.
    _mapUploadLockedMap = false;
    _mapUploadSourceId = _mapUploadOverrideId;
    _mapUploadState.snapshot = _mapUploadOverride;
    _mapUploadOverride = ArduMower::Domain::Robot::MowerMap();
  } else {
    _map.beginRead();
    _mapUploadLockedMap = true;
    _mapUploadSourceId = _currentMapId;
    _mapUploadState.snapshot = _map;
  }
  // Temporäre Intercept-Buffer für die Dauer des eigenen Uploads zurücksetzen,
  // damit keine alten/inkonsistenten Zustände eine spätere App-Übertragung stören.
  tempWaypointsBuffer.clear();
  tempExclusionSizes.clear();
  tempMapCountsReceived = false;
  _mapUploadState.active = true;
  _mapUploadState.phase = MapUploadState::start;
  _mapUploadState.polygonIdx = 0;
  _mapUploadState.pointIdx = 0;
  _mapUploadState.chunkRetry = 0;
  _mapUploadState.totalPointsSent = 0;
  _mapUploadState.lastResponse[0] = '\0';
  _mapUploadState.lastOk = false;
  _mapUploadState.waitingForResponse = false;
  _mapUploadState.lastCommandQueued = false;
  // _map bleibt im reading-Zustand, um parseATNCommand/setMap während des Uploads zu blockieren

  auto &snap = _mapUploadState.snapshot;
#ifdef ENABLE_MAP
  // Filtere die Wegpunkte für den Upload anhand der aktuellen Runtime-Toggles.
  // Die Karte selbst bleibt ungefiltert, damit die Toggles nur die Anzeige/Upload
  // beeinflussen und nicht die gespeicherte Berechnung.
  // Die gespeicherte Karte trägt ihre eigenen Mäh-Einstellungen; die aktuellen
  // Runtime-Toggles gehören zur gerade geladenen Karte.
  auto settings = useOverride ? settingsFromMap(snap) : _mowSettings;
  auto filtered = ArduMower::Modem::PathPlanner::filterRouteByToggles(snap.waypoints, snap, settings);
  snap.waypoints = filtered;
#else
  Log(INFO, "%sstartMapUploadFromLoop: started (perimeter=%d exclusions=%d dock=%d waypoints=%d)",
      _LOG_, snap.perimeter.size(), snap.exclusions.size(), snap.dockpoints.size(), snap.waypoints.size());
#endif
  Log(INFO, "%sstartMapUploadFromLoop: started (perimeter=%d exclusions=%d dock=%d waypoints=%d%s)",
      _LOG_, snap.perimeter.size(), snap.exclusions.size(), snap.dockpoints.size(), snap.waypoints.size(),
#ifdef ENABLE_MAP
      ", filtered from snapshot"
#else
      ""
#endif
  );
  if (!snap.waypoints.empty()) {
    Log(INFO, "%sstartMapUploadFromLoop: first waypoint %.2f,%.2f  last waypoint %.2f,%.2f",
        _LOG_, snap.waypoints.front().X, snap.waypoints.front().Y,
        snap.waypoints.back().X, snap.waypoints.back().Y);
  }
  if (!snap.perimeter.empty()) {
    Log(INFO, "%sstartMapUploadFromLoop: first perimeter %.2f,%.2f  last perimeter %.2f,%.2f",
        _LOG_, snap.perimeter.front().X, snap.perimeter.front().Y,
        snap.perimeter.back().X, snap.perimeter.back().Y);
  }
}

bool MowerAdapter::sendMapChunkAsync(const std::vector<ArduMower::Domain::Robot::MapPoint> &pts, int baseIdx)
{
  if (_mapUploadState.pointIdx >= pts.size())
    return false;

  // Kleinere Chunks, damit der Sunray-Serial-Empfangspuffer nicht überläuft.
  const size_t chunkSize = 10;
  size_t endIdx = _mapUploadState.pointIdx + chunkSize;
  if (endIdx > pts.size()) endIdx = pts.size();

  char buf[512];
  int n = snprintf(buf, sizeof(buf), "AT+W,%d", baseIdx + _mapUploadState.pointIdx);
  // Zwei Nachkommastellen reichen Sunray (cm-Auflösung) und halten die Pakete kurz.
  for (size_t j = _mapUploadState.pointIdx; j < endIdx; j++) {
    int written = snprintf(buf + n, sizeof(buf) - n, ",%.2f,%.2f", pts[j].X, pts[j].Y);
    if (written < 0 || (size_t)(n + written) >= sizeof(buf)) break;
    n += written;
  }
  String cmd(buf);

  _mapUploadState.lastBaseIdx = baseIdx;
  _mapUploadState.lastExpectedNextIdx = baseIdx + (int)endIdx;
  Log(INFO, "%ssendMapChunkAsync: %s (expect next W,%d)", _LOG_, cmd.c_str(), _mapUploadState.lastExpectedNextIdx);

  bool queued = sendCommandWithResponseAsync(cmd, [&](const char* response, bool ok) {
    if (response != nullptr) {
      strncpy(_mapUploadState.lastResponse, response, sizeof(_mapUploadState.lastResponse) - 1);
      _mapUploadState.lastResponse[sizeof(_mapUploadState.lastResponse) - 1] = '\0';
    } else {
      _mapUploadState.lastResponse[0] = '\0';
    }
    _mapUploadState.lastOk = ok;
    _mapUploadState.waitingForResponse = false;
  }, true, 5000);

  if (!queued) {
    Log(WARN, "%ssendMapChunkAsync: router busy, will retry", _LOG_);
    _mapUploadState.lastCommandQueued = false;
    return true; // stay in current phase, retry next loop
  }

  _mapUploadState.lastCommandQueued = true;
  _mapUploadState.waitingForResponse = true;
  return true;
}

static bool responseOkForPhase(int phase, const char* response)
{
  if (response == nullptr || *response == '\0') return true;
  char first = response[0];
  switch (phase) {
    case ArduMower::Modem::MapUploadState::perimeter:
    case ArduMower::Modem::MapUploadState::exclusions:
    case ArduMower::Modem::MapUploadState::dockpoints:
    case ArduMower::Modem::MapUploadState::waypoints:
      return first == 'W';
    case ArduMower::Modem::MapUploadState::counts:
      return first == 'N';
    case ArduMower::Modem::MapUploadState::exclusionSizes:
      return first == 'X';
    default:
      return true;
  }
}

static bool isPointUploadPhase(int phase)
{
  switch (phase) {
    case ArduMower::Modem::MapUploadState::perimeter:
    case ArduMower::Modem::MapUploadState::exclusions:
    case ArduMower::Modem::MapUploadState::dockpoints:
    case ArduMower::Modem::MapUploadState::waypoints:
      return true;
    default:
      return false;
  }
}

static int parseResponseIndex(int phase, const char* response)
{
  if (response == nullptr) return -1;
  if (*response != 'W' && *response != 'N' && *response != 'X')
    return -1;
  const char* comma = strchr(response, ',');
  if (comma == nullptr) return -1;
  const char* p = comma + 1;
  while (*p == ' ') p++;
  if (!isdigit((unsigned char)*p)) return -1;
  int value = 0;
  while (isdigit((unsigned char)*p)) {
    value = value * 10 + (*p - '0');
    p++;
  }
  return value;
}

void MowerAdapter::processMapUpload()
{
  if (!_mapUploadState.active)
    return;

  if (_mapUploadState.waitingForResponse) {
    processPendingCommand();
    return;
  }

  using namespace ArduMower::Domain::Robot;

  // Evaluate response of previous command (except for start phase)
  if (_mapUploadState.phase != MapUploadState::start && _mapUploadState.lastCommandQueued) {
    _mapUploadState.lastCommandQueued = false;
    {
      char* p = _mapUploadState.lastResponse;
      size_t n = strlen(p);
      while (n > 0 && (p[n-1] == ' ' || p[n-1] == '\t' || p[n-1] == '\r' || p[n-1] == '\n')) {
        p[n-1] = '\0';
        n--;
      }
    }
    bool responseValid = _mapUploadState.lastOk && responseOkForPhase(_mapUploadState.phase, _mapUploadState.lastResponse);
    if (responseValid && isPointUploadPhase(_mapUploadState.phase)) {
      int respIdx = parseResponseIndex(_mapUploadState.phase, _mapUploadState.lastResponse);
      if (respIdx >= 0 && respIdx != _mapUploadState.lastExpectedNextIdx) {
        Log(WARN, "%sprocessMapUpload: response index mismatch in phase %d (expected %d, got %d)",
            _LOG_, _mapUploadState.phase, _mapUploadState.lastExpectedNextIdx, respIdx);
        responseValid = false;
      }
    }
    if (!responseValid) {
      _mapUploadState.chunkRetry++;
      Log(WARN, "%sprocessMapUpload: command failed in phase %d (retry %d/3)", _LOG_, _mapUploadState.phase, _mapUploadState.chunkRetry);
      if (_mapUploadState.chunkRetry >= 3) {
        Log(ERR, "%sprocessMapUpload: command failed in phase %d after 3 retries", _LOG_, _mapUploadState.phase);
        if (_mapUploadLockedMap) _map.endRead();
        _mapUploadLockedMap = false;
        _mapUploadState.snapshot = ArduMower::Domain::Robot::MowerMap();
        _mapUploadState.phase = MapUploadState::error;
        _mapUploadState.active = false;
        return;
      }
      // Resend same chunk (pointIdx was not advanced)
      _mapUploadState.lastResponse[0] = '\0';
    } else {
      // Success: advance pointIdx for chunk phases
      _mapUploadState.chunkRetry = 0;
      switch (_mapUploadState.phase) {
        case MapUploadState::perimeter:
        case MapUploadState::exclusions:
        case MapUploadState::dockpoints:
        case MapUploadState::waypoints:
          // Anhand der vom Mower bestätigten Indizes weiterschalten,
          // damit Chunk-Größe nicht aus der Sync gerät.
          _mapUploadState.pointIdx = _mapUploadState.lastExpectedNextIdx - _mapUploadState.lastBaseIdx;
          break;
        default:
          break;
      }
    }
  }

  switch (_mapUploadState.phase) {
    case MapUploadState::start:
      Log(INFO, "%sprocessMapUpload: uploading map...", _LOG_);
      _mapUploadState.phase = MapUploadState::perimeter;
      _mapUploadState.pointIdx = 0;
      _mapUploadState.chunkRetry = 0;
      _mapUploadState.totalPointsSent = 0;
      break;

    case MapUploadState::perimeter:
      if (_mapUploadState.pointIdx >= _mapUploadState.snapshot.perimeter.size()) {
        _mapUploadState.totalPointsSent += _mapUploadState.snapshot.perimeter.size();
        _mapUploadState.phase = MapUploadState::exclusions;
        _mapUploadState.polygonIdx = 0;
        _mapUploadState.pointIdx = 0;
        _mapUploadState.chunkRetry = 0;
        _mapUploadState.lastResponse[0] = '\0';
        break;
      }
      sendMapChunkAsync(_mapUploadState.snapshot.perimeter, _mapUploadState.totalPointsSent);
      break;

    case MapUploadState::exclusions:
      if (_mapUploadState.polygonIdx >= _mapUploadState.snapshot.exclusions.size()) {
        _mapUploadState.phase = MapUploadState::dockpoints;
        _mapUploadState.pointIdx = 0;
        _mapUploadState.chunkRetry = 0;
        _mapUploadState.lastResponse[0] = '\0';
        break;
      }
      if (_mapUploadState.pointIdx >= _mapUploadState.snapshot.exclusions[_mapUploadState.polygonIdx].size()) {
        _mapUploadState.totalPointsSent += _mapUploadState.snapshot.exclusions[_mapUploadState.polygonIdx].size();
        _mapUploadState.polygonIdx++;
        _mapUploadState.pointIdx = 0;
        _mapUploadState.chunkRetry = 0;
        _mapUploadState.lastResponse[0] = '\0';
        break;
      }
      sendMapChunkAsync(_mapUploadState.snapshot.exclusions[_mapUploadState.polygonIdx], _mapUploadState.totalPointsSent);
      break;

    case MapUploadState::dockpoints:
      if (_mapUploadState.pointIdx >= _mapUploadState.snapshot.dockpoints.size()) {
        _mapUploadState.totalPointsSent += _mapUploadState.snapshot.dockpoints.size();
        _mapUploadState.phase = MapUploadState::waypoints;
        _mapUploadState.pointIdx = 0;
        _mapUploadState.chunkRetry = 0;
        _mapUploadState.lastResponse[0] = '\0';
        break;
      }
      sendMapChunkAsync(_mapUploadState.snapshot.dockpoints, _mapUploadState.totalPointsSent);
      break;

    case MapUploadState::waypoints:
      if (_mapUploadState.pointIdx >= _mapUploadState.snapshot.waypoints.size()) {
        _mapUploadState.totalPointsSent += _mapUploadState.snapshot.waypoints.size();
        _mapUploadState.phase = MapUploadState::counts;
        _mapUploadState.lastResponse[0] = '\0';
        break;
      }
      sendMapChunkAsync(_mapUploadState.snapshot.waypoints, _mapUploadState.totalPointsSent);
      break;

    case MapUploadState::counts: {
      size_t totalExclPoints = 0;
      for (const auto &excl : _mapUploadState.snapshot.exclusions)
        totalExclPoints += excl.size();
      String cmd = "AT+N," + String(_mapUploadState.snapshot.perimeter.size()) + "," + String(totalExclPoints) + ","
                  + String(_mapUploadState.snapshot.dockpoints.size()) + "," + String(_mapUploadState.snapshot.waypoints.size()) + ",0";
      bool queued = sendCommandWithResponseAsync(cmd, [&](const char* response, bool ok) {
        if (response != nullptr) {
          strncpy(_mapUploadState.lastResponse, response, sizeof(_mapUploadState.lastResponse) - 1);
          _mapUploadState.lastResponse[sizeof(_mapUploadState.lastResponse) - 1] = '\0';
        } else {
          _mapUploadState.lastResponse[0] = '\0';
        }
        _mapUploadState.lastOk = ok;
        _mapUploadState.waitingForResponse = false;
      }, true, 5000);
      if (!queued) return;
      _mapUploadState.waitingForResponse = true;
      _mapUploadState.phase = MapUploadState::exclusionSizes;
      _mapUploadState.polygonIdx = 0;
      _mapUploadState.lastResponse[0] = '\0';
      break;
    }

    case MapUploadState::exclusionSizes:
      if (_mapUploadState.polygonIdx >= _mapUploadState.snapshot.exclusions.size()) {
        _mapUploadState.phase = MapUploadState::finalizing;
        _mapUploadState.lastResponse[0] = '\0';
        break;
      }
      {
        String cmd = "AT+X," + String(_mapUploadState.polygonIdx) + "," + String(_mapUploadState.snapshot.exclusions[_mapUploadState.polygonIdx].size());
        bool queued = sendCommandWithResponseAsync(cmd, [&](const char* response, bool ok) {
          if (response != nullptr) {
            strncpy(_mapUploadState.lastResponse, response, sizeof(_mapUploadState.lastResponse) - 1);
            _mapUploadState.lastResponse[sizeof(_mapUploadState.lastResponse) - 1] = '\0';
          } else {
            _mapUploadState.lastResponse[0] = '\0';
          }
          _mapUploadState.lastOk = ok;
          _mapUploadState.waitingForResponse = false;
        }, true, 5000);
        if (!queued) return;
        _mapUploadState.waitingForResponse = true;
        _mapUploadState.polygonIdx++;
        _mapUploadState.lastResponse[0] = '\0';
      }
      break;

    case MapUploadState::finalizing:
      {
      const auto uploadedCrc = _mapUploadState.snapshot.computeMapCrc();
      const auto uploadedPerimeter = _mapUploadState.snapshot.perimeter.size();
      const auto uploadedExclusions = _mapUploadState.snapshot.exclusions.size();
      const auto uploadedDockpoints = _mapUploadState.snapshot.dockpoints.size();
      const auto uploadedWaypoints = _mapUploadState.snapshot.waypoints.size();
      if (_mapUploadLockedMap) _map.endRead();
      _mapUploadLockedMap = false;
      _mapUploadState.snapshot = ArduMower::Domain::Robot::MowerMap();
      _mapUploadState.phase = MapUploadState::done;
      _mapUploadState.active = false;
      // ID der tatsächlich hochgeladenen Karte (nicht zwingend die aktuelle).
      _lastUploadedMapId = _mapUploadSourceId;
      _lastUploadedMapCrc = uploadedCrc;
      Log(INFO, "%sprocessMapUpload: Map upload complete (%d perimeter, %d exclusions, %d dockpoints, %d waypoints) CRC %d",
          _LOG_, uploadedPerimeter, uploadedExclusions, uploadedDockpoints, uploadedWaypoints, uploadedCrc);
      Log(INFO, "%sprocessMapUpload: upload complete", _LOG_);
      break;
      }

    case MapUploadState::done:
    case MapUploadState::error:
    case MapUploadState::idle:
    default:
      _mapUploadState.snapshot = ArduMower::Domain::Robot::MowerMap();
      _mapUploadState.active = false;
      break;
  }
}

void MowerAdapter::loop()
{
  startMapUploadFromLoop();
  processPendingCommand();
  processMapUpload();

  // Poll one command per loop() call to avoid flooding the mower's command
  // parser with back-to-back requests. Each request*() method has its own
  // interval throttle and will return true if it is too early to send again.
  static byte loopCase = 0;
  switch (loopCase)
  {
  case 0: // AT+S (mower state)
    requestStatus();
    break;
  case 1: // AT+T (statistics incl. battery)
    requestStats();
    break;
  case 2: // AT+S3 (sensor summary)
    requestSensorSummary();
    break;
  case 3: // AT+C (control state incl. speed)
    requestControl();
    break;
#if defined(ENABLE_LIVE_MAP) || defined(ENABLE_GPS_DASHBOARD)
  case 4: // AT+S4 (GPS details incl. satellites)
    requestGpsDetails();
    break;
#endif
  }
#if defined(ENABLE_LIVE_MAP) || defined(ENABLE_GPS_DASHBOARD)
  loopCase++;
  if (loopCase > 3) loopCase = 0;
#else
  loopCase++;
  if (loopCase > 2) loopCase = 0;
#endif
}

bool MowerAdapter::sendCommand(const String& command, bool encrypt)
{
  Checksum chk;
  chk.update(command.c_str());

  char buf[256];
  snprintf(buf, sizeof(buf), "%s,0x%02x", command.c_str(), chk.value());
  Log(COMM, "> %s", buf);

  if (encrypt)
    enc.encrypt(buf, strlen(buf));

  return router.sendWithoutResponse(buf);
}

bool MowerAdapter::sendCommandWithResponseAsync(const String& command, std::function<void(const char*, bool)> callback, bool encrypt, int timeoutMs)
{
  if (_pendingCommand.active) {
    Log(WARN, "%ssendCommandWithResponseAsync: already busy", _LOG_);
    return false;
  }

  Checksum chk;
  chk.update(command.c_str());

  char buf[256];
  snprintf(buf, sizeof(buf), "%s,0x%02x", command.c_str(), chk.value());
  Log(COMM, "> %s", buf);

  if (encrypt)
    enc.encrypt(buf, strlen(buf));

  _pendingCommand.callback = callback;
  _pendingCommand.startTime = millis();
  _pendingCommand.timeoutMs = timeoutMs;
  _pendingCommand.response[0] = '\0';
  _pendingCommand.ok = false;
  _pendingCommand.active = true;
  _pendingCommand.done = false;
  _pendingCommand.command = command;

  bool queued = router.send(buf, [&](const char* resp, XferError error) {
    if (resp != nullptr) {
      strncpy(_pendingCommand.response, resp, sizeof(_pendingCommand.response) - 1);
      _pendingCommand.response[sizeof(_pendingCommand.response) - 1] = '\0';
    } else {
      _pendingCommand.response[0] = '\0';
    }
    _pendingCommand.ok = (error == XferError::SUCCESS);
    _pendingCommand.done = true;
  });

  if (!queued) {
    _pendingCommand.active = false;
    return false;
  }

  return true;
}

void MowerAdapter::processPendingCommand()
{
  if (!_pendingCommand.active)
    return;

  router.loop();

  if (!_pendingCommand.done) {
    if ((int)(millis() - _pendingCommand.startTime) >= _pendingCommand.timeoutMs) {
      Log(WARN, "%sprocessPendingCommand timeout for: %s", _LOG_, _pendingCommand.command.c_str());
      _pendingCommand.ok = false;
      _pendingCommand.done = true;
    }
    return;
  }

  auto callback = _pendingCommand.callback;
  const char* response = _pendingCommand.response;
  bool ok = _pendingCommand.ok;
  _pendingCommand.active = false;
  _pendingCommand.callback = nullptr;

  if (callback)
    callback(response, ok);
}

bool MowerAdapter::sendCommandWithResponse(const String& command, char* response, size_t responseLen, bool encrypt, int timeoutMs)
{
  if (responseLen == 0) return false;
  bool done = false;
  bool ok = sendCommandWithResponseAsync(command, [&](const char* resp, bool success) {
    if (resp != nullptr) {
      strncpy(response, resp, responseLen - 1);
      response[responseLen - 1] = '\0';
    } else {
      response[0] = '\0';
    }
    done = true;
    ok = success;
  }, encrypt, timeoutMs);

  if (!ok) return false;

  uint32_t start = millis();
  while (!done && (int)(millis() - start) < timeoutMs) {
    processPendingCommand();
    vTaskDelay(1 / portTICK_PERIOD_MS);
  }

  if (!done) {
    Log(WARN, "%ssendCommandWithResponse timeout for: %s", _LOG_, command.c_str());
    _pendingCommand.active = false;
    _pendingCommand.callback = nullptr;
    return false;
  }

  return ok;
}

void processCSVResponse(const char* res, std::function<void(int, const char*, size_t)> fn)
{
  int index = -1;
  const char* p = res;

  while (*p)
  {
    index++;
    const char* comma = strchr(p, ',');
    size_t len = comma ? (size_t)(comma - p) : strlen(p);
    fn(index, p, len);

    p += len;
    if (*p == ',') p++;
  }
}

void processCSVResponse(const String& res, std::function<void(int, const char*, size_t)> fn)
{
  processCSVResponse(res.c_str(), fn);
}

// ===== Transiente (RAM-only) Karten =====

static const char *TRANSIENT_ID_PREFIX = "__t_";

String MowerAdapter::allocateTransientId() {
  return String(TRANSIENT_ID_PREFIX) + String(_transientIdCounter++);
}

const MowerAdapter::TransientMap* MowerAdapter::findTransientMap(const String &id) const {
  for (const auto &t : _transientMaps) {
    if (t.id == id) return &t;
  }
  return nullptr;
}

String MowerAdapter::findOrCreateTransientMap(const ArduMower::Domain::Robot::MowerMap &map, const String &name, double rotation) {
  String hash = _mapManager.computeHash(map);
  for (const auto &t : _transientMaps) {
    if (t.hash == hash) return t.id;
  }
  TransientMap t;
  t.id = allocateTransientId();
  t.name = name.length() > 0 ? name : _mapManager.generateDefaultName();
  t.area = _mapManager.computeArea(map);
  t.hash = hash;
  t.crc = _mapManager.computeCrc(map);
  t.rotation = rotation;
  t.timestamp = map.timestamp;
  t.map = map;
  _transientMaps.push_back(t);
  return t.id;
}

bool MowerAdapter::removeTransientMap(const String &id) {
  for (auto it = _transientMaps.begin(); it != _transientMaps.end(); ++it) {
    if (it->id == id) {
      _transientMaps.erase(it);
      return true;
    }
  }
  return false;
}

void MowerAdapter::updateTransientMapMeta(const String &id, const ArduMower::Domain::Robot::MowerMap &map, double rotation) {
  for (auto &t : _transientMaps) {
    if (t.id == id) {
      t.area = _mapManager.computeArea(map);
      // Hash bewusst nicht pro Bearbeitungsschritt neu berechnen (teuer);
      // er dient nur der Anzeige und wird beim Speichern ohnehin neu gebildet.
      t.crc = _mapManager.computeCrc(map);
      t.rotation = rotation;
      t.timestamp = map.timestamp;
      t.map = map;
      break;
    }
  }
}

const MowerAdapter::MapDraft* MowerAdapter::findMapDraft(const String &id) const {
  for (const auto &draft : _mapDrafts) {
    if (draft.id == id) return &draft;
  }
  return nullptr;
}

void MowerAdapter::storeCurrentMapDraft() {
  if (_currentMapId.length() == 0 || _currentMapId.startsWith("__t_")) return;
  for (auto &draft : _mapDrafts) {
    if (draft.id == _currentMapId) {
      draft.map = _map;
      return;
    }
  }
  _mapDrafts.push_back({_currentMapId, _map});
}

void MowerAdapter::removeMapDraft(const String &id) {
  for (auto it = _mapDrafts.begin(); it != _mapDrafts.end(); ++it) {
    if (it->id == id) {
      _mapDrafts.erase(it);
      return;
    }
  }
}

bool MowerAdapter::isNameUsed(const String &name, const String &excludeId) const {
  if (name.length() == 0) return false;
  for (const auto &m : _mapManager.list()) {
    if (m.id != excludeId && m.name.equalsIgnoreCase(name)) return true;
  }
  for (const auto &t : _transientMaps) {
    if (t.id != excludeId && t.name.equalsIgnoreCase(name)) return true;
  }
  if (_pendingRenameId.length() > 0 &&
      _pendingRenameId != excludeId &&
      _pendingRenameName.equalsIgnoreCase(name)) {
    return true;
  }
  return false;
}
