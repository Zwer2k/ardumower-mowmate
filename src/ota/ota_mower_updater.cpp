
#include "ota_mower_updater.h"
#include "log.h"
#include <SPIFFS.h>

using namespace ArduMower::Modem::Ota;

bool MowerUpdater::_isFlashing = false;

void MowerUpdater::addIdleCallback(IdleCallback cb)
{
  _idleCallbacks.push_back(cb);
}

void MowerUpdater::runIdleCallbacks()
{
  for (auto &cb : _idleCallbacks)
    cb();
}

void StatusMessage::marshal(const ArduinoJson::JsonObject &o) const
{
  o["progress"] = progress;
  
  // More detailed status based on progress ranges
  if (progress >= 100) {
    o["status"] = "success";
  } else if (progress >= 95) {
    o["status"] = "finishing";
  } else if (progress >= 10) {
    o["status"] = "flashing";
  } else if (progress >= 5) {
    o["status"] = "erasing";
  } else if (progress > 0) {
    o["status"] = "initializing";
  } else {
    o["status"] = "starting";
  }
}


#ifdef MOWER_TERMINAL
MowerUpdater::MowerUpdater(Terminal &terminal, HardwareSerial &mowerFirmwareSerial) : 
  _terminal(terminal), _serial(mowerFirmwareSerial), 
  firmwareWriter(FirmwareWriterSTM32(mowerFirmwareSerial))
{
}
#else
MowerUpdater::MowerUpdater(HardwareSerial &mowerFirmwareSerial) : 
  _serial(mowerFirmwareSerial), 
  firmwareWriter(FirmwareWriterSTM32(mowerFirmwareSerial))
{
}
#endif

void MowerUpdater::startUpdate(String filename, UpdateComplete updateComplete) 
{ 
    if (filename.isEmpty()) {
        Log(ERR, "MowerUpdater::startUpdate called with empty filename");
        updateComplete("error-invalid-filename");
        return;
    }
    
    if (!SPIFFS.exists(filename)) {
        Log(ERR, "MowerUpdater::startUpdate file does not exist: %s", filename.c_str());
        updateComplete("error-file-not-found");
        return;
    }
    
    if (_pending) {
        Log(ERR, "MowerUpdater::startUpdate update already pending");
        updateComplete("error-busy");
        return;
    }

    _filename = filename;
    _updateComplete = updateComplete;
    _progress = 0; // Reset progress
    _pending = true;
    
    Log(INFO, "MowerUpdater::startUpdate initiating update for: %s", filename.c_str());
    
#ifdef MOWER_TERMINAL
  _terminal.suspend([this] {
    Log(INFO, "MowerUpdater::startUpdate terminal suspended successfully");
    this->setSerialPortReady(true);
  });
#else
  this->setSerialPortReady(true);
#endif
}

void MowerUpdater::startUpdate(uint8_t *buffer, size_t length, UpdateComplete updateComplete)
{
    if (!buffer || length == 0) {
        if (buffer) free(buffer);
        Log(ERR, "MowerUpdater::startUpdate called with empty buffer");
        updateComplete("error-file-empty");
        return;
    }

    if (_pending) {
        free(buffer);
        Log(ERR, "MowerUpdater::startUpdate update already pending");
        updateComplete("error-busy");
        return;
    }

    _filename = "";
    _buffer = buffer;
    _bufferLength = length;
    _bufferPos = 0;
    _updateComplete = updateComplete;
    _progress = 0;
    _pending = true;

    Log(INFO, "MowerUpdater::startUpdate initiating update from memory (%u bytes)", (unsigned)length);

#ifdef MOWER_TERMINAL
  _terminal.suspend([this] {
    Log(INFO, "MowerUpdater::startUpdate terminal suspended successfully");
    this->setSerialPortReady(true);
  });
#else
  this->setSerialPortReady(true);
#endif
}

void MowerUpdater::addStatusHandler(StatusHandler handler)
{
  _statusHandler = handler;
}

