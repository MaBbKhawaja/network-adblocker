#pragma once
#include <Arduino.h>
// DNS sinkhole ("ad blocker"). Call dnsblockBegin() once Wi-Fi and time are up.
void   dnsblockBegin();
String dnsblockJson();                  // stats for /api/dns
void   dnsblockPause(uint32_t minutes); // temporarily forward everything
void   dnsblockResume();
void   dnsblockRequestUpdate();         // refetch the blocklists now
