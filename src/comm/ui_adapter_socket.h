#pragma once

// Map transfers and firmware status updates share the WebSocket queue. Keep
// enough entries available so progress frames are not dropped during a map
// transfer or reconnect burst.
#ifndef WS_MAX_QUEUED_MESSAGES
#define WS_MAX_QUEUED_MESSAGES 128
#endif

#include "log.h"
#include <ESPAsyncWebServer.h>
#include <map>
#include <deque>
#include <ArduinoJson.h>
#include "domain.h"
#include "schedule.h"
#ifdef MOWER_TERMINAL
#include "terminal.h"
#endif
#include "ota_mower_updater.h"
#include <vector>

namespace ArduMower
{
  namespace Modem
  {
    namespace Http
    {
      enum RequestDataType {
        requestHello = 0,
        modemLogSettings,
        mowerConsoleRequest,
        requestGpsDetails,
        stopGpsDetails,
        requestSensorSummary,
        stopSensorSummary,
        requestUbx,
        requestLogExport,
        joystickMove,
        navigateTo,
        setMap,
        uploadMap,
        robotCommand,
        setMowSettings,
        requestMowSettings,
        clearWaypoints,
        calculateWaypoints,
        listMaps,
        createMap,
        copyMap,
        loadMap,
        saveMap,
        renameMap,
        deleteMap,
        discardMap,
        setActiveMap,
        importMap,
        exportMap,
        setSchedule,
        saveSchedule,
        requestSchedule,
        requestClock,
        requestDataTypeLength
      };

      enum ResponseDataType {
        responseHello = 0,
        mowerState,
        mowerStats,
        desiredState,
        modemLog,
        mowerConsole,
        map,
        sensorSummary,
        gpsDetails,
        ubxResponse,
        logExport,
        mowSettings,
        operationProgress,
        mapList,
        drivenTrack,
        mowerMap,
        schedule,
        clock,
        obstacles,
        mapAck,
        responseDataTypeLength
      };

      enum class MapPointType {
        Perimeter,
        Exclusion,
        Dockpoints,
        Waypoints,
        SearchWire
      };

      // WebSocket event types for deferred processing in loopTask.
      // All wsEvent() work is queued here and executed in processWsEvents()
      // so that itemMap, _frameBuffer, and handleData() run exclusively in
      // the loopTask — eliminating cross-task races with the async_tcp task.
      enum class WsEvtType { CONNECT, DISCONNECT, DATA_START, DATA_CONTINUE, DATA_FINAL };

      struct WsEvent {
        WsEvtType type;
        uint32_t clientId;
        // For DATA events: owned copy of payload (null for CONNECT/DISCONNECT)
        uint8_t *data;
        size_t len;
        // For DATA events: frame metadata (index, len, num, final, opcode)
        AwsFrameInfo info;
      };

      class UiSocketHandler;

      class UiSocketItem
      {
        friend class UiSocketHandler;
      public:
        UiSocketItem(
          UiSocketHandler *socketHandler,
          uint32_t clientId,
          ArduMower::Domain::Robot::StateSource &source);
        void handleData(RequestDataType dataType, JsonDocument &jsonData);
        bool sendText(String text);
        bool sendTextRaw(String text);
        void ping();
        AwsClientStatus status();
        uint32_t clientId() { return _clientId; }
        uint32_t drivenTrackSequence() const { return _drivenTrackSequence; }
        void setDrivenTrackSequence(uint32_t sequence) { _drivenTrackSequence = sequence; }
        ~UiSocketItem();
      
      private:
        UiSocketHandler *_socketHandler;
        uint32_t _clientId;
        uint32_t _drivenTrackSequence = 0;
        ArduMower::Domain::Robot::StateSource &_source;
      };

      struct MapChunkSendState {
        bool active = false;
        uint32_t clientId = 0;
        uint32_t timestamp = 0;
        uint32_t transferId = 0;
        int phase = 0;
        size_t exclusionIdx = 0;
        size_t idx = 0;
        uint32_t lastRetryMs = 0;
        ArduMower::Domain::Robot::MowerMap snapshot;
        String metaHash;
        int metaCrc = 0;
        double metaArea = 0.0;
        double metaRotation = 0.0;
      };

      class UiSocketHandler
      {
        friend class UiSocketItem;
      public:
#ifdef MOWER_TERMINAL
        UiSocketHandler(
          Terminal &terminal,
          AsyncWebServer &server,
          ArduMower::Domain::Robot::StateSource &source,
          ArduMower::Domain::Robot::CommandExecutor &cmd,
          Ota::MowerUpdater &mowerUpdater,
          ArduMower::Modem::Schedule::Manager &scheduleManager
        );
#else
        UiSocketHandler(
          AsyncWebServer &server,
          ArduMower::Domain::Robot::StateSource &source,
          ArduMower::Domain::Robot::CommandExecutor &cmd,
          Ota::MowerUpdater &mowerUpdater,
          ArduMower::Modem::Schedule::Manager &scheduleManager
        );
#endif

