#include <Wire.h>
#include <SPI.h>
#include <mcp_can.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <RTClib.h>
#include <WiFi.h>
#include "time.h"
#include <Preferences.h>

#include <Fonts/FreeSansBold18pt7b.h> 
#include <Fonts/FreeSansBold12pt7b.h> 
#include <Fonts/FreeSansBold9pt7b.h> 

// ================= KONFIGURASI ALAT =================
const char* FW_VERSION = "V5.1"; // Versi Firmware Dashboard

// ================= KONFIGURASI PIN (ESP32-C3) =================
#define SDA_PIN 8       
#define SCL_PIN 9       
#define SPI_SCK 4       
#define SPI_MISO 5      
#define SPI_MOSI 6      
#define MCP_CS_PIN 7    
#define MCP_INT_PIN 10  
#define BUTTON_PIN 3    

// ================= KONFIGURASI NTP =================
const char* ntpServer  = "pool.ntp.org";
const long  gmtOffset_sec = 7 * 3600; 
const int   daylightOffset_sec = 0;

// ================= OBJEK HARDWARE & PENYIMPANAN =================
Adafruit_SSD1306 display(128, 32, &Wire, -1);
RTC_DS3231 rtc;
MCP_CAN CAN0(MCP_CS_PIN);
Preferences preferences;

const char* DAY_NAMES[] = {"MINGGU", "SENIN", "SELASA", "RABU", "KAMIS", "JUMAT", "SABTU"};
const char* MONTH_NAMES[] = {"JAN", "FEB", "MAR", "APR", "MEI", "JUN", "JUL", "AGU", "SEP", "OKT", "NOV", "DES"};

// ================= TABEL LOOKUP SOC (AKURASI TINGGI) =================
const uint16_t socToBms[101] = {
  0, 60,70,80,90,95,105,115,125,135,140,150,160,170,180,185,195,205,215,225,
  230,240,250,260,270,275,285,295,305,315,320,330,340,350,360,365,375,385,395,405,
  410,420,430,440,450,455,465,475,485,495,500,510,520,530,540,550,555,565,575,585,
  590,600,610,620,630,635,645,655,665,675,680,690,700,710,720,725,735,745,755,765,
  770,780,790,800,810,815,825,835,845,855,860,870,880,890,900,905,915,925,935,945,950
};

float getSoCFromLookup(uint16_t raw) {
  if (raw >= socToBms[100]) return 100.0f;
  if (raw <= socToBms[0]) return 0.0f;
  for (int i = 0; i < 100; i++) {
    if (raw >= socToBms[i] && raw <= socToBms[i + 1]) {
      float range = (float)(socToBms[i + 1] - socToBms[i]);
      float delta = (float)(raw - socToBms[i]);
      if (range == 0) return (float)i;
      return (float)i + (delta / range);
    }
  }
  return 0.0f;
}

// ================= VARIABEL DATA =================
int rpm = 0;
int speed_kmh = 0;
float volts = 0.0;
float amps = 0.0;
float power_watt = 0.0;
int soc = 0;
int tempCtrl = 0, tempMotor = 0, tempBatt = 0;
String currentMode = "PARK";
String lastMode = "PARK";

// Variabel Charger dari Referensi Komunitas
bool isCharging = false;
bool chargerConnected = false;
bool oriChargerDetected = false;
float chargerCurrent = 0.0f; 
unsigned long lastChargerMsg = 0;
unsigned long lastOriChargerMsg = 0;

int currentPage = 1; // Sekarang halamannya ada 5
unsigned long lastButtonPress = 0;
unsigned long lastModeChange = 0;
unsigned long lastDisplayUpdate = 0;
bool showModePopup = false;

String ssid = "";
String password = "";
String splashText = "";

void checkSerialCommands();

