#pragma once

namespace ArduMower
{
  namespace Modem
  {
    namespace Ota
    {
      // Was das Modem selbst über die verfügbare Firmware weiß. Ermittelt der
      // OTA-Server im Hintergrund, verschickt wird es über den UI-WebSocket.
      struct FirmwareStatus
      {
        bool reachable;         // GitHub-API vom Modem aus erreichbar
        bool checking;          // Prüfung läuft gerade, Ergebnis folgt
        bool updateAvailable;   // latest ist neuer als current
        bool checked;           // seit dem Boot mindestens einmal geprüft
        const char *current;    // laufende Version, NULL bei unbekanntem Build
        const char *latest;     // neuste Release-Version, NULL wenn ungeprüft
        const char *error;      // Grund der Nichterreichbarkeit, sonst NULL
      };

      class Ota
      {
      public:
        virtual void begin() = 0;
        virtual void loop() = 0;
        virtual bool active() = 0;
      };
    }
  }
}
