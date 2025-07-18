#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>
#include <Adafruit_STMPE610.h>
#include <RH_RF95.h>

// --- Pin Definitions ---
#define STMPE_CS 6
#define TFT_CS   9
#define TFT_DC   10
#define RFM95_CS 8
#define RFM95_RST 4
#define RFM95_INT 7
#define RF95_FREQ 915.0

// --- Touch Calibration ---
#define TS_MINX 150   //150
#define TS_MAXX 3800  //3800
#define TS_MINY 13   //130
#define TS_MAXY 3800  //4000
#define TS_THRESHOLD 10  //50

// --- Display Constants ---
#define SCREEN_WIDTH 320
#define SCREEN_HEIGHT 240
#define KEY_WIDTH 30
#define KEY_HEIGHT 30
#define KEY_GAP 2
#define KEYBOARD_Y 80
#define INPUT_BOX_HEIGHT 30
#define MAX_INPUT_LEN 20
#define SENT_MSG_Y 35
#define RECV_MSG_Y 55

Adafruit_ILI9341 tft = Adafruit_ILI9341(TFT_CS, TFT_DC);
Adafruit_STMPE610 ts = Adafruit_STMPE610(STMPE_CS);
RH_RF95 rf95(RFM95_CS, RFM95_INT);

// --- Globals ---
String inputText = "";
bool redrawInput = true;
String lastSentMsg = "";
String lastRecvMsg = "";

// --- QWERTY Layout ---
const char* qwertyKeys[] = {
  "1234567890",
  "QWERTYUIOP",
  "ASDFGHJKL",
  "ZXCVBNM",
  "C_S<,.?"
};
const int qKeyRows = 5;
const int qKeyCols[] = {10,10, 9, 7, 7}; // Number of keys in each row

// --- Functions ---
void drawKeyboard() {
  tft.setTextSize(2);
  tft.setTextColor(ILI9341_WHITE, ILI9341_BLACK);
  
  for (int row = 0; row < qKeyRows; row++) {
    int cols = qKeyCols[row];
    int startX = (SCREEN_WIDTH - (cols * (KEY_WIDTH + KEY_GAP) - KEY_GAP)) / 2; // Center the row
    for (int col = 0; col < cols; col++) {
      char key = qwertyKeys[row][col];
      int x = startX + col * (KEY_WIDTH + KEY_GAP);
      int y = KEYBOARD_Y + row * (KEY_HEIGHT + KEY_GAP);
      int currentKeyWidth = KEY_WIDTH;
      uint16_t fillColor = ILI9341_DARKGREY;

      // Adjust for space bar (two key widths)
      if (row == 4 && col == 2) {
        currentKeyWidth = KEY_WIDTH * 2 + KEY_GAP; // Two key widths + gap
      }
      // Adjust colors for CLEAR and SEND
      if (row == 4 && col == 0) { // CLEAR
        fillColor = ILI9341_RED;
      }
      if (row == 4 && col == 2) { // SEND
        fillColor = ILI9341_GREEN;
      }

      tft.fillRect(x, y, currentKeyWidth, KEY_HEIGHT, fillColor);
      tft.drawRect(x, y, currentKeyWidth, KEY_HEIGHT, ILI9341_WHITE);
      tft.setCursor(x + 8, y + 6);
      if (key == '<' && row == 4) tft.print('\b');
      else if (key == '_' && row == 4) tft.print(' '); // Space bar
      else if (key == 'C' && row == 4) tft.print('C'); // CLEAR
      else if (key == 'S' && row == 4) tft.print('S'); // SEND
      else tft.print(key);
    }
  }
}

void drawInputBox() {
  tft.fillRect(0, 0, SCREEN_WIDTH, INPUT_BOX_HEIGHT, ILI9341_BLUE);
  tft.setCursor(5, 10);
  tft.setTextColor(ILI9341_WHITE);
  tft.setTextSize(2);
  tft.print(inputText);
  redrawInput = false;
}

void drawMessage(const char* msg, uint16_t color, int y) {
  tft.fillRect(0, y, SCREEN_WIDTH, 20, ILI9341_BLACK);
  tft.setCursor(10, y);
  tft.setTextColor(color);
  tft.setTextSize(2);
  tft.print(msg);
}

