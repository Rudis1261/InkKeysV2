#include "settings.h" //Customize your settings in settings.h!

#include <Adafruit_NeoPixel.h>
#include <Encoder.h>
#include <GxEPD2_BW.h>          
#include <Bounce2.h>
#include <HID-Project.h>
// #include <HID-Settings.h>
#include <SPI.h>
//#include "epd2in9_V2.h"
#include "epdpaint.h"
#include "fonts.h"
//#include "imagedata.h"

#define COLORED     0
#define UNCOLORED   1

typedef struct {
  Adafruit_NeoPixel Strip;
  String Mode;
  int Brightness;
} LedStrip;

LedStrip leds[LEDS] = {
  {
    Adafruit_NeoPixel(8, LED_KEY_PIN, NEO_GRB + NEO_KHZ800),
    "white",
    10,
  },
  {
    Adafruit_NeoPixel(12, LED_RING_PIN, NEO_GRB + NEO_KHZ800),
    "white",
    10,
  },
};

byte rows[] = { 6, 7 };
const int rowCount = sizeof(rows) / sizeof(rows[0]);

byte cols[] = { 2, 3, 4, 5 };
const int colCount = sizeof(cols) / sizeof(cols[0]);

byte keyValues = 0;
int ledKeyIndex[] = { 0, 7, 1, 6, 2, 5, 3, 4 };

Bounce2::Button rotButton;

// Ring states
enum { VOLUME, BRIGHTNESS, ROTATE };
int currentState = VOLUME;

long debounce[] = {};

unsigned long lastDebounceTime = 0;
unsigned long debounceDelay = 150;
volatile long oldPosition = 0;
int brightness = 10;

Encoder rotary(PIN_ROTA, PIN_ROTB);

GxEPD2_290_T94_V2 display(/*CS=*/CS_PIN, /*DC=*/DC_PIN, /*RST=*/RST_PIN, /*BUSY=*/BUSY_PIN);

// DISPLAY
unsigned char image[1024];
Paint paint(image, 0, 0);    // width should be the multiple of 8 
unsigned long time_start_ms;
unsigned long time_now_s;
//Epd epd;
// DISPLAY

void initDisplay() {
  display.init(0, true, 0, false);
  display.writeScreenBuffer();
  display.refresh();
  display.writeScreenBufferAgain();
  display.powerOff();
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

void setup() {
  Serial.begin(57600);

  Consumer.begin();

  // Clear the screen
  initDisplay();

  currentState = -1;
  pinMode(ROT_PIN, INPUT_PULLUP);
  rotButton.attach(ROT_PIN);
  rotButton.setPressedState(LOW);
  rotButton.interval(10);
  beginState(VOLUME);

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
    leds[i].Strip.setBrightness(leds[i].Brightness);
    leds[i].Strip.fill(Adafruit_NeoPixel::Color(255, 255, 255), 0);
    leds[i].Strip.show();
  }

  delay(5000);
}

void FillLeds(uint32_t color) {
  for (int i = 0; i < LEDS; i++) leds[i].Strip.fill(color, 0);
}

void ShowLeds() {
  for (int i = 0; i < LEDS; i++) leds[i].Strip.show();
}

void LedBrightness(int brightness) {
  brightness = abs(brightness);
  if (brightness > 255) brightness = 255;
  for (int i = 0; i < LEDS; i++) leds[i].Strip.setBrightness(brightness);
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

void LighKeyPressed() {
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
  int before = brightness;

  if (delta > 0) brightness++;
  else brightness--;
  oldPosition = newPosition;

  // Clamp the values
  if (brightness < 0) brightness = 0;
  if (brightness > 100) brightness = 100;

  // DEBUGGING
  Serial.print("BRIGHTNESS: ");
  Serial.print(brightness);
  Serial.print(", BEFORE: ");
  Serial.print(before);
  Serial.print(", NEW: ");
  Serial.print(newPosition);
  Serial.print(", OLD: ");
  Serial.print(oldPosition);
  Serial.print(", DELTA: ");
  Serial.println(delta);

  // We don't want 100% (255) brightness, it's just too much for the little LEDS
  LedBrightness(map(brightness, 0, 100, 10, 200));
  FillLeds(Adafruit_NeoPixel::Color(255, 255, 255));
}

void RotLowValue() {
  if (brightness < 20) {
    SetPixelColor(Adafruit_NeoPixel::Color(0, 255, 0), 1, 0);
  }
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

void beginState( int state ) {
  if ( currentState == state ) return;

  // VOLUME, BRIGHTNESS, ROTATE
  currentState = state;

  // Reset position to the current rotary knob value
  oldPosition = rotary.read();

  // Use state to update UI
  switch (currentState) {
    case VOLUME:
      SetPixelColor(Adafruit_NeoPixel::Color(50, 255, 255), 1, 0);
      break;
    case BRIGHTNESS:
      SetPixelColor(Adafruit_NeoPixel::Color(50, 255, 255), 1, 0);
      SetPixelColor(Adafruit_NeoPixel::Color(50, 255, 255), 1, 1);
      break;
    case ROTATE:
      initDisplay();
      SetPixelColor(Adafruit_NeoPixel::Color(50, 255, 255), 1, 0);
      SetPixelColor(Adafruit_NeoPixel::Color(50, 255, 255), 1, 1);
      SetPixelColor(Adafruit_NeoPixel::Color(50, 255, 255), 1, 2);
      break;
  }
  ShowLeds();
  delay(500);
}

void checkState() {
  switch (currentState) {
    case VOLUME:
      FillLeds(Adafruit_NeoPixel::Color(255, 255, 255));
      RotVolume();
      if (rotButton.pressed()) {
        beginState(BRIGHTNESS);
      }
      break;
    case BRIGHTNESS:
      FillLeds(Adafruit_NeoPixel::Color(255, 255, 255));
      RotBrightness();
      RotLowValue();
      if (rotButton.pressed()) {
        beginState(ROTATE);
      }
      break;
    case ROTATE:
      FillLeds(Adafruit_NeoPixel::Color(255, 255, 255));
      printTestImage();
      if (rotButton.pressed()) {
        beginState(VOLUME);
      }
      break;
  }
}

void loop() {
  rotButton.update();
  checkState();
  ScanButtons();
  LighKeyPressed();
  ShowLeds();
  delay(30);
}
