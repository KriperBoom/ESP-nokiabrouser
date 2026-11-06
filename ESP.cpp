##include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>
#include <SPI.h>
#include <WiFi.h>
#include <HTTPClient.h>

#define TFT_CS   15
#define TFT_RST  4
#define TFT_DC   2
#define TFT_LED  22 

Adafruit_ST7735 tft = Adafruit_ST7735(TFT_CS, TFT_DC, TFT_RST);

String savedSsid = "";
String savedPass = "";

#define BTN_UP         13 
#define BTN_DOWN       12 
#define BTN_SELECT     14 
#define BTN_POWER      27 
#define BTN_BACK_PG    26 
#define BTN_FORWARD_PG 25 

#define ST7735_DARKGRAY  0x4208 
#define ST7735_LIGHTGRAY 0xC618 
#define ST7735_IOS_BG    0x10A2 

enum BrowserState { BSTATE_HOME, BSTATE_KEYBOARD, BSTATE_VIEWER, BSTATE_HISTORY, BSTATE_WIFI_SCAN, BSTATE_SETTINGS, BSTATE_REBOOT_MENU };
BrowserState browserState = BSTATE_HOME;
BrowserState previousBrowserState = BSTATE_HOME;

bool darkMode = true;
int accentColorIndex = 0; 
uint16_t accentColors[] = { ST7735_BLUE, ST7735_RED, ST7735_GREEN, ST7735_WHITE };
String accentNames[] = { "BLUE", "RED", "GREEN", "WHITE" };

bool incognitoMode = false;
int homeMenuCursor = 0; 

bool isScreenOff = false;
bool isScreenLocked = false; 

int clockHours = 12;
int clockMins = 0;
unsigned long lastMinuteMillis = 0;

int settingsCursor = 0;
int autoSleepLevel = 0; 
unsigned long sleepIntervals[] = { 0, 15000, 30000, 60000 };
unsigned long lastActivityTime = 0;
bool lastPowerState = HIGH;
unsigned long powerPressStartTime = 0;
bool powerHeldTriggered = false;

String currentUrl = "https://kriperboom.github.io/ESP-nokiabrouser/index.html";
String keyboardInputText = "";
int kbRow = 0;
int kbCol = 0;
int kbMode = 1; 

const char* kbLow[4][10] = {
  { "q", "w", "e", "r", "t", "y", "u", "i", "o", "p" },
  { "a", "s", "d", "f", "g", "h", "j", "k", "l", "@" },
  { "z", "x", "c", "v", "b", "n", "m", ".", "_", "-" },
  { "123", "[_]", "[_]", "DEL", "ENT", "", "", "", "", "" }
};

const char* kbSym[4][10] = {
  { "1", "2", "3", "4", "5", "6", "7", "8", "9", "0" },
  { "(", ")", "<", ">", "_", "-", "?", "!", ".", "," },
  { "/", "@", "#", "$", "%", "&", "*", "+", "=", ";" },
  { "ABC", "[_]", "[_]", "DEL", "ENT", "", "", "", "", "" }
};

struct RenderLine {
  String text;
  bool isHeader;
  bool isLink;
  String linkUrl;
  uint16_t textColor;
};

RenderLine screenLines[40];
int totalLines = 0;
int viewerCursorY = 0; 
int scrollOffset = 0;    
String currentPageTitle = "Browser";

String historyUrls[10];
String historyTitles[10];
int historyCount = 0;
int historyCursor = 0;

String foundNetworks[10];
bool foundEncrypted[10];
int wifiNetworkCount = 0;
int wifiCursor = 0;

void fetchRealWebpage(String url, bool addToHistory = true);
void drawBrowserHome();
void drawVirtualKeyboard();
void drawContentView();
void drawHistoryPage();
void drawWifiScanPage();
void drawSettingsPage();
void drawLockScreen();
void drawRebootMenu();

void fadeInBacklight() {
  for (int i = 0; i <= 255; i += 25) {
    analogWrite(TFT_LED, i);
    delay(8);
  }
  analogWrite(TFT_LED, 255);
}

void fadeOutBacklight() {
  for (int i = 255; i >= 0; i -= 25) {
    analogWrite(TFT_LED, i);
    delay(8);
  }
  analogWrite(TFT_LED, 0);
}

void resetSleepTimer() {
  lastActivityTime = millis();
}

void updateClock() {
  if (millis() - lastMinuteMillis > 60000) {
    lastMinuteMillis = millis();
    clockMins++;
    if (clockMins >= 60) {
      clockMins = 0;
      clockHours = (clockHours + 1) % 24;
    }
  }
}

String getFormattedTime() {
  return (clockHours < 10 ? "0" : "") + String(clockHours) + ":" + (clockMins < 10 ? "0" : "") + String(clockMins);
}

void drawCPhoneLogo() {
  fadeInBacklight();
  tft.fillScreen(ST7735_BLACK);
  
  int cx = 64;
  int cy = 54;
  int boxSize = 56;

  tft.fillRoundRect(cx - boxSize/2, cy - boxSize/2, boxSize, boxSize, 12, ST7735_WHITE);
  tft.drawRoundRect(cx - boxSize/2 - 1, cy - boxSize/2 - 1, boxSize + 2, boxSize + 2, 13, ST7735_DARKGRAY);

  tft.fillCircle(cx, cy, 20, ST7735_BLACK);
  tft.drawCircle(cx, cy, 20, ST7735_WHITE);
  tft.drawEllipse(cx, cy, 8, 20, ST7735_WHITE);
  tft.drawFastHLine(cx - 19, cy, 38, ST7735_WHITE);

  tft.setTextSize(2);
  tft.setTextColor(ST7735_WHITE);
  tft.setCursor(cx - 38, cy + 34);
  tft.print("CBrowser");

  tft.setTextSize(1);
  tft.setTextColor(ST7735_LIGHTGRAY);
  tft.setCursor(20, cy + 56);
  tft.print("ESP32 Web Engine");
  delay(1200);
}

