#include "Battery.h"
#include <Arduino.h>
#include <cstdlib>

// PaperS3 wiring and polarity match M5Stack's Power_Class.inl:
// GPIO3: battery ADC through a 22k/22k divider (x2).
// GPIO4: CHG_STAT, active low. GPIO5: USB_DET, >200 mV indicates USB power.
// https://github.com/m5stack/M5Unified/blob/master/src/utility/Power_Class.inl
static constexpr uint8_t batteryPin = 3;
static constexpr uint8_t chargePin = 4;
static constexpr uint8_t usbPin = 5;
static BatteryStatus status;
static uint32_t lastSample = 0, filteredMillivolts = 0;
static bool initialized = false;

void beginBattery() {
  pinMode(chargePin, INPUT);
  analogReadResolution(12);
  analogSetPinAttenuation(batteryPin, ADC_11db);
  analogSetPinAttenuation(usbPin, ADC_11db);
  pollBatteryStatus(true);
}

static uint32_t averageMillivolts(uint8_t pin) {
  analogReadMilliVolts(pin); // Discard the first conversion after channel switch.
  uint32_t sum = 0;
  for (int i = 0; i < 16; ++i) sum += analogReadMilliVolts(pin);
  return (sum + 8) / 16;
}

bool pollBatteryStatus(bool force) {
  const uint32_t now = millis();
  if (!force && initialized && now - lastSample < 2000) return false;
  lastSample = now;
  const uint32_t measured = averageMillivolts(batteryPin) * 2;
  const bool usbPowered = averageMillivolts(usbPin) > 200;
  const bool charging = usbPowered && digitalRead(chargePin) == LOW;
  int percent = -1;
  if (estimateBatteryPercent(measured) >= 0) {
    filteredMillivolts = !filteredMillivolts ? measured : (filteredMillivolts * 3 + measured + 2) / 4;
    percent = estimateBatteryPercent(filteredMillivolts);
    // Avoid an e-paper update for every one-percent ADC fluctuation.
    if (status.percent >= 0 && percent > 0 && percent < 100 && std::abs(percent - status.percent) < 2)
      percent = status.percent;
  } else filteredMillivolts = 0;
  bool changed = !initialized || percent != status.percent || charging != status.charging || usbPowered != status.usbPowered;
  status.percent = percent;
  status.millivolts = filteredMillivolts;
  status.charging = charging;
  status.usbPowered = usbPowered;
  initialized = true;
  return changed;
}

const BatteryStatus& batteryStatus() { return status; }
