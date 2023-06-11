#include <Adafruit_FT6206.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>

#define TFT_CS 10
#define TFT_DC 9

Adafruit_ILI9341 tft = Adafruit_ILI9341(TFT_CS, TFT_DC);
Adafruit_FT6206 cts = Adafruit_FT6206();

#define MIN_PRESSURE 10
#define MAX_PRESSURE 1000
#define NUM_BUTTONS 3
#define NUM_SENSORS 2

int buttonHeight = 60;
int buttonWidth;
int buttonTop;

int relayPins[3] = {13, 12, 11};
int productFlowPin = 2;
int wasteFlowPin = 3;

struct buttonCtrl {
  String label;
  int pin;
  boolean state;
};

struct sensorCtrl {
  String label;
  int pin;
  volatile int* flowCounter;
};

buttonCtrl buttons[NUM_BUTTONS] = {
  {"Feed", 13, false}, 
  {"Purge", 12, true}, 
  {"Pump", 11, false}
};

volatile int productFlowCounter = 0;
volatile int wasteFlowCounter = 0;

sensorCtrl sensors[2] = {
  {"Product", 2, &productFlowCounter},
  {"Waste  ", 3, &wasteFlowCounter}
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
  }

  // Attach the interrupts to the flow sensor pins
  attachInterrupt(digitalPinToInterrupt(productFlowPin), productFlowInterrupt, RISING);
  attachInterrupt(digitalPinToInterrupt(wasteFlowPin), wasteFlowInterrupt, RISING);

  tft.begin();

  // Rotate the screen 90 degrees
  tft.setRotation(3);
  tft.fillScreen(ILI9341_BLACK);

  // Pass in 'sensitivity' coefficient
  if (!cts.begin(40)) {
    Serial.println("Unable to start touchscreen.");
  } else {
    Serial.println("Touchscreen started.");
  }

  buttonWidth = tft.width() / NUM_BUTTONS;
  buttonTop = tft.height() - buttonHeight;
  tft.setTextSize(3);
  // tft.setFont(&FreeMonoBoldOblique12pt7b);

  for (int i = 0; i < NUM_BUTTONS; i++) {
    int padding = (buttons[i].label.length() == 4) ? 20 : 10;
    tft.drawRect(i * buttonWidth, buttonTop, buttonWidth, buttonHeight, ILI9341_WHITE);
    uint16_t color = buttons[i].state ? ILI9341_DARKGREEN : ILI9341_RED;
    tft.fillRect(i * buttonWidth + 1, buttonTop + 1, buttonWidth - 1, buttonHeight - 1, color);
    tft.setCursor(i * buttonWidth + padding, buttonTop + 20);
    tft.println(buttons[i].label);
  }
  tft.setFont();
}

void loop() {
  // Disable the interrupts while we read the counter values
  noInterrupts();
  int productFlowCount = productFlowCounter;
  int wasteFlowCount = wasteFlowCounter;
  interrupts();

  // Convert the counter values to flow rates
  float productFlowRate = productFlowCount / 5880.0;
  float wasteFlowRate = wasteFlowCount / 5880.0;

  // Display the new sensor readings
  tft.setCursor(0, 0);
  tft.setTextColor(ILI9341_WHITE, ILI9341_BLACK);
  tft.setTextSize(3);
  for (int i = 0; i < NUM_SENSORS; i++) {
    tft.print(sensors[i].label);
    tft.print("  ");
    tft.print(*sensors[i].flowCounter / 5.88);
    tft.println(" mL");
  }

  // Check for touch events and handle them
  if (cts.touched()) {
    TS_Point p = cts.getPoint();
    Serial.print("X = ");
    Serial.print(p.x);
    Serial.print("\tY = ");
    Serial.println(p.y);

    for (int i = 0; i < 3; i++) {
      if (p.x >= i * buttonWidth && p.x < (i + 1) * buttonWidth &&
          p.y >= tft.height() / 2 && p.y < tft.height() / 2 + buttonHeight) {
        // The touch event was within the area of button i. Toggle the state of
        // relay i.
        bool newState = !digitalRead(relayPins[i]);
        digitalWrite(relayPins[i], newState);

        // Update the color of the button to indicate the new state
        uint16_t color = newState ? ILI9341_GREEN : ILI9341_RED;
        tft.fillRect(i * buttonWidth, tft.height() / 2, buttonWidth,
                     buttonHeight, color);
      }
    }
  }
}
