#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>
#include <SPI.h>
#include <WiFi.h>
#include <HTTPClient.h>

#define TFT_CS   15
#define TFT_RST  4
#define TFT_DC   2
#define TFT_LED  22 

Adafruit_ST7735 tft = Adafruit_ST7735(TFT_CS, TFT_DC, TFT_RST);

String savedSsid = "YOUR_WIFI_NAME";
String savedPass = "YOUR_WIFI_PASSWORD";

#define BTN_UP         13 
#define BTN_DOWN       12 
#define BTN_SELECT     14 
#define BTN_POWER      27 
#define BTN_BACK_PG    26 
#define BTN_FORWARD_PG 25 

// Цвета для интерфейса
#define ST7735_DARKGRAY  0x4208 
#define ST7735_LIGHTGRAY 0xC618 
#define ST7735_IOS_DARK_BG   0x0000
#define ST7735_IOS_LIGHT_BG  0xFFFF
#define ST7735_IOS_CARD_DARK 0x18E3
#define ST7735_IOS_CARD_LGT  0xEF7B
#define ST7735_IOS_BLUE      0x041F
#define ST7735_IOS_GRAY      0x8410

enum AppState { STATE_HOME, STATE_BROWSER, STATE_SETTINGS, STATE_WIFI_SCAN, STATE_KEYBOARD, STATE_REBOOT_MENU };
AppState currentState = STATE_HOME;
AppState prevState = STATE_HOME;

struct MenuItem {
  String name;
  String url;
};

MenuItem menuItems[] = {
  { "1. ESP-NokiaBrowser", "https://kriperboom.github.io/ESP-nokiabrouser/index.html" },
  { "2. NeverSSL", "http://neverssl.com" },
  { "3. CNN Lite", "http://lite.cnn.com" },
  { "4. Wi-Fi Setup", "" },
  { "5. Settings", "" }
};
const int menuCount = 5;
int cursorIndex = 0;       

bool darkMode = true;
int accentColorIndex = 0; 
uint16_t accentColors[] = { ST7735_IOS_BLUE, ST7735_RED, ST7735_GREEN, ST7735_WHITE };
String accentNames[] = { "BLUE", "RED", "GREEN", "WHITE" };

int autoSleepLevel = 0;
unsigned long sleepIntervals[] = { 0, 15000, 30000, 60000 };
String sleepNames[] = { "OFF", "15s", "30s", "60s" };
unsigned long lastActivityTime = 0;
bool isSleeping = false;

int settingsCursor = 0; 
bool screenOn = true;
bool lastPowerState = HIGH; 
unsigned long powerPressStartTime = 0;
bool powerHeldTriggered = false;

String historyUrls[10];
String historyTitles[10];
int historyCount = 0;
int historyPos = -1;

struct RenderLine {
  String text;
  bool isHeader;
  bool isLink;
  String linkUrl;
  uint16_t textColor;
};

RenderLine screenLines[40];
int totalLines = 0;
int browserCursorY = 0; 
int scrollOffset = 0;    
String currentPageTitle = "Browser";

String foundNetworks[10];
int wifiNetworkCount = 0;
int wifiCursor = 0;

String keyboardInputText = "";
int kbRow = 0;
int kbCol = 0;
String inputTargetType = ""; 
String tempSsid = "";
int kbMode = 0; 

const char* kbCaps[4][10] = {
  { "Q", "W", "E", "R", "T", "Y", "U", "I", "O", "P" },
  { "A", "S", "D", "F", "G", "H", "J", "K", "L", "@" },
  { "Z", "X", "C", "V", "B", "N", "M", ".", "_", "-" },
  { "^", "123", "SPC", "DEL", "ENT", "", "", "", "", "" }
};
const int capsCols[4] = { 10, 10, 10, 5 };

