#include <Arduino.h>
#include "Services.h"
#include "Sounds.h"

namespace {
constexpr uint8_t buzzerPin = 21;
constexpr uint8_t buzzerChannel = 7;

void note(uint32_t frequency, uint32_t duration) {
  if (!settings.sounds) return;
  if (!ledcSetup(buzzerChannel, frequency, 10)) return;
  ledcAttachPin(buzzerPin, buzzerChannel);
  ledcWriteTone(buzzerChannel, frequency);
  // Short, synchronous notes finish before display updates or sleep. No
  // background task or running PWM is left behind while reading.
  delay(duration);
  ledcWrite(buzzerChannel, 0);
  ledcDetachPin(buzzerPin);
  pinMode(buzzerPin, OUTPUT);
  digitalWrite(buzzerPin, LOW);
}
}

void beginSounds() {
  pinMode(buzzerPin, OUTPUT);
  digitalWrite(buzzerPin, LOW);
}
void playButtonClick() { note(2400, 12); }
void playStartupSound() {
  if (!settings.sounds) return;
  note(1600, 60);
  delay(25);
  note(2400, 85);
}
void playPowerdownSound() {
  if (!settings.sounds) return;
  note(2000, 60);
  delay(25);
  note(1200, 100);
}
