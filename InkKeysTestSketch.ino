#include "settings.h" // Customize your settings in settings.h!
#include <Adafruit_NeoPixel.h>
#include <Encoder.h>
#include <GxEPD2_BW.h>
#include <Bounce2.h>
#include <HID-Project.h>
#include <SPI.h>
#include "epdpaint.h"
#include "fonts.h"

#define COLORED     0
#define UNCOLORED   1

enum { VOLUME, BRIGHTNESS, ROTATE };
int currentState = VOLUME;

byte rows[] = { 6, 7 };
const int rowCount = sizeof(rows) / sizeof(rows[0]);

byte cols[] = { 2, 3, 4, 5 };
const int colCount = sizeof(cols) / sizeof(cols[0]);

byte keyValues = 0;

int ledKeyIndex[] = { 0, 7, 1, 6, 2, 5, 3, 4 };

volatile long oldPosition = 0;

unsigned char image[1024];
Paint paint(image, 0, 0); // width should be the multiple of 8
unsigned long time_start_ms;
unsigned long time_now_s;

Bounce2::Button rotButton;

typedef struct {
  Adafruit_NeoPixel Strip;
  String Mode;
  int Brightness;
  int OffsetBrightness;
  int MinBrightness;
  int MaxBrightness;
} LedStrip;

LedStrip leds[LEDS] = {
  {
    Adafruit_NeoPixel(8, LED_KEY_PIN, NEO_GRB + NEO_KHZ800),
    "white",
    10,
    20,
    0,
    200,
  },
  {
    Adafruit_NeoPixel(12, LED_RING_PIN, NEO_GRB + NEO_KHZ800),
    "white",
    10,
    0,
    0,
    200,
  },
};

Encoder rotary(PIN_ROTA, PIN_ROTB);

GxEPD2_290_T94_V2 display(/*CS=*/CS_PIN, /*DC=*/DC_PIN, /*RST=*/RST_PIN, /*BUSY=*/BUSY_PIN);

void clearDisplay() {
  display.writeScreenBuffer();
  display.refresh();
  display.writeScreenBufferAgain();
  display.powerOff();
}

void printMode(char* mode) {
  paint.SetWidth(128);
  paint.SetHeight(20);
  paint.SetRotate(ROTATE_180);
  paint.Clear(UNCOLORED);
  paint.DrawStringAt(10, 4, mode, &Font16, COLORED);
  display.writeImage(paint.GetImage(), 1, 1, paint.GetWidth(), paint.GetHeight(), true, false, false);
  display.refresh(true);
}

void printTestImage() {
  time_now_s = (millis() - time_start_ms) / 1000;
  char time_string[] = {'0', '0', ':', '0', '0', '\0'};
  time_string[0] = time_now_s / 60 / 10 + '0';
  time_string[1] = time_now_s / 60 % 10 + '0';
  time_string[3] = time_now_s % 60 / 10 + '0';
  time_string[4] = time_now_s % 60 % 10 + '0';

  paint.SetWidth(32);
  paint.SetHeight(96);
  paint.SetRotate(ROTATE_90);

  paint.Clear(UNCOLORED);
  paint.DrawStringAt(0, 4, time_string, &Font24, COLORED);

  display.writeImage(paint.GetImage(), 100, 100, paint.GetWidth(), paint.GetHeight(), false, false, false);
  display.refresh(true);
}

// func predefs
void RainbowCycle(uint16_t ledIndex, uint8_t wait = 10, int times = 1);

void setup() {
  Serial.begin(57600);
  Consumer.begin();

  display.init(0, true, 0, false);

  currentState = -1;
  pinMode(ROT_PIN, INPUT_PULLUP);
  rotButton.attach(ROT_PIN);
  rotButton.setPressedState(LOW);
  rotButton.interval(10);

  for (int x = 0; x < rowCount; x++) {
    Serial.print(rows[x]);
    Serial.println(" as input");
    pinMode(rows[x], INPUT);
  }

  for (int x = 0; x < colCount; x++) {
    Serial.print(cols[x]);
    Serial.println(" as input-pullup");
    pinMode(cols[x], INPUT_PULLUP);
  }

  // Initilize LEDS
  for (int i = 0; i < LEDS; i++) {
    Serial.println(i);
    leds[i].Strip.begin();
    leds[i].Strip.setBrightness(leds[i].Brightness + leds[i].OffsetBrightness);
    leds[i].Strip.fill(Adafruit_NeoPixel::Color(255, 255, 255), 0);
    leds[i].Strip.show();
  }

  delay(15000);
  RainbowCycle(1);
  clearDisplay();
  beginState(VOLUME);
}

void IncreaseBrightness() {
  for (int i = 0; i < LEDS; i++) {
    leds[i].Brightness = leds[i].Brightness + 1;
    leds[i].Brightness = leds[i].Brightness > leds[i].MaxBrightness ? leds[i].MaxBrightness : leds[i].Brightness;
    leds[i].Strip.setBrightness(leds[i].Brightness);
  }
}

void DecreaseBrightness() {
  for (int i = 0; i < LEDS; i++) {
    leds[i].Brightness = leds[i].Brightness - 1;
    leds[i].Brightness = leds[i].Brightness < leds[i].MinBrightness ? leds[i].MinBrightness : leds[i].Brightness;
    leds[i].Strip.setBrightness(leds[i].Brightness);
  }
}

void FillLeds(uint32_t color) {
  for (int i = 0; i < LEDS; i++) leds[i].Strip.fill(color, 0);
}

