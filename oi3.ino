const char* REV = "20260524";

const int DCIN PROGMEM = A7;      // measure input voltage
const int DC3V PROGMEM = A6;      // measure 3,3V
const int encA PROGMEM = 24;      // encoder-A
const int encB PROGMEM = 18;      // encoder-B
const int MEM PROGMEM = A1;       // K3NG CW key button
const int CW1 PROGMEM = 34;       // out
const int CW2 PROGMEM = 35;       //
const int PTT1 PROGMEM = 41;      //
const int PTT2 PROGMEM = 22;      //
const int PTT3 PROGMEM = 25;      // PTT mic
const int MENU PROGMEM = 36;      // MODE button
const int FSK PROGMEM = 23;       // FSK output
const int INTERLOCK PROGMEM = 2;  // in
const int FootSW PROGMEM = 19;    // in
const byte BCD1 PROGMEM = 42;     // __
const byte BCD2 PROGMEM = 43;     //   |
const byte BCD3 PROGMEM = 44;     //   |- band data
const byte BCD4 PROGMEM = 45;     // __|  from band decoder
const int PADDLEL PROGMEM = 26;    // in
const int PADDLER PROGMEM = 28;    // in
const int SEQUENCER PROGMEM = 40; // out
const int PTTPA PROGMEM = 31;     // out PA PTT
const int SDPLUG PROGMEM = A5;    // in  microSD detect
const int SDCS PROGMEM = 53;      // out
const int AFSK PROGMEM = 29;      // out Switch TX audio path
const int PTT232 PROGMEM = 3;     // in  PTT from USB/serial interface
const int FSKDET PROGMEM = 33;    // in  FSK detector from USB/serial interface
const int WINKEY PROGMEM = 27;    // out disable DTR/RTS from from USB/serial interface during run winkey emulator
const int TONE PROGMEM = 4;       // out
const int ACC4 PROGMEM = 47;      // if enable ACC SHIFT OUT KEYBOARD
const int ACC5 PROGMEM = 13;      // if enable ACC SHIFT OUT KEYBOARD
const int ACC6 PROGMEM = 5;      // if enable ACC SHIFT OUT KEYBOARD
const int ACC7 PROGMEM = 30;       // if enable ACC SHIFT IN KEYBOARD
const int ACC8 PROGMEM = 32;
const int ACC9 PROGMEM = 11;       // if enable ACC SHIFT IN KEYBOARD
const int ACC10 PROGMEM = 12;      // if enable ACC SHIFT IN KEYBOARD
const int ACC11 PROGMEM = 38;
const int ACC12 PROGMEM = A3;
const int ACC13 PROGMEM = A4;
const int ACC14 PROGMEM = A8;
const int ACC15 PROGMEM = 21;     // SCL/interrupt/ if enable ACC SHIFT OUT KEYBOARD
const int ACC16 PROGMEM = 20;     // SDA/interrupt (was GPS PPS — free)
const int ACC17 PROGMEM = A9;
const int ACC19 PROGMEM = A11;    // if define Icom ACC voltage input
const int SelfRES PROGMEM = 39;
const int ETHINST PROGMEM = 46;     // in Ethernet module install detect
// const int MISO = 50;
// const int MOSI = 51;
// const int SCK  = 52;
const int SMTpad1 PROGMEM = 48;   // ^ Internal SMT pad
const int SMTpad2 PROGMEM = 49;   // -
const int SMTpad3 PROGMEM = A0;   // v



// ===================== LCD =====================
// HW: 16x2 parallel HD44780, 4-bit mode (PCB_REV_3_1415)
#include <LiquidCrystal.h>

#define lcd_rs      A2
#define lcd_enable  37
#define lcd_d4      6
#define lcd_d5      7
#define lcd_d6      8
#define lcd_d7      9
#define LCD_COLUMNS 16
#define LCD_ROWS    2

LiquidCrystal lcd(lcd_rs, lcd_enable, lcd_d4, lcd_d5, lcd_d6, lcd_d7);

void lcd_setup() {
  lcd.begin(LCD_COLUMNS, LCD_ROWS);
  String boot = String("FW ") + REV;
  byte col = (LCD_COLUMNS - boot.length()) / 2;
  lcd.setCursor(col, 0);
  lcd.print(boot);
  delay(500);
  lcd.clear();
}
// =================== END LCD ===================


// ===================== SD CONFIG =====================
// HW: microSD on SPI, CS=SDCS, presence on SDPLUG (analog, <128 = plugged)
// Reads key=value pairs from /oi3.cfg at boot. Missing SD/file → keep defaults.
#include <SPI.h>
#include <SD.h>

String ConfigFile = "oi3.cfg";
char charConfigFile[10];
File myFile;

// Defaults mirror current /oi3.cfg so the unit boots identically without SD
byte          NET_ID          = 0x02;   // hex; 0x00 disables TrxNet
String        YOUR_CALL       = "OK1HRA";
bool          InterlockEnable = true;
int           SEQUENCERlead   = 0;
int           SEQUENCERtail   = 0;
int           PAlead          = 10;
int           PAtail          = 0;
int           PTTlead         = 10;
int           PTTtail         = 210;
byte          PTTmodeLSB      = 3;
byte          PTTmodeUSB      = 3;
byte          PTTmodeAM       = 3;
byte          PTTmodeCW       = 3;
byte          PTTmodeRTTY     = 1;
byte          PTTmodeFM       = 3;
byte          PTTmodeCWR      = 3;
byte          PTTmodeRTR      = 1;
byte          PTTmodeDefault  = 3;
int           BAND_DECODER_IN = 1;
unsigned long SERBAUD2        = 9600;
byte          CIV_ADRESS      = 0x98;   // hex
bool          CIV_CW_VIA_CIV  = true;
bool          EthernetEnable  = true;
bool          EthernetDHCP    = true;
// Keyer cfg (overridable from oi3.cfg; EEPROM may further override at boot)
byte          KeyerDefaultWpm    = 28;     // 5..50
char          KeyerMode          = 'B';    // 'A' or 'B' (iambic)
bool          KeyerPaddleReverse = false;  // false=PADDLER is dit, true=swap (PADDLEL is dit)
uint16_t      KeyerSidetoneHz    = 600;    // 0=off, otherwise 300..1200
byte          KeyerRttyBaud      = 45;     // 45 / 50 / 75 / 100 (45 => 45.45 Bd)
bool          KeyerFskReverse    = false;  // false: MARK=HIGH/SPACE=LOW; true: inverted

static unsigned char hexToDecBy4bit(unsigned char hex) {
  if (hex > 0x39) hex -= 7;
  return (hex & 0xf);
}

void readSDSettings() {
  char character;
  String settingName;  settingName.reserve(20);
  String settingValue; settingValue.reserve(10);
  myFile = SD.open(charConfigFile);
  if (!myFile) return;
  while (myFile.available()) {
    character = myFile.read();
    while (myFile.available() && character != '[') character = myFile.read();
    character = myFile.read();
    while (myFile.available() && character != '=') { settingName += character; character = myFile.read(); }
    character = myFile.read();
    while (myFile.available() && character != ']') { settingValue += character; character = myFile.read(); }
    if (character == ']') {
      Serial.print('['); Serial.print(settingName); Serial.print('=');
      Serial.print(settingValue); Serial.println(']');

      if      (settingName == "NET_ID")          { unsigned char b[3]; settingValue.toCharArray((char*)b, 3);
                                                   NET_ID = hexToDecBy4bit(b[0])<<4 | hexToDecBy4bit(b[1]); }
      else if (settingName == "YOUR_CALL")       YOUR_CALL = settingValue;
      else if (settingName == "InterlockEnable") InterlockEnable = (bool)settingValue.toInt();
      else if (settingName == "SEQUENCERlead")   SEQUENCERlead = settingValue.toInt();
      else if (settingName == "SEQUENCERtail")   SEQUENCERtail = settingValue.toInt();
      else if (settingName == "PAlead")          PAlead = settingValue.toInt();
      else if (settingName == "PAtail")          PAtail = settingValue.toInt();
      else if (settingName == "PTTlead")         PTTlead = settingValue.toInt();
      else if (settingName == "PTTtail")         PTTtail = settingValue.toInt();
      else if (settingName == "PTTmodeLSB")      PTTmodeLSB = (byte)settingValue.toInt();
      else if (settingName == "PTTmodeUSB")      PTTmodeUSB = (byte)settingValue.toInt();
      else if (settingName == "PTTmodeAM")       PTTmodeAM = (byte)settingValue.toInt();
      else if (settingName == "PTTmodeCW")       PTTmodeCW = (byte)settingValue.toInt();
      else if (settingName == "PTTmodeRTTY")     PTTmodeRTTY = (byte)settingValue.toInt();
      else if (settingName == "PTTmodeFM")       PTTmodeFM = (byte)settingValue.toInt();
      else if (settingName == "PTTmodeCWR")      PTTmodeCWR = (byte)settingValue.toInt();
      else if (settingName == "PTTmodeRTR")      PTTmodeRTR = (byte)settingValue.toInt();
      else if (settingName == "PTTmodeDefault")  PTTmodeDefault = (byte)settingValue.toInt();
      else if (settingName == "BAND_DECODER_IN") BAND_DECODER_IN = settingValue.toInt();
      else if (settingName == "SERBAUD2")        SERBAUD2 = (unsigned long)settingValue.toInt();
      else if (settingName == "CIV_ADRESS")      { unsigned char b[3]; settingValue.toCharArray((char*)b, 3);
                                                   CIV_ADRESS = hexToDecBy4bit(b[0])<<4 | hexToDecBy4bit(b[1]); }
      else if (settingName == "CIV_CW_VIA_CIV")  CIV_CW_VIA_CIV = (bool)settingValue.toInt();
      else if (settingName == "EthernetEnable")  EthernetEnable = (bool)settingValue.toInt();
      else if (settingName == "EthernetDHCP")    EthernetDHCP = (bool)settingValue.toInt();
      else if (settingName == "KeyerDefaultWpm") KeyerDefaultWpm = (byte)settingValue.toInt();
      else if (settingName == "KeyerMode")       { char c = settingValue.length() > 0 ? settingValue.charAt(0) : 'B';
                                                   if (c >= 'a' && c <= 'z') c -= 32;
                                                   KeyerMode = (c == 'A') ? 'A' : 'B'; }
      else if (settingName == "KeyerPaddleReverse") KeyerPaddleReverse = (bool)settingValue.toInt();
      else if (settingName == "KeyerSidetoneHz") KeyerSidetoneHz = (uint16_t)settingValue.toInt();
      else if (settingName == "KeyerRttyBaud")   KeyerRttyBaud = (byte)settingValue.toInt();
      else if (settingName == "KeyerFskReverse") KeyerFskReverse = (bool)settingValue.toInt();

      settingName = "";
      settingValue = "";
    }
  }
  myFile.close();
}

void sdcfg_setup() {
  Serial.println(F("--- SD CONFIG ---"));
  pinMode(SDPLUG, INPUT);
    digitalWrite(SDPLUG, HIGH);            // pull up
  pinMode(SDCS, OUTPUT);

  lcd.setCursor(0, 1);
  if (analogRead(SDPLUG) > 128) {          // SD not inserted
    lcd.print(F("SD card missing "));
    Serial.println(F("SD card missing"));
    delay(1000);
    lcd.setCursor(0, 1); lcd.print(F("                "));
    return;
  }
  if (!SD.begin(SDCS)) {
    lcd.print(F("SD init fail    "));
    Serial.println(F("SD init fail"));
    delay(1000);
    lcd.setCursor(0, 1); lcd.print(F("                "));
    return;
  }
  ConfigFile.toCharArray(charConfigFile, sizeof(charConfigFile));
  if (!SD.exists(charConfigFile)) {
    lcd.print(ConfigFile); lcd.print(F(" missing "));
    Serial.print(ConfigFile); Serial.println(F(" missing"));
    delay(1000);
    lcd.setCursor(0, 1); lcd.print(F("                "));
    return;
  }
  lcd.print(F("loaded ")); lcd.print(ConfigFile);
  readSDSettings();
}
// =================== END SD CONFIG ===================