void MowerUpdater::updateStatus(byte progress)
{
  // Ensure progress is within valid range
  if (progress > 100) progress = 100;
  
  // Always update on 0 and 100, or if progress increased by at least 5%
  if (((progress == 0 || progress == 100) && (progress != _progress)) || 
      (progress > _progress + 5)) 
  {
    _progress = progress;
    Log(INFO, "MowerUpdater progress: %d%%", progress);
    
    if (_statusHandler != NULL) {
      _statusHandler(_progress);
    }
  }
}

void MowerUpdater::loop()
{
  if (_serialPortReady && _pending) 
  {
    String result = handleFlash();
    _serialPortReady = false;
    _pending = false;
    _filename = "";
    if (_buffer) {
      free(_buffer);
      _buffer = nullptr;
    }
    _bufferLength = 0;
    _bufferPos = 0;
#ifdef MOWER_TERMINAL
  _terminal.resume();
#endif
    _updateComplete(result);
  }
}

void MowerUpdater::setSerialPortReady(bool serialPortReady) 
{ 
    Log(DBG, "MowerUpdater::setSerialPortReady");
    _serialPortReady = serialPortReady;
}

void MowerUpdater::printBootloaderInfo()
{
  char blversion = firmwareWriter.version();
  uint8_t majorVersion = (blversion >> 4) & 0x0F;
  uint8_t minorVersion = blversion & 0x0F;
  String mcuId = firmwareWriter.getId();
  
  Log(INFO, "Bootloader Info - Version: %d.%d, MCU: %s, Target File: %s", 
      majorVersion, minorVersion, mcuId.c_str(), _filename.c_str());
}

