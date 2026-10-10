#pragma once

#ifdef MOWER_TERMINAL
#include "terminal.h"
#endif
#include "stm32ota/stm32ota.h"
#include <list>
#include <FS.h>
#include <ArduinoJson.h>

namespace ArduMower
{
  namespace Modem
  {
    namespace Ota
    {
      class StatusMessage
      {
      public:
        uint32_t timestamp;
        byte progress;

        StatusMessage(byte progress) : timestamp(millis()), progress(progress){};
  void marshal(const ArduinoJson::JsonObject &o) const;
      };

      using UpdateComplete = std::function<void(String updateResult)>;
      using StatusHandler = std::function<void(byte progress)>;
      using IdleCallback = std::function<void(void)>;

      class MowerUpdater
      {
      public:
  #ifdef MOWER_TERMINAL
  MowerUpdater(Terminal &terminal, HardwareSerial &mowerFirmwareSerial);
  #else
  MowerUpdater(HardwareSerial &mowerFirmwareSerial);
  #endif

        void startUpdate(String filename, UpdateComplete updateComplete);
        // Flasht direkt aus dem Speicher (PSRAM), ohne Umweg über SPIFFS.
        // Übernimmt buffer und gibt ihn nach dem Flashen mit free() frei.
        void startUpdate(uint8_t *buffer, size_t length, UpdateComplete updateComplete);
        String handleFlash();
        void addStatusHandler(StatusHandler handler);
        void addIdleCallback(IdleCallback cb);
        void loop();
        static bool isFlashing() { return _isFlashing; }

      private:
        #ifdef MOWER_TERMINAL
        Terminal &_terminal;
        #endif
        HardwareSerial &_serial;
        String _filename;
        FirmwareWriterSTM32 firmwareWriter;
        File fsUploadFile;
        uint8_t *_buffer = nullptr;
        size_t _bufferLength = 0;
        size_t _bufferPos = 0;
        bool _pending = false;

        bool _serialPortReady = false;
        bool _fileUploaded = false;
        UpdateComplete _updateComplete;
        StatusHandler _statusHandler;
        std::list<IdleCallback> _idleCallbacks;

        byte _progress = 255;
        static bool _isFlashing;

        void runIdleCallbacks();
        void updateStatus(byte progress);
        void setSerialPortReady(bool serialPortReady);
        void printBootloaderInfo();
  #ifdef MOWER_TERMINAL
  void resumeTerminal() { _terminal.resume(); }
  #endif
      };
    }
  }
}
