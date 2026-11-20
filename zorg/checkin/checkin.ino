// checkin.ino - neighbour check-in node (ESP32-C3 SuperMini)
// Reads a presence sensor, decides with checkin_core.h and sends a short text
// to a Meshtastic node over a serial cable (Meshtastic "Serial module", TEXTMSG mode).
//
// Libraries: "RTClib" by Adafruit. Board: ESP32C3 Dev Module (USB CDC on boot: enabled).
// Put checkin_core.h in the same folder as this file.
//
// IMPORTANT: this sketch was written for real hardware but has not been tested on it yet.
// Test every part with the checklist on the guide page before you rely on it.

#include <Wire.h>
#include <RTClib.h>
#include "checkin_core.h"

// ---------- settings you change ----------
#define HOUSE "12"                  // house number used in the messages
#define SERIAL_BAUD 9600            // must match the Meshtastic Serial module setting
checkin::Config cfg;                // times: see checkin_core.h (07:00, 10:30, 30 min)

// ---------- pins (ESP32-C3 SuperMini) ----------
const int PIN_PRESENCE = 0;   // LD2410C OUT (HIGH = presence)
const int PIN_BUTTON   = 1;   // big button to GND
const int PIN_AWAY     = 3;   // "I'm away" switch to GND (closed = away)
const int PIN_BUZZER   = 4;   // active buzzer (use a transistor if it draws more than 20 mA)
const int PIN_LED      = 5;   // LED + 330 ohm to GND
const int PIN_SDA      = 6;   // DS3231
const int PIN_SCL      = 7;
const int PIN_MESH_TX  = 10;  // to the RX pin of the Meshtastic node (common GND!)

// ---------- messages (Dutch; change to your language) ----------
const char MSG_OK[]        = "Check-in OK nr " HOUSE;
const char MSG_ALERT[]     = "ALARM check-in gemist nr " HOUSE ": nog geen activiteit vandaag. Bel of loop even langs.";
const char MSG_REPEAT[]    = "HERHALING check-in gemist nr " HOUSE ". Bel of loop even langs.";
const char MSG_RECOVERED[] = "Check-in nr " HOUSE ": weer activiteit, alles in orde.";
const char MSG_TEST[]      = "TEST check-in nr " HOUSE;

RTC_DS3231 rtc;
checkin::State state;
uint32_t lastTick = 0, lastBeep = 0, buttonDownAt = 0;
uint8_t presenceStreak = 0;
bool buttonWas = false;
bool buttonEvent = false;

void sendText(const char* text) {
  Serial1.println(text);
  Serial.println(text);  // also visible on the USB serial monitor
}

void beep(uint16_t ms) {
  digitalWrite(PIN_BUZZER, HIGH);
  delay(ms);
  digitalWrite(PIN_BUZZER, LOW);
}

void setup() {
  pinMode(PIN_PRESENCE, INPUT);
  pinMode(PIN_BUTTON, INPUT_PULLUP);
  pinMode(PIN_AWAY, INPUT_PULLUP);
  pinMode(PIN_BUZZER, OUTPUT);
  pinMode(PIN_LED, OUTPUT);
  Serial.begin(115200);
  Serial1.begin(SERIAL_BAUD, SERIAL_8N1, -1, PIN_MESH_TX);  // transmit only
  Wire.begin(PIN_SDA, PIN_SCL);
  if (!rtc.begin()) {
    while (true) { digitalWrite(PIN_LED, !digitalRead(PIN_LED)); delay(100); }  // fast blink: no RTC
  }
  if (rtc.lostPower()) {
    // First start: take the time of compiling. Flash the board right after compiling,
    // or set the time another way, otherwise the clock is wrong.
    rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
  }
  beep(100);
}

void loop() {
  // button: short press = "I'm OK", long press (3 s) = send a test message
  bool down = digitalRead(PIN_BUTTON) == LOW;
  if (down && !buttonWas) buttonDownAt = millis();
  if (!down && buttonWas) {
    if (millis() - buttonDownAt >= 3000) { sendText(MSG_TEST); beep(300); }
    else buttonEvent = true;
  }
  buttonWas = down;

  // once per second: sensor, clock, decision
  if (millis() - lastTick >= 1000) {
    lastTick = millis();
    presenceStreak = (digitalRead(PIN_PRESENCE) == HIGH) ? min<int>(presenceStreak + 1, 10) : 0;
    DateTime now = rtc.now();
    checkin::Input in;
    in.minuteOfDay = now.hour() * 60 + now.minute();
    in.activity = presenceStreak >= 2 || buttonEvent;  // 2 s of presence filters out glitches
    in.away = digitalRead(PIN_AWAY) == LOW;
    buttonEvent = false;

    switch (checkin::step(state, cfg, in)) {
      case checkin::EV_SEND_OK:           sendText(MSG_OK); break;
      case checkin::EV_SEND_ALERT:        sendText(MSG_ALERT); break;
      case checkin::EV_SEND_ALERT_REPEAT: sendText(MSG_REPEAT); break;
      case checkin::EV_SEND_RECOVERED:    sendText(MSG_RECOVERED); break;
      default: break;
    }

    // status LED: steady = fine, slow blink = prompting, fast blink = alert sent, off = away
    uint8_t sec = now.second();
    switch (state.phase) {
      case checkin::PROMPTING: digitalWrite(PIN_LED, sec % 2); break;
      case checkin::ALERTED:   digitalWrite(PIN_LED, (millis() / 250) % 2); break;
      case checkin::PAUSED:    digitalWrite(PIN_LED, LOW); break;
      default:                 digitalWrite(PIN_LED, sec % 10 == 0 ? HIGH : LOW); break;  // short flash every 10 s
    }
    // gentle beep once a minute while prompting
    if (state.phase == checkin::PROMPTING && sec == 0) beep(150);
  }
}