void setup() {
  Serial.begin(115200);
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(MCP_INT_PIN, INPUT_PULLUP); 

  preferences.begin("cfg", false);
  ssid = preferences.getString("ssid", "pixel8");      
  password = preferences.getString("pass", "1sampai8");
  splashText = preferences.getString("splash", "POLYTRON");

  Wire.begin(SDA_PIN, SCL_PIN);
  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("OLED Gagal!");
    while(1);
  }

  // Splash Screen
  display.clearDisplay();
  display.setFont(&FreeSansBold9pt7b); 
  display.setTextColor(SSD1306_WHITE);
  int textWidth = splashText.length() * 11; 
  int xPos = (128 - textWidth) / 2;
  if(xPos < 0) xPos = 0; 
  display.setCursor(xPos, 22); 
  display.print(splashText);
  display.display();

  if (!rtc.begin()) {
    Serial.println("RTC Gagal!");
  }

  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(100);
  WiFi.begin(ssid.c_str(), password.c_str());
  
  int wifiTimeout = 0;
  while (WiFi.status() != WL_CONNECTED && wifiTimeout < 20) {
    checkSerialCommands(); 
    delay(500);
    wifiTimeout++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
    struct tm timeinfo;
    if (getLocalTime(&timeinfo)) {
      rtc.adjust(DateTime(timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday, timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec));
    }
  } 

  WiFi.disconnect(true); 
  WiFi.mode(WIFI_OFF);
  delay(1500); 

  SPI.begin(SPI_SCK, SPI_MISO, SPI_MOSI, MCP_CS_PIN);
  if(CAN0.begin(MCP_ANY, CAN_250KBPS, MCP_8MHZ) == CAN_OK) {
    CAN0.setMode(MCP_NORMAL);
  } else {
    display.clearDisplay();
    display.setFont(); 
    display.setCursor(0,0);
    display.print("CAN BUS GAGAL!");
    display.display();
  }
}

void checkSerialCommands() {
  if (Serial.available()) {
    String input = Serial.readStringUntil('\n');
    input.trim();
    if (input.startsWith("WIFI,")) {
      int firstComma = input.indexOf(',');
      int secondComma = input.indexOf(',', firstComma + 1);
      if (firstComma > 0 && secondComma > 0) {
        String newSSID = input.substring(firstComma + 1, secondComma);
        String newPass = input.substring(secondComma + 1);
        preferences.putString("ssid", newSSID);
        preferences.putString("pass", newPass);
        delay(1000);
        ESP.restart();
      }
    }
    else if (input.startsWith("SPLASH,")) {
      int comma = input.indexOf(',');
      if (comma > 0) {
        String newSplash = input.substring(comma + 1);
        if (newSplash.length() > 10) newSplash = newSplash.substring(0, 10);
        preferences.putString("splash", newSplash);
        delay(1000);
        ESP.restart();
      }
    }
  }
}

