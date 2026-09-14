#ifndef logToUi_h
#define logToUi_h

#include "log.h"
#include <ArduinoJson.h>
#include "ringbuffer.h"

#define RINGBUFFER_SIZE 100
#define LOG_LINE_MAX 128
// Max. lines per streaming update. Bounds the JSON/WebSocket frame size while
// still allowing bursts to drain: at one update per 100 ms this is 200 lines/s.
#define LOG_LINES_PER_SEND 20

struct LogLine {
    uint32_t nr;
    LogLevel level;
    char     text[LOG_LINE_MAX];
    uint32_t freeHeap;
};

class LogToUi {
    public:
        LogToUi();

        byte modemLogLevel;
        size_t printf(const char *format, ...);
        size_t log(const LogLevel logLevel, const char *format, ...);
        bool hasData();
        void marshal(JsonObject o);
        // Confirm that everything the last marshal() produced has been sent,
        // so those lines are not streamed again. Called by the sender after a
        // successful send; without it the same lines are re-marshalled.
        void commitSent();
        void exportAll(String &csv);
        uint16_t marshalBatch(const JsonObject &o, uint16_t startIdx, uint16_t maxLines);
        uint32_t timestamp = 0;

    private:
        Ringbuffer<LogLine, RINGBUFFER_SIZE> *modemLog = NULL;
        uint32_t modemLineNrIn = 0;
        // Line number of the oldest line not yet streamed to the UI, and the
        // number one past the last line the most recent marshal() produced.
        uint32_t nextSendNr = 0;
        uint32_t marshalledUpToNr = 0;
};

extern LogToUi logToUi;

#endif // logToUi_h