const char* kbLow[4][10] = {
  { "q", "w", "e", "r", "t", "y", "u", "i", "o", "p" },
  { "a", "s", "d", "f", "g", "h", "j", "k", "l", "@" },
  { "z", "x", "c", "v", "b", "n", "m", ".", "_", "-" },
  { "^", "123", "SPC", "DEL", "ENT", "", "", "", "", "" }
};
const int lowCols[4] = { 10, 10, 10, 5 };

const char* kbSym[4][10] = {
  { "1", "2", "3", "4", "5", "6", "7", "8", "9", "0" },
  { "(", ")", "<", ">", "_", "-", "?", "!", ".", "," },
  { "/", "@", "#", "$", "%", "&", "*", "+", "=", ";" },
  { "^", "ABC", "SPC", "DEL", "ENT", "", "", "", "", "" }
};
const int symCols[4] = { 10, 10, 10, 5 };

void fetchRealWebpage(String url, bool addToHistory = true);

void fadeInBacklight() {
  for (int i = 0; i <= 255; i += 15) {
    analogWrite(TFT_LED, i);
    delay(10);
  }
  analogWrite(TFT_LED, 255);
}

void fadeOutBacklight() {
  for (int i = 255; i >= 0; i -= 15) {
    analogWrite(TFT_LED, i);
    delay(10);
  }
  analogWrite(TFT_LED, 0);
}

void resetSleepTimer() {
  lastActivityTime = millis();
  if (isSleeping) {
    isSleeping = false;
    fadeInBacklight();
  }
}

void drawBootLogo() {
  fadeInBacklight();
  tft.fillScreen(ST7735_IOS_DARK_BG);
  int cx = 64, cy = 64, radius = 24;
  tft.drawCircle(cx, cy, radius, ST7735_IOS_BLUE);
  tft.drawCircle(cx, cy, radius + 1, ST7735_IOS_BLUE);
  tft.drawEllipse(cx, cy, radius / 2, radius, ST7735_CYAN);
  tft.drawEllipse(cx, cy, radius, radius / 2, ST7735_CYAN);
  tft.drawFastHLine(cx - radius, cy, radius * 2, ST7735_CYAN);
  tft.drawFastVLine(cx, cy - radius, radius * 2, ST7735_CYAN);

  tft.setTextSize(1);
  tft.setTextColor(ST7735_WHITE);
  tft.setCursor(20, 110);
  tft.print("iOS Browser");
  delay(1500);
}

void drawNetworkStatus() {
  tft.setCursor(114, 3);
  tft.setTextSize(1);
  tft.setTextColor(darkMode ? ST7735_WHITE : ST7735_BLACK);
  if (WiFi.status() == WL_CONNECTED) tft.print("V"); 
  else tft.print("X"); 
}

void drawIOSHeader(String title) {
  uint16_t bgCol = darkMode ? ST7735_IOS_DARK_BG : ST7735_IOS_LIGHT_BG;
  tft.fillRect(0, 0, 128, 16, bgCol);
  tft.setTextColor(darkMode ? ST7735_WHITE : ST7735_BLACK);
  tft.setTextSize(1);
  tft.setCursor(4, 4);
  if (title.length() > 14) title = title.substring(0, 14);
  tft.print(title);
  drawNetworkStatus();
}

void drawRebootMenu() {
  uint16_t cardBg = darkMode ? ST7735_IOS_CARD_DARK : ST7735_IOS_CARD_LGT;
  uint16_t textCol = darkMode ? ST7735_WHITE : ST7735_BLACK;

  tft.fillRoundRect(10, 30, 108, 90, 8, cardBg);
  tft.drawRoundRect(10, 30, 108, 90, 8, ST7735_IOS_BLUE);

  tft.setTextSize(1);
  tft.setTextColor(textCol);
  tft.setCursor(24, 42);
  tft.print("Power Off Menu");

  tft.drawFastHLine(16, 56, 96, ST7735_IOS_GRAY);

  tft.setCursor(18, 70);
  tft.print("[SEL] Reboot");
  
  tft.setCursor(18, 94);
  tft.print("[PWR] Cancel");
}

