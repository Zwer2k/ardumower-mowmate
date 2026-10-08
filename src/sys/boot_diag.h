#pragma once

#include <cstdint>

namespace ArduMower
{
  namespace Modem
  {
    // Hält Diagnosedaten über einen Neustart hinweg (RTC-RAM ohne Init, übersteht
    // Software-Reset, Panic, Watchdog und Brownout, aber keinen Power-On-Reset).
    // So lässt sich nach einem Absturz im UI-Log erkennen, warum und in welcher
    // Phase der ESP neu gestartet ist, ohne dass ein serieller Monitor hing.
    namespace BootDiag
    {
      // Beim Start einmal aufrufen: liest Reset-Grund und gemerkte Daten aus,
      // loggt sie und setzt den Speicher für den nächsten Lauf zurück.
      void begin();

      // Grund eines gewollten ESP.restart() merken (z. B. Heap-Guard).
      void setRestartReason(const char *reason);

      // Phase einer langen/riskanten Operation merken; nullptr löscht sie.
      // Wird die Phase beim nächsten Start noch gefunden, ist der ESP während
      // dieser Operation abgestürzt.
      void setPhase(const char *phase);

      // Letztes bemerkenswertes Ereignis merken (z. B. Ergebnis der letzten
      // Wegpunkt-Berechnung), um es mit einem späteren Neustart zu verknüpfen.
      void setLastEvent(const char *event);

      // Zusammenfassung des letzten Resets (Grund, Neustart-Ursache, Phase).
      const char *lastResetSummary();
      // Nur der Reset-Grund (z. B. "software", "panic"), kurz genug für den
      // Heartbeat, dessen Logzeile auf 128 Zeichen begrenzt ist.
      const char *lastResetReason();
    }
  }
}
