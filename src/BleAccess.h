#pragma once
#include <Arduino.h>

// ESPConfig uses its own service; this separate service transfers internal files.
static constexpr const char* FileServiceUuid = "fb1e5000-54ae-4a28-9f74-dfccb248601d";
static constexpr const char* FileCommandUuid = "fb1e5001-54ae-4a28-9f74-dfccb248601d";
static constexpr const char* FileResponseUuid = "fb1e5002-54ae-4a28-9f74-dfccb248601d";
static constexpr const char* FileDataUuid = "fb1e5003-54ae-4a28-9f74-dfccb248601d";
extern bool bleActive;
extern String bleMessage;
bool startBle();
void stopBle();
void pollBle();
String bleName();
String blePasscode();
bool bleConnected();
