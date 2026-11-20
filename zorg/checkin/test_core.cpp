// Run on a computer:  g++ -std=c++17 -Wall -o test_core test_core.cpp && ./test_core
#include <cassert>
#include <cstdio>
#include "checkin_core.h"
using namespace checkin;

static int failures = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); failures++; } } while (0)
static uint16_t hm(int h, int m) { return h * 60 + m; }

// run minute by minute from a to b, return the events in order
struct Run { Event ev[20]; uint16_t at[20]; int n = 0; };
static Run run(State& s, const Config& c, uint16_t a, uint16_t b, bool away = false,
               int actAt = -1, int actUntil = -1) {
  Run r;
  for (uint16_t t = a; t != b; t = (t + 1) % 1440) {
    Input in{t, actAt >= 0 && t >= actAt && t < actUntil, away};
    Event e = step(s, c, in);
    if (e != EV_NONE && r.n < 20) { r.ev[r.n] = e; r.at[r.n] = t; r.n++; }
  }
  return r;
}

int main() {
  Config c;

  { // 1 normal day: activity at 07:30 -> OK at 10:30
    State s; run(s, c, hm(6, 0), hm(7, 0));
    Run r = run(s, c, hm(7, 0), hm(12, 0), false, hm(7, 30), hm(7, 32));
    CHECK(r.n == 1 && r.ev[0] == EV_SEND_OK && r.at[0] == hm(10, 30));
    CHECK(s.phase == OK_DONE);
  }
  { // 2 no activity: prompt at 10:30, alert 11:00, repeats 11:30 and 12:00, then silence
    State s; run(s, c, hm(6, 0), hm(7, 0));
    Run r = run(s, c, hm(7, 0), hm(14, 0));
    CHECK(r.n == 3);
    CHECK(r.ev[0] == EV_SEND_ALERT && r.at[0] == hm(11, 0));
    CHECK(r.ev[1] == EV_SEND_ALERT_REPEAT && r.at[1] == hm(11, 30));
    CHECK(r.ev[2] == EV_SEND_ALERT_REPEAT && r.at[2] == hm(12, 0));
    CHECK(s.phase == ALERTED);
  }
  { // 3 resident reacts during the prompt: no message at all
    State s; run(s, c, hm(6, 0), hm(7, 0));
    Run r = run(s, c, hm(7, 0), hm(14, 0), false, hm(10, 45), hm(10, 46));
    CHECK(r.n == 0 && s.phase == OK_DONE);
  }
  { // 4 recovery after the alert
    State s; run(s, c, hm(6, 0), hm(7, 0));
    Run r = run(s, c, hm(7, 0), hm(13, 0), false, hm(11, 10), hm(11, 11));
    CHECK(r.n == 2 && r.ev[0] == EV_SEND_ALERT && r.ev[1] == EV_SEND_RECOVERED && r.at[1] == hm(11, 10));
    CHECK(s.phase == OK_DONE);
  }
  { // 5 activity before the window start (05:00) does not count
    State s; run(s, c, hm(4, 0), hm(5, 0));
    Run r1 = run(s, c, hm(5, 0), hm(7, 0), false, hm(5, 0), hm(5, 2));
    Run r2 = run(s, c, hm(7, 0), hm(11, 5));
    CHECK(r1.n == 0 && r2.n == 1 && r2.ev[0] == EV_SEND_ALERT);
  }
  { // 6 away switch: no events, and after coming back at 12:00 everything is fine
    State s; run(s, c, hm(6, 0), hm(7, 30));
    Run r = run(s, c, hm(7, 30), hm(12, 0), true);
    CHECK(r.n == 0 && s.phase == PAUSED);
    Run r2 = run(s, c, hm(12, 0), hm(14, 0), false);
    CHECK(r2.n == 0 && s.phase == OK_DONE);
  }
  { // 6b away switched off before the deadline counts as activity
    State s; run(s, c, hm(6, 0), hm(7, 30));
    run(s, c, hm(7, 30), hm(9, 0), true);
    Run r = run(s, c, hm(9, 0), hm(11, 0), false);
    CHECK(r.n == 1 && r.ev[0] == EV_SEND_OK);
  }
  { // 7 boot after the deadline is silent; next day works
    State s;
    Run r = run(s, c, hm(15, 0), hm(23, 59));
    CHECK(r.n == 0 && s.phase == OK_DONE);
    Run r2 = run(s, c, hm(23, 59), hm(11, 10));  // over midnight, no activity
    CHECK(r2.n == 1 && r2.ev[0] == EV_SEND_ALERT && r2.at[0] == hm(11, 0));
  }
  { // 8 midnight arithmetic
    CHECK(elapsed(1430, 10) == 20);
    CHECK(crossed(1439, 0, 0) == true);
    CHECK(crossed(hm(6, 59), hm(7, 0), hm(7, 0)) == true);
    CHECK(crossed(hm(7, 0), hm(7, 1), hm(7, 0)) == false);
  }
  { // 9 dailyOk off
    Config c2; c2.dailyOk = false;
    State s; run(s, c2, hm(6, 0), hm(7, 0));
    Run r = run(s, c2, hm(7, 0), hm(12, 0), false, hm(8, 0), hm(8, 1));
    CHECK(r.n == 0);
  }
  { // 10 two days in a row: alert, recovery, next day normal
    State s; run(s, c, hm(6, 0), hm(7, 0));
    run(s, c, hm(7, 0), hm(11, 5));                      // alert sent
    Run r = run(s, c, hm(11, 5), hm(10, 40), false, hm(8, 0), hm(8, 1)); // ~24 h, activity at 08:00 next morning (day window)
    CHECK(s.phase == OK_DONE);
    bool gotOk = false; for (int i = 0; i < r.n; i++) if (r.ev[i] == EV_SEND_OK && r.at[i] == hm(10, 30)) gotOk = true;
    CHECK(gotOk);
  }
  if (failures == 0) printf("all tests passed\n");
  return failures ? 1 : 0;
}
