#ifndef _MOWER_ADAPTER_H
#define _MOWER_ADAPTER_H

#include "domain.h"
#include "mower_map.h"
#include "map_manager.h"
#include "router.h"
#include "encrypt.h"
#include "settings.h"

namespace ArduMower
{
  namespace Modem
  {
    struct PendingCommand {
      String command;
      std::function<void(const char* response, bool ok)> callback;
      uint32_t startTime = 0;
      int timeoutMs = 0;
      bool active = false;
      bool done = false;
      char response[READER_BUF_SIZE];
      bool ok = false;
    };

    struct MapUploadState {
      bool active = false;
      enum Phase {
        idle,
        start,
        perimeter,
        exclusions,
        dockpoints,
        waypoints,
        counts,
        exclusionSizes,
        finalizing,
        done,
        error
      } phase = idle;
      size_t polygonIdx = 0;
      size_t pointIdx = 0;
      int chunkRetry = 0;
      int totalPointsSent = 0;
      int lastBaseIdx = 0;
      int lastExpectedNextIdx = 0;
      char lastResponse[READER_BUF_SIZE];
      bool lastOk = false;
      bool waitingForResponse = false;
      bool lastCommandQueued = false;
      ArduMower::Domain::Robot::MowerMap snapshot;
    };

    class MowerAdapter : public RxDrain, public TxDrain, public ArduMower::Domain::Robot::StateSource, public ArduMower::Domain::Robot::CommandExecutor {
    private:
      Settings::Settings &settings;
      Router &router;
      bool sendIsInitialized;
      Encrypt enc;
      ArduMower::Domain::Robot::Properties _props;
      ArduMower::Domain::Robot::State::State _state;
      ArduMower::Domain::Robot::Stats::Stats _stats;
      ArduMower::Domain::Robot::DesiredState _desiredState;
      ArduMower::Domain::Robot::SensorSummary _sensorSummary;
      ArduMower::Domain::Robot::GpsDetails _gpsDetails;
      ArduMower::Domain::Robot::UbxResponse _ubxResponse;
      ArduMower::Domain::Robot::Obstacles _obstacles;
      ArduMower::Domain::Robot::MowerMap _map;
      ArduMower::Domain::Robot::MowSettings _mowSettings;
      ArduMower::Modem::MapManager _mapManager;
      String _lastUploadedMapId;
      uint32_t _lastStateRequest = 0;
      uint32_t _lastStatsRequest = 0;
      uint32_t _lastSensorSummaryRequest = 0;
      uint32_t _lastObstaclesRequest = 0;
      uint32_t _lastGpsDetailsRequest = 0;
      uint32_t _lastControlRequest = 0;
      PendingCommand _pendingCommand;
      MapUploadState _mapUploadState;
      volatile bool _mapUploadPending = false;
      // Track last applied position settings to avoid redundant AT+P commands
      bool _lastPosApplied = false;
      bool _lastPosAbsolute = false;
      double _lastPosLon = 0.0;
      double _lastPosLat = 0.0;
      bool _mapListDirty = true;
      String _currentMapId;
      String _currentMapHash;
      int _currentMapCrc = 0;
      double _currentMapArea = 0.0;
      bool _currentMapUnsaved = false;
      String _pendingRenameId;
      String _pendingRenameName;

      // Transiente (RAM-only) Karten: erscheinen im Dropdown, werden aber nicht
      // automatisch in SPIFFS persistiert. Das vermeidet Flash-Verschleiß bei
      // jedem "New Map" oder abgefangenem Kartentransfer.
      struct TransientMap {
        String id;
        String name;
        double area = 0.0;
        String hash;
        int crc = 0;
        double rotation = 0.0;
        uint32_t timestamp = 0;
        bool requiresRename = true;
        ArduMower::Domain::Robot::MowerMap map;
      };
      std::vector<TransientMap> _transientMaps;
      struct MapDraft {
        String id;
        ArduMower::Domain::Robot::MowerMap map;
      };
      std::vector<MapDraft> _mapDrafts;
      uint32_t _transientIdCounter = 0;
      String allocateTransientId();
      String findOrCreateTransientMap(const ArduMower::Domain::Robot::MowerMap &map, const String &name, double rotation);
      const TransientMap* findTransientMap(const String &id) const;
      bool removeTransientMap(const String &id);
      void updateTransientMapMeta(const String &id, const ArduMower::Domain::Robot::MowerMap &map, double rotation);
      const MapDraft* findMapDraft(const String &id) const;
      void storeCurrentMapDraft();
      void removeMapDraft(const String &id);
      bool isNameUsed(const String &name, const String &excludeId = "") const;

