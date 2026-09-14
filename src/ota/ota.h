#pragma once

#include <stddef.h>
#include <stdint.h>

namespace ArduMower
{
  namespace Modem
  {
    namespace Ota
    {
      // Platz für "v123.456.789" plus Terminator – Release-Tags sind kurz.
      static const size_t FIRMWARE_VERSION_LEN = 16;
      // So viele installierbare Releases merkt sich das Modem für die Auswahl.
      static const size_t FIRMWARE_VERSION_SLOTS = 10;

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
        const char *target;     // Firmware-Variante dieses Boards
        // Installierbare Releases, neuste zuerst. Nur stabile vX.Y.Z-Tags, für
        // die es auch ein Asset für dieses Board gibt.
        const char (*versions)[FIRMWARE_VERSION_LEN];
        uint8_t versionCount;
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
