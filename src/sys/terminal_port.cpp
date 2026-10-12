#include "terminal_port.h"

#ifdef MOWER_TERMINAL

#include "terminal.h"
#include "log.h"

#define _LOG_ "TerminalPort::"

using namespace ArduMower::Modem;

namespace
{
  // Reihenfolge der Suche: der häufigste Wert (Sunray) zuerst
  const uint32_t candidates[] = {115200, 57600, 38400, 19200, 9600, 230400, 460800, 921600};
  const size_t candidateCount = sizeof(candidates) / sizeof(candidates[0]);

  const uint32_t SAMPLE_MS = 300;          // Abhörzeit je Baudrate
  const uint32_t SAMPLE_ENOUGH = 96;       // genug Bytes für ein Urteil
  const uint32_t SAMPLE_MIN = 24;          // weniger: zu wenig Daten
  const uint32_t MONITOR_WINDOW = 64;      // Bytes je Prüffenster im Betrieb
  const uint32_t RESCAN_PAUSE_MS = 30000;  // nach einer erfolglosen Suche
  const int MAX_ROUNDS = 3;                // Suchdurchläufe, wenn Daten kommen

  bool isText(int c)
  {
    return (c >= 0x20 && c <= 0x7e) || c == '\r' || c == '\n' || c == '\t';
  }

  // Anteil lesbarer Zeichen in Prozent
  uint32_t textPercent(uint32_t text, uint32_t bytes)
  {
    return bytes == 0 ? 0 : text * 100 / bytes;
  }
}

TerminalPort *ArduMower::Modem::terminalPortInstance = nullptr;

int TerminalStream::read()
{
  int c = _serial.read();
  if (c >= 0) {
    windowBytes++;
    if (isText(c)) windowText++;
  }
  return c;
}

TerminalPort::TerminalPort(HardwareSerial &serial, Settings::TerminalSerial &settings)
    : _serial(serial), _settings(settings), _stream(serial)
{
  terminalPortInstance = this;
}

void TerminalPort::begin(Terminal &terminal, bool enabled)
{
  _terminal = &terminal;
  _enabled = enabled;
  if (!_enabled) {
    _state = State::Fixed;   // keine Überwachung, keine Suche
    Log(INFO, "%sterminal disabled in mower capabilities, port not started", _LOG_);
    return;
  }
  if (_settings.baud != 0 && Settings::TerminalSerial::validBaud(_settings.baud)) {
    _baud = _settings.baud;
    _state = State::Fixed;
  } else {
    _baud = 115200;
    _state = State::Unconfirmed;
  }
  _serial.begin(_baud, _settings.serialConfig());
  _stream.resetWindow();
  Log(INFO, "%sbegin baud=%u format=%s%s", _LOG_, (unsigned)_baud, _settings.format.c_str(),
      _state == State::Fixed ? "" : " (auto)");
}

void TerminalPort::apply()
{
  if (!_enabled) return;
  // Nach dem Firmware-Upload: eine unterbrochene Suche nicht fortsetzen
  if (_state == State::Scanning) abortScan();
  _serial.begin(_baud, _settings.serialConfig());
  _stream.resetWindow();
}

const char *TerminalPort::state() const
{
  switch (_state) {
    case State::Fixed: return "fixed";
    case State::Unconfirmed: return "unconfirmed";
    case State::Confirmed: return "auto";
    case State::Scanning: return "detecting";
    case State::Failed: return "failed";
  }
  return "fixed";
}

void TerminalPort::loop()
{
  switch (_state) {
    case State::Fixed:
      return;

    case State::Unconfirmed:
    case State::Confirmed:
    case State::Failed: {
      if (_stream.windowBytes < MONITOR_WINDOW) return;
      const uint32_t pct = textPercent(_stream.windowText, _stream.windowBytes);
      _stream.resetWindow();
      if (pct >= 95) {
        if (_state != State::Confirmed) {
          Log(INFO, "%sreadable text at %u baud", _LOG_, (unsigned)_baud);
        }
        _state = State::Confirmed;
      } else if (pct < 80) {
        if (_state == State::Failed && millis() - _lastScanEnd < RESCAN_PAUSE_MS) return;
        Log(INFO, "%sunreadable data at %u baud (%u%% text), detecting baud rate", _LOG_,
            (unsigned)_baud, (unsigned)pct);
        startScan();
      }
      return;
    }

    case State::Scanning:
      // Ein Firmware-Upload hält das Terminal an: Suche sofort abbrechen,
      // der Upload stellt den Port selbst ein
      if (_terminal != nullptr && _terminal->isSuspended()) {
        abortScan();
        return;
      }
      sampleCandidate();
      return;
  }
}

void TerminalPort::startScan()
{
  _previousBaud = _baud;
  _candidate = 0;
  _round = 0;
  _scanSawData = false;
  if (_terminal == nullptr || _terminal->isSuspended()) return;
  // Das Terminal liest während der Suche nicht mit
  _terminal->pauseReading(true);
  _state = State::Scanning;
  startCandidate();
}

void TerminalPort::abortScan()
{
  Log(INFO, "%sbaud rate detection interrupted (mower firmware upload)", _LOG_);
  _baud = _previousBaud;
  _state = State::Unconfirmed;
  _lastScanEnd = millis();
  _stream.resetWindow();
  if (_terminal != nullptr) _terminal->pauseReading(false);
}

void TerminalPort::startCandidate()
{
  _serial.updateBaudRate(candidates[_candidate]);
  while (_serial.available()) _serial.read();   // Bytes der alten Baudrate verwerfen
  _sampleStart = millis();
  _sampleBytes = 0;
  _sampleText = 0;
}

void TerminalPort::sampleCandidate()
{
  while (_serial.available() && _sampleBytes < SAMPLE_ENOUGH) {
    int c = _serial.read();
    _sampleBytes++;
    if (isText(c)) _sampleText++;
  }
  if (_sampleBytes < SAMPLE_ENOUGH && millis() - _sampleStart < SAMPLE_MS) return;

  if (_sampleBytes > 0) _scanSawData = true;
  if (_sampleBytes >= SAMPLE_MIN && textPercent(_sampleText, _sampleBytes) >= 95) {
    _baud = candidates[_candidate];
    finishScan(true);
    return;
  }
  if (++_candidate < candidateCount) {
    startCandidate();
    return;
  }
  // Alle Baudraten durch: nochmal, solange überhaupt Daten kommen
  if (_scanSawData && ++_round < MAX_ROUNDS) {
    _candidate = 0;
    _scanSawData = false;
    startCandidate();
    return;
  }
  _baud = _previousBaud;
  finishScan(false);
}

void TerminalPort::finishScan(bool found)
{
  _serial.updateBaudRate(_baud);
  _stream.resetWindow();
  _lastScanEnd = millis();
  _state = found ? State::Confirmed : State::Failed;
  if (found) {
    Log(INFO, "%sdetected %u baud", _LOG_, (unsigned)_baud);
  } else {
    Log(WARN, "%sno readable text at any baud rate, staying at %u baud", _LOG_, (unsigned)_baud);
  }
  if (_terminal != nullptr) _terminal->pauseReading(false);
}

#endif
