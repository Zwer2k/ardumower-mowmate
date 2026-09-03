#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <vector>
#include <time.h>

namespace ArduMower
{
  namespace Modem
  {
    namespace Schedule
    {
      enum class Mode : uint8_t
      {
        Daily = 0,
        Weekly = 1,
        Monthly = 2
      };

      struct Entry
      {
        uint8_t id = 0;
        bool enabled = true;
        String name;
        String mapId;
        String mapName;
        Mode mode = Mode::Daily;
        uint8_t hour = 8;
        uint8_t minute = 0;
        uint8_t daysOfWeek = 0x7F;   // Bit0=So ... Bit6=Sa (weekly)
        uint32_t daysOfMonth = 0x1;  // Bit0=1. ... Bit30=31. (monthly)

        void marshal(JsonObject o) const;
        bool unmarshal(JsonObject o);
      };

      class Manager
      {
      public:
        Manager(const String &filename = "/schedule.json");

        void begin();
        bool save();

        bool enabled() const { return _enabled; }
        const std::vector<Entry> &entries() const { return _entries; }
        uint32_t nextRun() const { return _nextRun; }
        int nextRunEntryId() const { return _nextRunEntryId; }
        const String &nextRunMapName() const { return _nextRunMapName; }

        // Replaces the complete working copy (RAM). Does NOT write SPIFFS.
        bool setConfig(bool enabled, const std::vector<Entry> &entries);

        bool dirty() const { return _dirty; }
        void clearDirty() { _dirty = false; }

        // Recomputes _nextRun from current time. Call after config change and
        // after a trigger fired. Returns false if no valid future run exists
        // (e.g. NTP not yet synced).
        bool computeNextRun();

        void marshal(JsonObject o) const;

      private:
        String _filename;
        bool _enabled = false;
        std::vector<Entry> _entries;
        uint32_t _nextRun = 0; // epoch seconds, 0 = none
        int _nextRunEntryId = -1;
        String _nextRunMapName;
        bool _dirty = false;

        bool unmarshal(JsonObject o);
        time_t nextOccurrence(const Entry &e, time_t from) const;
      };
    }
  }
}