void drawHomePage() {
  uint16_t bgCol = darkMode ? ST7735_IOS_DARK_BG : ST7735_IOS_LIGHT_BG;
  uint16_t textCol = darkMode ? ST7735_WHITE : ST7735_BLACK;
  uint16_t selBg = accentColors[accentColorIndex];

  tft.fillScreen(bgCol);
  drawIOSHeader("Home");

  tft.setCursor(4, 22);
  tft.setTextColor(textCol);
  tft.print("Select item:");

  int startY = 38;
  for (int i = 0; i < menuCount; i++) {
    int itemY = startY + (i * 19);
    if (i == cursorIndex) {
      tft.fillRoundRect(2, itemY - 2, 124, 15, 4, selBg);
      tft.setTextColor(ST7735_WHITE, selBg);
    } else {
      tft.setTextColor(textCol, bgCol);
    }
    tft.setCursor(6, itemY + 1);
    tft.print(menuItems[i].name);
  }
}

void drawSettingsPage() {
  uint16_t bgCol = darkMode ? ST7735_IOS_DARK_BG : ST7735_IOS_LIGHT_BG;
  uint16_t textCol = darkMode ? ST7735_WHITE : ST7735_BLACK;
  uint16_t selBg = accentColors[accentColorIndex];

  tft.fillScreen(bgCol);
  drawIOSHeader("Settings");

  String sItems[4] = {
    darkMode ? "Theme: Dark" : "Theme: Light",
    "Accent: " + accentNames[accentColorIndex],
    "Auto-Sleep: " + sleepNames[autoSleepLevel],
    "Save & Exit"
  };

  int startY = 32;
  for (int i = 0; i < 4; i++) {
    int itemY = startY + (i * 26);
    if (i == settingsCursor) {
      tft.fillRoundRect(6, itemY - 2, 116, 20, 4, selBg);
      tft.setTextColor(ST7735_WHITE, selBg);
    } else {
      tft.setTextColor(textCol, bgCol);
    }
    tft.setCursor(10, itemY + 4);
    tft.print(sItems[i]);
  }
}

void drawWifiScanPage() {
  uint16_t bgCol = darkMode ? ST7735_IOS_DARK_BG : ST7735_IOS_LIGHT_BG;
  uint16_t textCol = darkMode ? ST7735_WHITE : ST7735_BLACK;
  uint16_t selBg = accentColors[accentColorIndex];

  tft.fillScreen(bgCol);
  drawIOSHeader("Select Wi-Fi");

  if (wifiNetworkCount == 0) {
    tft.setCursor(10, 40);
    tft.setTextColor(textCol);
    tft.print("No networks found!");
  } else {
    int startY = 25;
    for (int i = 0; i < wifiNetworkCount && i < 10; i++) {
      int itemY = startY + (i * 12);
      if (i == wifiCursor) {
        tft.fillRoundRect(2, itemY - 1, 124, 11, 3, selBg);
        tft.setTextColor(ST7735_WHITE, selBg);
      } else {
        tft.setTextColor(textCol, bgCol);
      }
      tft.setCursor(4, itemY);
      String ssidStr = foundNetworks[i];
      if (ssidStr.length() > 18) ssidStr = ssidStr.substring(0, 18);
      tft.print(ssidStr);
    }
  }
}