uint16_t parseColor(String colorStr) {
  colorStr.trim();
  colorStr.toLowerCase();
  if (colorStr == "red") return ST7735_RED;
  if (colorStr == "green") return ST7735_GREEN;
  if (colorStr == "blue") return ST7735_BLUE;
  if (colorStr == "white") return ST7735_WHITE;
  if (colorStr == "yellow") return ST7735_YELLOW;
  if (colorStr == "cyan") return ST7735_CYAN;
  if (colorStr == "magenta") return ST7735_MAGENTA;
  return darkMode ? ST7735_WHITE : ST7735_BLACK; 
}

void drawPixelIcon(int x, int y, int type, uint16_t color) {
  if (type == 0) {
    tft.drawCircle(x + 4, y + 4, 3, color);
    tft.drawFastHLine(x + 1, y + 4, 6, color);
  } else if (type == 1) {
    tft.fillRect(x + 1, y + 1, 2, 2, color);
    tft.fillRect(x + 5, y + 1, 2, 2, color);
    tft.fillRect(x + 1, y + 5, 2, 2, color);
    tft.fillRect(x + 5, y + 5, 2, 2, color);
  } else if (type == 2) {
    tft.drawCircle(x + 4, y + 4, 3, color);
    tft.drawFastVLine(x + 4, y + 2, 3, color);
  } else if (type == 3) {
    tft.drawRect(x, y + 2, 8, 4, color);
  } else if (type == 4) {
    tft.drawRect(x + 2, y + 2, 4, 4, color);
    tft.drawPixel(x + 4, y, color);
    tft.drawPixel(x + 4, y + 7, color);
  }
}

void addWrappedText(String text, bool isHeader, bool isLink, String linkUrl, uint16_t color) {
  int maxCharsPerLine = isHeader ? 10 : 21; 
  while (text.length() > 0 && totalLines < 40) {
    if (text.length() <= maxCharsPerLine) {
      screenLines[totalLines] = {text, isHeader, isLink, linkUrl, color};
      totalLines++;
      break;
    }
    int splitIdx = text.lastIndexOf(' ', maxCharsPerLine);
    if (splitIdx <= 0) splitIdx = maxCharsPerLine;
    String linePart = text.substring(0, splitIdx);
    linePart.trim();
    if (linePart.length() > 0) {
      screenLines[totalLines] = {linePart, isHeader, isLink, linkUrl, color};
      totalLines++;
    }
    text = text.substring(splitIdx);
    text.trim();
  }
}

void parseHTMLToLines(String html) {
  totalLines = 0;
  currentPageTitle = "Webpage";

  int titleStart = html.indexOf("<title>");
  if (titleStart == -1) titleStart = html.indexOf("<TITLE>");
  if (titleStart != -1) {
    int titleEnd = html.indexOf("</title>", titleStart);
    if (titleEnd == -1) titleEnd = html.indexOf("</TITLE>", titleStart);
    if (titleEnd != -1) {
      currentPageTitle = html.substring(titleStart + 7, titleEnd);
      currentPageTitle.trim();
    }
  }

  addWrappedText("<-- Back to Browser", false, true, "HOME_BACK", accentColors[accentColorIndex]);

  int scriptStart = html.indexOf("<script");
  while (scriptStart != -1) {
    int scriptEnd = html.indexOf("</script>", scriptStart);
    if (scriptEnd != -1) html = html.substring(0, scriptStart) + html.substring(scriptEnd + 9);
    else break;
    scriptStart = html.indexOf("<script");
  }

  uint16_t defaultColor = darkMode ? ST7735_WHITE : ST7735_BLACK;
  uint16_t currentTextColor = defaultColor;
  
  int pos = 0;
  while (pos < html.length() && totalLines < 40) {
    int tagOpen = html.indexOf('<', pos);
    if (tagOpen == -1) {
      String textPart = html.substring(pos);
      textPart.trim();
      if (textPart.length() > 0) addWrappedText(textPart, false, false, "", currentTextColor);
      break;
    }
    if (tagOpen > pos) {
      String textPart = html.substring(pos, tagOpen);
      textPart.trim();
      textPart.replace("&nbsp;", " ");
      if (textPart.length() > 0) addWrappedText(textPart, false, false, "", currentTextColor);
    }
    int tagClose = html.indexOf('>', tagOpen);
    if (tagClose == -1) break;
    
    String tag = html.substring(tagOpen + 1, tagClose);
    tag.trim();
    String tagLower = tag;
    tagLower.toLowerCase();
    
    if (tagLower.startsWith("h1")) {
      int endH1 = html.indexOf("</h1>", tagClose);
      if (endH1 != -1) {
        String h1Text = html.substring(tagClose + 1, endH1);
        h1Text.trim();
        if (h1Text.length() > 0) addWrappedText(h1Text, true, false, "", defaultColor); 
        pos = endH1 + 5;
        continue;
      }
    } else if (tagLower.startsWith("a ") || tagLower.startsWith("a href")) {
      int aClose = html.indexOf("</a>", tagClose);
      if (aClose != -1) {
        String linkText = html.substring(tagClose + 1, aClose);
        linkText.trim();
        String hrefUrl = "";
        int realQuoteIdx = tag.indexOf("href=");
        if (realQuoteIdx != -1) {
          char quoteChar = tag.charAt(realQuoteIdx + 5);
          int startQuote = realQuoteIdx + 6;
          int endQuote = tag.indexOf(quoteChar, startQuote);
          if (endQuote != -1) hrefUrl = tag.substring(startQuote, endQuote);
        }
        if (linkText.length() > 0) addWrappedText(linkText, false, true, hrefUrl, accentColors[accentColorIndex]);
        pos = aClose + 4;
        continue;
      }
    } else if (tagLower.startsWith("p") || tagLower.startsWith("div")) {
      int styleIdx = tagLower.indexOf("style=\"color:");
      if (styleIdx != -1) {
        int colStart = styleIdx + 13;
        int colEnd = tagLower.indexOf(';', colStart);
        if (colEnd == -1) colEnd = tagLower.indexOf('"', colStart);
        if (colEnd != -1) currentTextColor = parseColor(tagLower.substring(colStart, colEnd));
      }
    }
    pos = tagClose + 1;
  }
}