char getKeyFromTouch(int x, int y) {
  int row = (y - KEYBOARD_Y) / (KEY_HEIGHT + KEY_GAP);
  if (row < 0 || row >= qKeyRows) return '\0';

  int cols = qKeyCols[row];
  int startX = (SCREEN_WIDTH - (cols * (KEY_WIDTH + KEY_GAP) - KEY_GAP)) / 2; // Center the row
  int col = (x - startX) / (KEY_WIDTH + KEY_GAP);

  // Adjust for space bar (two key widths)
  if (row == 4 && x >= startX + (KEY_WIDTH + KEY_GAP) && x < startX + (KEY_WIDTH * 2 + KEY_GAP * 2)) {
    col = 1; // Space bar
  }

  if (col >= 0 && col < cols) {
    char key = qwertyKeys[row][col];
    if (key == '<') return '\b';
    if (key == 'S' && row == 4) return 1; // SEND
    if (key == 'C' && row == 4) return 2; // CLEAR
    if (key == '_' && row == 4) return ' '; // Space bar
    return key;
  }
  return '\0';
}

void setupLoRa() {
  pinMode(RFM95_RST, OUTPUT);
  digitalWrite(RFM95_RST, HIGH); delay(10);
  digitalWrite(RFM95_RST, LOW); delay(10);
  digitalWrite(RFM95_RST, HIGH); delay(10);

  if (!rf95.init()) {
    Serial.println("LoRa init failed");
    drawMessage("LoRa Init Failed", ILI9341_RED, SENT_MSG_Y);
    while (1);
  }

  if (!rf95.setFrequency(RF95_FREQ)) {
    Serial.println("LoRa freq failed");
    drawMessage("LoRa Freq Failed", ILI9341_RED, RECV_MSG_Y);
    while (1);
  }

  rf95.setTxPower(13, false);
}

void setup() {
  Serial.begin(115200);
  if (!ts.begin()) {
    Serial.println("Touchscreen init failed");
    while (1);
  }
  tft.begin();
  tft.setRotation(1);
  tft.fillScreen(ILI9341_BLACK);

  setupLoRa();

  drawKeyboard();
  drawInputBox();
}

void loop() {
  // Handle LoRa receiving
  if (rf95.available()) {
    uint8_t buf[RH_RF95_MAX_MESSAGE_LEN];
    uint8_t len = sizeof(buf);
    if (rf95.recv(buf, &len)) {
      buf[len] = 0;
      String recvMsg = "Recv: " + String((char*)buf);
      if (recvMsg != lastRecvMsg) {
        lastRecvMsg = recvMsg;
        drawMessage(recvMsg.c_str(), ILI9341_YELLOW, RECV_MSG_Y);
      }
    }
  }

  if (ts.bufferEmpty()) return;
  TS_Point p = ts.getPoint();
  while (!ts.bufferEmpty()) ts.getPoint(); // Clear rest

  if (p.z < TS_THRESHOLD) return;

  int x = map(p.y, TS_MINY, TS_MAXY, 0, SCREEN_WIDTH);
  int y = map(p.x, TS_MINX, TS_MAXX, 0, SCREEN_HEIGHT);

  static unsigned long lastTouch = 0;
  if (millis() - lastTouch < 250) return;
  lastTouch = millis();

  char key = getKeyFromTouch(x, y);
  if (key == 1) { // SEND
    if (inputText.length()) {
      rf95.send((uint8_t*)inputText.c_str(), inputText.length());
      rf95.waitPacketSent();
      lastSentMsg = "Sent: " + inputText;
      drawMessage(lastSentMsg.c_str(), ILI9341_GREEN, SENT_MSG_Y);
      inputText = "";
      redrawInput = true;
    }
  } else if (key == 2) { // CLEAR
    inputText = "";
    redrawInput = true;
  } else if (key == '\b') {
    if (inputText.length()) {
      inputText.remove(inputText.length() - 1);
      redrawInput = true;
    }
  } else if (key && inputText.length() < MAX_INPUT_LEN) {
    inputText += key;
    redrawInput = true;
  }

  if (redrawInput) drawInputBox();
}
