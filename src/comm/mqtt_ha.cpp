#include "mqtt_ha.h"
#include "log.h"
#include <ArduinoJson.h>

using namespace ArduMower::Modem::HomeAssistant;
using namespace ArduMower::Domain::Robot;

float batteryVoltageToLevel(const float v);
String chipIdString();

// Sensor publish interval (ms) - don't flood MQTT with sensor updates
static const uint32_t SENSOR_INTERVAL = 30000;

void Adapter::loop(const uint32_t now)
{
  loopReport(now);
  loopCommand(now);
  loopSensors(now);
  loopStats(now);
  loopMapInfo(now);
  loopActors(now);
}

void Adapter::loopCommand(const uint32_t now)
{
  if (commandSpeedPending && cmd.changeSpeed(commandSpeedValue))
    commandSpeedPending = false;
  if (commandMowerEnabledPending && cmd.mowerEnabled(commandMowerEnabledValue))
    commandMowerEnabledPending = false;
}

void Adapter::loopReport(const uint32_t now)
{
  auto *state = source.stateP();
  auto *desiredState = source.desiredStateP();

  bool changes = false;

  if (fabs(state->batteryVoltage - batteryVoltage) > 0.1)
    changes = true;
  else if (state->job != job)
    changes = true;
  else if (desiredState->speed != speed)
    changes = true;
  else if (desiredState->mowerMotorEnabled != mowerEnabled)
    changes = true;

  if (!changes)
    return;

  JsonDocument o;

  uint8_t roundBatterylevel = (uint8_t)roundf(batteryVoltageToLevel(state->batteryVoltage));
  o["battery_level"] = roundBatterylevel;

  switch (state->job)
  {
  case 0: // idle
    o["state"] = "paused";
    break;
  case 1: // mow
    o["state"] = "cleaning";
    break;
  case 2: // charge
    o["state"] = "docked";
    break;
  case 3: // error
    o["state"] = "error";
    break;
  case 4: // dock
    o["state"] = "returning";
    break;
  }

  if (!desiredState->mowerMotorEnabled)
    o["fan_speed"] = "off";
  else if (desiredState->speed <= 0.25)
    o["fan_speed"] = "min";
  else if (desiredState->speed <= 0.4)
    o["fan_speed"] = "medium";
  else if (desiredState->speed <= 0.50)
    o["fan_speed"] = "high";
  else
    o["fan_speed"] = "max";

  String buffer;
  size_t len = serializeJson(o, buffer);
  if (len < 1)
  {
    Log(ERR, "HomeAssistant::Adapter::loop::serialize-zero");
    return;
  }
  Log(DBG, "HomeAssistant::Adapter::loop::serialize(%u, %s)", len, buffer.c_str());

  if (!tx("/ha/state", buffer))
    return;

  job = state->job;
  batteryVoltage = state->batteryVoltage;
  speed = desiredState->speed;
  mowerEnabled = desiredState->mowerMotorEnabled;
}

void Adapter::loopSensors(const uint32_t now)
{
  static uint32_t lastPublish = 0;
  if (now - lastPublish < SENSOR_INTERVAL)
    return;

  auto *state = source.stateP();
  if (state->timestamp == 0)
    return;

  lastPublish = now;

  // Publish individual sensor values as simple strings for HA sensor entities
  char buf[32];

  // Battery voltage
  snprintf(buf, sizeof(buf), "%.2f", state->batteryVoltage);
  tx("/ha/sensor/battery_voltage", buf);

  // Battery level (percentage)
  snprintf(buf, sizeof(buf), "%d", (int)roundf(batteryVoltageToLevel(state->batteryVoltage)));
  tx("/ha/sensor/battery_level", buf);

  // Temperature
  snprintf(buf, sizeof(buf), "%.1f", state->temperature);
  tx("/ha/sensor/temperature", buf);

  // Amps
  snprintf(buf, sizeof(buf), "%.2f", state->amps);
  tx("/ha/sensor/amps", buf);

  // Position accuracy
  snprintf(buf, sizeof(buf), "%.2f", state->position.accuracy);
  tx("/ha/sensor/position_accuracy", buf);

  // Satellites
  snprintf(buf, sizeof(buf), "%d", state->position.visibleSatellites);
  tx("/ha/sensor/satellites", buf);

  // GPS solution (as string)
  const char *solStr = "invalid";
  if (state->position.solution == 1)
    solStr = "float";
  else if (state->position.solution == 2)
    solStr = "fix";
  tx("/ha/sensor/gps_solution", solStr);

  // Map CRC
  snprintf(buf, sizeof(buf), "%d", state->mapCrc);
  tx("/ha/sensor/map_crc", buf);

  // Job state (as string)
  const char *jobStr = "unknown";
  switch (state->job)
  {
  case 0: jobStr = "idle"; break;
  case 1: jobStr = "mow"; break;
  case 2: jobStr = "charge"; break;
  case 3: jobStr = "error"; break;
  case 4: jobStr = "dock"; break;
  }
  tx("/ha/sensor/job", jobStr);

  // Mow progress (mowPointIndex)
  snprintf(buf, sizeof(buf), "%d", state->position.mowPointIndex);
  tx("/ha/sensor/mow_progress", buf);
}

