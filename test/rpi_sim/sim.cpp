// Host-side check of rpi_status.ino: what the "RPI" menu item puts on the LCD
// for each /wl-host packet, without an OI3.
//
//   g++ -std=c++11 -Wall -o /tmp/rpi_sim test/rpi_sim/sim.cpp && /tmp/rpi_sim
//
// Prints both rows for every case (custom characters as [bars] [lock] [open]
// [AP] [heart]) and asserts the parts that matter.

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>

// ---- the Arduino bits rpi_status.ino uses --------------------------------------
#define PROGMEM
#define PGM_P const char*
#define PSTR(s) (s)
#define memcpy_P memcpy
#define strlen_P strlen
#define strcpy_P strcpy
#define snprintf_P snprintf
static unsigned long g_now = 100000;
static unsigned long millis() { return g_now; }
struct Lcd {
  uint8_t cg[8][8];
  int loads = 0;
  void createChar(uint8_t slot, uint8_t* bits) { memcpy(cg[slot], bits, 8); loads++; }
  void setCursor(int, int) {}
} lcd;

#include "../../rpi_status.ino"

// ---- helpers ---------------------------------------------------------------------
static std::string show(const char* row) {
  std::string out;
  for (const char* p = row; *p; p++) {
    switch (*p) {
      case 1: out += "[bars]"; break;
      case 2: out += "[lock]"; break;
      case 3: out += "[open]"; break;
      case 4: out += "[AP]"; break;
      case 5: out += "[heart]"; break;
      default: out += *p;
    }
  }
  return out;
}

static void packet(uint8_t state, uint8_t flags, int8_t rssi, uint8_t pct,
                   const uint8_t ip[4], const char* ssid) {
  uint8_t buf[43];
  size_t n = strlen(ssid);
  buf[0] = 1; buf[1] = state; buf[2] = flags; buf[3] = (uint8_t)rssi; buf[4] = pct; buf[5] = 0;
  memcpy(buf + 6, ip, 4);
  buf[10] = (uint8_t)n;
  memcpy(buf + 11, ssid, n);
  onWlHost("705.01", buf, 11 + n);
}

static char r0[13], r1[17];
static void render(const char* label) {
  rpi_format_row0(r0);
  rpi_format_row1(r1);
  assert(strlen(r0) == 12 && strlen(r1) == 16);
  printf("%-34s |%s|%s\n%-34s |%s|\n", label, show(r0).c_str(), "USB", "", show(r1).c_str());
}