// ===================== ETHERNET =====================
// HW: W5100/W5500 module on SPI, ETHINST (D46) pull-up — LOW = module present.
// Boot: block up to ETH_BOOT_TIMEOUT waiting for link, show IP on LCD ~1 s.
// Runtime: every ETH_CHECK_INTERVAL poll linkStatus(); on link-up reinit;
// on DHCP refresh lease via Ethernet.maintain(). Runtime messages → Serial only.
#include <Ethernet.h>
#include <EthernetUdp.h>

bool          EthLinkStatus      = false;
bool          EthModulePresent   = false;
unsigned long EthLastCheckMs     = 0;
const unsigned long ETH_CHECK_INTERVAL = 1000;
const unsigned long ETH_BOOT_TIMEOUT   = 5000;
const unsigned long ETH_SLIDE_MIN_MS   = 500;

byte      ethMac[]      = {0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0x00};
IPAddress ethStaticIP   (192, 168, 1, 220);
IPAddress ethGateway    (192, 168, 1, 1);
IPAddress ethSubnet     (255, 255, 255, 0);
IPAddress ethDns        (8, 8, 8, 8);

static bool ethernet_bringUp() {
  if (EthernetDHCP) {
    if (Ethernet.begin(ethMac) == 0) return false;
    IPAddress ip = Ethernet.localIP();
    if (ip[0] == 0 && ip[1] == 0 && ip[2] == 0 && ip[3] == 0) return false;
  } else {
    Ethernet.begin(ethMac, ethStaticIP, ethDns, ethGateway, ethSubnet);
  }
  Ethernet.setRetransmissionTimeout(500);
  Ethernet.setRetransmissionCount(2);
  return true;
}

static void ethernet_showIP() {
  IPAddress ip = Ethernet.localIP();
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(F("IP address:"));
  lcd.setCursor(0, 1);
  lcd.print(ip);
  Serial.print(F("Ethernet IP: "));
  Serial.println(ip);
  delay(1000);
  lcd.clear();
}

void ethernet_setup() {
  Serial.println(F("--- ETHERNET ---"));
  pinMode(ETHINST, INPUT_PULLUP);
  delay(2);
  EthModulePresent = (digitalRead(ETHINST) == LOW);

  if (!EthernetEnable) {
    Serial.println(F("Ethernet disabled in cfg"));
    lcd.setCursor(0, 0);
    lcd.print(F("Eth: disabled   "));
    delay(1000);
    lcd.clear();
    return;
  }
  if (!EthModulePresent) {
    Serial.println(F("ETH module not present"));
    lcd.setCursor(0, 0);
    lcd.print(F("ETH module n/a  "));
    delay(1000);
    lcd.clear();
    return;
  }

  ethMac[5] = 0xFF - NET_ID;

  // Slide 2: Net-ID + waiting for link (min 0.5 s, max ETH_BOOT_TIMEOUT)
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(F("Net-ID: "));
  lcd.print(NET_ID, HEX);
  lcd.setCursor(0, 1);
  lcd.print(F("Eth: waiting..."));

  unsigned long t0 = millis();
  while (millis() - t0 < ETH_BOOT_TIMEOUT) {
    if (Ethernet.linkStatus() == LinkON) {
      EthLinkStatus = true;
      break;
    }
    delay(50);
  }
  while (millis() - t0 < ETH_SLIDE_MIN_MS) delay(10);

  if (!EthLinkStatus) {
    Serial.println(F("Ethernet: no link"));
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print(F("Eth: no link"));
    delay(1000);
    lcd.clear();
    return;
  }

  if (!ethernet_bringUp()) {
    Serial.println(F("Ethernet: DHCP fail"));
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print(F("DHCP fail"));
    delay(1000);
    lcd.clear();
    return;
  }

  ethernet_showIP();
  EthLastCheckMs = millis();
}

void ethernet_loop() {
  if (!EthernetEnable || !EthModulePresent) return;
  if (millis() - EthLastCheckMs < ETH_CHECK_INTERVAL) return;
  EthLastCheckMs = millis();

  bool linkNow = (Ethernet.linkStatus() == LinkON);
  if (linkNow && !EthLinkStatus) {
    EthLinkStatus = true;
    Serial.println(F("Eth: link up"));
    if (ethernet_bringUp()) {
      Serial.print(F("Ethernet IP: "));
      Serial.println(Ethernet.localIP());
    } else {
      Serial.println(F("Eth: DHCP fail on relink"));
    }
  } else if (!linkNow && EthLinkStatus) {
    EthLinkStatus = false;
    Serial.println(F("Eth: link lost"));
  }

  if (EthLinkStatus && EthernetDHCP) {
    Ethernet.maintain();
  }
}
// =================== END ETHERNET ===================


// ===================== ICOM CI-V =====================
// HW: ICOM transceiver on Serial2 (Mega: RX2=17, TX2=16) @ SERBAUD2.
// Reads frequency and mode via CI-V; supports both poll (controller-initiated)
// and sniff (radio transceive broadcast). On change updates CivFreq/CivMode
// and calls civ_publish_*() hooks (currently empty — TrxNet placeholder).
// Watchdog: after CIV_WATCHDOG_MS without data, CivValid drops to false;
// last-known CivFreq/CivMode are preserved.
// Gating: civ_setup() always opens Serial2 (port shared with other features
// like CIV_CW_VIA_CIV); civ_loop() is a no-op unless BAND_DECODER_IN == 1.
// State machine ported from k3ng_keyer icomSM().

const uint8_t       CIV_OWN_ADDRESS         = 0x0E;
const unsigned long CIV_POLL_INTERVAL_SLOW  = 2000;
const unsigned long CIV_POLL_INTERVAL_FAST  = 200;
const unsigned long CIV_WATCHDOG_MS         = 10000;

unsigned long CivFreq  = 0;       // Hz, last parsed frequency
uint8_t       CivMode  = 0xFF;    // raw CI-V mode byte; 0xFF = unknown
bool          CivValid = false;

static unsigned long civLastPollMs       = 0;
static unsigned long civCurrentInterval  = CIV_POLL_INTERVAL_SLOW;
static unsigned long civFastUntilMs      = 0;
static unsigned long civLastParseMs      = 0;
static uint8_t       civSm               = 1;
static uint8_t       civRd[11];

// --- TrxNet publish hooks (defined in TRXNET section below) ---
void civ_publish_freq();
void civ_publish_mode();

static void civ_send_request(uint8_t cmd) {
  Serial2.write((uint8_t)0xFE);
  Serial2.write((uint8_t)0xFE);
  Serial2.write((uint8_t)CIV_ADRESS);
  Serial2.write((uint8_t)CIV_OWN_ADDRESS);
  Serial2.write((uint8_t)cmd);
  Serial2.write((uint8_t)0xFD);
  Serial2.flush();
}

void civ_request_fast_poll(unsigned long duration_ms);

// IC-7610 in dual-receive mode targets VFO B by default; this sub-cmd forces
// MAIN band selection before SET. Other ICOM models NAK this sub-cmd, so it's
// gated by CI-V address.
static void civ_send_main_band_select() {
  Serial2.write((uint8_t)0xFE);
  Serial2.write((uint8_t)0xFE);
  Serial2.write((uint8_t)CIV_ADRESS);
  Serial2.write((uint8_t)CIV_OWN_ADDRESS);
  Serial2.write((uint8_t)0x07);
  Serial2.write((uint8_t)0xD2);
  Serial2.write((uint8_t)0x00);
  Serial2.write((uint8_t)0xFD);
  Serial2.flush();
}

void civ_send_setfreq(uint32_t hz) {
  if (CIV_ADRESS == 0x98) civ_send_main_band_select();
  uint8_t b1 = ((hz /         10UL) % 10) << 4 | ( hz                 % 10);
  uint8_t b2 = ((hz /       1000UL) % 10) << 4 | ((hz /        100UL) % 10);
  uint8_t b3 = ((hz /     100000UL) % 10) << 4 | ((hz /      10000UL) % 10);
  uint8_t b4 = ((hz /   10000000UL) % 10) << 4 | ((hz /    1000000UL) % 10);
  uint8_t b5 = ((hz / 1000000000UL) % 10) << 4 | ((hz /  100000000UL) % 10);
  Serial2.write((uint8_t)0xFE);
  Serial2.write((uint8_t)0xFE);
  Serial2.write((uint8_t)CIV_ADRESS);
  Serial2.write((uint8_t)CIV_OWN_ADDRESS);
  Serial2.write((uint8_t)0x05);
  Serial2.write(b1); Serial2.write(b2); Serial2.write(b3);
  Serial2.write(b4); Serial2.write(b5);
  Serial2.write((uint8_t)0xFD);
  Serial2.flush();
  civ_request_fast_poll(10000);
}

void civ_send_setmode(uint8_t civmode) {
  if (CIV_ADRESS == 0x98) civ_send_main_band_select();
  Serial2.write((uint8_t)0xFE);
  Serial2.write((uint8_t)0xFE);
  Serial2.write((uint8_t)CIV_ADRESS);
  Serial2.write((uint8_t)CIV_OWN_ADDRESS);
  Serial2.write((uint8_t)0x06);
  Serial2.write(civmode);
  Serial2.write((uint8_t)0x01);
  Serial2.write((uint8_t)0xFD);
  Serial2.flush();
  civ_request_fast_poll(10000);
}

// CI-V freq is 5 BCD bytes, LSB-first. Each byte's high nibble is the more
// significant decimal digit of that pair.
static unsigned long civ_decode_bcd_freq(uint8_t b1, uint8_t b2, uint8_t b3, uint8_t b4, uint8_t b5) {
  unsigned long f = 0;
  f += ((unsigned long)(b5 >> 4))   * 1000000000UL;
  f += ((unsigned long)(b5 & 0x0F)) *  100000000UL;
  f += ((unsigned long)(b4 >> 4))   *   10000000UL;
  f += ((unsigned long)(b4 & 0x0F)) *    1000000UL;
  f += ((unsigned long)(b3 >> 4))   *     100000UL;
  f += ((unsigned long)(b3 & 0x0F)) *      10000UL;
  f += ((unsigned long)(b2 >> 4))   *       1000UL;
  f += ((unsigned long)(b2 & 0x0F)) *        100UL;
  f += ((unsigned long)(b1 >> 4))   *         10UL;
  f +=  (unsigned long)(b1 & 0x0F);
  return f;
}