void Adapter::loopStats(const uint32_t now)
{
  static uint32_t lastPublish = 0;
  if (now - lastPublish < 60000) // Stats update every 60s
    return;

  auto *stats = source.statsP();
  if (stats->timestamp == 0)
    return;

  lastPublish = now;

  char buf[32];

  // Mow distance traveled (m)
  snprintf(buf, sizeof(buf), "%.1f", stats->mowDistanceTraveled);
  tx("/ha/stats/mow_distance", buf);

  // Durations (minutes)
  snprintf(buf, sizeof(buf), "%lu", (unsigned long)stats->durations.mow);
  tx("/ha/stats/mow_duration", buf);

  snprintf(buf, sizeof(buf), "%lu", (unsigned long)stats->durations.charge);
  tx("/ha/stats/charge_duration", buf);

  snprintf(buf, sizeof(buf), "%lu", (unsigned long)stats->durations.idle);
  tx("/ha/stats/idle_duration", buf);

  // Obstacles total
  snprintf(buf, sizeof(buf), "%lu", (unsigned long)stats->obstacles.count);
  tx("/ha/stats/obstacles", buf);

  // Temperature min/max
  snprintf(buf, sizeof(buf), "%.1f", stats->tempMin);
  tx("/ha/stats/temp_min", buf);

  snprintf(buf, sizeof(buf), "%.1f", stats->tempMax);
  tx("/ha/stats/temp_max", buf);

  // GPS checksum errors
  snprintf(buf, sizeof(buf), "%lu", (unsigned long)stats->gpsChecksumErrors);
  tx("/ha/stats/gps_checksum_errors", buf);

  // GPS jumps
  snprintf(buf, sizeof(buf), "%lu", (unsigned long)stats->gpsJumps);
  tx("/ha/stats/gps_jumps", buf);

  // Free memory (bytes)
  snprintf(buf, sizeof(buf), "%lu", (unsigned long)stats->freeMemory);
  tx("/ha/stats/free_memory", buf);
}

void Adapter::loopMapInfo(const uint32_t now)
{
  // Check if map list changed (dirty flag from MowerAdapter)
  bool forceUpdate = source.mapListDirty();
  if (forceUpdate)
    source.clearMapListDirty();

  static uint32_t lastPublish = 0;
  static String lastMapId;
  static String lastMapName;
  static double lastArea = -1;
  static int lastCrc = -1;
  static int lastPerimeter = -1;
  static int lastExclusions = -1;
  static int lastDock = -1;
  static int lastWaypoints = -1;
  static String lastUploadedId;

  // Only rate-limit if not forced by dirty flag
  if (!forceUpdate && now - lastPublish < 10000)
    return;

  lastPublish = now;

  String mapId = source.currentMapId();
  double area = source.currentMapArea();
  int crc = source.currentMapCrc();

  // Resolve human-readable map name from mapList
  String mapName;
  if (mapId.length() > 0) {
    for (const auto &info : source.mapList()) {
      if (info.id == mapId) {
        mapName = info.name;
        break;
      }
    }
    // Fallback: if not found in list, use ID
    if (mapName.length() == 0)
      mapName = mapId;
  } else {
    mapName = "none";
  }

  // Get point counts from the map
  int perimeterPts = 0, exclusionPts = 0, dockPts = 0, waypointPts = 0;
  {
    auto map = source.mowerMap();
    perimeterPts = (int)map.perimeter.size();
    dockPts = (int)map.dockpoints.size();
    waypointPts = (int)map.waypoints.size();
    for (const auto &ex : map.exclusions)
      exclusionPts += (int)ex.size();
  }
  int totalPts = perimeterPts + exclusionPts + dockPts + waypointPts;

  String uploadedId = source.lastUploadedMapId();
  bool uploadedChanged = (uploadedId != lastUploadedId);
  bool changed = forceUpdate || uploadedChanged || (mapId != lastMapId) || (mapName != lastMapName) || (area != lastArea) || (crc != lastCrc) ||
                 (perimeterPts != lastPerimeter) || (exclusionPts != lastExclusions) ||
                 (dockPts != lastDock) || (waypointPts != lastWaypoints);
  if (!changed)
    return;

  lastMapId = mapId;
  lastMapName = mapName;
  lastArea = area;
  lastCrc = crc;
  lastPerimeter = perimeterPts;
  lastExclusions = exclusionPts;
  lastDock = dockPts;
  lastWaypoints = waypointPts;
  lastUploadedId = uploadedId;

  char buf[32];

  // Map area
  snprintf(buf, sizeof(buf), "%.1f", area);
  tx("/ha/map/area", buf);

  // Point counts
  snprintf(buf, sizeof(buf), "%d", perimeterPts);
  tx("/ha/map/perimeter_points", buf);

  snprintf(buf, sizeof(buf), "%d", exclusionPts);
  tx("/ha/map/exclusion_points", buf);

  snprintf(buf, sizeof(buf), "%d", dockPts);
  tx("/ha/map/dock_points", buf);

  snprintf(buf, sizeof(buf), "%d", waypointPts);
  tx("/ha/map/waypoints", buf);

  snprintf(buf, sizeof(buf), "%d", totalPts);
  tx("/ha/map/total_points", buf);

  // Publish uploaded map name (the map currently loaded on the mower)
  // When upload is active, show "uploading", otherwise show the map name or "none"
  String uploadedMapName;
  if (cmd.uploadMapToMowerActive()) {
    uploadedMapName = "uploading";
  } else {
    String uploadedId = source.lastUploadedMapId();
    if (uploadedId.length() > 0) {
      for (const auto &info : source.mapList()) {
        if (info.id == uploadedId) {
          uploadedMapName = info.name;
          break;
        }
      }
      if (uploadedMapName.length() == 0) uploadedMapName = uploadedId;
    } else {
      uploadedMapName = "none";
    }
  }
  tx("/ha/map/uploaded", uploadedMapName);

  // Publish map select state (human-readable name, must match options)
  tx("/ha/select/map/state", mapName);
}

