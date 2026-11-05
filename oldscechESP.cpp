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

#define ST7735_DARKGRAY  0x4208 
#define ST7735_LIGHTGRAY 0xC618 
#define ST7735_IOS_BG    0x10A2 

enum AppState { STATE_HOME, STATE_BROWSER, STATE_SETTINGS, STATE_WIFI_SCAN, STATE_KEYBOARD };
AppState currentState = STATE_HOME;

struct MenuItem {
  String name;
  String url;
};

MenuItem menuItems[] = {
  { "1. ESP-NokiaBrouser", "https://kriperboom.github.io/ESP-nokiabrouser/index.html" },
  { "2. NeverSSL", "http://neverssl.com" },
  { "3. CNN Lite", "http://lite.cnn.com" },
  { "4. Wi-Fi Setup", "" },
  { "5. Settings", "" }
};
const int menuCount = 5;
int cursorIndex = 0;       

bool darkMode = true;
int accentColorIndex = 0; 
uint16_t accentColors[] = { ST7735_BLUE, ST7735_RED, ST7735_GREEN, ST7735_WHITE };
String accentNames[] = { "BLUE", "RED", "GREEN", "WHITE" };

int autoSleepLevel = 0;
unsigned long sleepIntervals[] = { 0, 15000, 30000, 60000 };
String sleepNames[] = { "OFF", "15s", "30s", "60s" };
unsigned long lastActivityTime = 0;
bool isSleeping = false;

int settingsCursor = 0; 
bool screenOn = true;
bool lastPowerState = HIGH; 

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
  tft.fillScreen(ST7735_BLACK);
  int cx = 64, cy = 64, radius = 24;
  tft.drawCircle(cx, cy, radius, ST7735_BLUE);
  tft.drawCircle(cx, cy, radius + 1, ST7735_BLUE);
  tft.drawEllipse(cx, cy, radius / 2, radius, ST7735_CYAN);
  tft.drawEllipse(cx, cy, radius, radius / 2, ST7735_CYAN);
  tft.drawFastHLine(cx - radius, cy, radius * 2, ST7735_CYAN);
  tft.drawFastVLine(cx, cy - radius, radius * 2, ST7735_CYAN);

  tft.setTextSize(1);
  tft.setTextColor(ST7735_WHITE);
  tft.setCursor(20, 110);
  tft.print("ESP-NokiaBrouser");
  delay(1500);
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

  addWrappedText("<-- Back to Menu", false, true, "HOME_BACK", accentColors[accentColorIndex]);

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

void drawHomePage() {
  uint16_t bgCol = darkMode ? ST7735_BLACK : ST7735_WHITE;
  uint16_t textCol = darkMode ? ST7735_WHITE : ST7735_BLACK;
  uint16_t selBg = accentColors[accentColorIndex];
  uint16_t selText = (accentColorIndex == 2 || accentColorIndex == 3) ? ST7735_BLACK : ST7735_WHITE;

  tft.fillScreen(bgCol);
  tft.fillRect(0, 0, 128, 14, accentColors[accentColorIndex]);
  tft.setTextColor(ST7735_WHITE);
  tft.setTextSize(1);
  tft.setCursor(2, 3);
  tft.print("ESP-NokiaBrouser");
  
  drawNetworkStatus();

  tft.setCursor(4, 20);
  tft.setTextColor(textCol);
  tft.print("Select item:");

  int startY = 38;
  for (int i = 0; i < menuCount; i++) {
    int itemY = startY + (i * 19);
    if (i == cursorIndex) {
      tft.fillRect(2, itemY - 2, 124, 15, selBg);
      tft.setTextColor(selText, selBg);
    } else {
      tft.setTextColor(textCol, bgCol);
    }
    tft.setCursor(6, itemY + 1);
    tft.print(menuItems[i].name);
  }

  tft.setTextColor(textCol, bgCol);
  tft.setCursor(2, 145);
  tft.print("[UP/DN] Move [SEL] Open");
}