// Frame layouts handled:
//   FE FE <to> <radio> 03 <BCD1..5> FD     freq response to our poll
//   FE FE 00  <radio> 00 <BCD1..5> FD     freq transceive broadcast
//   FE FE <radio> <us> 05 <BCD1..5> FD    set-freq sniff (own outgoing echo)
//   FE FE <to> <radio> 04 <mode> <flt> FD  mode response
static void civ_state_machine(uint8_t b) {
  civRd[10] = 0;
  switch (civSm) {
    case 1:  if (b == 0xFE) { civSm = 2; civRd[0] = b; } break;
    case 2:  if (b == 0xFE) { civSm = 3; civRd[1] = b; } else { civSm = 1; } break;
    case 3:  if (b == 0x00 || b == 0xE0 || b == CIV_OWN_ADDRESS || b == 0xF1) {
               civSm = 4; civRd[2] = b;
             } else if (b == CIV_ADRESS) {
               civSm = 6; civRd[2] = b;
             } else { civSm = 1; } break;
    case 4:  if (b == CIV_ADRESS) { civSm = 5; civRd[3] = b; } else { civSm = 1; } break;
    case 5:  if (b == 0x00 || b == 0x03) { civSm = 8;  civRd[4] = b; }
             else if (b == 0x04)         { civSm = 14; civRd[4] = b; }
             else                        { civSm = 1; } break;
    case 6:  if (b == 0x00 || b == 0xE0 || b == 0xF1) { civSm = 7; civRd[3] = b; } else { civSm = 1; } break;
    case 7:  if (b == 0x00 || b == 0x05) { civSm = 8; civRd[4] = b; } else { civSm = 1; } break;
    case 8:  if (b <= 0x99) { civSm = 9;  civRd[5] = b; } else { civSm = 1; } break;
    case 9:  if (b <= 0x99) { civSm = 10; civRd[6] = b; } else { civSm = 1; } break;
    case 10: if (b <= 0x99) { civSm = 11; civRd[7] = b; } else { civSm = 1; } break;
    case 11: if (b <= 0x99) { civSm = 12; civRd[8] = b; } else { civSm = 1; } break;
    case 12: if (b <= 0x99) { civSm = 13; civRd[9] = b; } else { civSm = 1; } break;
    case 13: if (b == 0xFD) { civSm = 1;  civRd[10] = b; } else { civSm = 1; } break;
    case 14: if (b <= 0x12) { civSm = 15; civRd[5] = b; } else { civSm = 1; } break;
    case 15: if (b <= 0x03) { civSm = 16; civRd[6] = b; } else { civSm = 1; } break;
    case 16: if (b == 0xFD) { civSm = 1;  civRd[7] = b; } else { civSm = 1; civRd[7] = 0; } break;
  }
}

void civ_request_fast_poll(unsigned long duration_ms) {
  civFastUntilMs = millis() + duration_ms;
  civCurrentInterval = CIV_POLL_INTERVAL_FAST;
}

void civ_setup() {
  Serial.println(F("--- ICOM CI-V ---"));
  Serial2.begin(SERBAUD2);
  // first poll fires immediately on first civ_loop() tick
  civLastPollMs  = millis() - CIV_POLL_INTERVAL_SLOW;
  civLastParseMs = millis();
}

void civ_loop() {
  if (BAND_DECODER_IN != 1) return;

  // fast-poll window expired → drop back to slow cadence
  if (civFastUntilMs != 0 && (long)(millis() - civFastUntilMs) >= 0) {
    civFastUntilMs = 0;
    civCurrentInterval = CIV_POLL_INTERVAL_SLOW;
  }

  // periodic freq poll
  if (millis() - civLastPollMs >= civCurrentInterval) {
    civ_send_request(0x03);
    civLastPollMs = millis();
  }

  // sniff & parse anything on the bus
  while (Serial2.available() > 0) {
    civ_state_machine((uint8_t)Serial2.read());

    // freq frame complete (cmd 0x00 transceive, 0x03 poll response, or 0x05 set echo)
    if ((civRd[4] == 0x00 || civRd[4] == 0x03 || civRd[4] == 0x05) && civRd[10] == 0xFD) {
      unsigned long f = civ_decode_bcd_freq(civRd[5], civRd[6], civRd[7], civRd[8], civRd[9]);
      civLastParseMs = millis();
      civRd[10] = 0;
      civRd[4]  = 0;
      bool wasInvalid = !CivValid;
      CivValid = true;
      if (wasInvalid || f != CivFreq) {
        CivFreq = f;
        Serial.print(F("[CIV freq=")); Serial.print(CivFreq); Serial.println(']');
        civ_publish_freq();
      }
      // chain mode request after a freq response (k3ng pattern)
      civ_send_request(0x04);
    }
    // mode frame complete
    else if (civRd[4] == 0x04 && civRd[7] == 0xFD) {
      uint8_t m = civRd[5];
      civLastParseMs = millis();
      civRd[7] = 0;
      civRd[4] = 0;
      if (m != CivMode) {
        CivMode = m;
        Serial.print(F("[CIV mode="));
        if (m < 0x10) Serial.print('0');
        Serial.print(m, HEX);
        Serial.println(']');
        civ_publish_mode();
      }
    }
  }

  // watchdog
  if (CivValid && (millis() - civLastParseMs) > CIV_WATCHDOG_MS) {
    CivValid = false;
    Serial.println(F("[CIV watchdog: no data]"));
  }
}
// =================== END ICOM CI-V ===================


// ===================== TRXNET =====================
// HW: shares Ethernet (W5100/W5500) with the ETHERNET section. UDP port 5683.
// Publishes /hz, /mode on change. Subscribes /s-hz, /s-mode (commands from
// peers — translated to CI-V SET on the radio; the radio's echo path through
// civ_state_machine() updates CivFreq/CivMode and re-publishes).
// Gating: NET_ID == 0x00 disables TrxNet entirely (protocol sentinel).
// Init is deferred to trxnet_loop() so plug-in after boot works without reset.
// Greeting: on new peer, sends a CON snapshot of /hz + /mode (drained one
// peer per loop iteration, gated until CIV state is known).

#include <TrxNet.h>

const uint16_t TRXNET_PORT = 5683;

EthernetUDP trxUdp;
TrxNet      net(trxUdp);
char        trxDeviceName[TRXNET_MAX_DEVICE_NAME];
bool        trxNetStarted = false;

volatile uint32_t trxPendingHz   = 0;
volatile uint8_t  trxPendingMode = 0;     // CI-V mode byte
volatile bool     trxSHzPending  = false;
volatile bool     trxSModePending= false;

char     trxPendingGreet[TRXNET_MAX_PEERS][TRXNET_MAX_DEVICE_NAME];
uint8_t  trxPendingGreetCount = 0;

void onPeerJoined(const TrxPeer* peer);
void onSetCw(const char* from, const uint8_t* data, size_t len);  // defined in KEYER block

void civ_publish_freq() {
  if (!trxNetStarted) return;
  uint32_t f = CivFreq;
  net.publish("/hz", (uint8_t*)&f, sizeof(f));
}

void civ_publish_mode() {
  if (!trxNetStarted) return;
  uint8_t m = CivMode;
  net.publish("/mode", &m, sizeof(m));
}

static void onSetHz(const char* /*from*/, const uint8_t* data, size_t len) {
  if (len < sizeof(uint32_t)) return;
  memcpy((void*)&trxPendingHz, data, sizeof(uint32_t));
  trxSHzPending = true;
}

static void onSetMode(const char* /*from*/, const uint8_t* data, size_t len) {
  if (len < sizeof(uint8_t)) return;
  trxPendingMode = data[0];
  trxSModePending = true;
}

void onPeerJoined(const TrxPeer* peer) {
  if (!peer) return;
  if (trxPendingGreetCount >= TRXNET_MAX_PEERS) return;
  strncpy(trxPendingGreet[trxPendingGreetCount], peer->name, TRXNET_MAX_DEVICE_NAME - 1);
  trxPendingGreet[trxPendingGreetCount][TRXNET_MAX_DEVICE_NAME - 1] = '\0';
  trxPendingGreetCount++;
}

static void republishState(const char* peerName) {
  uint32_t f = CivFreq;
  uint8_t  m = CivMode;
  net.publishTo(peerName, "/hz",   (uint8_t*)&f, sizeof(f), TRX_CON);
  net.publishTo(peerName, "/mode", &m,            sizeof(m), TRX_CON);
}

void trxnet_setup() {
  Serial.println(F("--- TRXNET ---"));
  if (NET_ID == 0x00) {
    Serial.println(F("TrxNet disabled (NET_ID=0x00)"));
    return;
  }
  // net.begin() is deferred to trxnet_loop() until Ethernet link is up.
  Serial.println(F("TrxNet waiting for ETH link"));
}

void trxnet_loop() {
  // Deferred init / re-probe on link-up
  if (NET_ID != 0x00 && EthernetEnable && EthModulePresent && EthLinkStatus) {
    if (!trxNetStarted) {
      snprintf(trxDeviceName, sizeof(trxDeviceName), "OI3.%02x", NET_ID);
      net.setPort(TRXNET_PORT);
      net.onPeerAdded(onPeerJoined);   // must be set BEFORE begin()
      net.begin(trxDeviceName);
      net.subscribe("/s-hz",   onSetHz);
      net.subscribe("/s-mode", onSetMode);
      net.subscribe("/s-cw",   onSetCw);
      trxNetStarted = true;
      Serial.print(F("TrxNet begin: "));
      Serial.println(trxDeviceName);
    }
    net.loop();
  }

  if (trxSHzPending) {
    trxSHzPending = false;
    civ_send_setfreq(trxPendingHz);
  }
  if (trxSModePending) {
    trxSModePending = false;
    civ_send_setmode(trxPendingMode);
  }

  // Drain greeting queue — one peer per iteration. Gated until CIV state is
  // populated so peers don't get a /hz=0 or /mode=0xFF snapshot.
  if (trxPendingGreetCount > 0 && trxNetStarted
      && CivValid && CivFreq != 0 && CivMode != 0xFF) {
    trxPendingGreetCount--;
    republishState(trxPendingGreet[trxPendingGreetCount]);
  }
}
// =================== END TRXNET ===================


// ===================== PTT SEQUENCER =====================
// HW: PTT1=D41 (DB25 pin 8 / REM), PTT2=D22 (DB25 pin 20 / DAT),
//     PTT3=D25 (DB25 pin 22 / MIC), PTTPA=D31, SEQUENCER=D40,
//     INTERLOCK=D2 (in, dual), FootSW=D19 (in, LOW=TX), PTT232=D3 (in, HIGH=TX).
// Non-blocking 6-level state machine ported from k3ng OpenInterfaceSequencer():
//   level 0 -> 5 (SEQ on) -> 4 (PA on) -> 1/2/3 (PTT123 on)  with lead delays
//   level 1/2/3 -> 4 (PTT123 off) -> 5 (PA off) -> 0 (SEQ off) with tail delays
// PTT output 1/2/3 selected by PttOutByCivMode[CivMode], fallback PTTmodeDefault
// when !CivValid or mode unknown.
// InterlockEnable=1: D2 is safety interlock from PA — toggle on change; when
// active during TX, abort the sequencer immediately (drop all PTT/PA/SEQ LOW).
// InterlockEnable=0: D2 is a PTT-in (LOW=TX); sets pttExternal=true so the
// sequencer stops at level 4 (PA up, PTT123 skipped — external system drives
// PTT line into the radio itself).

const int PTT_PINS[4] = {0, PTT1, PTT2, PTT3};  // 1-based index, [0] unused

byte          PttOutByCivMode[16];
byte          SequencerLevel       = 0;       // 0=off, 1/2/3=PTT123, 4=PA, 5=SEQ
bool          PttActive            = false;
bool          pttExternal          = false;
byte          ptt_interlock_active = 0;
byte          PttOut               = 3;        // current selected PTT output 1/2/3
unsigned long LastSeqChange        = 0;

static bool   pttFootSwLast        = true;     // HIGH at idle (pull-up)
static bool   pttPtt232Last        = false;    // LOW at idle
static int    pttInterlockLast     = -1;       // -1 = not yet sampled

byte ptt_output_for_civmode() {
  if (!CivValid || CivMode == 0xFF) return PTTmodeDefault;
  if (CivMode >= 16)                return PTTmodeDefault;
  byte v = PttOutByCivMode[CivMode];
  if (v < 1 || v > 3)               return PTTmodeDefault;
  return v;
}

void ptt_request(bool tx) {
  if (tx == PttActive) return;
  PttActive = tx;
  if (tx) PttOut = ptt_output_for_civmode();
  LastSeqChange = millis();
}

static void ptt_abort_all() {
  digitalWrite(SEQUENCER, LOW);
  digitalWrite(PTTPA, LOW);
  digitalWrite(PTT1, LOW);
  digitalWrite(PTT2, LOW);
  digitalWrite(PTT3, LOW);
  SequencerLevel = 0;
  PttActive = false;
}