void drawNetworkStatus() {
  tft.setCursor(114, 3);
  tft.setTextColor(ST7735_WHITE);
  if (WiFi.status() == WL_CONNECTED) {
    tft.print("V"); 
  } else {
    tft.print("X"); 
  }
}

void drawLockScreen() {
  tft.fillScreen(ST7735_BLACK);
  
  tft.drawRoundRect(10, 15, 108, 128, 8, accentColors[accentColorIndex]);
  tft.drawRoundRect(11, 16, 106, 126, 7, ST7735_DARKGRAY);

  tft.setTextSize(1);
  tft.setTextColor(ST7735_LIGHTGRAY);
  tft.setCursor(20, 26);
  tft.print("CPhone Lock");

  tft.setTextSize(3);
  tft.setTextColor(ST7735_WHITE);
  tft.setCursor(22, 55);
  tft.print(getFormattedTime());

  tft.setTextSize(1);
  tft.setTextColor(accentColors[accentColorIndex]);
  tft.setCursor(48, 88);
  tft.print("[ LOCKED ]");

  tft.setTextColor(ST7735_LIGHTGRAY);
  tft.setCursor(22, 114);
  tft.print("Press [SEL] to");
  tft.setCursor(34, 126);
  tft.print("unlock");
}

void drawRebootMenu() {
  uint16_t selBg = accentColors[accentColorIndex];
  uint16_t textCol = darkMode ? ST7735_WHITE : ST7735_BLACK;

  tft.fillScreen(darkMode ? ST7735_BLACK : ST7735_WHITE);
  tft.fillRect(0, 0, 128, 14, selBg);
  tft.setTextColor(ST7735_WHITE);
  tft.setTextSize(1);
  tft.setCursor(2, 3);
  tft.print("Power Menu");

  tft.fillRect(10, 26, 108, 96, ST7735_IOS_BG);
  tft.drawRect(10, 26, 108, 96, ST7735_DARKGRAY);

  tft.setTextColor(ST7735_WHITE);
  tft.setCursor(20, 38);
  tft.print("Restart Device?");
  tft.drawFastHLine(16, 54, 96, ST7735_DARKGRAY);

  tft.setCursor(18, 68);
  tft.print("[SEL] Reboot");
  tft.setCursor(18, 92);
  tft.print("[PWR] Cancel");

  tft.setCursor(2, 145);
  tft.setTextColor(textCol, darkMode ? ST7735_BLACK : ST7735_WHITE);
  tft.print("[SEL]=Confirm [PWR]=Back");
}

void drawCurrentState() {
  if (browserState == BSTATE_HOME) drawBrowserHome();
  else if (browserState == BSTATE_SETTINGS) drawSettingsPage();
  else if (browserState == BSTATE_HISTORY) drawHistoryPage();
  else if (browserState == BSTATE_WIFI_SCAN) drawWifiScanPage();
  else if (browserState == BSTATE_KEYBOARD) drawVirtualKeyboard();
  else if (browserState == BSTATE_VIEWER) drawContentView();
  else if (browserState == BSTATE_REBOOT_MENU) drawRebootMenu();
}

void drawBrowserHome() {
  uint16_t bgCol = darkMode ? ST7735_BLACK : ST7735_WHITE;
  uint16_t textCol = darkMode ? ST7735_WHITE : ST7735_BLACK;
  uint16_t selBg = accentColors[accentColorIndex];
  uint16_t selText = (accentColorIndex == 2 || accentColorIndex == 3) ? ST7735_BLACK : ST7735_WHITE;

  tft.fillScreen(bgCol);
  tft.fillRect(0, 0, 128, 14, selBg);
  tft.setTextColor(ST7735_WHITE);
  tft.setTextSize(1);
  tft.setCursor(2, 3);
  tft.print(getFormattedTime());
  drawNetworkStatus();

  String options[5] = {
    "1. Open URL...",
    "2. Quick Demo Page",
    "3. History (" + String(historyCount) + ")",
    incognitoMode ? "4. Incognito: ON" : "4. Incognito: OFF",
    "5. Settings & Time"
  };

  int iconTypes[5] = {0, 1, 2, 3, 4};

  int startY = 22;
  for (int i = 0; i < 5; i++) {
    int itemY = startY + (i * 22);
    if (i == homeMenuCursor) {
      tft.fillRect(2, itemY - 2, 124, 18, selBg);
      tft.setTextColor(selText, selBg);
    } else {
      tft.setTextColor(textCol, bgCol);
    }
    uint16_t iconColor = (i == homeMenuCursor) ? selText : textCol;
    drawPixelIcon(6, itemY + 3, iconTypes[i], iconColor);
    
    tft.setCursor(18, itemY + 3);
    tft.print(options[i]);
  }

  tft.setCursor(2, 145);
  tft.setTextColor(textCol, bgCol);
  tft.print("[UP/DN] Move [SEL] Select");
}

