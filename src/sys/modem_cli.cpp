#include "modem_cli.h"
#include "settings.h"

void ArduMower::Modem::Cli::drainRx(const char* line, bool &stop)
{
  stop = true;
  if (strcmp(line, "AT") == 0)
    router.sendWithoutResponse("OK");
  else if (strcmp(line, "AT+VERSION") == 0)
    respondVersion();
  else if (strncmp(line, "AT+NAME=", 8) == 0)
    echoResponse(line);
  else if (strncmp(line, "AT+WIFI=", 8) == 0)
    echoResponse(line);
  else if (strcmp(line, "AT+RESET") == 0)
    echoResponse(line);
  else if (strcmp(line, "AT+TEST") == 0)
    echoResponse(line);
  else
    stop = false;
}

void ArduMower::Modem::Cli::respondVersion()
{
  char response[128];
  snprintf(response, sizeof(response), "+VERSION=%s", ArduMower::Modem::Settings::Properties.version());
  router.sendWithoutResponse(response);
}

void ArduMower::Modem::Cli::echoResponse(const char* req)
{
  router.sendWithoutResponse(req + 2);
}
