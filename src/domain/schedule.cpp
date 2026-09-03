#include "schedule.h"
#include "log.h"
#include <SPIFFS.h>

#define _LOG_ "Schedule::"

using namespace ArduMower::Modem::Schedule;

namespace {
  const char *_t_enabled = "enabled";
  const char *_t_entries = "entries";
  const char *_t_id = "id";
  const char *_t_name = "name";
  const char *_t_mapId = "mapId";
  const char *_t_mapName = "mapName";
  const char *_t_mode = "mode";
  const char *_t_hour = "hour";
  const char *_t_minute = "minute";
  const char *_t_daysOfWeek = "daysOfWeek";
  const char *_t_daysOfMonth = "daysOfMonth";
}

void Entry::marshal(JsonObject o) const
{
  o[_t_id] = id;
  o[_t_enabled] = enabled;
  o[_t_name] = name;
  o[_t_mapId] = mapId;
  o[_t_mapName] = mapName;
  o[_t_mode] = static_cast<int>(mode);
  o[_t_hour] = hour;
  o[_t_minute] = minute;
  o[_t_daysOfWeek] = daysOfWeek;
  o[_t_daysOfMonth] = daysOfMonth;
}

bool Entry::unmarshal(JsonObject o)
{
  id = o[_t_id] | 0;
  enabled = o[_t_enabled] | true;
  name = o[_t_name] | "";
  mapId = o[_t_mapId] | "";
  mapName = o[_t_mapName] | "";
  int modeInt = o[_t_mode] | 0;
  if (modeInt < 0 || modeInt > 2) modeInt = 0;
  mode = static_cast<Mode>(modeInt);
  hour = o[_t_hour] | 8;
  if (hour > 23) hour = 23;
  minute = o[_t_minute] | 0;
  if (minute > 59) minute = 59;
  daysOfWeek = o[_t_daysOfWeek] | 0x7F;
  daysOfMonth = o[_t_daysOfMonth] | 0x1;
  return true;
}

Manager::Manager(const String &filename) : _filename(filename)
{
}

void Manager::begin()
{
  if (!SPIFFS.begin(true))
  {
    Log(ERR, "%sbegin::spiffs-begin-error", _LOG_);
    return;
  }

  File file = SPIFFS.open(_filename.c_str());
  if (!file || file.isDirectory())
  {
    Log(INFO, "%sbegin::no-existing-file", _LOG_);
    computeNextRun();
    return;
  }

  JsonDocument doc;
  auto err = deserializeJson(doc, file);
  file.close();

  if (err != DeserializationError::Ok)
  {
    Log(ERR, "%sbegin::deserializeJson-error(%s)", _LOG_, err.c_str());
    computeNextRun();
    return;
  }

  if (!unmarshal(doc.as<JsonObject>()))
  {
    Log(ERR, "%sbegin::unmarshal-error", _LOG_);
  }
  else
  {
    Log(INFO, "%sbegin::success entries=%u", _LOG_, (unsigned)_entries.size());
  }

  _dirty = false;
  computeNextRun();
}

bool Manager::save()
{
  if (!SPIFFS.begin(true))
  {
    Log(ERR, "%ssave::spiffs-begin-error", _LOG_);
    return false;
  }

  JsonDocument doc;
  marshal(doc.to<JsonObject>());

  File file = SPIFFS.open(_filename.c_str(), FILE_WRITE);
  if (!file)
  {
    Log(ERR, "%ssave::file-open-error", _LOG_);
    return false;
  }

  serializeJson(doc, file);
  file.close();
  _dirty = false;
  Log(INFO, "%ssave::success entries=%u", _LOG_, (unsigned)_entries.size());
  return true;
}

bool Manager::setConfig(bool enabled, const std::vector<Entry> &entries)
{
  _enabled = enabled;
  _entries = entries;
  _dirty = true;
  return computeNextRun();
}

void Manager::marshal(JsonObject o) const
{
  o[_t_enabled] = _enabled;
  auto arr = o[_t_entries].to<JsonArray>();
  for (const auto &e : _entries)
  {
    auto obj = arr.add<JsonObject>();
    e.marshal(obj);
  }
}

bool Manager::unmarshal(JsonObject o)
{
  _enabled = o[_t_enabled] | false;

  _entries.clear();
  JsonArray arr = o[_t_entries];
  if (arr)
  {
    for (JsonObject obj : arr)
    {
      Entry e;
      if (e.unmarshal(obj))
      {
        _entries.push_back(e);
      }
    }
  }
  return true;
}

time_t Manager::nextOccurrence(const Entry &e, time_t from) const
{
  if (!e.enabled || e.mapId.length() == 0) return 0;

  struct tm localFrom;
  if (!localtime_r(&from, &localFrom)) return 0;

  struct tm candidate = localFrom;
  candidate.tm_hour = e.hour;
  candidate.tm_min = e.minute;
  candidate.tm_sec = 0;
  candidate.tm_isdst = -1;

  // Start with today at scheduled time
  time_t t = mktime(&candidate);
  if (t < from)
  {
    // Scheduled time already passed today -> move to next day
    t += 24 * 3600;
  }

  // Daily is simple
  if (e.mode == Mode::Daily)
  {
    return t;
  }

  // Weekly: find next day where the day-of-week bit is set.
  // localtime_r of t gives tm_wday (0=Sunday).
  if (e.mode == Mode::Weekly)
  {
    for (int i = 0; i < 14; ++i)
    {
      struct tm ti;
      if (!localtime_r(&t, &ti)) return 0;
      uint8_t bit = 1u << ti.tm_wday;
      if ((e.daysOfWeek & bit) && t >= from)
      {
        return t;
      }
      t += 24 * 3600;
    }
    return 0;
  }

  // Monthly: find next day where day-of-month bit is set.
  if (e.mode == Mode::Monthly)
  {
    for (int i = 0; i < 64; ++i)
    {
      struct tm ti;
      if (!localtime_r(&t, &ti)) return 0;
      int dom = ti.tm_mday;
      uint32_t bit = (dom >= 1 && dom <= 31) ? (1u << (dom - 1)) : 0u;
      if ((e.daysOfMonth & bit) && t >= from)
      {
        return t;
      }
      t += 24 * 3600;
    }
    return 0;
  }

  return 0;
}

bool Manager::computeNextRun()
{
  _nextRun = 0;
  _nextRunEntryId = -1;
  _nextRunMapName = "";

  time_t now = time(nullptr);
  if (now < 1609459200)
  {
    Log(DBG, "%scomputeNextRun::time-not-synced", _LOG_);
    return false;
  }

  if (!_enabled || _entries.empty())
  {
    return false;
  }

  time_t best = 0;
  int bestId = -1;
  String bestMapName;

  for (const auto &e : _entries)
  {
    time_t t = nextOccurrence(e, now);
    if (t == 0) continue;
    if (best == 0 || t < best)
    {
      best = t;
      bestId = e.id;
      bestMapName = e.mapName;
    }
  }

  _nextRun = (uint32_t)best;
  _nextRunEntryId = bestId;
  _nextRunMapName = bestMapName;

  if (best > 0)
  {
    struct tm ti;
    localtime_r(&best, &ti);
    Log(INFO, "%scomputeNextRun::next=%04d-%02d-%02d %02d:%02d id=%d map=%s",
        _LOG_, ti.tm_year + 1900, ti.tm_mon + 1, ti.tm_mday,
        ti.tm_hour, ti.tm_min, bestId, bestMapName.c_str());
    return true;
  }

  Log(INFO, "%scomputeNextRun::no-valid-run", _LOG_);
  return false;
}
