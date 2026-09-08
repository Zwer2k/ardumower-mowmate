#ifndef _HA_H
#define _HA_H

#include <Arduino.h>
#include <functional>
#include "settings.h"
#include "domain.h"

namespace ArduMower
{
  namespace Modem
  {
    namespace HomeAssistant
    {
      class Adapter
      {
      private:
        ArduMower::Modem::Settings::Settings &settings;
        ArduMower::Domain::Robot::StateSource &source;
        ArduMower::Domain::Robot::CommandExecutor &cmd;
        std::function<bool(const String &, const String &)> tx;

        float batteryVoltage;
        int   job;
        float speed;
        bool  mowerEnabled;

        bool  commandSpeedPending;
        float commandSpeedValue;
        bool  commandMowerEnabledPending;
        bool  commandMowerEnabledValue;

        void loopReport(const uint32_t now);
        void loopCommand(const uint32_t now);
        void loopSensors(const uint32_t now);
        void loopStats(const uint32_t now);
        void loopMapInfo(const uint32_t now);
        void loopActors(const uint32_t now);

        // Pending actor commands
        bool  pendingStart = false;
        bool  pendingStop = false;
        bool  pendingDock = false;
        bool  pendingSkipWaypoint = false;
        float pendingSpeed = -1;
        String pendingMapId;

      public:
        Adapter(ArduMower::Modem::Settings::Settings &_settings,
                ArduMower::Domain::Robot::StateSource &_source,
                ArduMower::Domain::Robot::CommandExecutor &_cmd,
                std::function<bool(const String &, const String &)> _tx)
            : settings(_settings), source(_source), cmd(_cmd), tx(_tx),
              batteryVoltage(0), job(0), speed(0), mowerEnabled(false),
              commandSpeedPending(false), commandSpeedValue(0),
              commandMowerEnabledPending(false), commandMowerEnabledValue(false){};

        void loop(const uint32_t now);
        void onFanSpeedMessage(const String &payload);
        void onButtonMessage(const String &button, const String &payload);
        void onSpeedMessage(const String &payload);
        void onMapSelectMessage(const String &payload);
        void onUploadMessage(const String &payload);
      };

      class DiscoveryDocument
      {
      private:
        ArduMower::Modem::Settings::Settings &settings;
        String chipIdStr;
        String deviceName;

        void addDeviceBlock(JsonObject doc);
        void addVacuumEntity(JsonDocument &doc, const String &topicPrefix);
        void addBatteryVoltageSensor(JsonDocument &doc, const String &topicPrefix);
        void addTemperatureSensor(JsonDocument &doc, const String &topicPrefix);
        void addAmpsSensor(JsonDocument &doc, const String &topicPrefix);
        void addPositionAccuracySensor(JsonDocument &doc, const String &topicPrefix);
        void addSatellitesSensor(JsonDocument &doc, const String &topicPrefix);
        void addGpsSolutionSensor(JsonDocument &doc, const String &topicPrefix);
        void addWifiOnlineBinarySensor(JsonDocument &doc, const String &topicPrefix);
        void addMapCrcSensor(JsonDocument &doc, const String &topicPrefix);
        void addJobStateSensor(JsonDocument &doc, const String &topicPrefix);

        // Stats sensors
        void addStatsMowDistanceSensor(JsonDocument &doc, const String &topicPrefix);
        void addStatsMowDurationSensor(JsonDocument &doc, const String &topicPrefix);
        void addStatsChargeDurationSensor(JsonDocument &doc, const String &topicPrefix);
        void addStatsIdleDurationSensor(JsonDocument &doc, const String &topicPrefix);
        void addStatsObstaclesSensor(JsonDocument &doc, const String &topicPrefix);
        void addStatsTempMinSensor(JsonDocument &doc, const String &topicPrefix);
        void addStatsTempMaxSensor(JsonDocument &doc, const String &topicPrefix);
        void addStatsGpsChecksumErrorsSensor(JsonDocument &doc, const String &topicPrefix);
        void addStatsGpsJumpsSensor(JsonDocument &doc, const String &topicPrefix);
        void addStatsFreeMemorySensor(JsonDocument &doc, const String &topicPrefix);

        // Map info sensors
        void addMapAreaSensor(JsonDocument &doc, const String &topicPrefix);

        // Map point count sensors
        void addMapPerimeterPointsSensor(JsonDocument &doc, const String &topicPrefix);
        void addMapExclusionPointsSensor(JsonDocument &doc, const String &topicPrefix);
        void addMapDockPointsSensor(JsonDocument &doc, const String &topicPrefix);
        void addMapWaypointsSensor(JsonDocument &doc, const String &topicPrefix);
        void addMapTotalPointsSensor(JsonDocument &doc, const String &topicPrefix);

        // Mow progress sensor
        void addMowProgressSensor(JsonDocument &doc, const String &topicPrefix);

        // Actors: Buttons
        void addStartButton(JsonDocument &doc, const String &topicPrefix);
        void addStopButton(JsonDocument &doc, const String &topicPrefix);
        void addDockButton(JsonDocument &doc, const String &topicPrefix);
        void addSkipWaypointButton(JsonDocument &doc, const String &topicPrefix);

        // Actors: Number (speed)
        void addSpeedNumber(JsonDocument &doc, const String &topicPrefix);

        // Actors: Select (map)
        void addMapSelect(JsonDocument &doc, const String &topicPrefix, const std::vector<ArduMower::Domain::Robot::MapInfo> &maps);

        // Upload status
        void addUploadedMapSensor(JsonDocument &doc, const String &topicPrefix);
        void addUploadButton(JsonDocument &doc, const String &topicPrefix);

      public:
        DiscoveryDocument(ArduMower::Modem::Settings::Settings &_settings)
            : settings(_settings){};

        // Returns the discovery config topic prefix (homeassistant/)
        String discoveryPrefix();

        // Publish all discovery configs. Returns false on first failure.
        bool publishAll(std::function<bool(const String &, const String &, bool)> publishFn,
                        const String &topicPrefix,
                        const std::vector<ArduMower::Domain::Robot::MapInfo> &maps);
      };
    }
  }
}
#endif