void Adapter::loopActors(const uint32_t now)
{
  // Process pending actor commands
  if (pendingStart) {
    if (cmd.start()) pendingStart = false;
  }
  if (pendingStop) {
    if (cmd.stop()) pendingStop = false;
  }
  if (pendingDock) {
    if (cmd.dock()) pendingDock = false;
  }
  if (pendingSkipWaypoint) {
    if (cmd.skipWaypoint()) pendingSkipWaypoint = false;
  }
  if (pendingSpeed >= 0) {
    if (cmd.changeSpeed(pendingSpeed)) pendingSpeed = -1;
  }
  if (pendingMapId.length() > 0) {
    // Load the map into the modem (same as UI "loadMap"), so it becomes the
    // current map and the select state stays in sync.
    if (source.loadMap(pendingMapId)) {
      pendingMapId = "";
      // Publish the new select state immediately so HA doesn't revert to the
      // old value while waiting for the next loopMapInfo cycle.
      String mapId = source.currentMapId();
      String mapName;
      if (mapId.length() > 0) {
        for (const auto &info : source.mapList()) {
          if (info.id == mapId) {
            mapName = info.name;
            break;
          }
        }
        if (mapName.length() == 0) mapName = mapId;
      } else {
        mapName = "none";
      }
      tx("/ha/select/map/state", mapName);
    }
  }

  // Publish current speed as number state (for the number entity)
  static uint32_t lastSpeedPublish = 0;
  static float lastSpeed = -1;
  auto *desiredState = source.desiredStateP();
  if (now - lastSpeedPublish > SENSOR_INTERVAL || desiredState->speed != lastSpeed) {
    lastSpeedPublish = now;
    lastSpeed = desiredState->speed;
    char buf[16];
    snprintf(buf, sizeof(buf), "%.2f", desiredState->speed);
    tx("/ha/number/speed/state", buf);
  }

  // Map select state is published from loopMapInfo (with change detection).
}

void Adapter::onButtonMessage(const String &button, const String &payload)
{
  if (payload != "PRESS")
    return;

  if (button == "start")
    pendingStart = true;
  else if (button == "stop")
    pendingStop = true;
  else if (button == "dock")
    pendingDock = true;
  else if (button == "skip_waypoint")
    pendingSkipWaypoint = true;
}

void Adapter::onSpeedMessage(const String &payload)
{
  float speed = payload.toFloat();
  if (speed >= 0 && speed <= 1.0) {
    pendingSpeed = speed;
  }
}

void Adapter::onMapSelectMessage(const String &payload)
{
  if (payload.length() == 0 || payload == "none")
    return;

  // Resolve human-readable name back to map ID
  for (const auto &info : source.mapList()) {
    if (info.name == payload) {
      pendingMapId = info.id;
      return;
    }
  }
  // Fallback: maybe an ID was sent directly
  pendingMapId = payload;
}

void Adapter::onUploadMessage(const String &payload)
{
  if (payload == "PRESS") {
    cmd.uploadMapToMower();
  }
}

void Adapter::onFanSpeedMessage(const String &payload)
{
  if (payload == "off")
  {
    commandMowerEnabledPending = true;
    commandMowerEnabledValue = false;
    return;
  }
  else if (!source.desiredStateP()->mowerMotorEnabled)
  {
    commandMowerEnabledPending = true;
    commandMowerEnabledValue = true;
  }

  float speed;

  if (payload == "min")
    speed = 0.2;
  else if (payload == "medium")
    speed = 0.33;
  else if (payload == "high")
    speed = 0.46;
  else if (payload == "max")
    speed = 0.59;
  else
    return;

  commandSpeedPending = true;
  commandSpeedValue = speed;
}

// --- Discovery Document ---

String DiscoveryDocument::discoveryPrefix()
{
  return "homeassistant";
}

String chipIdString()
{
  uint32_t chipId = 0;
  for (auto i = 0; i < 17; i = i + 8)
  {
    chipId |= ((ESP.getEfuseMac() >> (40 - i)) & 0xff) << i;
  }

  return String(chipId);
}

void DiscoveryDocument::addDeviceBlock(JsonObject dev)
{
  chipIdStr = chipIdString();
  deviceName = settings.general.name;

  // identifiers is required for device registry
  auto ids = dev["identifiers"].to<JsonArray>();
  ids.add("ardumower-" + chipIdStr);

  dev["name"] = deviceName;
  dev["model"] = "Ardumower Modem";
  dev["manufacturer"] = "Ardumower";
  dev["sw_version"] = "1.0";
}

void DiscoveryDocument::addVacuumEntity(JsonDocument &doc, const String &topicPrefix)
{
  chipIdStr = chipIdString();
  deviceName = settings.general.name;

  doc["name"] = deviceName;
  doc["unique_id"] = "ardumower-" + chipIdStr + "-vacuum";
  doc["~"] = topicPrefix;
  doc["schema"] = "state";
  doc["state_topic"] = "~/ha/state";
  doc["json_attributes_topic"] = "~/props";
  doc["command_topic"] = "~/command";
  doc["set_fan_speed_topic"] = "~/ha/set_fan_speed";
  doc["availability_topic"] = "~/online";
  doc["payload_available"] = "true";
  doc["payload_not_available"] = "false";

  auto features = doc["supported_features"].to<JsonArray>();
  features.add("start");
  features.add("stop");
  features.add("return_home");
  features.add("battery");
  features.add("status");
  features.add("fan_speed");

  auto speeds = doc["fan_speed_list"].to<JsonArray>();
  speeds.add("off");
  speeds.add("min");
  speeds.add("medium");
  speeds.add("high");
  speeds.add("max");

  auto dev = doc["device"].to<JsonObject>();
  addDeviceBlock(dev);
}

