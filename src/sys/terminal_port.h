#pragma once

#ifdef MOWER_TERMINAL

#include <Arduino.h>
#include "settings.h"

namespace ArduMower
{
  namespace Modem
  {
    class Terminal;

    // Liegt zwischen Serial1 und dem Terminal: reicht alle Bytes durch und
    // zählt dabei, wie viele davon lesbarer Text sind. Bei falscher Baudrate
    // kommt oft keine vollständige Zeile an, die Rohbytes zeigen es trotzdem.
    class TerminalStream : public Stream
    {
    public:
      explicit TerminalStream(HardwareSerial &serial) : _serial(serial) {}

      int available() override { return _serial.available(); }
      int read() override;
      int peek() override { return _serial.peek(); }
      void flush() override { _serial.flush(); }
      size_t write(uint8_t b) override { return _serial.write(b); }
      size_t write(const uint8_t *buffer, size_t size) override { return _serial.write(buffer, size); }

      uint32_t windowBytes = 0;
      uint32_t windowText = 0;
      void resetWindow() { windowBytes = windowText = 0; }

    private:
      HardwareSerial &_serial;
    };

    // Baudrate und Datenformat des Terminal-Ports. Bei "Auto" (baud 0) läuft
    // der Port zunächst mit 115200; kommt dort unlesbarer Text an, werden die
    // üblichen Baudraten nacheinander kurz abgehört und die erste übernommen,
    // bei der lesbarer Text ankommt.
    class TerminalPort
    {
    public:
      TerminalPort(HardwareSerial &serial, Settings::TerminalSerial &settings);

      Stream &stream() { return _stream; }
      // enabled = false: Terminal nicht verdrahtet, der Port bleibt aus
      void begin(Terminal &terminal, bool enabled = true);
      bool enabled() const { return _enabled; }
      void loop();
      // Nach dem Flashen der Mäher-Firmware: der STM32-Bootloader stellt den
      // Port auf 115200 8E1/8N1 um.
      void apply();

      uint32_t baud() const { return _baud; }
      bool autoMode() const { return _settings.baud == 0; }
      // "fixed", "auto" (übernommen), "unconfirmed" (noch kein Text gesehen),
      // "detecting", "failed" (kein lesbarer Text bei allen Baudraten)
      const char *state() const;

    private:
      enum class State { Fixed, Unconfirmed, Confirmed, Scanning, Failed };

      HardwareSerial &_serial;
      Settings::TerminalSerial &_settings;
      TerminalStream _stream;
      Terminal *_terminal = nullptr;
      bool _enabled = true;
      State _state = State::Fixed;
      uint32_t _baud = 115200;
      uint32_t _previousBaud = 115200;

      size_t _candidate = 0;
      int _round = 0;
      uint32_t _sampleStart = 0;
      uint32_t _sampleBytes = 0;
      uint32_t _sampleText = 0;
      uint32_t _lastScanEnd = 0;
      bool _scanSawData = false;

      void startScan();
      void abortScan();
      void startCandidate();
      void sampleCandidate();
      void finishScan(bool found);
    };

    extern TerminalPort *terminalPortInstance;
  }
}

#endif