        ~UiSocketHandler();
        
        void begin();
        void loop();
        void logToUiLoop();
        bool cmdToMower(String cmd);
        void sendData(ResponseDataType dataType, UiSocketItem *sendTo = NULL, bool force = false);
        void sendMapList(UiSocketItem *sendTo = NULL);
        void sendMapAck(UiSocketItem *sendTo, uint32_t syncId, bool accepted);
        void sendSchedule(UiSocketItem *sendTo = NULL);
        void sendClock(UiSocketItem *sendTo = NULL);
        bool setSchedule(bool enabled, const std::vector<ArduMower::Modem::Schedule::Entry> &entries);
        bool saveSchedule();
        void processScheduleTrigger();
        void broadcastFlashProgress(size_t current, size_t total);
        void wsEvent(AsyncWebSocket * server, AsyncWebSocketClient * client, AwsEventType type, void * arg, uint8_t *data, size_t len);
#if defined(ENABLE_LIVE_MAP) || defined(ENABLE_GPS_DASHBOARD)
        bool sendUbx(const String &hexCmd);
#endif
        void resetRequestTimestamp(ResponseDataType dataType);
        void joystickMove(float linear, float angular);
        void navigateTo(float x, float y);
        void sendProgress(String operation, int progress, String message = "");
        void requestStats();
        void requestStatsNow();
        void cmdStart();
        void cmdStop();
        void cmdDock();
        void cmdSkipWaypoint();
        void cmdReboot();
        void cmdPowerOff();
        void cmdMowerEnabled(bool enabled);
        void cmdMowerAuto();
        void sendBufferedLogTo(UiSocketItem* item, uint16_t maxChunks = 0xFFFF);
        void setMap(const ArduMower::Domain::Robot::MowerMap &map);
        void setMowSettings(const ArduMower::Domain::Robot::MowSettings &s);
        void clearWaypoints();
        void calculateWaypoints();
        void processCalculateWaypoints();
        void uploadMapToMower();
        void processUploadToMower();
        void abortMapChunkSend();
        void sendWaypointsDirect(const std::vector<ArduMower::Domain::Robot::MapPoint> &waypoints, uint32_t timestamp);
        void pushDrivenTrackPoint(float x, float y, uint32_t timestamp);
        void sendDrivenTrack(UiSocketItem *sendTo = NULL);
        size_t clientCount() { return countConnectedClients(); }
        uint32_t lastClientActivity() const { return _lastClientActivity; }
        uint32_t lastWsConnectionEvent() const { return _lastWsConnectionEvent; }
        void markClientActivity() { _lastClientActivity = millis(); }
        void markWsConnectionEvent() { _lastWsConnectionEvent = millis(); }
        bool isMapChunkSendActive() const { return mapChunkSendState.active; }
        bool isClientReceivingChunk(uint32_t clientId) const;
        bool lockSendMutex() { return _sendMutex != NULL && xSemaphoreTake(_sendMutex, pdMS_TO_TICKS(100)) == pdTRUE; }
        void unlockSendMutex() { if (_sendMutex != NULL) xSemaphoreGive(_sendMutex); }

        bool sendMapChunkText(uint32_t clientId, const String& text);

        // Lock-safe access to itemMap. All itemMap/frameBuffer access from
        // any task MUST go through _clientsMutex to prevent iterator
        // invalidation / use-after-free when clients connect/disconnect.
        void lockClients() { xSemaphoreTake(_clientsMutex, portMAX_DELAY); }
        void unlockClients() { xSemaphoreGive(_clientsMutex); }
        UiSocketItem* findClient(uint32_t clientId);

        // Send text to a specific client using the library's locked API.
        // Handles heap guard, sendMutex, and activity marking.
        bool sendTextToId(uint32_t clientId, const char* data, size_t len);

        static const uint32_t mapSendDelayMs = 1000;
        uint32_t _mapSendPendingUntil = 0;
#ifdef MOWER_TERMINAL
        void sendBufferedTerminalTo(UiSocketItem* item, uint16_t maxChunks = 0xFFFF);
#endif

#if defined(ENABLE_LIVE_MAP) || defined(ENABLE_GPS_DASHBOARD)
        bool gpsDetailsActive = false;
#endif
        bool sensorSummaryActive = false;
#if defined(ENABLE_LIVE_MAP) || defined(ENABLE_GPS_DASHBOARD)
        bool ubxResponseActive = false;
        String pendingUbxCmd;