void drawSettingsPage() {
  uint16_t bgCol = darkMode ? ST7735_BLACK : ST7735_WHITE;
  uint16_t textCol = darkMode ? ST7735_WHITE : ST7735_BLACK;
  uint16_t selBg = accentColors[accentColorIndex];
  uint16_t selText = (accentColorIndex == 2 || accentColorIndex == 3) ? ST7735_BLACK : ST7735_WHITE;

  tft.fillScreen(bgCol);
  tft.fillRect(0, 0, 128, 14, selBg);
  tft.setTextColor(ST7735_WHITE);
  tft.setTextSize(1);
  tft.setCursor(2, 3);
  tft.print("Settings");

  String sItems[7] = {
    darkMode ? "Theme: Dark" : "Theme: Light",
    "Accent: " + accentNames[accentColorIndex],
    "Set Hour: " + String(clockHours),
    "Set Minute: " + String(clockMins),
    "Wi-Fi Setup",
    "Auto-Sleep: " + (autoSleepLevel == 0 ? "OFF" : String(sleepIntervals[autoSleepLevel]/1000) + "s"),
    "[ SAVE & EXIT ]"
  };

  int startY = 16;
  for (int i = 0; i < 7; i++) {
    int itemY = startY + (i * 18);
    if (i == settingsCursor) {
      tft.fillRect(4, itemY - 2, 120, 15, selBg);
      tft.setTextColor(selText, selBg);
    } else {
      tft.setTextColor(textCol, bgCol);
    }
    tft.setCursor(8, itemY + 1);
    tft.print(sItems[i]);
  }

  tft.setCursor(2, 145);
  tft.setTextColor(textCol, bgCol);
  tft.print("[UP/DN] Change [SEL] Next");
}

void drawHistoryPage() {
  uint16_t bgCol = darkMode ? ST7735_BLACK : ST7735_WHITE;
  uint16_t textCol = darkMode ? ST7735_WHITE : ST7735_BLACK;
  uint16_t selBg = accentColors[accentColorIndex];
  uint16_t selText = (accentColorIndex == 2 || accentColorIndex == 3) ? ST7735_BLACK : ST7735_WHITE;

  tft.fillScreen(bgCol);
  tft.fillRect(0, 0, 128, 14, selBg);
  tft.setTextColor(ST7735_WHITE);
  tft.setTextSize(1);
  tft.setCursor(2, 3);
  tft.print("History");

  if (historyCount == 0) {
    tft.setCursor(10, 40);
    tft.setTextColor(textCol);
    tft.print("History is empty.");
  } else {
    int startY = 25;
    for (int i = 0; i < historyCount && i < 10; i++) {
      int itemY = startY + (i * 12);
      if (i == historyCursor) {
        tft.fillRect(2, itemY - 1, 124, 11, selBg);
        tft.setTextColor(selText, selBg);
      } else {
        tft.setTextColor(textCol, bgCol);
      }
      tft.setCursor(4, itemY);
      String tStr = historyTitles[i];
      if (tStr.length() > 18) tStr = tStr.substring(0, 18);
      tft.print(tStr);
    }
    
    int clearY = startY + (historyCount * 12) + 4;
    if (historyCursor == historyCount) {
      tft.fillRect(2, clearY - 1, 124, 11, selBg);
      tft.setTextColor(selText, selBg);
    } else {
      tft.setTextColor(ST7735_RED, bgCol);
    }
    tft.setCursor(4, clearY);
    tft.print("[ Clear History ]");
  }

  tft.setCursor(2, 145);
  tft.setTextColor(textCol, bgCol);
  tft.print("[SEL]=Open [PWR]=Back");
}

void drawWifiScanPage() {
  uint16_t bgCol = darkMode ? ST7735_BLACK : ST7735_WHITE;
  uint16_t textCol = darkMode ? ST7735_WHITE : ST7735_BLACK;
  uint16_t selBg = accentColors[accentColorIndex];
  uint16_t selText = (accentColorIndex == 2 || accentColorIndex == 3) ? ST7735_BLACK : ST7735_WHITE;

  tft.fillScreen(bgCol);
  tft.fillRect(0, 0, 128, 14, selBg);
  tft.setTextColor(ST7735_WHITE);
  tft.setTextSize(1);
  tft.setCursor(2, 3);
  tft.print("Select Wi-Fi");

  if (wifiNetworkCount == 0) {
    tft.setCursor(10, 40);
    tft.setTextColor(textCol);
    tft.print("No networks found!");
  } else {
    int startY = 25;
    for (int i = 0; i < wifiNetworkCount && i < 10; i++) {
      int itemY = startY + (i * 12);
      if (i == wifiCursor) {
        tft.fillRect(2, itemY - 1, 124, 11, selBg);
        tft.setTextColor(selText, selBg);
      } else {
        tft.setTextColor(textCol, bgCol);
      }
      tft.setCursor(4, itemY);
      String ssidStr = foundNetworks[i];
      if (ssidStr.length() > 14) ssidStr = ssidStr.substring(0, 14);
      tft.print(ssidStr);

      tft.setCursor(110, itemY);
      if (foundNetworks[i] == savedSsid && WiFi.status() == WL_CONNECTED) {
        tft.print("[V]");
      } else if (foundEncrypted[i]) {
        tft.print("[#]");
      }
    }
  }

  tft.setCursor(2, 145);
  tft.setTextColor(textCol, bgCol);
  tft.print("[SEL] Connect [PWR] Back");
}

