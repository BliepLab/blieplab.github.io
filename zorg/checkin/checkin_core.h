// checkin_core.h - pure decision logic for the neighbour check-in node.
// No Arduino dependencies, so it can be tested on a normal computer.
// Time is handled in minutes since midnight (0..1439), local time.
#pragma once
#include <stdint.h>

namespace checkin {

struct Config {
  uint16_t windowStartMin = 7 * 60;       // activity counts from 07:00
  uint16_t deadlineMin    = 10 * 60 + 30; // first check at 10:30
  uint16_t graceMin       = 30;           // beeping time before an alert is sent
  uint8_t  alertRepeats   = 2;            // extra alert messages after the first
  uint16_t repeatEveryMin = 30;           // minutes between alert messages
  bool     dailyOk        = true;         // send a short "OK" every day
};

enum Phase : uint8_t {
  WAITING,    // window open or not yet started, waiting for the deadline
  OK_DONE,    // today's check passed (or was skipped)
  PROMPTING,  // no activity at the deadline: beep and wait for the resident
  ALERTED,    // grace time over: care circle has been messaged
  PAUSED      // "I'm away" switch is on
};

enum Event : uint8_t {
  EV_NONE,
  EV_SEND_OK,
  EV_SEND_ALERT,
  EV_SEND_ALERT_REPEAT,
  EV_SEND_RECOVERED
};

struct Input {
  uint16_t minuteOfDay;  // 0..1439
  bool activity;         // presence detected or button pressed right now
  bool away;             // "I'm away" switch is on
};

struct State {
  bool     init = false;
  Phase    phase = WAITING;
  bool     activity = false;   // activity seen inside today's window
  bool     awayPrev = false;
  uint16_t prev = 0;           // previous minuteOfDay
  uint16_t promptStart = 0;
  uint16_t lastAlert = 0;
  uint8_t  alertsSent = 0;
};

// minutes elapsed from 'from' to 'to', across midnight
inline uint16_t elapsed(uint16_t from, uint16_t to) {
  return (uint16_t)((to + 1440u - from) % 1440u);
}

// true when the clock moved over 'mark' between 'prev' and 'now'
inline bool crossed(uint16_t prev, uint16_t now, uint16_t mark) {
  if (prev == now) return false;
  if (prev < now) return prev < mark && now >= mark;
  return mark > prev || mark <= now;  // wrapped over midnight
}

inline bool inWindow(const Config& c, uint16_t now) {
  return now >= c.windowStartMin && now < c.deadlineMin;
}

// Call this every second or so. Returns an event when a message must be sent.
inline Event step(State& s, const Config& c, const Input& in) {
  Event ev = EV_NONE;
  const uint16_t now = in.minuteOfDay;

  if (!s.init) {
    s.init = true;
    s.prev = now;
    s.awayPrev = in.away;
    s.activity = false;
    s.phase = in.away ? PAUSED : (now >= c.deadlineMin ? OK_DONE : WAITING);
  }

  // "I'm away" switch
  if (in.away) {
    s.phase = PAUSED;
    s.awayPrev = true;
    s.prev = now;
    return EV_NONE;
  }
  if (s.awayPrev) {  // switch just turned off: resident is back
    s.awayPrev = false;
    s.activity = true;
    s.alertsSent = 0;
    s.phase = (now >= c.deadlineMin) ? OK_DONE : WAITING;
  }

  // new day: reset when the window start is passed
  if (crossed(s.prev, now, c.windowStartMin)) {
    s.phase = WAITING;
    s.activity = false;
    s.alertsSent = 0;
  }
  s.prev = now;

  // activity
  if (in.activity) {
    if (s.phase == WAITING && inWindow(c, now)) {
      s.activity = true;
    } else if (s.phase == PROMPTING) {
      s.activity = true;
      s.phase = OK_DONE;
    } else if (s.phase == ALERTED) {
      s.activity = true;
      s.phase = OK_DONE;
      s.alertsSent = 0;
      return EV_SEND_RECOVERED;
    }
  }

  // deadline reached
  if (s.phase == WAITING && now >= c.deadlineMin) {
    if (s.activity) {
      s.phase = OK_DONE;
      if (c.dailyOk) ev = EV_SEND_OK;
    } else {
      s.phase = PROMPTING;
      s.promptStart = now;
    }
    return ev;
  }

  // beeping time over
  if (s.phase == PROMPTING && elapsed(s.promptStart, now) >= c.graceMin) {
    s.phase = ALERTED;
    s.alertsSent = 1;
    s.lastAlert = now;
    return EV_SEND_ALERT;
  }

  // repeat alert a limited number of times
  if (s.phase == ALERTED && s.alertsSent <= c.alertRepeats &&
      elapsed(s.lastAlert, now) >= c.repeatEveryMin) {
    s.alertsSent++;
    s.lastAlert = now;
    return EV_SEND_ALERT_REPEAT;
  }
  return ev;
}

}  // namespace checkin