// Non-blocking port of k3ng OpenInterfaceSequencer(). State transitions are
// gated by millis() deltas instead of delay().
static void ptt_sequencer_step() {
  if (ptt_interlock_active == 1) {
    if (SequencerLevel != 0) ptt_abort_all();
    return;
  }
  unsigned long now = millis();
  switch (SequencerLevel) {
    case 0:
      if (PttActive) {
        digitalWrite(SEQUENCER, HIGH);
        LastSeqChange = now;
        SequencerLevel = 5;
        Serial.println(F("[PTT SEQ-H]"));
      }
      break;
    case 5:  // SEQ up — wait SEQUENCERlead, then PA up; or on release wait SEQUENCERtail then SEQ down
      if (PttActive && (now - LastSeqChange) >= (unsigned long)SEQUENCERlead) {
        digitalWrite(PTTPA, HIGH);
        LastSeqChange = now;
        SequencerLevel = 4;
        Serial.println(F("[PTT PA-H]"));
      } else if (!PttActive && (now - LastSeqChange) >= (unsigned long)SEQUENCERtail) {
        digitalWrite(SEQUENCER, LOW);
        LastSeqChange = now;
        SequencerLevel = 0;
        Serial.println(F("[PTT SEQ-L]"));
      }
      break;
    case 4:  // PA up — wait PAlead then PTT123 up (skipped if pttExternal); or on release wait PAtail then PA down
      if (PttActive && !pttExternal && (now - LastSeqChange) >= (unsigned long)PAlead) {
        digitalWrite(PTT_PINS[PttOut], HIGH);
        LastSeqChange = now;
        SequencerLevel = PttOut;
        Serial.print(F("[PTT")); Serial.print(PttOut); Serial.println(F("-H]"));
      } else if (!PttActive && (now - LastSeqChange) >= (unsigned long)PAtail) {
        digitalWrite(PTTPA, LOW);
        LastSeqChange = now;
        SequencerLevel = 5;
        Serial.println(F("[PTT PA-L]"));
      }
      break;
    case 1:
    case 2:
    case 3:
      if (!PttActive && (now - LastSeqChange) >= (unsigned long)PTTtail) {
        digitalWrite(PTT_PINS[SequencerLevel], LOW);
        LastSeqChange = now;
        Serial.print(F("[PTT")); Serial.print(SequencerLevel); Serial.println(F("-L]"));
        SequencerLevel = 4;
      } else if (PttActive) {
        // PTT continues — refresh LastSeqChange so next tail starts only on release
        LastSeqChange = now;
      }
      break;
  }
}

static void ptt_inputs_poll() {
  // FootSW: LOW = TX
  bool footSwNow = (digitalRead(FootSW) == HIGH);  // HIGH means released
  if (footSwNow != pttFootSwLast) {
    pttFootSwLast = footSwNow;
    ptt_request(!footSwNow);   // released==true -> tx false; pressed -> tx true
  }

  // PTT232: HIGH = TX
  bool ptt232Now = (digitalRead(PTT232) == HIGH);
  if (ptt232Now != pttPtt232Last) {
    pttPtt232Last = ptt232Now;
    ptt_request(ptt232Now);
  }

  // INTERLOCK: dual behaviour per InterlockEnable
  int interlockNow = digitalRead(INTERLOCK);
  if (pttInterlockLast == -1) {
    pttInterlockLast = interlockNow;
  } else if (interlockNow != pttInterlockLast) {
    pttInterlockLast = interlockNow;
    if (InterlockEnable) {
      // safety interlock from PA: edge → toggle abort flag
      ptt_interlock_active ^= 1;
      pttExternal = false;
      Serial.print(F("[Interlock="));
      Serial.print(ptt_interlock_active);
      Serial.println(']');
    } else {
      // PTT-in: LOW = TX; pttExternal skips PTT123 in sequencer
      bool ilTx = (interlockNow == LOW);
      pttExternal = ilTx;
      ptt_interlock_active = 0;
      ptt_request(ilTx);
    }
  }
}

void ptt_setup() {
  Serial.println(F("--- PTT SEQUENCER ---"));
  pinMode(PTT1, OUTPUT);      digitalWrite(PTT1, LOW);
  pinMode(PTT2, OUTPUT);      digitalWrite(PTT2, LOW);
  pinMode(PTT3, OUTPUT);      digitalWrite(PTT3, LOW);
  pinMode(PTTPA, OUTPUT);     digitalWrite(PTTPA, LOW);
  pinMode(SEQUENCER, OUTPUT); digitalWrite(SEQUENCER, LOW);
  pinMode(INTERLOCK, INPUT_PULLUP);
  pinMode(FootSW,    INPUT_PULLUP);
  pinMode(PTT232,    INPUT);

  for (uint8_t i = 0; i < 16; i++) PttOutByCivMode[i] = 0xFF;
  PttOutByCivMode[0x00] = PTTmodeLSB;
  PttOutByCivMode[0x01] = PTTmodeUSB;
  PttOutByCivMode[0x02] = PTTmodeAM;
  PttOutByCivMode[0x03] = PTTmodeCW;
  PttOutByCivMode[0x04] = PTTmodeRTTY;
  PttOutByCivMode[0x05] = PTTmodeFM;
  PttOutByCivMode[0x07] = PTTmodeCWR;
  PttOutByCivMode[0x08] = PTTmodeRTR;

  PttOut = PTTmodeDefault;
  SequencerLevel = 0;
  PttActive = false;
}

void ptt_loop() {
  ptt_inputs_poll();
  ptt_sequencer_step();
}
// =================== END PTT SEQUENCER ===================


// ===================== KEYER (CW + RTTY) =====================
// HW: PADDLEL=D26 (dit), PADDLER=D28 (dah), INPUT_PULLUP, LOW=pressed.
//     CW1=D34 / CW2=D35 = CW key output (driven via PTT sequencer).
//     FSK=D23 = RTTY FSK key. Idle = HIGH-Z (pinMode INPUT, line released);
//     driven OUTPUT only during RTTY TX. The OI3 FSK keying circuit interprets
//     any active MCU drive (HIGH or LOW) as keyed, so floating the pin between
//     sessions is the only correct way to release. During TX: start=SPACE,
//     data LSB-first, stop=MARK 1.5 bits, then released by ptt_tail_service
//     after 7-dit silence. Polarity (KeyerFskReverseNow) flips MARK/SPACE
//     drive levels but does not affect the idle release behaviour.
//     TONE=D4 = sidetone (shared with LCD menu beep, no mutex).
//
// Ported from K3NG CW Keyer by Anthony Good K3NG
//   https://github.com/k3ng/k3ng_cw_keyer
//
// Behaviour:
// - Paddles always live: sidetone + ASCII decoder run regardless of mode.
// - CW / CWR (CivMode 0x03 / 0x07): paddle keys CW1/CW2 via sequencer;
//   /s-cw chars are played as morse through the same path. Paddle touch
//   flushes any pending /s-cw send buffer (paddle has priority).
// - RTTY / RTR (0x04 / 0x08): decoded paddle ASCII + /s-cw chars are
//   enqueued to the RTTY FSK state machine (non-blocking, millis() paced).
//   FSK pin clocks bits at KeyerRttyBaudNow Bd; sidetone of dits/dahs
//   continues for the operator's ear only.
// - Other modes (LSB/USB/AM/FM, or !CivValid): sidetone + LCD echo only,
//   no TX path activated.
// - PTT timing: first element (sidetone + key) defers until the sequencer
//   reaches SequencerLevel == PttOut. K3NG's own PTTlead/PTTtail removed —
//   the sequencer alone owns PTT timing.
// - WPM editable via MEM1 (down) / MEM2 (up) when LCD is in PINNED state
//   (handled in LCD RUNTIME — this module exposes keyer_set_wpm()).
// - EEPROM addrs 1..6 persist user changes (5 s debounced flush).
//   On boot EEPROM > cfg (cfg only used if EEPROM byte == 0xFF).
//
// Public API for the rest of the firmware:
//   keyer_setup()           - init pins, load EEPROM, register state
//   keyer_loop()            - drain TrxNet, run paddle/buffer/RTTY state
//   keyer_enqueue_char(c)   - add a single char to the send queue
//   keyer_set_wpm(w)        - change WPM, mark EEPROM dirty (debounced)
//   keyer_abort()           - flush buffer + drop PTT immediately

#include <EEPROM.h>

// --- EEPROM map (addr 0 owned by LCD RUNTIME) ---
const uint8_t KEYER_EEPROM_WPM_ADDR        = 1;
const uint8_t KEYER_EEPROM_MODE_ADDR       = 2;   // 0=A, 1=B
const uint8_t KEYER_EEPROM_REVERSE_ADDR    = 3;   // 0=normal, 1=swapped
const uint8_t KEYER_EEPROM_SIDETONE_LO     = 4;
const uint8_t KEYER_EEPROM_SIDETONE_HI     = 5;
const uint8_t KEYER_EEPROM_RTTYBAUD_ADDR   = 6;
const uint8_t KEYER_EEPROM_FSKREV_ADDR     = 7;   // 0=normal, 1=reverse polarity
const unsigned long KEYER_EEPROM_DEBOUNCE_MS = 5000;

// --- Runtime state (loaded from EEPROM, falls back to cfg defaults) ---
uint8_t  KeyerWpm           = 28;     // active WPM
char     KeyerActiveMode    = 'B';    // active iambic mode 'A' or 'B'
bool     KeyerPaddleSwap    = false;  // current paddle reverse state
uint16_t KeyerSidetoneNow   = 600;    // 0 = sidetone off
uint8_t  KeyerRttyBaudNow   = 45;     // 45/50/75/100
bool     KeyerFskReverseNow = false;  // FSK polarity flip (runtime)

// --- Keyer FSM ---
enum KeyerState : uint8_t {
  KEYER_IDLE     = 0,
  KEYER_WAIT_SEQ,        // PTT requested, waiting for SequencerLevel == PttOut
  KEYER_CW_KEYING,       // playing a CW element (paddle- or buffer-driven)
  KEYER_RTTY_TX,         // RTTY FSK bits being clocked out
};
static KeyerState keyerState = KEYER_IDLE;

// --- CW send buffer (chars enqueued from TrxNet /s-cw or local source) ---
const uint8_t KEYER_SENDBUF_SIZE = 65;
static char    keyerSendBuf[KEYER_SENDBUF_SIZE];
static uint8_t keyerSendHead = 0;
static uint8_t keyerSendTail = 0;

// --- RTTY TX state machine (45.45 Bd default: ~22 ms per bit) ---
static uint8_t       keyerRttyCode      = 0;     // current Baudot 5-bit code
static uint8_t       keyerRttyBitIdx    = 0;     // 0=start, 1..5=data, 6=stop
static unsigned long keyerRttyNextBitMs = 0;
static unsigned long keyerRttyBitMs     = 22;    // 1000/45.45 ≈ 22

// --- Paddle decoder (k3ng-style: long number, 1=dit, 2=dah) ---
static long          keyerDecodeBuf     = 0;
static unsigned long keyerDecodeNextMs  = 0;
static bool          keyerSpaceSent     = true;

// --- EEPROM debounced flush ---
static unsigned long keyerEepromDirtyMs = 0;
static bool          keyerEepromDirty   = false;

// --- LCD row-1 TX text cursor (wrap mode) ---
static uint8_t       keyerLcdTxCol      = 0;

// --- TrxNet /s-cw drop-box (filled by onSetCw callback, drained in loop) ---
static char          trxPendingCW[KEYER_SENDBUF_SIZE];
static volatile bool trxCwPending       = false;
static volatile bool trxCwAbort         = false;

// --- Forward decls ---
void keyer_setup();
void keyer_loop();
void keyer_enqueue_char(char c);
void keyer_set_wpm(uint8_t w);
void keyer_abort();

// ----- TrxNet subscriber callback -----
// Spec sentinel for abort = 0x03 (ETX); 0xFF tolerated for legacy k3ng peers.
void onSetCw(const char* /*from*/, const uint8_t* data, size_t len) {
  if (len == 1 && (data[0] == 0x03 || data[0] == 0xFF)) { trxCwAbort = true; return; }
  size_t n = (len < KEYER_SENDBUF_SIZE - 1) ? len : (KEYER_SENDBUF_SIZE - 1);
  memcpy(trxPendingCW, data, n);
  trxPendingCW[n] = '\0';
  trxCwPending = true;
}