void drawVirtualKeyboard() {
  uint16_t bgCol = darkMode ? ST7735_IOS_DARK_BG : ST7735_IOS_LIGHT_BG;
  uint16_t textCol = darkMode ? ST7735_WHITE : ST7735_BLACK;
  uint16_t selBg = accentColors[accentColorIndex];

  tft.fillScreen(bgCol);
  drawIOSHeader("Wi-Fi Pass");

  tft.drawRect(4, 18, 120, 16, textCol);
  tft.setCursor(8, 22);
  tft.setTextColor(textCol);
  String showText = keyboardInputText;
  if (showText.length() > 18) showText = showText.substring(showText.length() - 18);
  tft.print(showText + "_");

  tft.fillRect(2, 38, 124, 102, ST7735_DARKGRAY);
  tft.drawRect(2, 38, 124, 102, ST7735_DARKGRAY);

  int startY = 42;
  for (int r = 0; r < 4; r++) {
    int colsInRow = (kbMode == 2) ? symCols[r] : ((kbMode == 1) ? lowCols[r] : capsCols[r]);
    int totalWidth = 120;
    int keyWidth = totalWidth / colsInRow;
    int startX = 4;

    for (int c = 0; c < colsInRow; c++) {
      String keyStr = (kbMode == 2) ? kbSym[r][c] : ((kbMode == 1) ? kbLow[r][c] : kbCaps[r][c]);
      int x = startX + (c * keyWidth);
      int y = startY + (r * 24);
      int w = keyWidth - 2;
      int h = 21;

      if (r == kbRow && c == kbCol) {
        tft.fillRoundRect(x, y, w, h, 4, selBg);
        tft.setTextColor(ST7735_WHITE, selBg);
      } else {
        tft.fillRoundRect(x, y, w, h, 4, ST7735_IOS_CARD_DARK);
        tft.setTextColor(ST7735_WHITE, ST7735_IOS_CARD_DARK);
      }

      int textX = x + (w - (keyStr.length() * 6)) / 2;
      int textY = y + 7;
      tft.setCursor(textX, textY);
      tft.print(keyStr);
    }
  }
}

void drawContentPage() {
  uint16_t bgCol = darkMode ? ST7735_IOS_DARK_BG : ST7735_IOS_LIGHT_BG;
  uint16_t textCol = darkMode ? ST7735_WHITE : ST7735_BLACK;
  uint16_t selBg = accentColors[accentColorIndex];
  uint16_t normalCursorBg = darkMode ? ST7735_DARKGRAY : ST7735_LIGHTGRAY;
  uint16_t normalCursorText = darkMode ? ST7735_WHITE : ST7735_BLACK;

  tft.fillScreen(bgCol);
  drawIOSHeader(currentPageTitle);

  if (browserCursorY < scrollOffset) scrollOffset = browserCursorY;
  if (browserCursorY >= scrollOffset + 11) scrollOffset = browserCursorY - 10;

  int startY = 18;
  for (int i = scrollOffset; i < totalLines; i++) {
    tft.setCursor(2, startY);
    
    if (screenLines[i].isHeader) {
      tft.setTextSize(2); 
      if (i == browserCursorY) {
        int w = screenLines[i].text.length() * 12;
        tft.fillRoundRect(2, startY - 1, w + 4, 16, 3, selBg);
        tft.setTextColor(ST7735_WHITE, selBg);
      } else {
        tft.setTextColor(screenLines[i].textColor, bgCol);
      }
      tft.print(screenLines[i].text);
      startY += 18; 
    } else {
      tft.setTextSize(1);
      
      if (i == browserCursorY) {
        int w = screenLines[i].text.length() * 6;
        if (screenLines[i].isLink) {
          tft.fillRoundRect(2, startY - 1, w + 4, 10, 2, selBg);
          tft.setTextColor(ST7735_WHITE, selBg);
        } else {
          tft.fillRoundRect(2, startY - 1, w + 4, 10, 2, normalCursorBg);
          tft.setTextColor(normalCursorText, normalCursorBg);
        }
      } else if (screenLines[i].isLink) {
        tft.setTextColor(selBg, bgCol);
      } else {
        tft.setTextColor(screenLines[i].textColor, bgCol);
      }

      tft.print(screenLines[i].text);
      startY += 10; 
    }
    
    if (startY > 145) break; 
  }
}