// ================= BACA CAN BUS =================
void readCAN() {
  while(CAN0.checkReceive() == CAN_MSGAVAIL) {
    long unsigned int rxId;
    unsigned char len = 0;
    unsigned char rxBuf[8];
    CAN0.readMsgBuf(&rxId, &len, rxBuf);
    
    rxId = rxId & 0x1FFFFFFF;

    // 1. Data Mode Votol
    if (rxId == 0x0A010810 && len >= 8) {
      uint8_t m = rxBuf[1];
      if (m == 0x00) currentMode = "PARK";
      else if (m == 0x61) currentMode = "CHARGE";
      else if (m == 0x70) currentMode = "DRIVE";
      else if (m == 0x50 || m == 0xF0 || m == 0x30 || m == 0xF8) currentMode = "REVERSE";
      else if (m == 0x72 || m == 0xB2) currentMode = "BRAKE"; 
      else if (m == 0xB0) currentMode = "SPORT";
      else if (m == 0x78 || m == 0x08) currentMode = "STAND"; 

      if (currentMode != lastMode) {
        lastMode = currentMode;
        if (currentMode != "BRAKE" && currentMode != "STAND") {
          showModePopup = true;
          lastModeChange = millis();
        }
      }

      rpm = rxBuf[2] | (rxBuf[3] << 8);
      speed_kmh = (int)(rpm * 0.1033f);
      tempCtrl = rxBuf[4];
      tempMotor = rxBuf[5];
    }
    
    // 2. Suhu Baterai
    if (rxId == 0x0E6C0D09 && len >= 5) {
      tempBatt = (rxBuf[0] + rxBuf[1] + rxBuf[2] + rxBuf[3] + rxBuf[4]) / 5;
    }

    // 3. Voltase & Arus Utama
    if (rxId == 0x0A6D0D09 && len >= 4) {
      uint16_t vRaw = (rxBuf[0] << 8) | rxBuf[1];
      volts = vRaw * 0.1f;
      
      int16_t iRawS = (int16_t)((rxBuf[2] << 8) | rxBuf[3]);
      amps = iRawS * 0.1f;
      if (abs(amps) < 0.2) amps = 0.0; 
      
      power_watt = volts * amps;
    }

    // 4. Persentase Baterai
    if (rxId == 0x0A6E0D09 && len >= 2) {
      uint16_t socVal = (uint16_t)((rxBuf[0] << 8) | rxBuf[1]);
      soc = (int)getSoCFromLookup(socVal);
    }

    // 5. DETEKSI CHARGER FISIK (Fast Charger CCS)
    if ((rxId == 0x1810D0F3 || rxId == 0x1811D0F3) && len >= 5) {
      uint16_t iRaw = (uint16_t)((rxBuf[2] << 8) | rxBuf[3]);
      chargerCurrent = iRaw * 0.1f; 
      chargerConnected = true;
      lastChargerMsg = millis();
    }
    // 6. DETEKSI CHARGER ORI (Bawaan Pabrik)
    if (rxId == 0x10261041) {
      oriChargerDetected = true;
      lastOriChargerMsg = millis();
    }
  }

  // Reset status jika dicabut
  if (millis() - lastChargerMsg > 5000) {
    chargerConnected = false;
    chargerCurrent = 0.0f;
  }
  if (millis() - lastOriChargerMsg > 5000) oriChargerDetected = false;

  isCharging = (chargerConnected || oriChargerDetected);
}