// ----- LCD row-1 writer (wrap mode, eats trailing spaces) -----
// Called after each element/char has actually been transmitted, so the LCD
// is a truthful log of what went on the air.
static void keyer_lcd_putc(char c) {
  // Space at end of row gets eaten — no wrap on whitespace
  if (c == ' ' && keyerLcdTxCol >= 16) return;
  if (keyerLcdTxCol >= 16) {
    lcd.setCursor(0, 1);
    lcd.print(F("                "));
    keyerLcdTxCol = 0;
  }
  lcd.setCursor(keyerLcdTxCol, 1);
  lcd.write((uint8_t)c);
  keyerLcdTxCol++;
}

// ----- Morse encode table (uppercase ASCII → "." dit / "-" dah pattern) -----
static const char* keyer_morse_for(char c) {
  if (c >= 'a' && c <= 'z') c -= 32;
  switch (c) {
    case 'A': return ".-";       case 'B': return "-...";
    case 'C': return "-.-.";     case 'D': return "-..";
    case 'E': return ".";        case 'F': return "..-.";
    case 'G': return "--.";      case 'H': return "....";
    case 'I': return "..";       case 'J': return ".---";
    case 'K': return "-.-";      case 'L': return ".-..";
    case 'M': return "--";       case 'N': return "-.";
    case 'O': return "---";      case 'P': return ".--.";
    case 'Q': return "--.-";     case 'R': return ".-.";
    case 'S': return "...";      case 'T': return "-";
    case 'U': return "..-";      case 'V': return "...-";
    case 'W': return ".--";      case 'X': return "-..-";
    case 'Y': return "-.--";     case 'Z': return "--..";
    case '0': return "-----";    case '1': return ".----";
    case '2': return "..---";    case '3': return "...--";
    case '4': return "....-";    case '5': return ".....";
    case '6': return "-....";    case '7': return "--...";
    case '8': return "---..";    case '9': return "----.";
    case '.': return ".-.-.-";   case ',': return "--..--";
    case '?': return "..--..";   case '/': return "-..-.";
    case '=': return "-...-";    // BT (break)
    case '+': return ".-.-.";    // AR (end of message)
    case '(': return "-.--.";    // KN (invitation to specific station)
    case ')': return "-.--.-";
    case '&': return "...-.-";   // SK (end of contact)
    case '*': return "-...-.-";  // BK (break-in)
    case '@': return ".--.-.";   case ':': return "---...";
    case '-': return "-....-";   case ' ': return " ";   // sentinel for wordspace
    default:  return NULL;
  }
}

// ----- Paddle-decoder reverse table (k3ng convert_cw_number_to_ascii + SK) -----
// 1 = dit, 2 = dah; accumulated as decimal digits.
static char keyer_decode_morse(long n) {
  switch (n) {
    case 12: return 'A';        case 2111: return 'B';
    case 2121: return 'C';      case 211: return 'D';
    case 1: return 'E';         case 1121: return 'F';
    case 221: return 'G';       case 1111: return 'H';
    case 11: return 'I';        case 1222: return 'J';
    case 212: return 'K';       case 1211: return 'L';
    case 22: return 'M';        case 21: return 'N';
    case 222: return 'O';       case 1221: return 'P';
    case 2212: return 'Q';      case 121: return 'R';
    case 111: return 'S';       case 2: return 'T';
    case 112: return 'U';       case 1112: return 'V';
    case 122: return 'W';       case 2112: return 'X';
    case 2122: return 'Y';      case 2211: return 'Z';
    case 22222: return '0';     case 12222: return '1';
    case 11222: return '2';     case 11122: return '3';
    case 11112: return '4';     case 11111: return '5';
    case 21111: return '6';     case 22111: return '7';
    case 22211: return '8';     case 22221: return '9';
    case 112211: return '?';    case 21121: return '/';
    case 221122: return ',';    case 121212: return '.';
    case 122121: return '@';    case 21112:  return '=';   // BT
    case 12121: return '+';     // AR
    case 21221: return '(';     // KN
    case 2111212: return '*';   // BK
    case 111212: return '&';    // SK (added per design decision)
    default:    return '*';     // unknown → asterisk (k3ng convention)
  }
}

// ----- Baudot (ITA2) encode table for RTTY FSK output -----
// Returns 5-bit code or -1 if not encodable. *needFigs set true if char
// requires the FIGS shift (digits, punctuation), false for letters/space/CR/LF.
static int8_t keyer_baudot_for(char c, bool* needFigs) {
  if (c >= 'a' && c <= 'z') c -= 32;
  *needFigs = false;
  switch (c) {
    case 'A': return 0x03;   case 'B': return 0x19;
    case 'C': return 0x0E;   case 'D': return 0x09;
    case 'E': return 0x01;   case 'F': return 0x0D;
    case 'G': return 0x1A;   case 'H': return 0x14;
    case 'I': return 0x06;   case 'J': return 0x0B;
    case 'K': return 0x0F;   case 'L': return 0x12;
    case 'M': return 0x1C;   case 'N': return 0x0C;
    case 'O': return 0x18;   case 'P': return 0x16;
    case 'Q': return 0x17;   case 'R': return 0x0A;
    case 'S': return 0x05;   case 'T': return 0x10;
    case 'U': return 0x07;   case 'V': return 0x1E;
    case 'W': return 0x13;   case 'X': return 0x1D;
    case 'Y': return 0x15;   case 'Z': return 0x11;
    case ' ':  return 0x04;  case '\r': return 0x08;
    case '\n': return 0x02;
    case '0':  *needFigs = true; return 0x16;
    case '1':  *needFigs = true; return 0x17;
    case '2':  *needFigs = true; return 0x13;
    case '3':  *needFigs = true; return 0x01;
    case '4':  *needFigs = true; return 0x0A;
    case '5':  *needFigs = true; return 0x10;
    case '6':  *needFigs = true; return 0x15;
    case '7':  *needFigs = true; return 0x07;
    case '8':  *needFigs = true; return 0x06;
    case '9':  *needFigs = true; return 0x18;
    case '-':  *needFigs = true; return 0x03;
    case '?':  *needFigs = true; return 0x19;
    case ':':  *needFigs = true; return 0x0E;
    case '(':  *needFigs = true; return 0x0F;
    case ')':  *needFigs = true; return 0x12;
    case '.':  *needFigs = true; return 0x1C;
    case ',':  *needFigs = true; return 0x0C;
    case '/':  *needFigs = true; return 0x1D;
    case '=':  *needFigs = true; return 0x1E;
    case '\'': *needFigs = true; return 0x05;
    default: return -1;
  }
}
static const uint8_t BAUDOT_LTRS = 0x1F;
static const uint8_t BAUDOT_FIGS = 0x1B;
static bool keyerRttyShiftFigs = false;

// ----- Paddle state (k3ng-style buffers + iambic A flag) -----
static byte    keyerDitBuf       = 0;
static byte    keyerDahBuf       = 0;
static byte    keyerBeingSent    = 0;   // 0=none, 1=dit, 2=dah (current element)
static byte    keyerLastSent     = 1;   // last element played (for squeeze alternation)
static byte    keyerIambicAFlag  = 0;   // iambic A: tracks simultaneous press during element
static unsigned long keyerLastTxMs = 0; // for PTT hang-time release
enum KeyerSending : uint8_t { KEYS_NONE = 0, KEYS_MANUAL = 1, KEYS_AUTO = 2 };
static KeyerSending keyerSendingMode = KEYS_NONE;
static bool    keyerAutoInterrupted = false;

// ----- FSK polarity & line release helpers -----
// MARK and SPACE bit levels flip together when KeyerFskReverseNow is true.
// Between RTTY sessions the line is RELEASED (pinMode INPUT, high-Z) so any
// external pull-up or opto-driver pull network sets the actual line level —
// driving the pin in either state from the MCU would keep the FSK keying
// circuit actively switched (the OI3 hardware behaviour).
static inline int keyer_fsk_mark()  { return KeyerFskReverseNow ? LOW  : HIGH; }
static inline int keyer_fsk_space() { return KeyerFskReverseNow ? HIGH : LOW;  }
static inline void keyer_fsk_release() { pinMode(FSK, INPUT); }
static inline void keyer_fsk_drive_space() { pinMode(FSK, OUTPUT); digitalWrite(FSK, keyer_fsk_space()); }
static inline void keyer_fsk_drive_mark()  { pinMode(FSK, OUTPUT); digitalWrite(FSK, keyer_fsk_mark());  }

// ----- Paddle pin reads (respects KeyerPaddleSwap) -----
// Default (KeyerPaddleSwap=false): dit = PADDLER (D28), dah = PADDLEL (D26).
// KeyerPaddleSwap=true swaps to dit = PADDLEL, dah = PADDLER.
static inline bool keyer_paddle_dit_pressed() {
  return digitalRead(KeyerPaddleSwap ? PADDLEL : PADDLER) == LOW;
}
static inline bool keyer_paddle_dah_pressed() {
  return digitalRead(KeyerPaddleSwap ? PADDLER : PADDLEL) == LOW;
}

// ----- Paddle scan -----
// During an element, read only the OPPOSITE paddle (squeeze buffering).
// When idle, read both. For iambic A, track simultaneous press during element.
static void keyer_check_paddles() {
  if (keyerBeingSent == 1) {
    if (keyer_paddle_dah_pressed()) keyerDahBuf = 1;
  } else if (keyerBeingSent == 2) {
    if (keyer_paddle_dit_pressed()) keyerDitBuf = 1;
  } else {
    if (keyer_paddle_dit_pressed()) keyerDitBuf = 1;
    if (keyer_paddle_dah_pressed()) keyerDahBuf = 1;
  }
  if (KeyerActiveMode == 'A' && (keyerBeingSent == 1 || keyerBeingSent == 2)
      && keyer_paddle_dit_pressed() && keyer_paddle_dah_pressed()) {
    keyerIambicAFlag = 1;
  }
}

// ----- Wait with module pumping & paddle scanning -----
// CW elements and RTTY bits block for tens to hundreds of ms — pump other
// module loops here so TrxNet CON ACKs, CI-V polling, Ethernet keepalive,
// and the PTT sequencer all keep advancing. LCD is skipped to avoid display
// re-entrancy during element timing.
static void keyer_wait_us_pump(unsigned long us) {
  unsigned long start = micros();
  while ((micros() - start) < us) {
    ethernet_loop();
    trxnet_loop();
    civ_loop();
    ptt_loop();
    keyer_check_paddles();
    if (trxCwAbort) break;
    if (keyerSendingMode == KEYS_AUTO &&
        (keyer_paddle_dit_pressed() || keyer_paddle_dah_pressed()
         || keyerDitBuf || keyerDahBuf)) {
      // Paddle hit during auto-sending → cut out (operator takes over)
      keyerAutoInterrupted = true;
      break;
    }
  }
}

// ----- Sequencer gate: request PTT, then block until SequencerLevel == PttOut -----
// This is the "WAITING_FOR_SEQUENCER" state from the design: first element
// (sidetone + key) only fires once the sequencer has brought up SEQ + PA + PTT123.
// Returns false on abort/timeout.
static bool keyer_begin_tx_wait_sequencer() {
  ptt_request(true);
  unsigned long t0 = millis();
  while (SequencerLevel != PttOut) {
    ethernet_loop();
    trxnet_loop();
    civ_loop();
    ptt_loop();
    if (trxCwAbort) return false;
    if ((millis() - t0) > 2000) return false;  // safety
  }
  return true;
}