void DiscoveryDocument::addBatteryVoltageSensor(JsonDocument &doc, const String &topicPrefix)
{
  doc["name"] = deviceName + " Battery Voltage";
  doc["unique_id"] = "ardumower-" + chipIdStr + "-battery_voltage";
  doc["state_topic"] = topicPrefix + "/ha/sensor/battery_voltage";
  doc["unit_of_measurement"] = "V";
  doc["device_class"] = "voltage";
  doc["state_class"] = "measurement";
  doc["suggested_display_precision"] = 1;
  doc["availability_topic"] = topicPrefix + "/online";
  doc["payload_available"] = "true";
  doc["payload_not_available"] = "false";

  auto dev = doc["device"].to<JsonObject>();
  addDeviceBlock(dev);
}

void DiscoveryDocument::addTemperatureSensor(JsonDocument &doc, const String &topicPrefix)
{
  doc["name"] = deviceName + " Temperature";
  doc["unique_id"] = "ardumower-" + chipIdStr + "-temperature";
  doc["state_topic"] = topicPrefix + "/ha/sensor/temperature";
  doc["unit_of_measurement"] = "°C";
  doc["device_class"] = "temperature";
  doc["state_class"] = "measurement";
  doc["availability_topic"] = topicPrefix + "/online";
  doc["payload_available"] = "true";
  doc["payload_not_available"] = "false";

  auto dev = doc["device"].to<JsonObject>();
  addDeviceBlock(dev);
}

void DiscoveryDocument::addAmpsSensor(JsonDocument &doc, const String &topicPrefix)
{
  doc["name"] = deviceName + " Current";
  doc["unique_id"] = "ardumower-" + chipIdStr + "-amps";
  doc["state_topic"] = topicPrefix + "/ha/sensor/amps";
  doc["unit_of_measurement"] = "A";
  doc["device_class"] = "current";
  doc["state_class"] = "measurement";
  doc["availability_topic"] = topicPrefix + "/online";
  doc["payload_available"] = "true";
  doc["payload_not_available"] = "false";

  auto dev = doc["device"].to<JsonObject>();
  addDeviceBlock(dev);
}

void DiscoveryDocument::addPositionAccuracySensor(JsonDocument &doc, const String &topicPrefix)
{
  doc["name"] = deviceName + " GPS Accuracy";
  doc["unique_id"] = "ardumower-" + chipIdStr + "-gps_accuracy";
  doc["state_topic"] = topicPrefix + "/ha/sensor/position_accuracy";
  doc["unit_of_measurement"] = "m";
  doc["state_class"] = "measurement";
  doc["icon"] = "mdi:crosshairs-gps";
  doc["availability_topic"] = topicPrefix + "/online";
  doc["payload_available"] = "true";
  doc["payload_not_available"] = "false";

  auto dev = doc["device"].to<JsonObject>();
  addDeviceBlock(dev);
}

void DiscoveryDocument::addSatellitesSensor(JsonDocument &doc, const String &topicPrefix)
{
  doc["name"] = deviceName + " Satellites";
  doc["unique_id"] = "ardumower-" + chipIdStr + "-satellites";
  doc["state_topic"] = topicPrefix + "/ha/sensor/satellites";
  doc["state_class"] = "measurement";
  doc["icon"] = "mdi:satellite-variant";
  doc["availability_topic"] = topicPrefix + "/online";
  doc["payload_available"] = "true";
  doc["payload_not_available"] = "false";

  auto dev = doc["device"].to<JsonObject>();
  addDeviceBlock(dev);
}

void DiscoveryDocument::addGpsSolutionSensor(JsonDocument &doc, const String &topicPrefix)
{
  doc["name"] = deviceName + " GPS Solution";
  doc["unique_id"] = "ardumower-" + chipIdStr + "-gps_solution";
  doc["state_topic"] = topicPrefix + "/ha/sensor/gps_solution";
  doc["icon"] = "mdi:map-marker";
  doc["availability_topic"] = topicPrefix + "/online";
  doc["payload_available"] = "true";
  doc["payload_not_available"] = "false";

  auto dev = doc["device"].to<JsonObject>();
  addDeviceBlock(dev);
}

void DiscoveryDocument::addMapCrcSensor(JsonDocument &doc, const String &topicPrefix)
{
  doc["name"] = deviceName + " Map CRC";
  doc["unique_id"] = "ardumower-" + chipIdStr + "-map_crc";
  doc["state_topic"] = topicPrefix + "/ha/sensor/map_crc";
  doc["icon"] = "mdi:map-check";
  doc["availability_topic"] = topicPrefix + "/online";
  doc["payload_available"] = "true";
  doc["payload_not_available"] = "false";

  auto dev = doc["device"].to<JsonObject>();
  addDeviceBlock(dev);
}

void DiscoveryDocument::addJobStateSensor(JsonDocument &doc, const String &topicPrefix)
{
  doc["name"] = deviceName + " Job State";
  doc["unique_id"] = "ardumower-" + chipIdStr + "-job_state";
  doc["state_topic"] = topicPrefix + "/ha/sensor/job";
  doc["icon"] = "mdi:robot-mower";
  doc["availability_topic"] = topicPrefix + "/online";
  doc["payload_available"] = "true";
  doc["payload_not_available"] = "false";

  auto dev = doc["device"].to<JsonObject>();
  addDeviceBlock(dev);
}

void DiscoveryDocument::addWifiOnlineBinarySensor(JsonDocument &doc, const String &topicPrefix)
{
  doc["name"] = deviceName + " Online";
  doc["unique_id"] = "ardumower-" + chipIdStr + "-online";
  doc["state_topic"] = topicPrefix + "/online";
  doc["payload_on"] = "true";
  doc["payload_off"] = "false";
  doc["device_class"] = "connectivity";

  auto dev = doc["device"].to<JsonObject>();
  addDeviceBlock(dev);
}

