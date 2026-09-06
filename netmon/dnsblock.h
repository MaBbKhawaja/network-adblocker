#pragma once
#include <Arduino.h>
// DNS sinkhole ("ad blocker"). Call dnsblockBegin() once Wi-Fi and time are up.
void   dnsblockBegin();
String dnsblockJson();                  // stats for /api/dns
void   dnsblockPause(uint32_t minutes); // temporarily forward everything
void   dnsblockResume();
void   dnsblockRequestUpdate();         // refetch the blocklists now
uint32_t dnsblockPausedSeconds();       // seconds of pause left, 0 = blocking
int dnsblockYtWatchers(uint32_t* ips, uint32_t* agoS, uint32_t* extAgoS, int max, uint32_t withinS); // clients that opened YouTube in a browser recently (+ seconds since their extension checked in, 0xFFFFFFFF = never)
void     dnsblockNoteExtension(uint32_t ip);   // the extension checked in from this address
void     dnsblockSetYtEnforce(bool on);        // "require the extension for YouTube" switch
bool     dnsblockYtEnforce();
uint32_t dnsblockYtEnforced();                 // lookups held back by the switch, since boot
// Runtime settings, edited on the dashboard and saved in the board's flash (merged with dnsconfig.h)
bool   dnsblockListAdd(const char* which, const char* value, String& err);   // which: allow | block | exempt
bool   dnsblockListRemove(const char* which, const char* value);
String dnsblockListsJson();
bool   dnsblockSetName(uint32_t ip, const char* name);                       // "" removes the name
void   dnsblockPauseDevice(uint32_t ip, uint32_t minutes);                   // 0 = resume this device
void dnsblockSetEnabled(bool on);              // per-board switch: off = forward everything, block nothing
bool dnsblockEnabled();