void ShowLeds() {
  for (int i = 0; i < LEDS; i++) leds[i].Strip.show();
}


void LedBrightness(int brightness) {
  for (int i = 0; i < LEDS; i++) {
    leds[i].Brightness = map(
      abs(brightness), 0, 100, leds[i].MinBrightness, leds[i].MaxBrightness
      ) + leds[i].OffsetBrightness;
    leds[i].Strip.setBrightness(leds[i].Brightness);
  } 
}

void SetPixelColor(uint32_t color, int stripIndex = 0, int ledIndex = 0) {
  leds[stripIndex].Strip.setPixelColor(ledIndex, color);
}

// This methods loops through the rows and columns activating an deactivating them
// to determine wether the button is pressed or not, resets to unpressed before
// scanning
void ScanButtons() {
  int index = 0;
  keyValues = 0;

  // iterate the columns
  for (int c = 0; c < colCount; c++) {

    // col: set to output to low
    pinMode(cols[c], OUTPUT);
    digitalWrite(cols[c], LOW);

    // row: interate through the rows
    for (int r = 0; r < rowCount; r++) {
      pinMode(rows[r], INPUT_PULLUP);

      // Set the bit to high, should the button be pressed
      if (digitalRead(rows[r]) == LOW) bitSet(keyValues, index);

      // disabled the row by setting to input
      pinMode(rows[r], INPUT);
      index++;
    }

    // disable the column
    pinMode(cols[c], INPUT);
  }
}

void LightKeyPressed() {
  for (int i = 0; i < 8; i++) {
    if (bitRead(keyValues, i) == LOW) continue;

    SetPixelColor(Adafruit_NeoPixel::Color(0, 255, 0), 0, ledKeyIndex[i]);
  }
}

void RotVolume() {
  long newPosition = rotary.read();
  if (newPosition == oldPosition) return;
  long delta = newPosition - oldPosition;

  if (delta > 0) {
    Consumer.write(MEDIA_VOLUME_UP);
    Serial.println("Volume UP");
  }
  else {
    Consumer.write(MEDIA_VOLUME_DOWN);
    Serial.println("Volume DOWN");
  }

  oldPosition = newPosition;
}

void RotBrightness() {
  long newPosition = rotary.read();
  if (newPosition == oldPosition) return;
  long delta = newPosition - oldPosition;

  if (delta > 0) IncreaseBrightness();
  else DecreaseBrightness();

  oldPosition = newPosition;
  delay(100);
}

// Slightly different, this makes the rainbow equally distributed throughout
void RainbowCycle(uint16_t ledIndex, uint8_t wait = 10, int times = 1) {
  uint16_t i, j;

  for (j = 0; j < 256 * times; j++) {
    for (i = 0; i < leds[ledIndex].Strip.numPixels(); i++) {
      leds[ledIndex].Strip.setPixelColor(i, Wheel(((i * 256 / leds[ledIndex].Strip.numPixels()) + j) & 255));
    }
    leds[ledIndex].Strip.show();
    delay(wait);
  }
  leds[ledIndex].Strip.clear();
  leds[ledIndex].Strip.show();
}

// Input a value 0 to 255 to get a color value.
// The colours are a transition r - g - b - back to r.
uint32_t Wheel(byte WheelPos) {
  WheelPos = 255 - WheelPos;
  if (WheelPos < 85) {
    return Adafruit_NeoPixel::Color(255 - WheelPos * 3, 0, WheelPos * 3);
  }
  if (WheelPos < 170) {
    WheelPos -= 85;
    return Adafruit_NeoPixel::Color(0, WheelPos * 3, 255 - WheelPos * 3);
  }
  WheelPos -= 170;
  return Adafruit_NeoPixel::Color(WheelPos * 3, 255 - WheelPos * 3, 0);
}

void switchState() {
  if (!rotButton.pressed()) return;

  switch (currentState) {
    case VOLUME:
      beginState(BRIGHTNESS);
      break;
    case BRIGHTNESS:
      beginState(VOLUME);
      break;
    // case ROTATE:
    //   beginState(VOLUME);
    //   break;
  }
}

void beginState( int state ) {
  if ( currentState == state ) return;
  currentState = state;
  oldPosition = rotary.read();

  // Use state to update UI
  switch (currentState) {
    case VOLUME:
      Serial.println("Switching mode to Volume");
      printMode("  VOLUME");
      break;
    case BRIGHTNESS:
      Serial.println("Switching mode to Brightness");
      printMode("BRIGHTNESS");
      SetPixelColor(Adafruit_NeoPixel::Color(50, 255, 255), 1, 1);
      break;
    case ROTATE:
      Serial.println("Switching mode to Rotate");
      printMode("  ROTATE");
      SetPixelColor(Adafruit_NeoPixel::Color(50, 255, 255), 1, 1);
      SetPixelColor(Adafruit_NeoPixel::Color(50, 255, 255), 1, 2);
      break;
  }

  // Always at least one LED
  SetPixelColor(Adafruit_NeoPixel::Color(50, 255, 255), 1, 0);
  ShowLeds();
  delay(200);
}

void checkState() {
  FillLeds(Adafruit_NeoPixel::Color(255, 255, 255));
  Serial.println(currentState);
  switch (currentState) {
    case VOLUME:
      RotVolume();
      break;
    case BRIGHTNESS:
      RotBrightness();
      break;
    case ROTATE:
      printTestImage();
      break;
  }
}

void loop() {
  rotButton.update();
  switchState();
  checkState();
  ScanButtons();
  LightKeyPressed();
  ShowLeds();
  delay(10);
}
