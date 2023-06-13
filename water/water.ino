#include <Adafruit_FT6206.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>

#include "water.h"

Adafruit_ILI9341 tft = Adafruit_ILI9341(TFT_CS, TFT_DC);
Adafruit_FT6206 cts = Adafruit_FT6206();

// Push button control setup
int buttonHeight = 60;
int buttonWidth = 0;
int buttonTop = 0;

buttonCtrl buttons[NUM_BUTTONS] = {
    {"Feed", 6, false},
    {"Purge", 5, true},
    {"Pump", 7, false}
};

// Flow sensor control setup
int productFlowPin = 2;
int wasteFlowPin = 3;

volatile unsigned long productFlowCounter = 0;
volatile unsigned long wasteFlowCounter = 0;

sensorCtrl sensors[NUM_SENSORS] = {
  {"Product", &productFlowCounter, 0, 0, 0.0, 0.0, 0.0, {0}, 0},
  {"Waste  ", &wasteFlowCounter, 0, 0, 0.0, 0.0, 0.0, {0}, 0}
};

void productFlowInterrupt() {
  productFlowCounter++;
}

void wasteFlowInterrupt() {
  wasteFlowCounter++;
}

void setup() {
  Serial.begin(115200);

  for (int i = 0; i < NUM_BUTTONS; i++) {
    pinMode(buttons[i].pin, OUTPUT);
    digitalWrite(buttons[i].pin, buttons[i].state);
  }

  // Attach the interrupts to the flow sensor pins
  attachInterrupt(digitalPinToInterrupt(productFlowPin), productFlowInterrupt, RISING);
  attachInterrupt(digitalPinToInterrupt(wasteFlowPin), wasteFlowInterrupt, RISING);

  // Start the TFT and rotate 90 degrees
  tft.begin();
  tft.setRotation(3);
  buttonWidth = tft.width() / NUM_BUTTONS;
  buttonTop = tft.height() - buttonHeight;

  tft.fillScreen(ILI9341_BLACK);

  // Start the touch screen with 'sensitivity' coefficient
  Serial.println((cts.begin(40)) ? "Touchscreen started." : "Unable to start touchscreen.");

  drawButtons();
}

void drawButtons(void) {

  tft.setTextSize(3);

  for (int i = 0; i < NUM_BUTTONS; i++) {
    int padding = (buttons[i].label.length() == 4) ? 20 : 10;
    uint16_t color = buttons[i].state ? ILI9341_DARKGREEN : ILI9341_RED;

    tft.drawRect(i * buttonWidth, buttonTop, buttonWidth, buttonHeight, ILI9341_WHITE);
    tft.fillRect(i * buttonWidth + 1, buttonTop + 1, buttonWidth - 1, buttonHeight - 1, color);
    tft.setCursor(i * buttonWidth + padding, buttonTop + 20);
    tft.setTextColor(ILI9341_WHITE, color);
    tft.println(buttons[i].label);
  }
}

boolean readSensors(void) {

  boolean changed = false;

  for (int i = 0; i < NUM_SENSORS; i++) {

    // Disable the interrupts while we read the counter value
    noInterrupts();
    unsigned long pulseCount = *sensors[i].pulseCount;
    interrupts();

    unsigned long now = millis();
    unsigned long deltaCount = pulseCount - sensors[i].oldPulseCount;
    unsigned long deltaTime = now - sensors[i].lastMillis;

    if (deltaTime >= 1000) {
      sensors[i].flowRate = ((deltaCount / (float)deltaTime) * 60000) / 5880.0;
      sensors[i].flowRates[sensors[i].bufferIndex] = sensors[i].flowRate;
      sensors[i].flowRateSum += sensors[i].flowRate;
      sensors[i].averageFlowRate = sensors[i].flowRateSum / AVERAGE_PERIOD;
      sensors[i].bufferIndex = (sensors[i].bufferIndex + 1) % AVERAGE_PERIOD;

      sensors[i].oldPulseCount = pulseCount;
      sensors[i].lastMillis = now;

      changed = true;
    }
    
  }

  return changed;
}

void loop() {
  
  if (readSensors()) {

    // Display the sensor readings if they've changed
    tft.setCursor(0, 0);
    tft.setTextColor(ILI9341_WHITE, ILI9341_BLACK);
    tft.setTextSize(3);

    for (int i = 0; i < NUM_SENSORS; i++) {
      tft.print(sensors[i].label);
      tft.print("  ");
      tft.print(sensors[i].flowRate, 2);
      tft.println(" L/min");
      tft.print("Avg: ");
      tft.print(sensors[i].averageFlowRate, 2);
      tft.println(" L/min");
    }

  }

  // Check for touch events and handle them
  if (cts.touched()) {

    TS_Point p = cts.getPoint();

    // flip coordinate system to match display
    int y = p.x;
    int x = p.y;

    // bottom row buttons
    if (y < buttonHeight) {
      int index = x / buttonWidth;
      buttons[index].state = !buttons[index].state;
      digitalWrite(buttons[index].pin, buttons[index].state);
      drawButtons();
    }
  }
}