void drawVirtualKeyboard() {
  uint16_t bgCol = darkMode ? ST7735_BLACK : ST7735_WHITE;
  uint16_t textCol = darkMode ? ST7735_WHITE : ST7735_BLACK;
  uint16_t selBg = accentColors[accentColorIndex];
  uint16_t selText = (accentColorIndex == 2 || accentColorIndex == 3) ? ST7735_BLACK : ST7735_WHITE;

  tft.fillScreen(bgCol);
  tft.fillRect(0, 0, 128, 14, selBg);
  tft.setTextColor(ST7735_WHITE);
  tft.setTextSize(1);
  tft.setCursor(2, 3);
  tft.print("Enter URL:");

  tft.drawRect(4, 18, 120, 16, textCol);
  tft.setCursor(8, 22);
  tft.setTextColor(textCol);
  String showText = keyboardInputText;
  if (showText.length() > 18) showText = showText.substring(showText.length() - 18);
  tft.print(showText + "_");

  tft.fillRect(2, 38, 124, 102, ST7735_IOS_BG);
  tft.drawRect(2, 38, 124, 102, ST7735_DARKGRAY);

  int startY = 42;
  for (int r = 0; r < 4; r++) {
    int y = startY + (r * 24);
    if (r < 3) {
      int keyWidth = 120 / 10;
      for (int c = 0; c < 10; c++) {
        String keyStr = (kbMode == 2) ? kbSym[r][c] : kbLow[r][c];
        int x = 4 + (c * keyWidth);
        int w = keyWidth - 2;
        int h = 21;

        if (r == kbRow && c == kbCol) {
          tft.fillRect(x, y, w, h, selBg);
          tft.setTextColor(selText, selBg);
        } else {
          tft.fillRect(x, y, w, h, ST7735_DARKGRAY);
          tft.setTextColor(ST7735_WHITE, ST7735_DARKGRAY);
        }

        int textX = x + (w - (keyStr.length() * 6)) / 2;
        tft.setCursor(textX, y + 7);
        tft.print(keyStr);
      }
    } else {
      int widths[5] = {24, 48, 0, 24, 24}; 
      int currentX = 4;
      for (int c = 0; c < 5; c++) {
        if (c == 2) continue; 
        String keyStr = (kbMode == 2) ? kbSym[3][c] : kbLow[3][c];
        int w = widths[c];
        if (c == 1) w = 46; 
        int h = 21;

        bool isSelected = false;
        if (kbRow == 3) {
          if (c == 1 && (kbCol == 1 || kbCol == 2)) isSelected = true;
          else if (kbCol == c) isSelected = true;
        }

        if (isSelected) {
          tft.fillRect(currentX, y, w, h, selBg);
          tft.setTextColor(selText, selBg);
        } else {
          tft.fillRect(currentX, y, w, h, ST7735_DARKGRAY);
          tft.setTextColor(ST7735_WHITE, ST7735_DARKGRAY);
        }

        int textX = currentX + (w - (keyStr.length() * 6)) / 2;
        tft.setCursor(textX, y + 7);
        tft.print(keyStr);

        currentX += w + 2;
      }
    }
  }

  tft.setCursor(2, 143);
  tft.setTextColor(textCol, bgCol);
  tft.print("[SEL]=Type [PWR]=Back");
}

