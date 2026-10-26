#pragma once
#include <cstdint>

struct BatteryStatus {
  int percent = -1;
  uint32_t millivolts = 0;
  bool charging = false;
  bool usbPowered = false;
};

// Same approximate voltage-to-level mapping used by M5Unified's ADC-powered
// boards. The PaperS3 has no fuel gauge. Implausible readings remain unknown.
inline int estimateBatteryPercent(uint32_t millivolts) {
  if (millivolts < 2500 || millivolts > 4500) return -1;
  const int level = (int(millivolts) - 3300) / 8;
  return level < 0 ? 0 : (level > 100 ? 100 : level);
}

void beginBattery();
bool pollBatteryStatus(bool force = false);
const BatteryStatus& batteryStatus();