        uint8_t ubxPollSequence = 0;
        uint8_t ubxConfigIndex = 0;

        uint32_t gpsDetailsRefCount = 0;
#endif
        uint32_t sensorSummaryRefCount = 0;

        bool _mapListPending = false;
        bool _drivenTrackPending = false;
        bool _flashProgressPending = false;
        int _flashProgressPct = 0;
        bool _scheduleDirty = true;
        bool _scheduleTriggerPending = false;
        uint8_t _scheduleTriggerPhase = 0;
        String _scheduleTriggerMapId;

      private:
        void startMapChunkSend(UiSocketItem* sendTo, bool force);
        void processMapChunkSend();
        void processScheduleTriggerStateMachine();
        bool sendMapChunk(MapPointType pointType, const std::vector<ArduMower::Domain::Robot::MapPoint>& points, uint32_t timestamp, uint32_t clientId, int exclusionIdx, size_t startIdx, size_t blockSize, bool reset, size_t &nextIdx);
        AsyncWebSocket *_ws;

        // Mutex for WebSocket send operations (protects _ws->text* calls).
        SemaphoreHandle_t _sendMutex;

        // Mutex for itemMap / _frameBuffer access. ALL access to these
        // containers from ANY task must be guarded by this mutex.
        SemaphoreHandle_t _clientsMutex;

        // Deferred event queue: wsEvent() runs in the async_tcp task and
        // pushes events here. processWsEvents() drains the queue in loopTask.
        // This moves ALL itemMap / _frameBuffer / handleData work into the
        // loopTask, eliminating cross-task races.
        SemaphoreHandle_t _wsEvtMutex;
        std::deque<WsEvent> _wsEvtQueue;
        void processWsEvents();
        void enqueueWsEvent(WsEvent &&evt);

        // Pending hello messages for newly connected clients.
        // Serialized in wsEvent() (async_tcp) and sent in processWsEvents()
        // (loopTask) so that the heavy burst-send runs outside async_tcp.
        struct PendingHello { uint32_t clientId; String json; };
        std::deque<PendingHello> _pendingHellos;
        SemaphoreHandle_t _helloMutex;

        uint32_t lastVersionRequestTimestamp = 0;
        uint32_t oldDataTimestamp[ResponseDataType::responseDataTypeLength];
        uint32_t lastDataRequestTimestamp[ResponseDataType::responseDataTypeLength];
        uint32_t lastSentTimestamp[ResponseDataType::responseDataTypeLength];
        uint32_t lastclientPing = 0;
#if defined(ENABLE_LIVE_MAP) || defined(ENABLE_GPS_DASHBOARD)
        uint32_t _lastSentUbxTimestamp = 0;
#endif
        
        int _progressPct = 0;
        String _progressOp;
        String _progressMsg;
        uint32_t _lastLogSend = 0;
        uint32_t _lastClientActivity = 0;
        uint32_t _lastWsConnectionEvent = 0;
        bool _dockOverrideActive = false;

        volatile bool _uploadToMowerPending = false;
        volatile bool _calculateWaypointsPending = false;
        volatile bool _calculateWaypointsRunning = false;
        uint32_t _calculateWaypointsTimestamp = 0;
        ArduMower::Domain::Robot::MowerMap _calculateWaypointsMap;
        ArduMower::Domain::Robot::MowSettings _calculateWaypointsSettings;

  #ifdef MOWER_TERMINAL
  Terminal &_terminal;
  #endif
        AsyncWebServer &_server;
        ArduMower::Domain::Robot::StateSource &_source;
        ArduMower::Domain::Robot::CommandExecutor &_cmd;
        Ota::MowerUpdater &_mowerUpdater;
        ArduMower::Modem::Schedule::Manager &_scheduleManager;

        void handleData(uint32_t clientId, char *data);
        std::map<uint32_t, UiSocketItem*> itemMap;
        MapChunkSendState mapChunkSendState;
        std::map<uint32_t, String> _frameBuffer;
        ArduMower::Domain::Robot::DrivenTrack _track;
        
        void versionRequestLoop();
        void stateRequestLoop();
        void sensorRequestLoop();
#if defined(ENABLE_LIVE_MAP) || defined(ENABLE_GPS_DASHBOARD)
        void gpsRequestLoop();
        void ubxLoop();
        void ubxPollLoop();
#endif
        template<typename T>
        void sendData(ResponseDataType dataType, UiSocketItem *sendTo, T data, bool force = false);
        bool sendTextAllWithRetry(const String& text);
        bool clientCanSend(uint32_t clientId);
        size_t countConnectedClients();
        void pingClients();
#ifdef MOWER_TERMINAL
        void sendTerminalLine(String line);
#endif
        void uploadStatusHandler(byte progress); 
      };
    }
  }
}