void drawSettingsPage() {
  uint16_t bgCol = darkMode ? ST7735_BLACK : ST7735_WHITE;
  uint16_t textCol = darkMode ? ST7735_WHITE : ST7735_BLACK;
  uint16_t selBg = accentColors[accentColorIndex];
  uint16_t selText = (accentColorIndex == 2 || accentColorIndex == 3) ? ST7735_BLACK : ST7735_WHITE;

  tft.fillScreen(bgCol);
  tft.fillRect(0, 0, 128, 14, accentColors[accentColorIndex]);
  tft.setTextColor(ST7735_WHITE);
  tft.setTextSize(1);
  tft.setCursor(2, 3);
  tft.print("Settings");

  String sItems[4] = {
    darkMode ? "Theme: Dark" : "Theme: Light",
    "Accent: " + accentNames[accentColorIndex],
    "Auto-Sleep: " + sleepNames[autoSleepLevel],
    "[ SAVE & EXIT ]"
  };

  int startY = 28;
  for (int i = 0; i < 4; i++) {
    int itemY = startY + (i * 26);
    if (i == settingsCursor) {
      tft.fillRect(10, itemY - 2, 108, 18, selBg);
      tft.setTextColor(selText, selBg);
    } else {
      tft.setTextColor(textCol, bgCol);
    }
    tft.setCursor(16, itemY + 3);
    tft.print(sItems[i]);
  }

  tft.setCursor(2, 145);
  tft.setTextColor(textCol, bgCol);
  tft.print("[UP/DN] Move [SEL] Select");
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
      if (ssidStr.length() > 18) ssidStr = ssidStr.substring(0, 18);
      tft.print(ssidStr);
    }
  }

  tft.setCursor(2, 145);
  tft.setTextColor(textCol, bgCol);
  tft.print("[UP/DN] Move [SEL] Choose");
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
  tft.print("Wi-Fi Pass:");

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
        tft.fillRect(x, y, w, h, selBg);
        tft.setTextColor(selText, selBg);
      } else {
        tft.fillRect(x, y, w, h, ST7735_DARKGRAY);
        tft.setTextColor(ST7735_WHITE, ST7735_DARKGRAY);
      }

      int textX = x + (w - (keyStr.length() * 6)) / 2;
      int textY = y + 7;
      tft.setCursor(textX, textY);
      tft.print(keyStr);
    }
  }

  tft.setCursor(2, 143);
  tft.setTextColor(textCol, bgCol);
  tft.print("[SEL]=Type [PWR]=Bksp");
}

