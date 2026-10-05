#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <FS.h>

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

    inline bool writeJsonAtomic(fs::FS &fs, const String &path, const JsonDocument &doc)
    {
      const String tmp = atomicTmpPath(path);
      File file = fs.open(tmp, FILE_WRITE);
      if (!file)
        return false;
      const size_t expected = measureJson(doc);
      const size_t written = serializeJson(doc, file);
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
