#pragma once

#include <stddef.h>

namespace ArduMower
{
  namespace Modem
  {
    namespace Http
    {
      /**
       * Entpackt Transfer-Encoding: chunked.
       *
       * HTTPClient::getStream() liefert den rohen Socket – die Chunk-Längen
       * stehen mit drin. Ein JSON-Parser liest die führende Hex-Zeile als Zahl,
       * verwirft sie am Filter und meldet ein leeres Dokument ohne Fehler.
       *
       * Die Quelle liefert readRaw(); dadurch ist die Zustandsmaschine ohne
       * Arduino-Stream testbar.
       */
      class ChunkedReader
      {
      public:
        virtual ~ChunkedReader() {}

        // Nächstes Nutzbyte oder -1, wenn der letzte Chunk gelesen ist.
        int read()
        {
          if (_remaining == 0 && !nextChunk()) return -1;
          const int c = readRaw();
          if (c < 0)
          {
            _done = true;
            return -1;
          }
          _remaining--;
          return c;
        }

        bool finished() const { return _done && _remaining == 0; }

      protected:
        // Ein Byte aus der Quelle, -1 wenn keins mehr kommt.
        virtual int readRaw() = 0;

      private:
        size_t _remaining = 0;
        bool _done = false;
        bool _first = true;

        bool nextChunk()
        {
          if (_done) return false;

          // Jeder Chunk endet mit CRLF, bevor die nächste Längenzeile kommt.
          if (!_first && (!skipByte() || !skipByte())) return fail();
          _first = false;

          size_t size = 0;
          bool anyDigit = false;
          for (;;)
          {
            const int c = readRaw();
            if (c < 0) return fail();

            if (c == '\r')
            {
              if (readRaw() < 0) return fail();  // \n
              break;
            }
            // Chunk-Extensions (";name=value") interessieren uns nicht.
            if (c == ';')
            {
              int skip;
              while ((skip = readRaw()) >= 0 && skip != '\n') {}
              if (skip < 0) return fail();
              break;
            }

            int digit;
            if (c >= '0' && c <= '9') digit = c - '0';
            else if (c >= 'a' && c <= 'f') digit = c - 'a' + 10;
            else if (c >= 'A' && c <= 'F') digit = c - 'A' + 10;
            else return fail();

            size = size * 16 + (size_t)digit;
            anyDigit = true;
          }

          // Länge 0 schliesst den Body ab.
          if (!anyDigit || size == 0) return fail();

          _remaining = size;
          return true;
        }

        bool skipByte() { return readRaw() >= 0; }
        bool fail() { _done = true; return false; }
      };
    }
  }
}