// ----- Emit one CW element (dit or dah) -----
// type: 1=dit, 2=dah
// keyHw: true → drive CW1/CW2 pin (CW/CWR mode); false → sidetone only
// (RTTY/none — paddle is just a tap-along, no antenna keying)
static void keyer_emit_element(byte type, bool keyHw) {
  unsigned long dit_ms = 1200UL / KeyerWpm;
  unsigned long element_ms = (type == 1) ? dit_ms : (3UL * dit_ms);

  keyerBeingSent = type;

  if (keyHw) {
    digitalWrite(CW1, HIGH);
    digitalWrite(CW2, HIGH);
  }
  if (KeyerSidetoneNow != 0) tone(TONE, KeyerSidetoneNow);

  keyer_wait_us_pump(element_ms * 1000UL);

  if (keyHw) {
    digitalWrite(CW1, LOW);
    digitalWrite(CW2, LOW);
  }
  if (KeyerSidetoneNow != 0) noTone(TONE);

  // Inter-element gap (1 dit time of silence)
  keyerBeingSent = 0;
  keyer_wait_us_pump(dit_ms * 1000UL);

  // Accumulate into paddle decoder only for live paddle elements
  if (keyerSendingMode == KEYS_MANUAL) {
    keyerDecodeBuf = (keyerDecodeBuf * 10) + (long)type;
    // 2 more dits ⇒ total 3-dit silence from element end ⇒ letterspace
    keyerDecodeNextMs = millis() + (2UL * dit_ms);
    keyerSpaceSent = false;
  }

  keyerLastSent = type;
  keyerLastTxMs = millis();
}

// ----- Auto-send one CW char (from TrxNet send buffer) -----
static void keyer_emit_cw_char(char c) {
  if (c == ' ') {
    // Wordspace: 4 more dits (we've already let inter-char letterspace pass).
    unsigned long dit_ms = 1200UL / KeyerWpm;
    keyerSendingMode = KEYS_AUTO;
    keyerAutoInterrupted = false;
    keyer_wait_us_pump(4UL * dit_ms * 1000UL);
    keyerSendingMode = KEYS_NONE;
    if (!keyerAutoInterrupted) keyer_lcd_putc(' ');
    return;
  }
  const char* pattern = keyer_morse_for(c);
  if (!pattern || *pattern == 0) return;
  if (!keyer_begin_tx_wait_sequencer()) return;

  keyerSendingMode = KEYS_AUTO;
  keyerAutoInterrupted = false;
  for (const char* p = pattern; *p; p++) {
    keyer_emit_element((*p == '.') ? 1 : 2, true);
    if (trxCwAbort || keyerAutoInterrupted) break;
  }
  keyerSendingMode = KEYS_NONE;

  if (!trxCwAbort && !keyerAutoInterrupted) {
    // Letterspace = 2 more dit times beyond the inter-element gap already paid
    unsigned long dit_ms = 1200UL / KeyerWpm;
    keyer_wait_us_pump(2UL * dit_ms * 1000UL);
    keyer_lcd_putc(c);
  }
}

// ----- Clock a single 5-bit Baudot code onto the FSK pin -----
// Frame: start (SPACE/LOW) + 5 data LSB-first + 1.5 stop (MARK/HIGH).
static void keyer_rtty_send_5bit(uint8_t code) {
  unsigned long bit_us = keyerRttyBitMs * 1000UL;
  pinMode(FSK, OUTPUT);                         // ensure driving (between chars during a session)
  digitalWrite(FSK, keyer_fsk_space());         // start bit (SPACE)
  keyer_wait_us_pump(bit_us);
  for (uint8_t i = 0; i < 5; i++) {
    digitalWrite(FSK, (code & 0x01) ? keyer_fsk_mark() : keyer_fsk_space());
    code >>= 1;
    keyer_wait_us_pump(bit_us);
  }
  digitalWrite(FSK, keyer_fsk_mark());          // stop = MARK
  keyer_wait_us_pump((bit_us * 3UL) / 2UL);     // 1.5 stop bits
  // Pin stays driven at MARK after stop; ptt_tail_service releases it later.
}

// ----- Auto-send one RTTY char (with FIGS/LTRS shift tracking) -----
static void keyer_emit_rtty_char(char c) {
  bool needFigs;
  int8_t code = keyer_baudot_for(c, &needFigs);
  if (code < 0) return;
  if (!keyer_begin_tx_wait_sequencer()) return;

  if (needFigs != keyerRttyShiftFigs) {
    keyer_rtty_send_5bit(needFigs ? BAUDOT_FIGS : BAUDOT_LTRS);
    keyerRttyShiftFigs = needFigs;
  }
  keyer_rtty_send_5bit((uint8_t)code);
  keyer_lcd_putc(c);
  keyerLastTxMs = millis();
}

// ----- Route a decoded paddle char based on current CivMode -----
static void keyer_decoder_commit_and_route(char decoded) {
  bool isCw   = (CivValid && (CivMode == 0x03 || CivMode == 0x07));
  bool isRtty = (CivValid && (CivMode == 0x04 || CivMode == 0x08));
  if (isCw) {
    // Char already went out as morse via the paddle — just echo to LCD
    keyer_lcd_putc(decoded);
  } else if (isRtty) {
    // Paddle was sidetone-only; transmit decoded char as RTTY FSK now
    keyer_emit_rtty_char(decoded);  // handles its own LCD echo
  } else {
    // No TX mode (SSB/AM/FM, or !CivValid) — LCD echo only
    keyer_lcd_putc(decoded);
  }
}

// ----- Decoder service: commit accumulated dit/dah sequence to ASCII -----
static void keyer_decoder_service() {
  unsigned long dit_ms = 1200UL / KeyerWpm;
  if (keyerDecodeBuf != 0 && millis() >= keyerDecodeNextMs) {
    char decoded = keyer_decode_morse(keyerDecodeBuf);
    keyerDecodeBuf = 0;
    keyer_decoder_commit_and_route(decoded);
    // After commit, schedule the wordspace timeout (4 more dits ⇒ 7 total from element end)
    keyerDecodeNextMs = millis() + (4UL * dit_ms);
    keyerSpaceSent = false;
  } else if (keyerDecodeBuf == 0 && !keyerSpaceSent
             && millis() >= keyerDecodeNextMs) {
    // Route through commit_and_route so RTTY mode actually sends Baudot space.
    // In CW mode this just echoes ' ' to LCD (the on-air gap is already there).
    keyer_decoder_commit_and_route(' ');
    keyerSpaceSent = true;
  }
}

// ----- Paddle FSM step (drives one element per call when paddle is pressed) -----
static void keyer_paddle_step() {
  keyer_check_paddles();
  if (!keyerDitBuf && !keyerDahBuf) return;

  // Paddle priority: drop any pending TrxNet send buffer
  keyerSendHead = keyerSendTail = 0;

  // Decide next element: squeeze alternates, single paddle is its own type
  byte t;
  if (keyerDitBuf && keyerDahBuf) t = (keyerLastSent == 1) ? 2 : 1;
  else if (keyerDitBuf)           t = 1;
  else                            t = 2;
  if (t == 1) keyerDitBuf = 0;
  else        keyerDahBuf = 0;

  // CW/CWR → drive CW pin via sequencer; other modes → sidetone only
  bool keyHw = (CivValid && (CivMode == 0x03 || CivMode == 0x07));
  if (keyHw) {
    if (!keyer_begin_tx_wait_sequencer()) return;
  }

  keyerSendingMode = KEYS_MANUAL;
  keyer_emit_element(t, keyHw);
  keyerSendingMode = KEYS_NONE;

  // Iambic A: if both paddles were pressed during element and both released
  // by element end, drop the remaining buffered element (no "extra" element)
  if (KeyerActiveMode == 'A' && keyerIambicAFlag
      && !keyer_paddle_dit_pressed() && !keyer_paddle_dah_pressed()) {
    keyerIambicAFlag = 0;
    keyerDitBuf = 0;
    keyerDahBuf = 0;
  }
}

// ----- TrxNet send buffer drain (one char per call, mode-dispatched) -----
static void keyer_send_buffer_drain() {
  if (keyerSendHead == keyerSendTail) return;
  // Paddle in flight or pending → defer (paddle has priority)
  if (keyerDitBuf || keyerDahBuf) return;
  if (keyer_paddle_dit_pressed() || keyer_paddle_dah_pressed()) return;

  char c = keyerSendBuf[keyerSendTail];
  keyerSendTail = (keyerSendTail + 1) % KEYER_SENDBUF_SIZE;

  if (!CivValid) { keyer_lcd_putc(c); return; }
  if (CivMode == 0x03 || CivMode == 0x07)      keyer_emit_cw_char(c);
  else if (CivMode == 0x04 || CivMode == 0x08) keyer_emit_rtty_char(c);
  else                                          keyer_lcd_putc(c);
}

// ----- PTT hang-time release -----
// Drop PTT after one full wordspace of silence with nothing pending. This keeps
// PTT up between consecutive chars / paddle elements but releases promptly when
// transmission stops.
static void keyer_ptt_tail_service() {
  if (!PttActive) return;
  if (keyerSendHead != keyerSendTail) return;
  if (keyerDitBuf || keyerDahBuf) return;
  if (keyer_paddle_dit_pressed() || keyer_paddle_dah_pressed()) return;
  unsigned long dit_ms = 1200UL / KeyerWpm;
  if ((millis() - keyerLastTxMs) >= (7UL * dit_ms)) {
    keyer_fsk_release();    // float FSK line — no continuous drive between sessions
    ptt_request(false);
  }
}

// ----- Public API -----

void keyer_enqueue_char(char c) {
  uint8_t next = (keyerSendHead + 1) % KEYER_SENDBUF_SIZE;
  if (next == keyerSendTail) return;   // buffer full, drop char
  keyerSendBuf[keyerSendHead] = c;
  keyerSendHead = next;
}

void keyer_set_wpm(uint8_t w) {
  if (w < 5)  w = 5;
  if (w > 50) w = 50;
  if (w == KeyerWpm) return;
  KeyerWpm = w;
  keyerEepromDirty = true;
  keyerEepromDirtyMs = millis();
}

void keyer_abort() {
  keyerSendHead = keyerSendTail = 0;
  keyerDitBuf = keyerDahBuf = 0;
  keyerIambicAFlag = 0;
  keyerDecodeBuf = 0;
  keyerSpaceSent = true;
  keyerBeingSent = 0;
  keyerSendingMode = KEYS_NONE;
  keyerRttyShiftFigs = false;     // next RTTY TX starts fresh (next FIGS char will re-shift)
  digitalWrite(CW1, LOW);
  digitalWrite(CW2, LOW);
  keyer_fsk_release();          // float the line (high-Z)
  noTone(TONE);
  ptt_request(false);
  keyerState = KEYER_IDLE;
}

// ----- EEPROM helpers -----

static void keyer_eeprom_load() {
  uint8_t e;

  e = EEPROM.read(KEYER_EEPROM_WPM_ADDR);
  if (e == 0xFF) KeyerWpm = KeyerDefaultWpm;
  else           KeyerWpm = (e < 5) ? 5 : (e > 50 ? 50 : e);

  e = EEPROM.read(KEYER_EEPROM_MODE_ADDR);
  if (e == 0xFF) KeyerActiveMode = KeyerMode;
  else           KeyerActiveMode = (e == 0) ? 'A' : 'B';

  e = EEPROM.read(KEYER_EEPROM_REVERSE_ADDR);
  if (e == 0xFF) KeyerPaddleSwap = KeyerPaddleReverse;
  else           KeyerPaddleSwap = (e != 0);

  uint16_t s = (uint16_t)EEPROM.read(KEYER_EEPROM_SIDETONE_LO)
             | ((uint16_t)EEPROM.read(KEYER_EEPROM_SIDETONE_HI) << 8);
  if (s == 0xFFFF) KeyerSidetoneNow = KeyerSidetoneHz;
  else             KeyerSidetoneNow = s;

  e = EEPROM.read(KEYER_EEPROM_RTTYBAUD_ADDR);
  if (e == 0xFF) KeyerRttyBaudNow = KeyerRttyBaud;
  else if (e == 45 || e == 50 || e == 75 || e == 100) KeyerRttyBaudNow = e;
  else KeyerRttyBaudNow = 45;

  e = EEPROM.read(KEYER_EEPROM_FSKREV_ADDR);
  if (e == 0xFF) KeyerFskReverseNow = KeyerFskReverse;
  else           KeyerFskReverseNow = (e != 0);

  // Pre-compute bit period; 45 ⇒ 45.45 Bd ⇒ 22 ms
  keyerRttyBitMs = (KeyerRttyBaudNow == 45) ? 22UL : (1000UL / KeyerRttyBaudNow);
}