      void updateCurrentMapMeta();
      void syncMowSettingsFromMap();
      // Cache für rohe Antwort-Strings (mit Checksumme) – für HTTP-Cache-Serving
      char _cachedRawState[READER_BUF_SIZE];
      char _cachedRawStats[READER_BUF_SIZE];
      char _cachedRawSensorSummary[READER_BUF_SIZE];
      char _cachedRawGpsDetails[READER_BUF_SIZE];
      // Temporärer Buffer für alle empfangenen Wegpunkte (alle Typen)
      std::vector<ArduMower::Domain::Robot::MapPoint> tempWaypointsBuffer;
      std::vector<int> tempExclusionSizes;
      int tempNPerimeter = 0;
      int tempNExclusions = 0;
      int tempNDockpoints = 0;
      int tempNWaypoints = 0;
      bool tempMapCountsReceived = false;
      void finalizeInterceptedMap();

      void parseArduMowerCommand(const char* line);
      void parseArduMowerResponse(const char* line);
      void parseVersionResponse(const char* line);
      void parseStateResponse(const char* line);
      void parseStatisticsResponse(const char* line);
      void parseSensorSummaryResponse(const char* line);
      void parseObstaclesResponse(const char* line);
#if defined(ENABLE_LIVE_MAP) || defined(ENABLE_GPS_DASHBOARD)
      void parseGpsDetailsResponse(const char* line);
      void parseUbxResponse(const char* line);
#endif
      void parseATCCommand(const char* line);
      void parseATWCommand(const char* line);
      void parseATNCommand(const char* line);
      void parseATXCommand(const char* line);

      bool sendCommand(const String& command, bool encrypt = true);
      bool sendCommandWithResponse(const String& command, char* response, size_t responseLen, bool encrypt = true, int timeoutMs = 3000);
      bool sendCommandWithResponseAsync(const String& command, std::function<void(const char*, bool)> callback, bool encrypt = true, int timeoutMs = 3000);
      void processPendingCommand();
      void processMapUpload();
      void startMapUploadFromLoop();
      bool sendMapChunkAsync(const std::vector<ArduMower::Domain::Robot::MapPoint> &pts, int baseIdx);
      bool assertSendIsInitialized();
      int containsNonUTF8(const String& input);
      String bytesToHexString(const String& byteString);
    public:
      MowerAdapter(Settings::Settings &_settings, Router &_router);
      void begin();
      virtual ArduMower::Domain::Robot::Properties props() { return _props; }
      virtual ArduMower::Domain::Robot::State::State state() { return _state; };
      virtual ArduMower::Domain::Robot::Stats::Stats stats() { return _stats; };
      virtual ArduMower::Domain::Robot::DesiredState desiredState() { return _desiredState; }
      virtual ArduMower::Domain::Robot::SensorSummary sensorSummary() { return _sensorSummary; }
      virtual ArduMower::Domain::Robot::GpsDetails gpsDetails() { return _gpsDetails; }
      virtual ArduMower::Domain::Robot::UbxResponse ubxResponse() { return _ubxResponse; }
      virtual ArduMower::Domain::Robot::Properties *propsP() { return &_props; }
      virtual ArduMower::Domain::Robot::State::State *stateP() { return &_state; };
      virtual ArduMower::Domain::Robot::Stats::Stats *statsP() { return &_stats; }
      virtual ArduMower::Domain::Robot::DesiredState *desiredStateP() { return &_desiredState; }
      virtual ArduMower::Domain::Robot::SensorSummary *sensorSummaryP() { return &_sensorSummary; }
      virtual ArduMower::Domain::Robot::GpsDetails *gpsDetailsP() { return &_gpsDetails; }
      virtual ArduMower::Domain::Robot::UbxResponse *ubxResponseP() { return &_ubxResponse; }
      virtual ArduMower::Domain::Robot::Obstacles obstacles() { return _obstacles; }
      virtual ArduMower::Domain::Robot::Obstacles *obstaclesP() { return &_obstacles; }
      virtual ArduMower::Domain::Robot::MowerMap mowerMap() { return _map; }
      virtual void beginMowerMapRead() override { _map.beginRead(); }
      virtual void endMowerMapRead() override { _map.endRead(); }
      virtual bool isMowerMapReading() override { return _map.isReading(); }
      virtual ArduMower::Domain::Robot::MowSettings mowSettings() { return _mowSettings; }
      virtual ArduMower::Domain::Robot::MowSettings *mowSettingsP() { return &_mowSettings; }