// --- Stats sensors ---

void DiscoveryDocument::addStatsMowDistanceSensor(JsonDocument &doc, const String &topicPrefix)
{
  doc["name"] = deviceName + " Mow Distance";
  doc["unique_id"] = "ardumower-" + chipIdStr + "-mow_distance";
  doc["state_topic"] = topicPrefix + "/ha/stats/mow_distance";
  doc["unit_of_measurement"] = "m";
  doc["device_class"] = "distance";
  doc["state_class"] = "total_increasing";
  doc["icon"] = "mdi:map-marker-distance";
  doc["availability_topic"] = topicPrefix + "/online";
  doc["payload_available"] = "true";
  doc["payload_not_available"] = "false";
  auto dev = doc["device"].to<JsonObject>();
  addDeviceBlock(dev);
}

void DiscoveryDocument::addStatsMowDurationSensor(JsonDocument &doc, const String &topicPrefix)
{
  doc["name"] = deviceName + " Mow Duration";
  doc["unique_id"] = "ardumower-" + chipIdStr + "-mow_duration";
  doc["state_topic"] = topicPrefix + "/ha/stats/mow_duration";
  doc["unit_of_measurement"] = "min";
  doc["device_class"] = "duration";
  doc["state_class"] = "total_increasing";
  doc["icon"] = "mdi:mower";
  doc["availability_topic"] = topicPrefix + "/online";
  doc["payload_available"] = "true";
  doc["payload_not_available"] = "false";
  auto dev = doc["device"].to<JsonObject>();
  addDeviceBlock(dev);
}

void DiscoveryDocument::addStatsChargeDurationSensor(JsonDocument &doc, const String &topicPrefix)
{
  doc["name"] = deviceName + " Charge Duration";
  doc["unique_id"] = "ardumower-" + chipIdStr + "-charge_duration";
  doc["state_topic"] = topicPrefix + "/ha/stats/charge_duration";
  doc["unit_of_measurement"] = "min";
  doc["device_class"] = "duration";
  doc["state_class"] = "total_increasing";
  doc["icon"] = "mdi:battery-charging";
  doc["availability_topic"] = topicPrefix + "/online";
  doc["payload_available"] = "true";
  doc["payload_not_available"] = "false";
  auto dev = doc["device"].to<JsonObject>();
  addDeviceBlock(dev);
}

void DiscoveryDocument::addStatsIdleDurationSensor(JsonDocument &doc, const String &topicPrefix)
{
  doc["name"] = deviceName + " Idle Duration";
  doc["unique_id"] = "ardumower-" + chipIdStr + "-idle_duration";
  doc["state_topic"] = topicPrefix + "/ha/stats/idle_duration";
  doc["unit_of_measurement"] = "min";
  doc["device_class"] = "duration";
  doc["state_class"] = "total_increasing";
  doc["icon"] = "mdi:sleep";
  doc["availability_topic"] = topicPrefix + "/online";
  doc["payload_available"] = "true";
  doc["payload_not_available"] = "false";
  auto dev = doc["device"].to<JsonObject>();
  addDeviceBlock(dev);
}

void DiscoveryDocument::addStatsObstaclesSensor(JsonDocument &doc, const String &topicPrefix)
{
  doc["name"] = deviceName + " Obstacles";
  doc["unique_id"] = "ardumower-" + chipIdStr + "-obstacles";
  doc["state_topic"] = topicPrefix + "/ha/stats/obstacles";
  doc["state_class"] = "total_increasing";
  doc["icon"] = "mdi:traffic-cone";
  doc["availability_topic"] = topicPrefix + "/online";
  doc["payload_available"] = "true";
  doc["payload_not_available"] = "false";
  auto dev = doc["device"].to<JsonObject>();
  addDeviceBlock(dev);
}

void DiscoveryDocument::addStatsTempMinSensor(JsonDocument &doc, const String &topicPrefix)
{
  doc["name"] = deviceName + " Temp Min";
  doc["unique_id"] = "ardumower-" + chipIdStr + "-temp_min";
  doc["state_topic"] = topicPrefix + "/ha/stats/temp_min";
  doc["unit_of_measurement"] = "°C";
  doc["device_class"] = "temperature";
  doc["icon"] = "mdi:thermometer-low";
  doc["availability_topic"] = topicPrefix + "/online";
  doc["payload_available"] = "true";
  doc["payload_not_available"] = "false";
  auto dev = doc["device"].to<JsonObject>();
  addDeviceBlock(dev);
}

void DiscoveryDocument::addStatsTempMaxSensor(JsonDocument &doc, const String &topicPrefix)
{
  doc["name"] = deviceName + " Temp Max";
  doc["unique_id"] = "ardumower-" + chipIdStr + "-temp_max";
  doc["state_topic"] = topicPrefix + "/ha/stats/temp_max";
  doc["unit_of_measurement"] = "°C";
  doc["device_class"] = "temperature";
  doc["icon"] = "mdi:thermometer-high";
  doc["availability_topic"] = topicPrefix + "/online";
  doc["payload_available"] = "true";
  doc["payload_not_available"] = "false";
  auto dev = doc["device"].to<JsonObject>();
  addDeviceBlock(dev);
}

void DiscoveryDocument::addStatsGpsChecksumErrorsSensor(JsonDocument &doc, const String &topicPrefix)
{
  doc["name"] = deviceName + " GPS Checksum Errors";
  doc["unique_id"] = "ardumower-" + chipIdStr + "-gps_checksum_errors";
  doc["state_topic"] = topicPrefix + "/ha/stats/gps_checksum_errors";
  doc["state_class"] = "total_increasing";
  doc["icon"] = "mdi:alert-circle-outline";
  doc["availability_topic"] = topicPrefix + "/online";
  doc["payload_available"] = "true";
  doc["payload_not_available"] = "false";
  auto dev = doc["device"].to<JsonObject>();
  addDeviceBlock(dev);
}

