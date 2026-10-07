#include <Arduino.h>
#include <esp_attr.h>
#include <esp_system.h>

#include "boot_diag.h"
#include "log.h"

#define _LOG_ "BootDiag::"

namespace
{
  constexpr uint32_t MAGIC = 0xB007D1A6;

  struct Persisted
  {
    uint32_t magic;
    char restartReason[64];
    char phase[96];
    char lastEvent[128];
  };

  RTC_NOINIT_ATTR Persisted persisted;
  char summary[360] = "unknown";

  const char *resetReasonName(esp_reset_reason_t r)
  {
    switch (r)
    {
    case ESP_RST_POWERON:   return "poweron";
    case ESP_RST_EXT:       return "external";
    case ESP_RST_SW:        return "software";
    case ESP_RST_PANIC:     return "panic";
    case ESP_RST_INT_WDT:   return "interrupt-wdt";
    case ESP_RST_TASK_WDT:  return "task-wdt";
    case ESP_RST_WDT:       return "other-wdt";
    case ESP_RST_DEEPSLEEP: return "deepsleep";
    case ESP_RST_BROWNOUT:  return "brownout";
    case ESP_RST_SDIO:      return "sdio";
    default:                return "unknown";
    }
  }

  void copy(char *dst, size_t size, const char *src)
  {
    if (!src) { dst[0] = '\0'; return; }
    strncpy(dst, src, size - 1);
    dst[size - 1] = '\0';
  }
}

namespace ArduMower
{
  namespace Modem
  {
    namespace BootDiag
    {
      void begin()
      {
        const esp_reset_reason_t reason = esp_reset_reason();
        const bool valid = persisted.magic == MAGIC && reason != ESP_RST_POWERON;
        if (valid)
        {
          persisted.restartReason[sizeof(persisted.restartReason) - 1] = '\0';
          persisted.phase[sizeof(persisted.phase) - 1] = '\0';
          persisted.lastEvent[sizeof(persisted.lastEvent) - 1] = '\0';
        }
        const char *restartReason = valid && persisted.restartReason[0] ? persisted.restartReason : "-";
        const char *phase = valid && persisted.phase[0] ? persisted.phase : "-";
        const char *lastEvent = valid && persisted.lastEvent[0] ? persisted.lastEvent : "-";

        snprintf(summary, sizeof(summary), "%s restart=%s phase=%s last=%s",
                 resetReasonName(reason), restartReason, phase, lastEvent);

        const bool crashed = reason == ESP_RST_PANIC || reason == ESP_RST_INT_WDT ||
                             reason == ESP_RST_TASK_WDT || reason == ESP_RST_WDT ||
                             reason == ESP_RST_BROWNOUT || (valid && persisted.phase[0]);
        // Eine Log-Zeile fasst nur 128 Zeichen (LogLine), daher je Teil eine Zeile.
        // Log ist ein if-Makro: Klammern verhindern ein falsch gebundenes else.
        const LogLevel level = crashed ? ERR : INFO;
        {
          Log(level, "%slast reset: %s", _LOG_, resetReasonName(reason));
        }
        if (restartReason[0] != '-')
        {
          Log(level, "%srestart: %s", _LOG_, restartReason);
        }
        if (phase[0] != '-')
        {
          Log(level, "%sphase: %s", _LOG_, phase);
        }
        if (lastEvent[0] != '-')
        {
          Log(level, "%slast: %s", _LOG_, lastEvent);
        }

        persisted.magic = MAGIC;
        persisted.restartReason[0] = '\0';
        persisted.phase[0] = '\0';
        persisted.lastEvent[0] = '\0';
      }

      void setRestartReason(const char *reason)
      {
        persisted.magic = MAGIC;
        copy(persisted.restartReason, sizeof(persisted.restartReason), reason);
      }

      void setPhase(const char *phase)
      {
        persisted.magic = MAGIC;
        copy(persisted.phase, sizeof(persisted.phase), phase);
      }

      void setLastEvent(const char *event)
      {
        persisted.magic = MAGIC;
        copy(persisted.lastEvent, sizeof(persisted.lastEvent), event);
      }

      const char *lastResetSummary()
      {
        return summary;
      }
    }
  }
}