void drawContentPage() {
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
  
  String dispTitle = currentPageTitle;
  if (dispTitle.length() > 13) dispTitle = dispTitle.substring(0, 13);
  tft.print(dispTitle);
  
  drawNetworkStatus();

  if (browserCursorY < scrollOffset) {
    scrollOffset = browserCursorY;
  }
  if (browserCursorY >= scrollOffset + 11) {
    scrollOffset = browserCursorY - 10;
  }

  int startY = 18;
  for (int i = scrollOffset; i < totalLines; i++) {
    tft.setCursor(2, startY);
    
    if (screenLines[i].isHeader) {
      tft.setTextSize(2); 
      if (i == browserCursorY) {
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
      
      if (i == browserCursorY) {
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

  if (totalLines > 11) {
    int scrollbarHeight = 130;
    int thumbHeight = max(10, scrollbarHeight * 11 / totalLines);
    int thumbY = 16 + (scrollOffset * (scrollbarHeight - thumbHeight) / max(1, (totalLines - 11)));
    tft.drawFastVLine(126, 16, scrollbarHeight, textCol);
    tft.fillRect(125, thumbY, 3, thumbHeight, ST7735_RED);
  }
}

void drawErrorPage(String errMsg) {
  totalLines = 0;
  currentPageTitle = "Error";
  addWrappedText("Failed to connect!", true, false, "", ST7735_RED);
  addWrappedText(errMsg, false, false, "", darkMode ? ST7735_WHITE : ST7735_BLACK);
  addWrappedText("---------------------", false, false, "", ST7735_DARKGRAY);
  addWrappedText("<- go to menu", false, true, "HOME_BACK", accentColors[accentColorIndex]);
  
  browserCursorY = 3; 
  scrollOffset = 0;
  drawContentPage();
}

void fetchRealWebpage(String url, bool addToHistory) {
  uint16_t bgCol = darkMode ? ST7735_BLACK : ST7735_WHITE;
  uint16_t textCol = darkMode ? ST7735_WHITE : ST7735_BLACK;
  uint16_t selBg = accentColors[accentColorIndex];

  if (url == "HOME_BACK") {
    currentState = STATE_HOME;
    drawHomePage();
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
      
      if (addToHistory) {
        if (historyPos < 9) {
          historyPos++;
          historyUrls[historyPos] = url;
          historyTitles[historyPos] = currentPageTitle;
          historyCount = historyPos + 1;
        } else {
          for (int i = 0; i < 9; i++) {
            historyUrls[i] = historyUrls[i + 1];
            historyTitles[i] = historyTitles[i + 1];
          }
          historyUrls[9] = url;
          historyTitles[9] = currentPageTitle;
          historyCount = 10;
          historyPos = 9;
        }
      }
    } else {
      drawErrorPage("HTTP Error code: " + String(httpCode));
      return;
    }
  } else {
    drawErrorPage("Check network & URL!");
    return;
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

  tft.fillScreen(ST7735_BLACK);
  tft.setCursor(2, 20);
  tft.setTextColor(ST7735_WHITE);
  tft.print("Starting offline...");

  WiFi.mode(WIFI_STA);
  WiFi.begin(savedSsid.c_str(), savedPass.c_str());
  unsigned long startAttempt = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startAttempt < 2000) {
    delay(100);
  }

  drawHomePage();
  resetSleepTimer();
}

unsigned long lastDebounce = 0;
const int delayTime = 150; 

void loop() {
  bool currentPowerState = digitalRead(BTN_POWER);
  if (currentPowerState == LOW && lastPowerState == HIGH) {
    if (millis() - lastDebounce > 300) {
      screenOn = !screenOn;
      if (screenOn) {
        fadeInBacklight();
      } else {
        fadeOutBacklight();
      }
      lastDebounce = millis();
      resetSleepTimer();
    }
  }
  lastPowerState = currentPowerState;

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
        if (!screenOn) {
          screenOn = true;
          fadeInBacklight();
        } else if (isSleeping) {
          isSleeping = false;
          fadeInBacklight();
        }
        resetSleepTimer();
        lastDebounce = millis();
      }
    }
    return; 
  }

  if (currentState == STATE_HOME) {
    if (digitalRead(BTN_UP) == LOW) {
      if (millis() - lastDebounce > delayTime) {
        cursorIndex = (cursorIndex - 1 + menuCount) % menuCount;
        drawHomePage();
        resetSleepTimer();
        lastDebounce = millis();
      }
    }
    if (digitalRead(BTN_DOWN) == LOW) {
      if (millis() - lastDebounce > delayTime) {
        cursorIndex = (cursorIndex + 1) % menuCount;
        drawHomePage();
        resetSleepTimer();
        lastDebounce = millis();
      }
    }
    if (digitalRead(BTN_SELECT) == LOW) {
      if (millis() - lastDebounce > delayTime) {
        if (cursorIndex == 3) {
          currentState = STATE_WIFI_SCAN;
          wifiCursor = 0;
          
          tft.fillScreen(darkMode ? ST7735_BLACK : ST7735_WHITE);
          tft.setCursor(2, 20);
          tft.setTextColor(darkMode ? ST7735_WHITE : ST7735_BLACK);
          tft.print("Scanning Wi-Fi...");
          
          WiFi.mode(WIFI_STA);
          WiFi.disconnect();
          delay(100);
          
          wifiNetworkCount = WiFi.scanNetworks();
          for (int i = 0; i < wifiNetworkCount && i < 10; i++) {
            foundNetworks[i] = WiFi.SSID(i);
          }
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
  } 
  else if (currentState == STATE_SETTINGS) {
    if (digitalRead(BTN_UP) == LOW) {
      if (millis() - lastDebounce > delayTime) {
        settingsCursor = (settingsCursor - 1 + 4) % 4;
        drawSettingsPage();
        resetSleepTimer();
        lastDebounce = millis();
      }
    }
    if (digitalRead(BTN_DOWN) == LOW) {
      if (millis() - lastDebounce > delayTime) {
        settingsCursor = (settingsCursor + 1) % 4;
        drawSettingsPage();
        resetSleepTimer();
        lastDebounce = millis();
      }
    }
    if (digitalRead(BTN_SELECT) == LOW) {
      if (millis() - lastDebounce > delayTime) {
        if (settingsCursor == 0) {
          darkMode = !darkMode; 
        } else if (settingsCursor == 1) {
          accentColorIndex = (accentColorIndex + 1) % 4; 
        } else if (settingsCursor == 2) {
          autoSleepLevel = (autoSleepLevel + 1) % 4; 
        } else if (settingsCursor == 3) {
          currentState = STATE_HOME;
          drawHomePage();
          resetSleepTimer();
          lastDebounce = millis();
          return;
        }
        drawSettingsPage();
        resetSleepTimer();
        lastDebounce = millis();
      }
    }
  }
  else if (currentState == STATE_WIFI_SCAN) {
    if (digitalRead(BTN_UP) == LOW) {
      if (millis() - lastDebounce > delayTime) {
        if (wifiNetworkCount > 0) {
          wifiCursor = (wifiCursor - 1 + wifiNetworkCount) % wifiNetworkCount;
          drawWifiScanPage();
        }
        resetSleepTimer();
        lastDebounce = millis();
      }
    }
    if (digitalRead(BTN_DOWN) == LOW) {
      if (millis() - lastDebounce > delayTime) {
        if (wifiNetworkCount > 0) {
          wifiCursor = (wifiCursor + 1) % wifiNetworkCount;
          drawWifiScanPage();
        }
        resetSleepTimer();
        lastDebounce = millis();
      }
    }
    if (digitalRead(BTN_SELECT) == LOW) {
      if (millis() - lastDebounce > delayTime) {
        if (wifiNetworkCount > 0) {
          tempSsid = foundNetworks[wifiCursor];
          currentState = STATE_KEYBOARD;
          inputTargetType = "PASS"; 
          keyboardInputText = "";
          kbRow = 0; kbCol = 0;
          kbMode = 0;
          drawVirtualKeyboard();
        } else {
          currentState = STATE_HOME;
          drawHomePage();
        }
        resetSleepTimer();
        lastDebounce = millis();
      }
    }
  }
  else if (currentState == STATE_KEYBOARD) {
    int maxCols = (kbMode == 2) ? symCols[kbRow] : ((kbMode == 1) ? lowCols[kbRow] : capsCols[kbRow]);

    if (digitalRead(BTN_UP) == LOW) {
      if (millis() - lastDebounce > delayTime) {
        kbRow = (kbRow - 1 + 4) % 4;
        int newMax = (kbMode == 2) ? symCols[kbRow] : ((kbMode == 1) ? lowCols[kbRow] : capsCols[kbRow]);
        if (kbCol >= newMax) kbCol = newMax - 1;
        drawVirtualKeyboard();
        resetSleepTimer();
        lastDebounce = millis();
      }
    }
    if (digitalRead(BTN_DOWN) == LOW) {
      if (millis() - lastDebounce > delayTime) {
        kbRow = (kbRow + 1) % 4;
        int newMax = (kbMode == 2) ? symCols[kbRow] : ((kbMode == 1) ? lowCols[kbRow] : capsCols[kbRow]);
        if (kbCol >= newMax) kbCol = newMax - 1;
        drawVirtualKeyboard();
        resetSleepTimer();
        lastDebounce = millis();
      }
    }
    if (digitalRead(BTN_BACK_PG) == LOW) {
      if (millis() - lastDebounce > delayTime) {
        int curMax = (kbMode == 2) ? symCols[kbRow] : ((kbMode == 1) ? lowCols[kbRow] : capsCols[kbRow]);
        kbCol = (kbCol - 1 + curMax) % curMax;
        drawVirtualKeyboard();
        resetSleepTimer();
        lastDebounce = millis();
      }
    }
    if (digitalRead(BTN_FORWARD_PG) == LOW) {
      if (millis() - lastDebounce > delayTime) {
        int curMax = (kbMode == 2) ? symCols[kbRow] : ((kbMode == 1) ? lowCols[kbRow] : capsCols[kbRow]);
        kbCol = (kbCol + 1) % curMax;
        drawVirtualKeyboard();
        resetSleepTimer();
        lastDebounce = millis();
      }
    }

    if (digitalRead(BTN_SELECT) == LOW) {
      if (millis() - lastDebounce > delayTime) {
        String selChar = (kbMode == 2) ? kbSym[kbRow][kbCol] : ((kbMode == 1) ? kbLow[kbRow][kbCol] : kbCaps[kbRow][kbCol]);
        
        if (selChar == "DEL") {
          if (keyboardInputText.length() > 0) {
            keyboardInputText = keyboardInputText.substring(0, keyboardInputText.length() - 1);
          }
        } else if (selChar == "ENT") {
          // Прямая обработка виртуального Enter
          savedSsid = tempSsid;
          savedPass = keyboardInputText;
          
          tft.fillScreen(darkMode ? ST7735_BLACK : ST7735_WHITE);
          tft.setCursor(2, 20);
          tft.setTextColor(darkMode ? ST7735_WHITE : ST7735_BLACK);
          tft.print("Connecting to:\n" + savedSsid);
          
          WiFi.begin(savedSsid.c_str(), savedPass.c_str());
          int timeout = 0;
          while (WiFi.status() != WL_CONNECTED && timeout < 15) {
            delay(500);
            timeout++;
          }
          
          currentState = STATE_HOME;
          drawHomePage();
        } else if (selChar == "SPC") {
          keyboardInputText += " ";
        } else if (selChar == "^") {
          if (kbMode == 0) kbMode = 1;
          else if (kbMode == 1) kbMode = 0;
          else kbMode = 0;
        } else if (selChar == "123" || selChar == "ABC") {
          if (kbMode == 2) kbMode = 0; 
          else kbMode = 2; 
          kbRow = 0; kbCol = 0;
        } else {
          keyboardInputText += selChar;
        }
        
        if (currentState == STATE_KEYBOARD) {
          drawVirtualKeyboard();
        }
        resetSleepTimer();
        lastDebounce = millis();
      }
    }
  }
  else if (currentState == STATE_BROWSER) {
    if (digitalRead(BTN_BACK_PG) == LOW) {
      if (millis() - lastDebounce > delayTime) {
        if (historyPos > 0) {
          historyPos--;
          fetchRealWebpage(historyUrls[historyPos], false);
        }
        resetSleepTimer();
        lastDebounce = millis();
      }
    }

    if (digitalRead(BTN_FORWARD_PG) == LOW) {
      if (millis() - lastDebounce > delayTime) {
        if (historyPos < historyCount - 1) {
          historyPos++;
          fetchRealWebpage(historyUrls[historyPos], false);
        }
        resetSleepTimer();
        lastDebounce = millis();
      }
    }

    if (digitalRead(BTN_UP) == LOW) {
      if (millis() - lastDebounce > delayTime) {
        if (browserCursorY > 0) {
          browserCursorY--;
          drawContentPage();
        }
        resetSleepTimer();
        lastDebounce = millis();
      }
    }

    if (digitalRead(BTN_DOWN) == LOW) {
      if (millis() - lastDebounce > delayTime) {
        if (browserCursorY < totalLines - 1) {
          browserCursorY++;
          drawContentPage();
        }
        resetSleepTimer();
        lastDebounce = millis();
      }
    }

    if (digitalRead(BTN_SELECT) == LOW) {
      if (millis() - lastDebounce > delayTime) {
        if (screenLines[browserCursorY].isLink) {
          String targetUrl = screenLines[browserCursorY].linkUrl;
          fetchRealWebpage(targetUrl, true);
        }
        resetSleepTimer();
        lastDebounce = millis();
      }
    }
  }
}