void DiscoveryDocument::addStatsGpsJumpsSensor(JsonDocument &doc, const String &topicPrefix)
{
  doc["name"] = deviceName + " GPS Jumps";
  doc["unique_id"] = "ardumower-" + chipIdStr + "-gps_jumps";
  doc["state_topic"] = topicPrefix + "/ha/stats/gps_jumps";
  doc["state_class"] = "total_increasing";
  doc["icon"] = "mdi:map-marker-alert";
  doc["availability_topic"] = topicPrefix + "/online";
  doc["payload_available"] = "true";
  doc["payload_not_available"] = "false";
  auto dev = doc["device"].to<JsonObject>();
  addDeviceBlock(dev);
}

void DiscoveryDocument::addStatsFreeMemorySensor(JsonDocument &doc, const String &topicPrefix)
{
  doc["name"] = deviceName + " Free Memory";
  doc["unique_id"] = "ardumower-" + chipIdStr + "-free_memory";
  doc["state_topic"] = topicPrefix + "/ha/stats/free_memory";
  doc["unit_of_measurement"] = "B";
  doc["device_class"] = "data_size";
  doc["state_class"] = "measurement";
  doc["icon"] = "mdi:memory";
  doc["availability_topic"] = topicPrefix + "/online";
  doc["payload_available"] = "true";
  doc["payload_not_available"] = "false";
  auto dev = doc["device"].to<JsonObject>();
  addDeviceBlock(dev);
}

// --- Map info sensors ---

void DiscoveryDocument::addMapAreaSensor(JsonDocument &doc, const String &topicPrefix)
{
  doc["name"] = deviceName + " Map Area";
  doc["unique_id"] = "ardumower-" + chipIdStr + "-map_area";
  doc["state_topic"] = topicPrefix + "/ha/map/area";
  doc["unit_of_measurement"] = "m²";
  doc["icon"] = "mdi:vector-square";
  doc["availability_topic"] = topicPrefix + "/online";
  doc["payload_available"] = "true";
  doc["payload_not_available"] = "false";
  auto dev = doc["device"].to<JsonObject>();
  addDeviceBlock(dev);
}

// --- Map point count sensors ---

void DiscoveryDocument::addMapPerimeterPointsSensor(JsonDocument &doc, const String &topicPrefix)
{
  doc["name"] = deviceName + " Map Perimeter Points";
  doc["unique_id"] = "ardumower-" + chipIdStr + "-map_perimeter_points";
  doc["state_topic"] = topicPrefix + "/ha/map/perimeter_points";
  doc["icon"] = "mdi:vector-polygon";
  doc["availability_topic"] = topicPrefix + "/online";
  doc["payload_available"] = "true";
  doc["payload_not_available"] = "false";
  auto dev = doc["device"].to<JsonObject>();
  addDeviceBlock(dev);
}

void DiscoveryDocument::addMapExclusionPointsSensor(JsonDocument &doc, const String &topicPrefix)
{
  doc["name"] = deviceName + " Map Exclusion Points";
  doc["unique_id"] = "ardumower-" + chipIdStr + "-map_exclusion_points";
  doc["state_topic"] = topicPrefix + "/ha/map/exclusion_points";
  doc["icon"] = "mdi:vector-difference";
  doc["availability_topic"] = topicPrefix + "/online";
  doc["payload_available"] = "true";
  doc["payload_not_available"] = "false";
  auto dev = doc["device"].to<JsonObject>();
  addDeviceBlock(dev);
}

void DiscoveryDocument::addMapDockPointsSensor(JsonDocument &doc, const String &topicPrefix)
{
  doc["name"] = deviceName + " Map Dock Points";
  doc["unique_id"] = "ardumower-" + chipIdStr + "-map_dock_points";
  doc["state_topic"] = topicPrefix + "/ha/map/dock_points";
  doc["icon"] = "mdi:home-import-outline";
  doc["availability_topic"] = topicPrefix + "/online";
  doc["payload_available"] = "true";
  doc["payload_not_available"] = "false";
  auto dev = doc["device"].to<JsonObject>();
  addDeviceBlock(dev);
}

void DiscoveryDocument::addMapWaypointsSensor(JsonDocument &doc, const String &topicPrefix)
{
  doc["name"] = deviceName + " Map Waypoints";
  doc["unique_id"] = "ardumower-" + chipIdStr + "-map_waypoints";
  doc["state_topic"] = topicPrefix + "/ha/map/waypoints";
  doc["icon"] = "mdi:map-marker-path";
  doc["availability_topic"] = topicPrefix + "/online";
  doc["payload_available"] = "true";
  doc["payload_not_available"] = "false";
  auto dev = doc["device"].to<JsonObject>();
  addDeviceBlock(dev);
}

void DiscoveryDocument::addMapTotalPointsSensor(JsonDocument &doc, const String &topicPrefix)
{
  doc["name"] = deviceName + " Map Total Points";
  doc["unique_id"] = "ardumower-" + chipIdStr + "-map_total_points";
  doc["state_topic"] = topicPrefix + "/ha/map/total_points";
  doc["icon"] = "mdi:counter";
  doc["availability_topic"] = topicPrefix + "/online";
  doc["payload_available"] = "true";
  doc["payload_not_available"] = "false";
  auto dev = doc["device"].to<JsonObject>();
  addDeviceBlock(dev);
}

// --- Mow progress sensor ---

