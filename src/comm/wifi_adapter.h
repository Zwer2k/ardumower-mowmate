#pragma once

#include <Arduino.h>
#include "settings.h"
#include "schedule.h"

namespace ArduMower
{
  namespace Modem
  {
    namespace Wifi
    {
      enum Mode
      {
        OFF = 0,
        STA = 1,
        AP = 2
      };

      class Adapter
      {
      private:
        Settings::Settings &_settings;
        ArduMower::Modem::Schedule::Manager &_schedule;
        Mode _mode;
        uint32_t _staTimeoutStart;
        uint32_t _offTimeoutStart;
        bool _ntpSynced = false;
        bool _ntpAttempted = false;
        uint32_t _ntpLastAttempt = 0;
        String _ntpServer1;
        String _ntpServer2;

        Mode mode();
        void loopOff();
        void loopSta();
        void loopAp();

        void beginOff();
        void beginSta();
        void beginAp();
        void stop();
        void trySyncTime();
        bool checkNtpDone();

        void switchToOff();

      public:
        Adapter(Settings::Settings &settings, ArduMower::Modem::Schedule::Manager &schedule) : _settings(settings), _schedule(schedule), _mode(Mode::OFF), _staTimeoutStart(0), _offTimeoutStart(0) {}

        void begin();
        void loop();
        void reconnect();
        void fullReconnect();
        bool ntpSynced() const { return _ntpSynced; }
      };
    }
  }
}