static void keyer_eeprom_flush_if_dirty() {
  if (!keyerEepromDirty) return;
  if ((millis() - keyerEepromDirtyMs) < KEYER_EEPROM_DEBOUNCE_MS) return;
  EEPROM.update(KEYER_EEPROM_WPM_ADDR,      KeyerWpm);
  EEPROM.update(KEYER_EEPROM_MODE_ADDR,     (KeyerActiveMode == 'A') ? 0 : 1);
  EEPROM.update(KEYER_EEPROM_REVERSE_ADDR,  KeyerPaddleSwap ? 1 : 0);
  EEPROM.update(KEYER_EEPROM_SIDETONE_LO,   (uint8_t)(KeyerSidetoneNow & 0xFF));
  EEPROM.update(KEYER_EEPROM_SIDETONE_HI,   (uint8_t)(KeyerSidetoneNow >> 8));
  EEPROM.update(KEYER_EEPROM_RTTYBAUD_ADDR, KeyerRttyBaudNow);
  EEPROM.update(KEYER_EEPROM_FSKREV_ADDR,   KeyerFskReverseNow ? 1 : 0);
  keyerEepromDirty = false;
}

// ----- Setup / Loop -----

void keyer_setup() {
  Serial.println(F("--- KEYER ---"));
  pinMode(PADDLEL, INPUT_PULLUP);
  pinMode(PADDLER, INPUT_PULLUP);
  pinMode(CW1, OUTPUT); digitalWrite(CW1, LOW);
  pinMode(CW2, OUTPUT); digitalWrite(CW2, LOW);
  keyer_eeprom_load();
  keyer_fsk_release();   // pinMode INPUT — high-Z; external pull network sets the line

  keyerState     = KEYER_IDLE;
  keyerSendHead  = keyerSendTail = 0;
  keyerLcdTxCol  = 0;
  keyerDecodeBuf = 0;
  keyerSpaceSent = true;

  Serial.print(F("Keyer wpm=")); Serial.print(KeyerWpm);
  Serial.print(F(" mode="));     Serial.print(KeyerActiveMode);
  Serial.print(F(" pad="));      Serial.print(KeyerPaddleSwap ? 'R' : 'N');
  Serial.print(F(" side="));     Serial.print(KeyerSidetoneNow);
  Serial.print(F("Hz rtty="));   Serial.print(KeyerRttyBaudNow);
  Serial.print(F("Bd fsk="));    Serial.println(KeyerFskReverseNow ? 'R' : 'N');
}

void keyer_loop() {
  // Drain TrxNet drop-box (callbacks fired during trxnet_loop)
  if (trxCwAbort)   { trxCwAbort = false; keyer_abort(); }
  if (trxCwPending) {
    trxCwPending = false;
    for (uint8_t i = 0; trxPendingCW[i] && i < KEYER_SENDBUF_SIZE - 1; i++) {
      keyer_enqueue_char(trxPendingCW[i]);
    }
  }

  // Live paddle (priority) → drives one element per outer iteration
  keyer_paddle_step();
  // Commit decoded char on letterspace timeout, route by CivMode
  keyer_decoder_service();
  // TrxNet auto-send buffer drain (one char per iteration)
  keyer_send_buffer_drain();
  // Release PTT after hang-time
  keyer_ptt_tail_service();
  // EEPROM debounced flush
  keyer_eeprom_flush_if_dirty();
}
// =================== END KEYER ===================


// ===================== LCD RUNTIME =====================
// Row-0 renderer running after setup(). Layout: HHHHHHHHHHHH|MMM
// (12-wide value + separator + 3-char mode).
// FSM has three states (separator shows which one):
//   PINNED   sep '|'  — value display, M1/M2 = WPM down/up (live keyer)
//   BROWSING sep '<'  — navigate menu items via M1/SET (prev/next)
//   EDITING  sep '='  — edit current item's value via M1/M2 (decrement/increment)
// MODE button (D36) transitions:
//   short press in PINNED   → BROWSING (tone 400 Hz)
//   short press in BROWSING → PINNED  (tone 300 Hz, persist menu idx)
//   long press  in BROWSING → EDITING (if item editable, tone 800 Hz, save pre-value)
//   short press in EDITING  → BROWSING with save (tone 600 Hz, write EEPROM)
//   long press  in EDITING  → BROWSING with cancel (tone 200 Hz, restore pre-value)
// Menu items 0..8: read-only (call/FW/IP/baud/CIV/freq/peers/PTT).
// Menu items 9..13: editable keyer settings (WPM/Mode/Pad/Sidetone/Bd).
// PINNED WPM ±: M1/M2 with auto-repeat (500 ms hold → 5/s repeat). M1 = down,
// M2 = up. Each fires keyer_set_wpm() which marks the keyer EEPROM dirty for
// debounced flush.
// Pull model: every LCD_TICK_MS samples buttons + sources, redraws only the
// parts of row 0 that changed. Row 1 is owned by the KEYER block for TX text.
// Buttons on MEM (A1) voltage divider, left-to-right on the panel:
//   M1   tmp < 5
//   SET  45 < tmp < 130
//   M2   130 < tmp < 210
// Persistence: EEPROM[LCD_EEPROM_IDX_ADDR] = current menu index, written on
// BROWSING→PINNED transition. Keyer values use their own EEPROM addresses
// (see KEYER block) — saved by lcd_edit_save().
#include <EEPROM.h>

const uint8_t LCD_MENU_ITEM_COUNT = 15;
const uint8_t LCD_VALUE_WIDTH     = 12;
const uint8_t LCD_MODE_WIDTH      = 3;
const uint8_t LCD_SEP_COL         = 12;
const uint8_t LCD_MODE_COL        = 13;
const uint8_t LCD_EEPROM_IDX_ADDR = 0;

const unsigned long LCD_TICK_MS              = 30;
const unsigned long LCD_MODE_HOLD_MS         = 800;
const unsigned long LCD_BTN_REPEAT_DELAY_MS  = 500;   // hold this long → start repeating
const unsigned long LCD_BTN_REPEAT_RATE_MS   = 200;   // 5 Hz repeat
const int           LCD_M1_MAX       = 5;     // M1:  tmp < 5
const int           LCD_SET_MIN      = 45;    // SET: 45 < tmp < 130
const int           LCD_SET_MAX      = 130;
const int           LCD_M2_MIN       = 130;   // M2:  130 < tmp < 210
const int           LCD_M2_MAX       = 210;

enum LcdState : uint8_t { LCD_PINNED = 0, LCD_BROWSING = 1, LCD_EDITING = 2 };

static LcdState      lcdState        = LCD_PINNED;
static uint8_t       lcdMenuIdx      = 0;
static char          lcdLastValue[LCD_VALUE_WIDTH + 1] = {0};
static char          lcdLastMode[LCD_MODE_WIDTH + 1]   = {0};
static char          lcdLastSep      = 0;
static unsigned long lcdLastTickMs   = 0;
static unsigned long lcdMenuPressMs  = 0;
static bool          lcdMenuDown     = false;
static uint8_t       lcdBtnPrev      = 0;  // 0=none, 1=M1, 2=SET, 3=M2
static unsigned long lcdBtnPressMs   = 0;
static unsigned long lcdBtnRepeatNextMs = 0;
static int32_t       lcdEditPreValue = 0;  // pre-edit snapshot for cancel/restore

static uint8_t lcd_read_button() {
  int tmp = analogRead(MEM);
  if (tmp < LCD_M1_MAX)                         return 1;  // M1
  if (tmp > LCD_SET_MIN && tmp < LCD_SET_MAX)   return 2;  // SET
  if (tmp > LCD_M2_MIN  && tmp < LCD_M2_MAX)    return 3;  // M2
  return 0;
}

// Left-align src into out[width], pad with spaces on the right.
// Truncates from the right if src is longer than width.
static void lcd_pad_left_align(const char* src, char* out, uint8_t width) {
  uint8_t len = strlen(src);
  if (len > width) len = width;
  for (uint8_t i = 0; i < len; i++) out[i] = src[i];
  for (uint8_t i = len; i < width; i++) out[i] = ' ';
  out[width] = 0;
}

static void lcd_format_value(uint8_t idx, char* out) {
  char raw[LCD_VALUE_WIDTH + 1];
  raw[0] = 0;

  switch (idx) {
    case 0:  // mycall
      YOUR_CALL.toCharArray(raw, sizeof(raw));
      break;
    case 1:  // FW
      snprintf(raw, sizeof(raw), "FW %s", REV);
      break;
    case 2:  // IP-hi "192.168."
      if (!EthLinkStatus) { strcpy(raw, "no link"); break; }
      {
        IPAddress ip = Ethernet.localIP();
        snprintf(raw, sizeof(raw), "%u.%u.", ip[0], ip[1]);
      }
      break;
    case 3:  // IP-lo "1.220"
      if (!EthLinkStatus) { strcpy(raw, "no link"); break; }
      {
        IPAddress ip = Ethernet.localIP();
        snprintf(raw, sizeof(raw), "%u.%u", ip[2], ip[3]);
      }
      break;
    case 4:  // baud
      snprintf(raw, sizeof(raw), "baud %lu", SERBAUD2);
      break;
    case 5:  // CI-V addr
      snprintf(raw, sizeof(raw), "CI-V 0x%02X", CIV_ADRESS);
      break;
    case 6:  // freq: MM.MMM.D kHz (HF) or MMM.MMM.D kHz (VHF), 100 Hz resolution
      if (!CivValid || CivFreq == 0) { strcpy(raw, "---.---.- kHz"); break; }
      {
        unsigned long khz   = CivFreq / 1000UL;
        unsigned long tenth = (CivFreq / 100UL) % 10UL;
        unsigned long mhz   = khz / 1000UL;
        unsigned long subk  = khz % 1000UL;
        snprintf(raw, sizeof(raw), "%lu.%03lu.%lu kHz", mhz, subk, tenth);
      }
      break;
    case 7:  // peers
      if (NET_ID == 0x00 || !trxNetStarted) { strcpy(raw, "Peers off"); break; }
      snprintf(raw, sizeof(raw), "Peers %d", net.peerCount());
      break;
    case 8:  // current PTT output (1/2/3) or 'D' for default fallback
      {
        byte n = ptt_output_for_civmode();
        bool isDefault = (!CivValid || CivMode == 0xFF || CivMode >= 16
                          || PttOutByCivMode[CivMode] < 1
                          || PttOutByCivMode[CivMode] > 3);
        const char* prefix = (SequencerLevel != 0) ? "TX " : "";
        if (isDefault) snprintf(raw, sizeof(raw), "%sPTT D", prefix);
        else           snprintf(raw, sizeof(raw), "%sPTT %u", prefix, n);
      }
      break;
    case 9:   // KEYER WPM
      snprintf(raw, sizeof(raw), "WPM %u", KeyerWpm);
      break;
    case 10:  // KEYER iambic mode
      snprintf(raw, sizeof(raw), "Mode %c", KeyerActiveMode);
      break;
    case 11:  // KEYER paddle reverse
      snprintf(raw, sizeof(raw), "Pad %c", KeyerPaddleSwap ? 'R' : 'N');
      break;
    case 12:  // KEYER sidetone Hz (0 = off)
      if (KeyerSidetoneNow == 0) strcpy(raw, "Side off");
      else snprintf(raw, sizeof(raw), "Side %uHz", KeyerSidetoneNow);
      break;
    case 13:  // KEYER RTTY baud: 45 shows "45.45 Bd", others "NN Bd"
      if (KeyerRttyBaudNow == 45) strcpy(raw, "RTY 45.45 Bd");
      else snprintf(raw, sizeof(raw), "RTY %u Bd", KeyerRttyBaudNow);
      break;
    case 14:  // KEYER FSK polarity
      snprintf(raw, sizeof(raw), "FSK %c", KeyerFskReverseNow ? 'R' : 'N');
      break;
    default:
      strcpy(raw, "?");
      break;
  }
  lcd_pad_left_align(raw, out, LCD_VALUE_WIDTH);
}