void DiscoveryDocument::addMowProgressSensor(JsonDocument &doc, const String &topicPrefix)
{
  doc["name"] = deviceName + " Mow Progress";
  doc["unique_id"] = "ardumower-" + chipIdStr + "-mow_progress";
  doc["state_topic"] = topicPrefix + "/ha/sensor/mow_progress";
  doc["icon"] = "mdi:progress-clock";
  doc["availability_topic"] = topicPrefix + "/online";
  doc["payload_available"] = "true";
  doc["payload_not_available"] = "false";
  auto dev = doc["device"].to<JsonObject>();
  addDeviceBlock(dev);
}

// --- Actors: Buttons ---

void DiscoveryDocument::addStartButton(JsonDocument &doc, const String &topicPrefix)
{
  doc["name"] = deviceName + " Start";
  doc["unique_id"] = "ardumower-" + chipIdStr + "-start";
  doc["command_topic"] = topicPrefix + "/ha/button/start";
  doc["icon"] = "mdi:play";
  doc["availability_topic"] = topicPrefix + "/online";
  doc["payload_available"] = "true";
  doc["payload_not_available"] = "false";
  auto dev = doc["device"].to<JsonObject>();
  addDeviceBlock(dev);
}

void DiscoveryDocument::addStopButton(JsonDocument &doc, const String &topicPrefix)
{
  doc["name"] = deviceName + " Stop";
  doc["unique_id"] = "ardumower-" + chipIdStr + "-stop";
  doc["command_topic"] = topicPrefix + "/ha/button/stop";
  doc["icon"] = "mdi:stop";
  doc["availability_topic"] = topicPrefix + "/online";
  doc["payload_available"] = "true";
  doc["payload_not_available"] = "false";
  auto dev = doc["device"].to<JsonObject>();
  addDeviceBlock(dev);
}

void DiscoveryDocument::addDockButton(JsonDocument &doc, const String &topicPrefix)
{
  doc["name"] = deviceName + " Dock";
  doc["unique_id"] = "ardumower-" + chipIdStr + "-dock";
  doc["command_topic"] = topicPrefix + "/ha/button/dock";
  doc["icon"] = "mdi:home-import-outline";
  doc["availability_topic"] = topicPrefix + "/online";
  doc["payload_available"] = "true";
  doc["payload_not_available"] = "false";
  auto dev = doc["device"].to<JsonObject>();
  addDeviceBlock(dev);
}

void DiscoveryDocument::addSkipWaypointButton(JsonDocument &doc, const String &topicPrefix)
{
  doc["name"] = deviceName + " Skip Waypoint";
  doc["unique_id"] = "ardumower-" + chipIdStr + "-skip_waypoint";
  doc["command_topic"] = topicPrefix + "/ha/button/skip_waypoint";
  doc["icon"] = "mdi:skip-next";
  doc["availability_topic"] = topicPrefix + "/online";
  doc["payload_available"] = "true";
  doc["payload_not_available"] = "false";
  auto dev = doc["device"].to<JsonObject>();
  addDeviceBlock(dev);
}

// --- Actors: Number ---

void DiscoveryDocument::addSpeedNumber(JsonDocument &doc, const String &topicPrefix)
{
  doc["name"] = deviceName + " Speed";
  doc["unique_id"] = "ardumower-" + chipIdStr + "-speed";
  doc["command_topic"] = topicPrefix + "/ha/number/speed/set";
  doc["state_topic"] = topicPrefix + "/ha/number/speed/state";
  doc["min"] = 0;
  doc["max"] = 1.0;
  doc["step"] = 0.05;
  doc["unit_of_measurement"] = "m/s";
  doc["icon"] = "mdi:speedometer";
  doc["availability_topic"] = topicPrefix + "/online";
  doc["payload_available"] = "true";
  doc["payload_not_available"] = "false";
  auto dev = doc["device"].to<JsonObject>();
  addDeviceBlock(dev);
}

// --- Actors: Select ---

void DiscoveryDocument::addMapSelect(JsonDocument &doc, const String &topicPrefix, const std::vector<ArduMower::Domain::Robot::MapInfo> &maps)
{
  doc["name"] = deviceName + " Map";
  doc["unique_id"] = "ardumower-" + chipIdStr + "-map_select";
  doc["command_topic"] = topicPrefix + "/ha/select/map/set";
  doc["state_topic"] = topicPrefix + "/ha/select/map/state";
  doc["icon"] = "mdi:map";
  doc["availability_topic"] = topicPrefix + "/online";
  doc["payload_available"] = "true";
  doc["payload_not_available"] = "false";

  // Populate options from actual map list (human-readable names)
  auto opts = doc["options"].to<JsonArray>();
  opts.add("none");
  for (const auto &m : maps) {
    opts.add(m.name.length() > 0 ? m.name : m.id);
  }

  auto dev = doc["device"].to<JsonObject>();
  addDeviceBlock(dev);
}

// --- Uploaded map ---

void DiscoveryDocument::addUploadedMapSensor(JsonDocument &doc, const String &topicPrefix)
{
  doc["name"] = deviceName + " Uploaded Map";
  doc["unique_id"] = "ardumower-" + chipIdStr + "-uploaded_map";
  doc["state_topic"] = topicPrefix + "/ha/map/uploaded";
  doc["icon"] = "mdi:map-check";
  doc["availability_topic"] = topicPrefix + "/online";
  doc["payload_available"] = "true";
  doc["payload_not_available"] = "false";
  auto dev = doc["device"].to<JsonObject>();
  addDeviceBlock(dev);
}