String MowerUpdater::handleFlash()
{
  Log(INFO, "MowerUpdater::handleFlash starting firmware update: %s", _filename.c_str());

  // Constants for better maintainability
  static const size_t BLOCK_SIZE = 256;
  static const byte PROGRESS_ERASE = 5;
  static const byte PROGRESS_FLASH_START = 10;
  static const byte PROGRESS_FLASH_END = 95;
  
  uint8_t binread[BLOCK_SIZE];
  const bool fromMemory = _buffer != nullptr;
  if (!fromMemory) {
    fsUploadFile = SPIFFS.open(_filename, "r");
    if (!fsUploadFile) {
      Log(ERR, "MowerUpdater::handleFlash failed to open file: %s", _filename.c_str());
      return "error-file-open";
    }
  }
  const size_t sourceSize = fromMemory ? _bufferLength : fsUploadFile.size();

  // Liest aus dem Speicher oder aus der SPIFFS-Datei
  auto readSource = [&](uint8_t *dst, size_t n) -> size_t {
    if (!fromMemory) return fsUploadFile.read(dst, n);
    if (_bufferPos + n > _bufferLength) n = _bufferLength - _bufferPos;
    memcpy(dst, _buffer + _bufferPos, n);
    _bufferPos += n;
    return n;
  };

  // RAII-style cleanup helper
  auto cleanup = [this]() {
    _isFlashing = false;
    if (fsUploadFile) {
      fsUploadFile.close();
    }
    firmwareWriter.switchToRunMode();
  };

  Log(INFO, "MowerUpdater::handleFlash source ready (%s), size: %u bytes",
      fromMemory ? "memory" : "SPIFFS", (unsigned)sourceSize);

  if (sourceSize == 0) {
    cleanup();
    return "error-file-empty";
  }

  {
    auto bini = sourceSize / BLOCK_SIZE;
    auto lastbuf = sourceSize % BLOCK_SIZE;
    
    Log(INFO, "MowerUpdater::handleFlash processing %u full blocks + %u remaining bytes", 
        (unsigned)bini, (unsigned)lastbuf);

    updateStatus(0);
    _isFlashing = true;

    if (!firmwareWriter.switchToFlashMode()) {
      cleanup();
      return "error-init";
    }

    // if (!firmwareWriter.checkFlashMode()) {
    //   cleanup();
    //   return "error-connection-check";
    // }

    Log(INFO, "MowerUpdater::handleFlash bootloader ready, erasing flash");
    updateStatus(PROGRESS_ERASE);
    
    if (!firmwareWriter.erase()) {
      cleanup();
      return "error-erase-flash";
    }

    Log(INFO, "MowerUpdater::handleFlash starting firmware flash");
    updateStatus(PROGRESS_FLASH_START);

    uint32_t currentAddress = STM32STADDR;
    uint32_t totalBytes = sourceSize;
    uint32_t bytesWritten = 0;

    // Helper function to write a block with retries
    auto writeBlock = [&](uint8_t* data, size_t size) -> String {
      static const int MAX_RETRIES = 3;
      for (int attempt = 0; attempt < MAX_RETRIES; attempt++) {
        if (attempt > 0) {
          Log(WARN, "Retry block at 0x%x (attempt %d/%d)", currentAddress, attempt + 1, MAX_RETRIES);
          delay(50);
          yield();
        }

        if (!firmwareWriter.sendCommand(STM32WR, 2000)) {
          if (attempt < MAX_RETRIES - 1) continue;
          Log(ERR, "Failed to send write command at address 0x%x", currentAddress);
          return "error-write-command";
        }
        yield();

        if (!firmwareWriter.sendAddress(currentAddress, 1000)) {
          if (attempt < MAX_RETRIES - 1) continue;
          Log(ERR, "Failed to send address 0x%x", currentAddress);
          return "error-write-address";
        }
        yield();

        if (!firmwareWriter.sendDataBlock(data, size, 2000)) {
          if (attempt < MAX_RETRIES - 1) continue;
          Log(ERR, "Failed to send data block at address 0x%x", currentAddress);
          return "error-write-data";
        }

        // All steps succeeded
        break;
      }

      currentAddress += size;
      bytesWritten += size;
      
      // Calculate accurate progress
      byte progress = PROGRESS_FLASH_START + 
                     (bytesWritten * (PROGRESS_FLASH_END - PROGRESS_FLASH_START)) / totalBytes;
      updateStatus(progress);
      
      return "";
    };

    // Process full blocks
    for (uint32_t i = 0; i < bini; i++) {
        unsigned long iterationStartTime = millis();

        size_t bytesRead = readSource(binread, BLOCK_SIZE);
        if (bytesRead != BLOCK_SIZE) {
          Log(ERR, "Failed to read block %u, expected %u bytes, got %u", 
              (unsigned)i, (unsigned)BLOCK_SIZE, (unsigned)bytesRead);
          cleanup();
          return "error-file-read";
        }
        runIdleCallbacks();
        yield();

        String error = writeBlock(binread, BLOCK_SIZE);
        if (!error.isEmpty()) {
          cleanup();
          return error;
        }
        
        runIdleCallbacks();
        yield();
        Log(DBG, "Block %u written in %lu ms", (unsigned)i, millis() - iterationStartTime);
    }

    // Handle the last partial block
    if (lastbuf > 0) {
        size_t bytesRead = readSource(binread, lastbuf);
        if (bytesRead != lastbuf) {
          Log(ERR, "Failed to read last block, expected %u bytes, got %u", 
              (unsigned)lastbuf, (unsigned)bytesRead);
          cleanup();
          return "error-file-read-last";
        }

        String error = writeBlock(binread, lastbuf);
        if (!error.isEmpty()) {
          cleanup();
          return error;
        }
    }

    if (fsUploadFile) fsUploadFile.close();
    Log(INFO, "MowerUpdater::handleFlash firmware written successfully");
    updateStatus(100);
  } // End of file handling scope

  _isFlashing = false;
  firmwareWriter.switchToRunMode();
  Log(INFO, "MowerUpdater::handleFlash update completed successfully");
  return "";
}