static void lcd_format_mode(char* out) {
  if (!CivValid || CivMode == 0xFF) { strcpy(out, "---"); return; }
  switch (CivMode) {
    case 0x00: strcpy(out, "LSB"); break;
    case 0x01: strcpy(out, "USB"); break;
    case 0x02: strcpy(out, "AM "); break;
    case 0x03: strcpy(out, "CW "); break;
    case 0x04: strcpy(out, "RTY"); break;
    case 0x05: strcpy(out, "FM "); break;
    case 0x07: strcpy(out, "CWR"); break;
    case 0x08: strcpy(out, "RTR"); break;
    default:   snprintf(out, LCD_MODE_WIDTH + 1, "?%02X", CivMode); break;
  }
}

static void lcd_render(bool force) {
  char value[LCD_VALUE_WIDTH + 1];
  char mode[LCD_MODE_WIDTH + 1];
  char sep;
  switch (lcdState) {
    case LCD_BROWSING: sep = '<'; break;
    case LCD_EDITING:  sep = '='; break;
    default:           sep = '|'; break;
  }

  lcd_format_value(lcdMenuIdx, value);
  lcd_format_mode(mode);

  if (force || strcmp(value, lcdLastValue) != 0) {
    lcd.setCursor(0, 0);
    lcd.print(value);
    strcpy(lcdLastValue, value);
  }
  if (force || sep != lcdLastSep) {
    lcd.setCursor(LCD_SEP_COL, 0);
    lcd.print(sep);
    lcdLastSep = sep;
  }
  if (force || strcmp(mode, lcdLastMode) != 0) {
    lcd.setCursor(LCD_MODE_COL, 0);
    lcd.print(mode);
    strcpy(lcdLastMode, mode);
  }
}

void lcd_runtime_init() {
  Serial.println(F("--- LCD RUNTIME ---"));
  pinMode(MEM,  INPUT);
  pinMode(MENU, INPUT);
  pinMode(TONE, OUTPUT);

  uint8_t saved = EEPROM.read(LCD_EEPROM_IDX_ADDR);
  if (saved >= LCD_MENU_ITEM_COUNT) saved = 0;
  lcdMenuIdx = saved;
  lcdState   = LCD_PINNED;

  lcd.clear();
  lcd_render(true);
  lcdLastTickMs = millis();
}

// ----- Editable-item helpers (items 9..13) -----

static bool lcd_item_is_editable(uint8_t idx) {
  return idx >= 9 && idx <= 14;
}

static int32_t lcd_item_get_value(uint8_t idx) {
  switch (idx) {
    case 9:  return (int32_t)KeyerWpm;
    case 10: return (KeyerActiveMode == 'A') ? 0 : 1;
    case 11: return KeyerPaddleSwap ? 1 : 0;
    case 12: return (int32_t)KeyerSidetoneNow;
    case 13: return (int32_t)KeyerRttyBaudNow;
    case 14: return KeyerFskReverseNow ? 1 : 0;
    default: return 0;
  }
}

static void lcd_item_set_value(uint8_t idx, int32_t v) {
  switch (idx) {
    case 9:  KeyerWpm = (uint8_t)v; break;
    case 10: KeyerActiveMode = (v == 0) ? 'A' : 'B'; break;
    case 11: KeyerPaddleSwap = (v != 0); break;
    case 12: KeyerSidetoneNow = (uint16_t)v; break;
    case 13: KeyerRttyBaudNow = (uint8_t)v;
             keyerRttyBitMs = (KeyerRttyBaudNow == 45) ? 22UL : (1000UL / KeyerRttyBaudNow);
             break;
    case 14: KeyerFskReverseNow = (v != 0);
             // Polarity affects only the active drive levels during RTTY TX;
             // line stays released (high-Z) at idle regardless of polarity.
             keyer_fsk_release();
             break;
  }
}

// Step the editable value by `delta` (sign matters for ranged items; boolean
// items toggle regardless of delta sign; baud list cycles by sign).
static void lcd_edit_step(int delta) {
  switch (lcdMenuIdx) {
    case 9: {  // WPM 5..50 step ±1
      int v = (int)KeyerWpm + delta;
      if (v < 5) v = 5;
      if (v > 50) v = 50;
      KeyerWpm = (uint8_t)v;
      break;
    }
    case 10:  // Iambic A ↔ B toggle
      KeyerActiveMode = (KeyerActiveMode == 'A') ? 'B' : 'A';
      break;
    case 11:  // Paddle Normal ↔ Reverse toggle
      KeyerPaddleSwap = !KeyerPaddleSwap;
      break;
    case 12: {  // Sidetone: 0 (off) or 300..1200 step ±10. Crosses 300↔0.
      int v = (int)KeyerSidetoneNow;
      if (delta > 0) {
        if (v == 0) v = 300;
        else { v += 10; if (v > 1200) v = 1200; }
      } else {
        if (v <= 300) v = 0;
        else v -= 10;
      }
      KeyerSidetoneNow = (uint16_t)v;
      break;
    }
    case 13: {  // RTTY baud cycle through {45, 50, 75, 100}
      static const uint8_t list[] = {45, 50, 75, 100};
      int8_t cur = 0;
      for (int i = 0; i < 4; i++) if (list[i] == KeyerRttyBaudNow) { cur = i; break; }
      cur = (delta > 0) ? ((cur + 1) & 3) : ((cur + 3) & 3);
      KeyerRttyBaudNow = list[cur];
      keyerRttyBitMs = (KeyerRttyBaudNow == 45) ? 22UL : (1000UL / KeyerRttyBaudNow);
      break;
    }
    case 14:  // FSK polarity Normal ↔ Reverse toggle
      KeyerFskReverseNow = !KeyerFskReverseNow;
      keyer_fsk_release();   // ensure line is released (polarity only affects TX bits)
      break;
  }
}

// Commit the currently-edited value to EEPROM (bypasses keyer's 5 s debounce
// because the user explicitly hit MODE-short to save).
static void lcd_edit_save() {
  switch (lcdMenuIdx) {
    case 9:  EEPROM.update(KEYER_EEPROM_WPM_ADDR,      KeyerWpm); break;
    case 10: EEPROM.update(KEYER_EEPROM_MODE_ADDR,     (KeyerActiveMode == 'A') ? 0 : 1); break;
    case 11: EEPROM.update(KEYER_EEPROM_REVERSE_ADDR,  KeyerPaddleSwap ? 1 : 0); break;
    case 12: EEPROM.update(KEYER_EEPROM_SIDETONE_LO,   (uint8_t)(KeyerSidetoneNow & 0xFF));
             EEPROM.update(KEYER_EEPROM_SIDETONE_HI,   (uint8_t)(KeyerSidetoneNow >> 8));
             break;
    case 13: EEPROM.update(KEYER_EEPROM_RTTYBAUD_ADDR, KeyerRttyBaudNow); break;
    case 14: EEPROM.update(KEYER_EEPROM_FSKREV_ADDR,   KeyerFskReverseNow ? 1 : 0); break;
  }
}

void lcd_loop() {
  if (millis() - lcdLastTickMs < LCD_TICK_MS) return;
  lcdLastTickMs = millis();
  unsigned long now = lcdLastTickMs;

  uint8_t btnNow = lcd_read_button();
  bool isEdge = (btnNow != 0 && lcdBtnPrev != btnNow);
  bool isHeld = (btnNow != 0 && btnNow == lcdBtnPrev);

  if (isEdge) {
    lcdBtnPressMs = now;
    lcdBtnRepeatNextMs = now + LCD_BTN_REPEAT_DELAY_MS;
  }
  bool isRepeat = isHeld && (now >= lcdBtnRepeatNextMs);
  if (isRepeat) lcdBtnRepeatNextMs = now + LCD_BTN_REPEAT_RATE_MS;

  // --- BROWSING: edge-only navigation via M1 (prev) / SET (next); M2 ignored ---
  if (lcdState == LCD_BROWSING && isEdge) {
    if (btnNow == 1) {
      lcdMenuIdx = (lcdMenuIdx + LCD_MENU_ITEM_COUNT - 1) % LCD_MENU_ITEM_COUNT;
    } else if (btnNow == 2) {
      lcdMenuIdx = (lcdMenuIdx + 1) % LCD_MENU_ITEM_COUNT;
    }
  }

  // --- EDITING: M1 = decrement, M2 = increment (auto-repeat); SET ignored ---
  if (lcdState == LCD_EDITING && (isEdge || isRepeat)) {
    if (btnNow == 1)      lcd_edit_step(-1);
    else if (btnNow == 3) lcd_edit_step(+1);
    if (isEdge) tone(TONE, 700, 20);
  }

  // --- PINNED: M1/M2 = WPM down/up with auto-repeat; SET ignored ---
  if (lcdState == LCD_PINNED && (isEdge || isRepeat)) {
    if (btnNow == 1)      keyer_set_wpm((KeyerWpm > 5)  ? KeyerWpm - 1 : 5);
    else if (btnNow == 3) keyer_set_wpm((KeyerWpm < 50) ? KeyerWpm + 1 : 50);
    if (isEdge && (btnNow == 1 || btnNow == 3)) tone(TONE, 600, 30);
  }

  lcdBtnPrev = btnNow;

  // --- MODE button: short / long press dispatch ---
  bool menuDownNow = (digitalRead(MENU) == LOW);
  if (menuDownNow && !lcdMenuDown) {
    lcdMenuPressMs = now;
    lcdMenuDown = true;
  } else if (!menuDownNow && lcdMenuDown) {
    unsigned long held = now - lcdMenuPressMs;
    lcdMenuDown = false;
    if (held < LCD_MODE_HOLD_MS) {
      // SHORT press
      if (lcdState == LCD_PINNED) {
        lcdState = LCD_BROWSING;
        tone(TONE, 400, 50);
      } else if (lcdState == LCD_BROWSING) {
        lcdState = LCD_PINNED;
        EEPROM.update(LCD_EEPROM_IDX_ADDR, lcdMenuIdx);
        tone(TONE, 300, 20);
      } else {  // LCD_EDITING → save + back to BROWSING
        lcd_edit_save();
        lcdState = LCD_BROWSING;
        tone(TONE, 600, 40);
      }
    } else {
      // LONG press
      if (lcdState == LCD_BROWSING && lcd_item_is_editable(lcdMenuIdx)) {
        lcdEditPreValue = lcd_item_get_value(lcdMenuIdx);
        lcdState = LCD_EDITING;
        tone(TONE, 800, 60);
      } else if (lcdState == LCD_EDITING) {
        // Cancel: restore pre-edit value
        lcd_item_set_value(lcdMenuIdx, lcdEditPreValue);
        lcdState = LCD_BROWSING;
        tone(TONE, 200, 60);
      }
      // long press in PINNED → no-op
    }
  }

  lcd_render(false);
}
// =================== END LCD RUNTIME ===================


void setup() {
  Serial.begin(115200);
  lcd_setup();
  sdcfg_setup();
  ethernet_setup();
  civ_setup();
  trxnet_setup();
  ptt_setup();
  keyer_setup();
  lcd_runtime_init();
}

void loop() {
  ethernet_loop();
  trxnet_loop();
  civ_loop();
  ptt_loop();
  keyer_loop();
  lcd_loop();
}