void DiscoveryDocument::addUploadButton(JsonDocument &doc, const String &topicPrefix)
{
  doc["name"] = deviceName + " Upload Map";
  doc["unique_id"] = "ardumower-" + chipIdStr + "-upload";
  doc["command_topic"] = topicPrefix + "/ha/button/upload";
  doc["icon"] = "mdi:upload";
  doc["availability_topic"] = topicPrefix + "/online";
  doc["payload_available"] = "true";
  doc["payload_not_available"] = "false";
  auto dev = doc["device"].to<JsonObject>();
  addDeviceBlock(dev);
}

bool DiscoveryDocument::publishAll(std::function<bool(const String &, const String &, bool)> publishFn,
                                   const String &topicPrefix,
                                   const std::vector<ArduMower::Domain::Robot::MapInfo> &maps)
{
  chipIdStr = chipIdString();
  deviceName = settings.general.name;

  String discoPrefix = discoveryPrefix();
  String baseId = "ardumower-" + chipIdStr;

  // Helper macro to publish one discovery config
  #define PUBLISH_DISCOVERY(component, objectId, addFn)                       \
    {                                                                         \
      JsonDocument doc;                                                       \
      addFn(doc, topicPrefix);                                                \
      String payload;                                                         \
      serializeJson(doc, payload);                                            \
      String t = discoPrefix + "/" + component + "/" + baseId + "/" +         \
                 objectId + "/config";                                        \
      Log(DBG, "HA Discovery: %s -> %s", t.c_str(), payload.c_str());        \
      if (!publishFn(t, payload, true))                                       \
        return false;                                                         \
    }

  PUBLISH_DISCOVERY("vacuum", "vacuum", addVacuumEntity);
  PUBLISH_DISCOVERY("sensor", "battery_voltage", addBatteryVoltageSensor);
  PUBLISH_DISCOVERY("sensor", "temperature", addTemperatureSensor);
  PUBLISH_DISCOVERY("sensor", "amps", addAmpsSensor);
  PUBLISH_DISCOVERY("sensor", "gps_accuracy", addPositionAccuracySensor);
  PUBLISH_DISCOVERY("sensor", "satellites", addSatellitesSensor);
  PUBLISH_DISCOVERY("sensor", "gps_solution", addGpsSolutionSensor);
  PUBLISH_DISCOVERY("sensor", "map_crc", addMapCrcSensor);
  PUBLISH_DISCOVERY("sensor", "job_state", addJobStateSensor);
  PUBLISH_DISCOVERY("binary_sensor", "online", addWifiOnlineBinarySensor);

  // Stats sensors
  PUBLISH_DISCOVERY("sensor", "mow_distance", addStatsMowDistanceSensor);
  PUBLISH_DISCOVERY("sensor", "mow_duration", addStatsMowDurationSensor);
  PUBLISH_DISCOVERY("sensor", "charge_duration", addStatsChargeDurationSensor);
  PUBLISH_DISCOVERY("sensor", "idle_duration", addStatsIdleDurationSensor);
  PUBLISH_DISCOVERY("sensor", "obstacles", addStatsObstaclesSensor);
  PUBLISH_DISCOVERY("sensor", "temp_min", addStatsTempMinSensor);
  PUBLISH_DISCOVERY("sensor", "temp_max", addStatsTempMaxSensor);
  PUBLISH_DISCOVERY("sensor", "gps_checksum_errors", addStatsGpsChecksumErrorsSensor);
  PUBLISH_DISCOVERY("sensor", "gps_jumps", addStatsGpsJumpsSensor);
  PUBLISH_DISCOVERY("sensor", "free_memory", addStatsFreeMemorySensor);

  // Map info sensors
  PUBLISH_DISCOVERY("sensor", "map_area", addMapAreaSensor);

  // Map point count sensors
  PUBLISH_DISCOVERY("sensor", "map_perimeter_points", addMapPerimeterPointsSensor);
  PUBLISH_DISCOVERY("sensor", "map_exclusion_points", addMapExclusionPointsSensor);
  PUBLISH_DISCOVERY("sensor", "map_dock_points", addMapDockPointsSensor);
  PUBLISH_DISCOVERY("sensor", "map_waypoints", addMapWaypointsSensor);
  PUBLISH_DISCOVERY("sensor", "map_total_points", addMapTotalPointsSensor);

  // Mow progress
  PUBLISH_DISCOVERY("sensor", "mow_progress", addMowProgressSensor);

  // Actors
  PUBLISH_DISCOVERY("button", "start", addStartButton);
  PUBLISH_DISCOVERY("button", "stop", addStopButton);
  PUBLISH_DISCOVERY("button", "dock", addDockButton);
  PUBLISH_DISCOVERY("button", "skip_waypoint", addSkipWaypointButton);
  PUBLISH_DISCOVERY("number", "speed", addSpeedNumber);

  // Map select with dynamic options
  {
    JsonDocument doc;
    addMapSelect(doc, topicPrefix, maps);
    String payload;
    serializeJson(doc, payload);
    String t = discoPrefix + "/select/" + baseId + "/map/config";
    Log(DBG, "HA Discovery: %s -> %s", t.c_str(), payload.c_str());
    if (!publishFn(t, payload, true))
      return false;
  }

  // Upload status + button
  PUBLISH_DISCOVERY("sensor", "uploaded_map", addUploadedMapSensor);
  PUBLISH_DISCOVERY("button", "upload", addUploadButton);

  #undef PUBLISH_DISCOVERY

  Log(INFO, "HA Discovery: published %d entities", 35);
  return true;
}

float batteryVoltageToLevel(const float v)
{
  const float cell = v / 7;
  const float empty = 3;
  const float full = 4.2;
  if (cell <= empty)
    return 0;
  if (cell >= full)
    return 100;

  const float rel = (cell - empty) / (full - empty);

  return rel * 100.0;
}
