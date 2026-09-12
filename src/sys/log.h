#pragma once

enum LogLevel {
  COMM = 32,
  DBG  = 16,
  INFO = 8,
  WARN = 4,
  ERR  = 2,
  CRIT = 1
};

#ifndef LOG_H
#define LOG_H 

#include <Arduino.h>
#include "logToUi.h"

#ifndef LOG_CON
#define LOG_CON logToUi
#endif 

#ifndef LOG_CON
#define LOG_CON Serial
#endif


#if LOG_CON == logToUi
  #define DEBUG_LEVEL logToUi.modemLogLevel
#else
  #ifndef DEBUG_LEVEL
  #ifdef ESP_MODEM_SIM
  #define DEBUG_LEVEL logLevel
  #elif ESP_MODEM_TEST
  #define DEBUG_LEVEL DBG
  #else
  #define DEBUG_LEVEL INFO
  #endif
  #endif
#endif

inline int normalizedLogMask(int configured)
{
  switch (configured)
  {
    case DBG:  return DBG | INFO | WARN | ERR | CRIT;
    case INFO: return INFO | WARN | ERR | CRIT;
    case WARN: return WARN | ERR | CRIT;
    case ERR:  return ERR | CRIT;
    default:   return configured;
  }
}

inline bool logLevelEnabled(int configured, LogLevel message)
{
  if (message == ERR && configured != COMM) return true;
  return (normalizedLogMask(configured) & message) == message;
}

#define DEBUG(...)  LOG_CON.printf(__VA_ARGS__)
#if LOG_CON == logToUi
  #define Log(X, ...) if(logLevelEnabled(DEBUG_LEVEL, X)) \
                                    { \
                                      LOG_CON.log(X, __VA_ARGS__); \
                                    }
#else
  #define Log(X, ...) if(logLevelEnabled(DEBUG_LEVEL, X)) \
                                    { \
                                      DEBUG("%.3f %d ", millis() / 1000.0, X); \
                                      DEBUG(__VA_ARGS__); \
                                      DEBUG("%s", "\r\n"); \
                                    }
#endif

#endif // LOG_H