int main() {
  const uint8_t IP[4] = {192, 168, 1, 23};
  const uint8_t AP_IP[4] = {192, 168, 4, 1};
  const uint8_t NOIP[4] = {0, 0, 0, 0};
  rpi_lcd_glyphs_init();
  assert(lcd.loads == 5);

  // Nothing heard yet.
  render("no packet yet");
  assert(strncmp(r0, "RPI wait", 8) == 0 && strncmp(r1, "waiting for RPI", 15) == 0);
  assert(!rpiSeen);

  // A packet without LTX does not unlock the menu item.
  packet(RPI_RUN, RPI_F_WIFI, -50, 255, IP, "Domov");
  assert(!rpiSeen);

  // RUN on a client WiFi, locked.
  const uint8_t F = RPI_F_WIFI | RPI_F_LTX | RPI_F_TRXLINK | RPI_F_LOCKED | RPI_F_LOCKABLE;
  packet(RPI_RUN, F, -62, 255, IP, "Domov");
  assert(rpiSeen);
  render("RUN, STA -62, locked");
  assert(r0[0] == RPI_G_BARS && memcmp(r0 + 1, "-62 Domov", 9) == 0 && r0[11] == RPI_G_LOCK);
  assert(strncmp(r1, "192.168.1.23", 12) == 0 && r1[15] == RPI_G_HEART);   // pulse
  g_now += 400;
  render("  0.4 s later (pulse over)");
  assert(r1[15] == ' ');
  assert(rpiBarsLevel == 2);

  // Signal levels rewrite CGRAM slot 1, only on a change.
  int loads = lcd.loads;
  packet(RPI_RUN, F, -45, 255, IP, "Domov"); rpi_format_row0(r0);
  assert(rpiBarsLevel == 3 && lcd.loads == loads + 1);
  packet(RPI_RUN, F, -48, 255, IP, "Domov"); rpi_format_row0(r0);
  assert(lcd.loads == loads + 1);
  packet(RPI_RUN, F, -85, 255, IP, "Domov"); rpi_format_row0(r0);
  assert(rpiBarsLevel == 0);

  // Unlocked Pi with fslock: the open lock blinks.
  const uint8_t FU = F & ~RPI_F_LOCKED;
  g_now = (g_now / 1000 + 1) * 1000 + 100;   // first half of a second
  packet(RPI_RUN, FU, -62, 255, IP, "Domov");
  render("RUN, unlocked (blink on)");
  assert(r0[11] == RPI_G_UNLOCK);
  g_now += 500;
  render("  0.5 s later (blink off)");
  assert(r0[11] == ' ');

  // No fslock at all: nothing in the lock position.
  packet(RPI_RUN, F & ~(RPI_F_LOCKED | RPI_F_LOCKABLE), -62, 255, IP, "Domov");
  render("RUN, no fslock");
  assert(r0[11] == ' ');

  // Long SSID scrolls, starting with a pause at its beginning.
  g_now = 100UL * (15 + 3 + 4) * RPI_MARQUEE_MS;   // step 0 of the cycle (n + gap + pause)
  packet(RPI_RUN, F, -62, 255, IP, "OK1HRA-shack-5G");
  render("RUN, long SSID (start)");
  assert(memcmp(r0 + 5, "OK1HRA", 6) == 0);
  g_now += 5 * RPI_MARQUEE_MS;   // 4 pause steps + 1
  render("  2.5 s later (scrolled by 1)");
  assert(memcmp(r0 + 5, "K1HRA-", 6) == 0);

  // Hotspot.
  g_now = 2000000;
  packet(RPI_RUN, RPI_F_WIFI | RPI_F_AP | RPI_F_LTX | RPI_F_TRXLINK, 0, 255, AP_IP, "WIFILT-setup-AB12");
  render("RUN, AP mode");
  assert(r0[0] == RPI_G_AP && memcmp(r0 + 1, "AP ", 3) == 0);
  assert(strncmp(r1, "192.168.4.1", 11) == 0);

  // Wired only.
  packet(RPI_RUN, RPI_F_LTX | RPI_F_TRXLINK | RPI_F_IP_ETH, 0, 255, IP, "");
  render("RUN, wired only");
  assert(memcmp(r0 + 1, "ETH", 3) == 0 && r0[0] == ' ');

  // Warnings alternate with the address.
  g_now = 4000000 + 2500;   // odd 2 s slot
  packet(RPI_RUN, RPI_F_WIFI | RPI_F_TRXLINK, -60, 255, IP, "Domov");
  render("RUN, local-trx gone (warning)");
  assert(strncmp(r1, "local-trx off", 13) == 0);
  g_now += 2000;
  render("  2 s later (address)");
  assert(strncmp(r1, "192.168.1.23", 12) == 0);
  g_now = 6000000 + 2500;
  packet(RPI_RUN, RPI_F_WIFI | RPI_F_LTX, -60, 255, IP, "Domov");
  render("RUN, radio slot not linked");
  assert(strncmp(r1, "TRX not linked", 14) == 0);
  g_now = 8000000 + 500;
  packet(RPI_RUN, F, -60, 255, NOIP, "Domov");
  render("RUN, no address");
  assert(strncmp(r1, "no IP", 5) == 0);

  // Update in progress, then dark during install.
  packet(RPI_UPDATE, F, -60, 42, IP, "Domov");
  render("UPDATE 42 %");
  assert(strncmp(r1, "UPDATE 42%", 10) == 0);
  g_now += 80000;
  render("  80 s without a packet");
  assert(strncmp(r1, "UPDATE", 6) == 0 && strncmp(r1 + 11, " 1:20", 5) == 0);
  assert(r0[0] == '?');                          // stale marker
  g_now += RPI_UPDATE_MS;
  render("  past the update limit");
  assert(strncmp(r1, "RPI OFFLINE", 11) == 0 && strncmp(r1 + 11, "  11m", 5) == 0);

  // Restart, reboot and stop keep their own clock.
  packet(RPI_RESTART, F, -60, 255, IP, "Domov");
  g_now += 7000;
  render("RESTART, 7 s");
  assert(strncmp(r1, "RESTART", 7) == 0 && strncmp(r1 + 11, " 0:07", 5) == 0);
  packet(RPI_REBOOT, F, -60, 255, IP, "Domov");
  g_now += 45000;
  render("REBOOT, 45 s");
  assert(strncmp(r1, "RPI REBOOT", 10) == 0 && strncmp(r1 + 11, " 0:45", 5) == 0);
  g_now += RPI_DOWN_MS;
  render("  past the down limit");
  assert(strncmp(r1, "RPI OFFLINE", 11) == 0);
  packet(RPI_STOP, F, -60, 255, IP, "Domov");
  g_now += 12000;
  render("STOP, 12 s");
  assert(strncmp(r1, "WIFILT STOP", 11) == 0);

  // Back to RUN, then silent: offline after 15 s, with the clock.
  packet(RPI_RUN, F, -60, 255, IP, "Domov");
  g_now += RPI_FRESH_MS + 1000;
  render("RUN, then 16 s silence");
  assert(strncmp(r1, "RPI OFFLINE", 11) == 0 && strncmp(r1 + 11, " 0:16", 5) == 0);
  g_now += 125UL * 60 * 1000;
  render("  two hours later");
  assert(strncmp(r1 + 11, " 99m+", 5) == 0);

  // Malformed packets change nothing.
  uint8_t bad[11] = {1, RPI_REBOOT, 0, 0, 0, 0, 1, 2, 3, 4, 9};   // SSID that is not there
  onWlHost("x", bad, sizeof(bad));
  assert(rpiState == RPI_RUN);
  onWlHost("x", bad, 5);
  bad[0] = 0; bad[10] = 0;
  onWlHost("x", bad, sizeof(bad));
  assert(rpiState == RPI_RUN);

  // The two locks differ in at least three rows.
  int diff = 0;
  for (int i = 0; i < 8; i++) diff += lcd.cg[RPI_G_LOCK][i] != lcd.cg[RPI_G_UNLOCK][i];
  assert(diff >= 3);

  printf("rpi_sim: all checks passed\n");
  return 0;
}