void fetchRealWebpage(String url, bool addToHistory) {
  uint16_t bgCol = darkMode ? ST7735_IOS_DARK_BG : ST7735_IOS_LIGHT_BG;
  if (url == "HOME_BACK") {
    currentState = STATE_HOME;
    drawHomePage();
    return;
  }

  tft.fillScreen(bgCol);
  drawIOSHeader("Loading...");

  HTTPClient http;
  http.begin(url);
  int httpCode = http.GET();

  if (httpCode > 0 && httpCode == HTTP_CODE_OK) {
    String payload = http.getString();
    totalLines = 0;
    currentPageTitle = "Webpage";
    if (addToHistory && historyPos < 9) {
      historyPos++;
      historyUrls[historyPos] = url;
      historyTitles[historyPos] = currentPageTitle;
      historyCount = historyPos + 1;
    }
  }
  http.end();

  browserCursorY = 0;
  scrollOffset = 0;
  drawContentPage();
}

void setup() {
  Serial.begin(115200);

  pinMode(BTN_UP, INPUT_PULLUP);
  pinMode(BTN_DOWN, INPUT_PULLUP);
  pinMode(BTN_SELECT, INPUT_PULLUP);
  pinMode(BTN_POWER, INPUT_PULLUP);
  pinMode(BTN_BACK_PG, INPUT_PULLUP);
  pinMode(BTN_FORWARD_PG, INPUT_PULLUP);
  
  pinMode(TFT_LED, OUTPUT);
  analogWrite(TFT_LED, 255);

  tft.initR(INITR_BLACKTAB);
  tft.setRotation(0); 

  drawBootLogo();
  drawHomePage();
  resetSleepTimer();
}

unsigned long lastDebounce = 0;