void drawContentView() {
  uint16_t bgCol = darkMode ? ST7735_BLACK : ST7735_WHITE;
  uint16_t textCol = darkMode ? ST7735_WHITE : ST7735_BLACK;
  uint16_t selBg = accentColors[accentColorIndex];
  uint16_t selText = (accentColorIndex == 2 || accentColorIndex == 3) ? ST7735_BLACK : ST7735_WHITE;
  uint16_t normalCursorBg = darkMode ? ST7735_DARKGRAY : ST7735_LIGHTGRAY;
  uint16_t normalCursorText = darkMode ? ST7735_WHITE : ST7735_BLACK;

  tft.fillScreen(bgCol);
  tft.fillRect(0, 0, 128, 14, selBg);
  tft.setTextColor(ST7735_WHITE);
  tft.setTextSize(1);
  tft.setCursor(2, 3);
  
  String dispTitle = incognitoMode ? "[Incognito] " + currentPageTitle : currentPageTitle;
  if (dispTitle.length() > 13) dispTitle = dispTitle.substring(0, 13);
  tft.print(dispTitle);
  drawNetworkStatus();

  if (viewerCursorY < scrollOffset) scrollOffset = viewerCursorY;
  if (viewerCursorY >= scrollOffset + 11) scrollOffset = viewerCursorY - 10;

  int startY = 18;
  for (int i = scrollOffset; i < totalLines; i++) {
    tft.setCursor(2, startY);
    
    if (screenLines[i].isHeader) {
      tft.setTextSize(2); 
      if (i == viewerCursorY) {
        int w = screenLines[i].text.length() * 12;
        tft.fillRect(2, startY - 1, w + 4, 16, selBg);
        tft.setTextColor(selText, selBg);
      } else {
        tft.setTextColor(screenLines[i].textColor, bgCol);
      }
      tft.print(screenLines[i].text);
      startY += 18; 
    } else {
      tft.setTextSize(1);
      
      if (i == viewerCursorY) {
        int w = screenLines[i].text.length() * 6;
        if (screenLines[i].isLink) {
          tft.fillRect(2, startY - 1, w + 4, 10, selBg);
          tft.setTextColor(selText, selBg);
        } else {
          tft.fillRect(2, startY - 1, w + 4, 10, normalCursorBg);
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
  uint16_t bgCol = darkMode ? ST7735_BLACK : ST7735_WHITE;
  uint16_t textCol = darkMode ? ST7735_WHITE : ST7735_BLACK;
  uint16_t selBg = accentColors[accentColorIndex];

  if (url == "HOME_BACK") {
    browserState = BSTATE_HOME;
    drawBrowserHome();
    return;
  }

  tft.fillScreen(bgCol);
  tft.fillRect(0, 0, 128, 14, selBg);
  tft.setTextColor(ST7735_WHITE);
  tft.setCursor(2, 3);
  tft.print("Loading...");
  
  tft.setCursor(2, 20);
  tft.setTextColor(textCol);
  tft.print("Connecting to:\n" + url);

  HTTPClient http;
  http.begin(url);
  int httpCode = http.GET();

  if (httpCode > 0) {
    if (httpCode == HTTP_CODE_OK) {
      String payload = http.getString();
      parseHTMLToLines(payload); 
      
      if (addToHistory && !incognitoMode) {
        if (historyCount < 10) {
          historyUrls[historyCount] = url;
          historyTitles[historyCount] = currentPageTitle;
          historyCount++;
        } else {
          for (int i = 0; i < 9; i++) {
            historyUrls[i] = historyUrls[i + 1];
            historyTitles[i] = historyTitles[i + 1];
          }
          historyUrls[9] = url;
          historyTitles[9] = currentPageTitle;
        }
      }
    } else {
      totalLines = 0;
      currentPageTitle = "Error";
      addWrappedText("HTTP Error: " + String(httpCode), true, false, "", ST7735_RED);
      addWrappedText("<- Back", false, true, "HOME_BACK", accentColors[accentColorIndex]);
    }
  } else {
    totalLines = 0;
    currentPageTitle = "Error";
    addWrappedText("Connection failed!", true, false, "", ST7735_RED);
    addWrappedText("<- Back", false, true, "HOME_BACK", accentColors[accentColorIndex]);
  }
  http.end();

  viewerCursorY = 0; 
  scrollOffset = 0;
  browserState = BSTATE_VIEWER;
  drawContentView();
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

  drawCPhoneLogo();

  WiFi.mode(WIFI_STA);
  WiFi.begin(savedSsid.c_str(), savedPass.c_str());
  unsigned long startAttempt = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startAttempt < 3000) {
    delay(100);
  }

  browserState = BSTATE_HOME;
  drawBrowserHome();
  resetSleepTimer();
}

unsigned long lastDebounce = 0;
const int delayTime = 150; 

void loop() {
  updateClock();

  if (!isScreenOff && !isScreenLocked && autoSleepLevel > 0) {
    if (millis() - lastActivityTime > sleepIntervals[autoSleepLevel]) {
      fadeOutBacklight();
      isScreenOff = true;
      isScreenLocked = true;
    }
  }

  bool currentPowerState = digitalRead(BTN_POWER);
  if (currentPowerState == LOW) {
    if (lastPowerState == HIGH) {
      powerPressStartTime = millis();
      powerHeldTriggered = false;
    } else if (!powerHeldTriggered && (millis() - powerPressStartTime > 5000)) {
      powerHeldTriggered = true; 
      if (browserState != BSTATE_REBOOT_MENU) {
        previousBrowserState = browserState;
        browserState = BSTATE_REBOOT_MENU;
        drawRebootMenu();
      }
    }
  } else {
    if (lastPowerState == LOW) {
      if (!powerHeldTriggered) { 
        if (millis() - lastDebounce > 300) {
          if (!isScreenOff) {
            fadeOutBacklight();
            isScreenOff = true;
            isScreenLocked = true;
          } else {
            isScreenLocked = true;
            isScreenOff = false;
            drawLockScreen();
            fadeInBacklight();
          }
          lastDebounce = millis();
          resetSleepTimer();
        }
      }
    }
  }
  lastPowerState = currentPowerState;

  if (browserState == BSTATE_REBOOT_MENU) {
    if (digitalRead(BTN_SELECT) == LOW && millis() - lastDebounce > 200) {
      ESP.restart();
    }
    if (digitalRead(BTN_POWER) == LOW && millis() - lastDebounce > 200) {
      browserState = previousBrowserState;
      drawCurrentState();
      lastDebounce = millis();
    }
    return;
  }

  if (isScreenOff) {
    if (digitalRead(BTN_UP) == LOW || digitalRead(BTN_DOWN) == LOW || 
        digitalRead(BTN_SELECT) == LOW || digitalRead(BTN_BACK_PG) == LOW || 
        digitalRead(BTN_FORWARD_PG) == LOW) {
      if (millis() - lastDebounce > 200) {
        isScreenOff = false;
        isScreenLocked = true;
        drawLockScreen();
        fadeInBacklight();
        resetSleepTimer();
        lastDebounce = millis();
      }
    }
    return;
  }

  if (isScreenLocked) {
    if (digitalRead(BTN_SELECT) == LOW) {
      if (millis() - lastDebounce > 200) {
        isScreenLocked = false;
        drawCurrentState();
        resetSleepTimer();
        lastDebounce = millis();
      }
    }
    return;
  }

  bool anyButtonPressed = (digitalRead(BTN_UP) == LOW || digitalRead(BTN_DOWN) == LOW || 
                           digitalRead(BTN_SELECT) == LOW || digitalRead(BTN_BACK_PG) == LOW || 
                           digitalRead(BTN_FORWARD_PG) == LOW);
  if (anyButtonPressed) {
    resetSleepTimer();
  }

  if (browserState == BSTATE_HOME) {
    if (digitalRead(BTN_UP) == LOW && millis() - lastDebounce > delayTime) {
      homeMenuCursor = (homeMenuCursor - 1 + 5) % 5;
      drawBrowserHome();
      lastDebounce = millis();
    }
    if (digitalRead(BTN_DOWN) == LOW && millis() - lastDebounce > delayTime) {
      homeMenuCursor = (homeMenuCursor + 1) % 5;
      drawBrowserHome();
      lastDebounce = millis();
    }
    if (digitalRead(BTN_SELECT) == LOW && millis() - lastDebounce > delayTime) {
      if (homeMenuCursor == 0) {
        browserState = BSTATE_KEYBOARD;
        keyboardInputText = "https://";
        kbRow = 0; kbCol = 0; kbMode = 1;
        drawVirtualKeyboard();
      } else if (homeMenuCursor == 1) {
        // Quick Demo Page всегда загружает стандартный URL
        String demoUrl = "https://kriperboom.github.io/ESP-nokiabrouser/index.html";
        fetchRealWebpage(demoUrl, true);
      } else if (homeMenuCursor == 2) {
        browserState = BSTATE_HISTORY;
        historyCursor = 0;
        drawHistoryPage();
      } else if (homeMenuCursor == 3) {
        incognitoMode = !incognitoMode;
        drawBrowserHome();
      } else if (homeMenuCursor == 4) {
        browserState = BSTATE_SETTINGS;
        settingsCursor = 0;
        drawSettingsPage();
      }
      lastDebounce = millis();
    }
  } 
  else if (browserState == BSTATE_SETTINGS) {
    if (digitalRead(BTN_UP) == LOW && millis() - lastDebounce > delayTime) {
      settingsCursor = (settingsCursor - 1 + 7) % 7;
      drawSettingsPage();
      lastDebounce = millis();
    }
    if (digitalRead(BTN_DOWN) == LOW && millis() - lastDebounce > delayTime) {
      settingsCursor = (settingsCursor + 1) % 7;
      drawSettingsPage();
      lastDebounce = millis();
    }
    if (digitalRead(BTN_SELECT) == LOW && millis() - lastDebounce > delayTime) {
      if (settingsCursor == 0) darkMode = !darkMode;
      else if (settingsCursor == 1) accentColorIndex = (accentColorIndex + 1) % 4;
      else if (settingsCursor == 2) clockHours = (clockHours + 1) % 24;
      else if (settingsCursor == 3) clockMins = (clockMins + 5) % 60;
      else if (settingsCursor == 4) {
        browserState = BSTATE_WIFI_SCAN;
        wifiCursor = 0;
        tft.fillScreen(darkMode ? ST7735_BLACK : ST7735_WHITE);
        tft.setCursor(2, 20);
        tft.print("Scanning Wi-Fi...");
        wifiNetworkCount = WiFi.scanNetworks();
        for (int i = 0; i < wifiNetworkCount && i < 10; i++) {
          foundNetworks[i] = WiFi.SSID(i);
          foundEncrypted[i] = (WiFi.encryptionType(i) != WIFI_AUTH_OPEN);
        }
        drawWifiScanPage();
        return;
      }
      else if (settingsCursor == 5) autoSleepLevel = (autoSleepLevel + 1) % 4;
      else if (settingsCursor == 6) {
        browserState = BSTATE_HOME;
        drawBrowserHome();
        lastDebounce = millis();
        return;
      }
      drawSettingsPage();
      lastDebounce = millis();
    }
    if (digitalRead(BTN_POWER) == LOW && millis() - lastDebounce > delayTime) {
      browserState = BSTATE_HOME;
      drawBrowserHome();
      lastDebounce = millis();
    }
  }
  else if (browserState == BSTATE_HISTORY) {
    int maxItems = (historyCount > 0) ? historyCount + 1 : 1;
    if (digitalRead(BTN_UP) == LOW && millis() - lastDebounce > delayTime) {
      historyCursor = (historyCursor - 1 + maxItems) % maxItems;
      drawHistoryPage();
      lastDebounce = millis();
    }
    if (digitalRead(BTN_DOWN) == LOW && millis() - lastDebounce > delayTime) {
      historyCursor = (historyCursor + 1) % maxItems;
      drawHistoryPage();
      lastDebounce = millis();
    }
    if (digitalRead(BTN_SELECT) == LOW && millis() - lastDebounce > delayTime) {
      if (historyCount > 0 && historyCursor == historyCount) {
        historyCount = 0;
        historyCursor = 0;
        drawHistoryPage();
      } else if (historyCount > 0) {
        fetchRealWebpage(historyUrls[historyCursor], false);
      }
      lastDebounce = millis();
    }
    if (digitalRead(BTN_POWER) == LOW && millis() - lastDebounce > delayTime) {
      browserState = BSTATE_HOME;
      drawBrowserHome();
      lastDebounce = millis();
    }
  }
  else if (browserState == BSTATE_WIFI_SCAN) {
    if (digitalRead(BTN_UP) == LOW && millis() - lastDebounce > delayTime) {
      if (wifiNetworkCount > 0) {
        wifiCursor = (wifiCursor - 1 + wifiNetworkCount) % wifiNetworkCount;
        drawWifiScanPage();
      }
      lastDebounce = millis();
    }
    if (digitalRead(BTN_DOWN) == LOW && millis() - lastDebounce > delayTime) {
      if (wifiNetworkCount > 0) {
        wifiCursor = (wifiCursor + 1) % wifiNetworkCount;
        drawWifiScanPage();
      }
      lastDebounce = millis();
    }
    if (digitalRead(BTN_SELECT) == LOW && millis() - lastDebounce > delayTime) {
      if (wifiNetworkCount > 0) {
        savedSsid = foundNetworks[wifiCursor];
        savedPass = "";
        WiFi.begin(savedSsid.c_str(), savedPass.c_str());
        delay(1000);
      }
      browserState = BSTATE_SETTINGS;
      drawSettingsPage();
      lastDebounce = millis();
    }
    if (digitalRead(BTN_POWER) == LOW && millis() - lastDebounce > delayTime) {
      browserState = BSTATE_SETTINGS;
      drawSettingsPage();
      lastDebounce = millis();
    }
  }
  else if (browserState == BSTATE_KEYBOARD) {
    if (digitalRead(BTN_UP) == LOW && millis() - lastDebounce > delayTime) {
      kbRow = (kbRow - 1 + 4) % 4;
      if (kbRow == 3 && kbCol > 4) kbCol = 4;
      drawVirtualKeyboard();
      lastDebounce = millis();
    }
    if (digitalRead(BTN_DOWN) == LOW && millis() - lastDebounce > delayTime) {
      kbRow = (kbRow + 1) % 4;
      if (kbRow == 3 && kbCol > 4) kbCol = 4;
      drawVirtualKeyboard();
      lastDebounce = millis();
    }
    if (digitalRead(BTN_BACK_PG) == LOW && millis() - lastDebounce > delayTime) {
      int maxC = (kbRow == 3) ? 4 : 9;
      kbCol = (kbCol - 1 + (maxC + 1)) % (maxC + 1);
      drawVirtualKeyboard();
      lastDebounce = millis();
    }
    if (digitalRead(BTN_FORWARD_PG) == LOW && millis() - lastDebounce > delayTime) {
      int maxC = (kbRow == 3) ? 4 : 9;
      kbCol = (kbCol + 1) % (maxC + 1);
      drawVirtualKeyboard();
      lastDebounce = millis();
    }
    if (digitalRead(BTN_SELECT) == LOW && millis() - lastDebounce > delayTime) {
      String selChar = "";
      if (kbRow < 3) {
        selChar = (kbMode == 2) ? kbSym[kbRow][kbCol] : kbLow[kbRow][kbCol];
      } else {
        if (kbCol == 0) selChar = (kbMode == 2) ? "ABC" : "123";
        else if (kbCol == 1 || kbCol == 2) selChar = "[_]";
        else if (kbCol == 3) selChar = "DEL";
        else if (kbCol == 4) selChar = "ENT";
      }

      if (selChar == "DEL") {
        if (keyboardInputText.length() > 0) keyboardInputText = keyboardInputText.substring(0, keyboardInputText.length() - 1);
      } else if (selChar == "ENT") {
        currentUrl = keyboardInputText;
        lastDebounce = millis();
        fetchRealWebpage(currentUrl, true);
        return; // Прерываем цикл, чтобы состояние переключилось на VIEWER и код не обрабатывал клавиатуру дальше в этом же кадре
      } else if (selChar == "[_]") {
        keyboardInputText += " ";
      } else if (selChar == "123" || selChar == "ABC") {
        kbMode = (kbMode == 2) ? 1 : 2;
      } else {
        keyboardInputText += selChar;
      }
      drawVirtualKeyboard();
      lastDebounce = millis();
    }
    if (digitalRead(BTN_POWER) == LOW && millis() - lastDebounce > delayTime) {
      browserState = BSTATE_HOME;
      drawBrowserHome();
      lastDebounce = millis();
    }
  }
  else if (browserState == BSTATE_VIEWER) {
    if (digitalRead(BTN_UP) == LOW && millis() - lastDebounce > delayTime) {
      if (viewerCursorY > 0) viewerCursorY--;
      drawContentView();
      lastDebounce = millis();
    }
    if (digitalRead(BTN_DOWN) == LOW && millis() - lastDebounce > delayTime) {
      if (viewerCursorY < totalLines - 1) viewerCursorY++;
      drawContentView();
      lastDebounce = millis();
    }
    if (digitalRead(BTN_SELECT) == LOW && millis() - lastDebounce > delayTime) {
      if (screenLines[viewerCursorY].isLink) {
        String targetUrl = screenLines[viewerCursorY].linkUrl;
        fetchRealWebpage(targetUrl, true);
      }
      lastDebounce = millis();
    }
    if (digitalRead(BTN_POWER) == LOW && millis() - lastDebounce > delayTime) {
      browserState = BSTATE_HOME;
      drawBrowserHome();
      lastDebounce = millis();
    }
  }
}