// ================= UPDATE LAYAR =================
void updateOLED() {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);

  // 1. OVERLAY CHARGING
  if (isCharging) {
    display.setFont(); 
    bool showAmps = (millis() / 2000) % 2 == 0; 
    
    display.setTextSize(1);
    display.setCursor(0, 0);
    if (oriChargerDetected) display.print("ORI CHARGER:");
    else display.print("FAST CHARGER:");

    display.setTextSize(2);
    display.setCursor(0, 15); 
    
    if (showAmps) {
      display.print("IN: "); 
      float displayAmps = (chargerCurrent > 0.1f) ? chargerCurrent : fabs(amps);
      display.print(displayAmps, 1); 
      display.print("A");
    } else {
      display.print("BAT: "); 
      display.print(soc); 
      display.print("%");
    }
    
    display.display();
    return;
  }

  // 2. OVERLAY KECEPATAN (>70 KM/H)
  if (speed_kmh > 70) {
    display.setFont(&FreeSansBold18pt7b);
    display.setCursor(0, 28); 
    display.printf("%d", speed_kmh);

    display.setFont();
    display.setTextSize(1);
    display.setCursor(88, 15); 
    display.print("KM/H");

    display.display();
    return;
  }

  // 3. OVERLAY POP-UP MODE (3 Detik)
  if (showModePopup) {
    if (millis() - lastModeChange < 3000) { 
      display.setFont(&FreeSansBold9pt7b);
      int textWidth = currentMode.length() * 11; 
      int xPos = (128 - textWidth) / 2;
      if (xPos < 0) xPos = 0;
      display.setCursor(xPos, 22);
      display.print(currentMode);
      display.display();
      return;
    } else {
      showModePopup = false;
      currentPage = 1;
    }
  }

  // 4. HALAMAN NORMAL
  display.setFont(); 
  
  switch (currentPage) {
    case 1: { 
      // --- UPDATE LAYOUT TANGGAL (Hari -> Tgl & Bln -> Thn) ---
      DateTime dt = rtc.now();
      display.setFont(&FreeSansBold18pt7b);
      display.setCursor(0, 28);
      display.printf("%02d:%02d", dt.hour(), dt.minute());
      display.setFont(); 
      display.setTextSize(1);
      
      display.setCursor(88, 5);  
      display.print(DAY_NAMES[dt.dayOfTheWeek()]); // Baris atas: Hari
      
      display.setCursor(88, 15); 
      display.printf("%d %s", dt.day(), MONTH_NAMES[dt.month() - 1]); // Baris tengah: Tgl & Bln
      
      display.setCursor(88, 25); 
      display.printf("%04d", dt.year()); // Baris bawah: Tahun
      break;
    }
    case 2: { 
      display.setTextSize(1);
      display.setCursor(0, 4); display.print("ECU");
      display.setCursor(43, 4); display.print("MOTOR");
      display.setCursor(86, 4); display.print("BATT");
      display.setTextSize(2);
      display.setCursor(0, 16); display.print(tempCtrl);
      display.setCursor(43, 16); display.print(tempMotor);
      display.setCursor(86, 16); display.print(tempBatt);
      break;
    }
    case 3: { 
      display.setTextSize(1);
      display.setCursor(8, 4); display.print("VOLT");
      display.setCursor(86, 4); display.print("ARUS");
      display.setTextSize(2);
      display.setCursor(0, 16); display.print(volts, 1);
      display.setTextSize(1); display.setCursor(55, 22); display.print("V");
      display.setTextSize(2);
      display.setCursor(78, 16); display.print(fabs(amps), 1);
      display.setTextSize(1); display.setCursor(120, 22); display.print("A");
      break;
    }
    case 4: { 
      int pwr = abs((int)power_watt);
      if(power_watt > 0.1f) { display.setTextSize(2); display.setCursor(10, 4); display.print("+"); }
      else if(power_watt < -0.1f) { display.setTextSize(2);
      display.setCursor(10, 4); display.print("-"); }
      display.setTextSize(3);
      display.setCursor(32, 2); 
      display.print(pwr);
      display.setTextSize(1);
      display.setCursor(102, 22); 
      display.print("watt");
      break;
    }
    case 5: {
      // --- HALAMAN BARU: SYSTEM INFO ---
      // Layar OLED 128x32 bisa memuat persis 4 baris teks kecil (size 1)
      display.setTextSize(1);
      display.setCursor(0, 0);  display.print("WIFI: "); display.print(ssid);
      display.setCursor(0, 8);  display.print("PASS: "); display.print(password);
      display.setCursor(0, 16); display.print("NAME: "); display.print(splashText);
      display.setCursor(0, 24); display.print("FW  : "); display.print(FW_VERSION);
      break;
    }
  }
  display.display();
}

void loop() {
  readCAN();
  checkSerialCommands();
  
  if (digitalRead(BUTTON_PIN) == LOW) {
    if (millis() - lastButtonPress > 300) { 
      currentPage++;
      // Sekarang maksimal halaman adalah 5
      if (currentPage > 5) currentPage = 1;
      lastButtonPress = millis();
    }
  }

  // Jeda 30 detik (30000 ms) sebelum kembali otomatis ke Jam
  if (millis() - lastButtonPress > 30000 && currentPage != 1 && !showModePopup && !isCharging && speed_kmh <= 70) {
    currentPage = 1;
  }

  if (millis() - lastDisplayUpdate > 100) {
    updateOLED();
    lastDisplayUpdate = millis();
  }
}