void loop() {
  bool currentPowerState = digitalRead(BTN_POWER);

  if (currentPowerState == LOW) {
    if (lastPowerState == HIGH) {
      powerPressStartTime = millis();
      powerHeldTriggered = false;
    } else if (!powerHeldTriggered && (millis() - powerPressStartTime > 5000)) {
      powerHeldTriggered = true;
      if (currentState != STATE_REBOOT_MENU) {
        prevState = currentState;
        currentState = STATE_REBOOT_MENU;
        drawRebootMenu();
      }
    }
  } else {
    if (lastPowerState == LOW && !powerHeldTriggered) {
      if (millis() - lastDebounce > 300) {
        screenOn = !screenOn;
        if (screenOn) fadeInBacklight(); else fadeOutBacklight();
        lastDebounce = millis();
        resetSleepTimer();
      }
    }
  }
  lastPowerState = currentPowerState;

  if (currentState == STATE_REBOOT_MENU) {
    if (digitalRead(BTN_SELECT) == LOW && millis() - lastDebounce > 200) {
      ESP.restart();
    }
    if (digitalRead(BTN_POWER) == LOW && millis() - lastDebounce > 200) {
      currentState = prevState;
      if (currentState == STATE_HOME) drawHomePage();
      else if (currentState == STATE_SETTINGS) drawSettingsPage();
      else if (currentState == STATE_WIFI_SCAN) drawWifiScanPage();
      else if (currentState == STATE_KEYBOARD) drawVirtualKeyboard();
      else if (currentState == STATE_BROWSER) drawContentPage();
      lastDebounce = millis();
    }
    return;
  }

  if (screenOn && autoSleepLevel > 0 && !isSleeping) {
    if (millis() - lastActivityTime > sleepIntervals[autoSleepLevel]) {
      isSleeping = true;
      fadeOutBacklight();
    }
  }

  if (!screenOn || isSleeping) {
    if (digitalRead(BTN_UP) == LOW || digitalRead(BTN_DOWN) == LOW || 
        digitalRead(BTN_SELECT) == LOW || digitalRead(BTN_BACK_PG) == LOW || 
        digitalRead(BTN_FORWARD_PG) == LOW) {
      if (millis() - lastDebounce > 200) {
        screenOn = true;
        isSleeping = false;
        fadeInBacklight();
        resetSleepTimer();
        lastDebounce = millis();
      }
    }
    return; 
  }

  if (currentState == STATE_HOME) {
    if (digitalRead(BTN_UP) == LOW && millis() - lastDebounce > 150) {
      cursorIndex = (cursorIndex - 1 + menuCount) % menuCount;
      drawHomePage();
      resetSleepTimer();
      lastDebounce = millis();
    }
    if (digitalRead(BTN_DOWN) == LOW && millis() - lastDebounce > 150) {
      cursorIndex = (cursorIndex + 1) % menuCount;
      drawHomePage();
      resetSleepTimer();
      lastDebounce = millis();
    }
    if (digitalRead(BTN_SELECT) == LOW && millis() - lastDebounce > 150) {
      if (cursorIndex == 3) {
        currentState = STATE_WIFI_SCAN;
        wifiCursor = 0;
        tft.fillScreen(darkMode ? ST7735_IOS_DARK_BG : ST7735_IOS_LIGHT_BG);
        drawIOSHeader("Scanning...");
        WiFi.mode(WIFI_STA);
        WiFi.disconnect();
        delay(100);
        wifiNetworkCount = WiFi.scanNetworks();
        for (int i = 0; i < wifiNetworkCount && i < 10; i++) foundNetworks[i] = WiFi.SSID(i);
        drawWifiScanPage();
      } else if (cursorIndex == 4) {
        currentState = STATE_SETTINGS;
        settingsCursor = 0;
        drawSettingsPage();
      } else {
        currentState = STATE_BROWSER;
        historyPos = -1;
        historyCount = 0;
        fetchRealWebpage(menuItems[cursorIndex].url, true);
      }
      resetSleepTimer();
      lastDebounce = millis();
    }
  } 
  else if (currentState == STATE_SETTINGS) {
    if (digitalRead(BTN_UP) == LOW && millis() - lastDebounce > 150) {
      settingsCursor = (settingsCursor - 1 + 4) % 4;
      drawSettingsPage();
      resetSleepTimer();
      lastDebounce = millis();
    }
    if (digitalRead(BTN_DOWN) == LOW && millis() - lastDebounce > 150) {
      settingsCursor = (settingsCursor + 1) % 4;
      drawSettingsPage();
      resetSleepTimer();
      lastDebounce = millis();
    }
    if (digitalRead(BTN_SELECT) == LOW && millis() - lastDebounce > 150) {
      if (settingsCursor == 0) darkMode = !darkMode;
      else if (settingsCursor == 1) accentColorIndex = (accentColorIndex + 1) % 4;
      else if (settingsCursor == 2) autoSleepLevel = (autoSleepLevel + 1) % 4;
      else if (settingsCursor == 3) { currentState = STATE_HOME; drawHomePage(); }
      drawSettingsPage();
      resetSleepTimer();
      lastDebounce = millis();
    }
  }
  else if (currentState == STATE_WIFI_SCAN) {
    if (digitalRead(BTN_UP) == LOW && millis() - lastDebounce > 150 && wifiNetworkCount > 0) {
      wifiCursor = (wifiCursor - 1 + wifiNetworkCount) % wifiNetworkCount;
      drawWifiScanPage();
      resetSleepTimer();
      lastDebounce = millis();
    }
    if (digitalRead(BTN_DOWN) == LOW && millis() - lastDebounce > 150 && wifiNetworkCount > 0) {
      wifiCursor = (wifiCursor + 1) % wifiNetworkCount;
      drawWifiScanPage();
      resetSleepTimer();
      lastDebounce = millis();
    }
    if (digitalRead(BTN_SELECT) == LOW && millis() - lastDebounce > 150) {
      if (wifiNetworkCount > 0) {
        tempSsid = foundNetworks[wifiCursor];
        currentState = STATE_KEYBOARD;
        inputTargetType = "PASS";
        keyboardInputText = "";
        kbRow = 0; kbCol = 0; kbMode = 0;
        drawVirtualKeyboard();
      } else {
        currentState = STATE_HOME;
        drawHomePage();
      }
      resetSleepTimer();
      lastDebounce = millis();
    }
  }
  else if (currentState == STATE_KEYBOARD) {
    if (digitalRead(BTN_UP) == LOW && millis() - lastDebounce > 150) {
      kbRow = (kbRow - 1 + 4) % 4;
      int newMax = (kbMode == 2) ? symCols[kbRow] : ((kbMode == 1) ? lowCols[kbRow] : capsCols[kbRow]);
      if (kbCol >= newMax) kbCol = newMax - 1;
      drawVirtualKeyboard();
      resetSleepTimer();
      lastDebounce = millis();
    }
    if (digitalRead(BTN_DOWN) == LOW && millis() - lastDebounce > 150) {
      kbRow = (kbRow + 1) % 4;
      int newMax = (kbMode == 2) ? symCols[kbRow] : ((kbMode == 1) ? lowCols[kbRow] : capsCols[kbRow]);
      if (kbCol >= newMax) kbCol = newMax - 1;
      drawVirtualKeyboard();
      resetSleepTimer();
      lastDebounce = millis();
    }
    if (digitalRead(BTN_BACK_PG) == LOW && millis() - lastDebounce > 150) {
      int curMax = (kbMode == 2) ? symCols[kbRow] : ((kbMode == 1) ? lowCols[kbRow] : capsCols[kbRow]);
      kbCol = (kbCol - 1 + curMax) % curMax;
      drawVirtualKeyboard();
      resetSleepTimer();
      lastDebounce = millis();
    }
    if (digitalRead(BTN_FORWARD_PG) == LOW && millis() - lastDebounce > 150) {
      int curMax = (kbMode == 2) ? symCols[kbRow] : ((kbMode == 1) ? lowCols[kbRow] : capsCols[kbRow]);
      kbCol = (kbCol + 1) % curMax;
      drawVirtualKeyboard();
      resetSleepTimer();
      lastDebounce = millis();
    }

    if (digitalRead(BTN_SELECT) == LOW && millis() - lastDebounce > 150) {
      String selChar = (kbMode == 2) ? kbSym[kbRow][kbCol] : ((kbMode == 1) ? kbLow[kbRow][kbCol] : kbCaps[kbRow][kbCol]);
      
      if (selChar == "DEL") {
        if (keyboardInputText.length() > 0) keyboardInputText = keyboardInputText.substring(0, keyboardInputText.length() - 1);
      } else if (selChar == "ENT") {
        savedSsid = tempSsid;
        savedPass = keyboardInputText;
        tft.fillScreen(darkMode ? ST7735_IOS_DARK_BG : ST7735_IOS_LIGHT_BG);
        drawIOSHeader("Connecting...");
        WiFi.begin(savedSsid.c_str(), savedPass.c_str());
        int timeout = 0;
        while (WiFi.status() != WL_CONNECTED && timeout < 15) { delay(500); timeout++; }
        currentState = STATE_HOME;
        drawHomePage();
      } else if (selChar == "SPC") {
        keyboardInputText += " ";
      } else if (selChar == "^") {
        kbMode = (kbMode == 0) ? 1 : 0;
      } else if (selChar == "123" || selChar == "ABC") {
        kbMode = (kbMode == 2) ? 0 : 2;
        kbRow = 0; kbCol = 0;
      } else {
        keyboardInputText += selChar;
      }
      
      if (currentState == STATE_KEYBOARD) drawVirtualKeyboard();
      resetSleepTimer();
      lastDebounce = millis();
    }
  }
  else if (currentState == STATE_BROWSER) {
    if (digitalRead(BTN_UP) == LOW && millis() - lastDebounce > 150 && browserCursorY > 0) {
      browserCursorY--;
      drawContentPage();
      resetSleepTimer();
      lastDebounce = millis();
    }
    if (digitalRead(BTN_DOWN) == LOW && millis() - lastDebounce > 150 && browserCursorY < totalLines - 1) {
      browserCursorY++;
      drawContentPage();
      resetSleepTimer();
      lastDebounce = millis();
    }
    if (digitalRead(BTN_SELECT) == LOW && millis() - lastDebounce > 150) {
      if (screenLines[browserCursorY].isLink) {
        fetchRealWebpage(screenLines[browserCursorY].linkUrl, true);
      }
      resetSleepTimer();
      lastDebounce = millis();
    }
  }
}
