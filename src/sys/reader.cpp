#include "reader.h"
#include "log.h"
#include <string.h>

bool isPrintableCharacter(char c);

void Reader::reset()
{
  pos = 0;
  buffer[0] = '\0';
}

void Reader::ensureCapacity(uint16_t needed)
{
  if (pos + needed >= READER_BUF_SIZE - 1)
  {
    pos = 0;
    buffer[0] = '\0';
  }
}

const char* Reader::update(char c)
{
  if (c == 0x7f)
  {
    if (pos > 0)
    {
      pos--;
      buffer[pos] = '\0';
    }
    return nullptr;
  }

  ensureCapacity(2);

  if (c == '\0')
  {
    // Ignore null bytes
  }
  else
  {
    buffer[pos++] = c;
    buffer[pos] = '\0';
  }

  uint16_t eolLen = eol.length();
  if (pos < eolLen) return nullptr;
  if (strncmp(buffer + pos - eolLen, eol.c_str(), eolLen) != 0)
    return nullptr;

  // Complete line found – extract without EOL
  uint16_t resultLen = pos - eolLen;
  buffer[resultLen] = '\0';
  pos = 0;
  return buffer;
}

String Reader::getAndClearBadChars() {
  String tmp = badChars;
  badChars = "";
  return tmp;
}

const char* Reader::peek()
{
  return buffer;
}

bool isPrintableCharacter(char c)
{
  return (c >= 0x20 && c < 0x7f) || c == '\r' || c == '\n';
}
