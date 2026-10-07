#pragma once

#include <Arduino.h>
#include <esp_heap_caps.h>
#include <sdkconfig.h>

// Verteilung von malloc()/new zwischen internem RAM und PSRAM.
//
// Das Arduino-Framework legt erst Blöcke ab 4 KB ins PSRAM
// (CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL). Alles darunter – Strings, JSON-Dokumente,
// WebSocket-Nachrichten, die vielen kleinen Vektoren des Pfadplaners – belegt den
// knappen internen RAM, während die 8 MB PSRAM ungenutzt bleiben. WiFi-Treiber,
// FreeRTOS-Objekte und Task-Stacks fordern internen RAM explizit an und sind von
// der Grenze nicht betroffen.
namespace ArduMower
{
  namespace Modem
  {
    namespace MemPolicy
    {
      // Blöcke ab dieser Größe gehen im Normalbetrieb ins PSRAM.
      static const size_t EXTMEM_THRESHOLD = 512;
      // Während der Wegpunkt-Berechnung geht praktisch alles ins PSRAM.
      static const size_t EXTMEM_THRESHOLD_HEAVY = 16;

      inline bool hasPsram()
      {
        return psramFound();
      }

      inline void setThreshold(size_t limit)
      {
#if CONFIG_SPIRAM_USE_MALLOC
        if (hasPsram()) heap_caps_malloc_extmem_enable(limit);
#else
        (void)limit;
#endif
      }

      inline void begin()
      {
        setThreshold(EXTMEM_THRESHOLD);
      }

      // Für die Dauer des Scopes landen auch kleine Blöcke im PSRAM.
      class HeavyAllocScope
      {
      public:
        HeavyAllocScope() { setThreshold(EXTMEM_THRESHOLD_HEAVY); }
        ~HeavyAllocScope() { setThreshold(EXTMEM_THRESHOLD); }
        HeavyAllocScope(const HeavyAllocScope &) = delete;
        HeavyAllocScope &operator=(const HeavyAllocScope &) = delete;
      };

      // Prüft, ob ein Puffer von len Bytes (mit Kopie, daher 2x) Platz hat und
      // danach noch headroom Bytes interner RAM für TCP-Stack und Frame-Header
      // übrig bleiben. Große Puffer liegen im PSRAM und werden dort geprüft.
      inline bool bufferFits(size_t len, size_t headroom)
      {
        const size_t internalFree = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        if (hasPsram() && len >= EXTMEM_THRESHOLD)
          return internalFree >= headroom &&
                 heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM) >= len * 2;
        return internalFree >= len * 2 + headroom;
      }
    }
  }
}
