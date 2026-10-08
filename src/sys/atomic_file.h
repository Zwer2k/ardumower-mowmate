#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <FS.h>
#include <functional>

// Replace a JSON file so that a power loss at any point leaves either the old
// or the new content behind, never a truncated file. The mower runs on a
// battery; an empty battery during a save used to corrupt the map index (all
// maps vanish) or the settings (back to defaults, WiFi credentials lost).
//
// The new content goes to "<path>.tmp" first and is only swapped in once it
// was written completely. SPIFFS cannot rename onto an existing file, so the
// old file is removed right before the rename; recoverAtomicFile() completes a
// swap that was interrupted in between.
namespace ArduMower
{
  namespace Util
  {
    inline String atomicTmpPath(const String &path) { return path + ".tmp"; }

    // Fortschritt beim Schreiben: (geschrieben, gesamt) in Bytes.
    using WriteProgress = std::function<void(size_t written, size_t total)>;

    // Leitet an die Datei weiter und meldet alle 4 KB den Fortschritt.
    class ProgressPrint : public Print
    {
    public:
      ProgressPrint(Print &out, size_t total, const WriteProgress &progress)
          : _out(out), _total(total), _progress(progress) {}
      size_t write(uint8_t c) override { return count(_out.write(c)); }
      size_t write(const uint8_t *buffer, size_t size) override { return count(_out.write(buffer, size)); }

    private:
      size_t count(size_t n)
      {
        _written += n;
        if (_progress && _written - _reported >= 4096)
        {
          _reported = _written;
          _progress(_written, _total);
        }
        return n;
      }
      Print &_out;
      size_t _total;
      const WriteProgress &_progress;
      size_t _written = 0;
      size_t _reported = 0;
    };

    inline bool writeJsonAtomic(fs::FS &fs, const String &path, const JsonDocument &doc,
                                const WriteProgress &progress = {})
    {
      const String tmp = atomicTmpPath(path);
      File file = fs.open(tmp, FILE_WRITE);
      if (!file)
        return false;
      const size_t expected = measureJson(doc);
      ProgressPrint out(file, expected, progress);
      const size_t written = serializeJson(doc, out);
      file.close();
      // A full filesystem truncates the write without any other error.
      if (written != expected)
      {
        fs.remove(tmp);
        return false;
      }
      if (fs.exists(path) && !fs.remove(path))
      {
        fs.remove(tmp);
        return false;
      }
      return fs.rename(tmp, path);
    }

    // Call before reading path.
    inline void recoverAtomicFile(fs::FS &fs, const String &path)
    {
      const String tmp = atomicTmpPath(path);
      if (!fs.exists(tmp))
        return;
      if (fs.exists(path))
        fs.remove(tmp); // the swap never started; tmp may be partial
      else
        fs.rename(tmp, path); // old file already gone; tmp was verified before that
    }
  }
}