      // Karten-Verwaltung
      virtual std::vector<ArduMower::Domain::Robot::MapInfo> mapList() override;
      virtual String activeMapId() override { return _mapManager.activeId(); }
      virtual String currentMapId() override { return _currentMapId; }
      virtual bool mapListDirty() override;
      virtual void clearMapListDirty() override;
      virtual bool createMap(const String &name) override;
      virtual bool copyMap(const String &name) override;
      virtual String saveMap(const String &name, double rotation = 0.0) override;
      virtual bool loadMap(const String &id) override;
      virtual bool renameMap(const String &id, const String &name) override;
      virtual bool deleteMap(const String &id) override;
      virtual bool discardMap() override;
      virtual bool setActiveMap(const String &id) override;
      virtual String currentMapHash() override;
      virtual int currentMapCrc() override;
      virtual double currentMapArea() override;
      virtual double currentMapRotation() override { return _map.rotation; }

      virtual bool importMowerMap(const String &json, ArduMower::Domain::Robot::MowerMap &outMap) override;
      virtual String exportMowerMap(const ArduMower::Domain::Robot::MowerMap &map) override;
      virtual String lastUploadedMapId() override { return _lastUploadedMapId; }

      // Zugriff auf gecachte rohe Antworten (für HTTP-Cache)
      virtual String cachedRawState() { return _cachedRawState; }
      virtual String cachedRawStats() { return _cachedRawStats; }
      virtual String cachedRawSensorSummary() { return _cachedRawSensorSummary; }
      virtual String cachedRawGpsDetails() { return _cachedRawGpsDetails; }
      virtual bool changeSpeed(float speed);
      virtual bool changeWayPerc(float perc);
      virtual bool changeMowHeight(int height);
      virtual bool tuneParam(int index, float value);
      virtual bool dock();
      virtual bool finishAndRestartEnabled(bool enabled);
      virtual bool mowerEnabled(bool enabled);
      virtual bool mowerAuto();
      virtual bool setFixTimeout(int timeout);
      virtual bool setWaypoint(float waypoint);
      virtual bool skipWaypoint();
      virtual bool start();
      virtual bool stop();
      virtual bool sonarEnabled(bool enabled);
      virtual bool requestVersion();
      virtual bool requestStatus();
      virtual bool requestStatusNow();
      virtual bool requestStats();
      virtual bool requestStatsNow();
      virtual bool requestSensorSummary();
      virtual bool requestControl();
      virtual bool requestObstacles();
      virtual bool applyPositionSettings();
#if defined(ENABLE_LIVE_MAP) || defined(ENABLE_GPS_DASHBOARD)
      virtual bool requestGpsDetails();
      virtual bool sendUbx(const String &hexCmd);
#endif
      virtual bool manualDrive(float linear, float angular);
      virtual bool navigateTo(float x, float y);
      virtual bool reboot();
      virtual bool rebootGPS();
      virtual bool powerOff();
      virtual bool uploadMapToMower() override;
      virtual bool uploadMapToMowerActive() override { return _mapUploadState.active; }
      virtual bool uploadMapToMowerSuccess() override { return _mapUploadState.phase == MapUploadState::done; }
      virtual ArduMower::Domain::Robot::UploadProgress uploadProgress() override;
      virtual bool customCmd(String cmd);
      virtual void drainRx(const char* line, bool &stop) override;
      virtual void drainTx(const char* line, bool &stop) override;
      virtual void setMap(const ArduMower::Domain::Robot::MowerMap &map);
      virtual void setMowSettings(const ArduMower::Domain::Robot::MowSettings &s);
      virtual void loop();
    };
  }
}
#endif
