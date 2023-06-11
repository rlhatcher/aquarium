#include <Adafruit_FT6206.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>

#define TFT_CS 10
#define TFT_DC 9

#define NUM_BUTTONS 3
#define NUM_SENSORS 2

typedef struct buttonCtrl {
  String label;
  int pin;
  boolean state;
};

typedef struct sensorCtrl {
  String label;
  int pin;
  volatile int* flowCounter;
};

Adafruit_ILI9341 tft = Adafruit_ILI9341(TFT_CS, TFT_DC);
Adafruit_FT6206 cts = Adafruit_FT6206();

int buttonHeight = 60;
int buttonWidth;
int buttonTop;

int productFlowPin = 2;
int wasteFlowPin = 3;

buttonCtrl buttons[NUM_BUTTONS] = {
  {"Feed", 13, false}, 
  {"Purge", 12, true}, 
  {"Pump", 11, false}
};

volatile int productFlowCounter = 0;
volatile int wasteFlowCounter = 0;

sensorCtrl sensors[NUM_SENSORS] = {
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

  // Rotate the screen 90 degrees
  tft.begin();
  tft.setRotation(3);
  tft.fillScreen(ILI9341_BLACK);

  // Pass in 'sensitivity' coefficient
  if (!cts.begin(40)) {
    Serial.println("Unable to start touchscreen.");
  } else {
    Serial.println("Touchscreen started.");
  }
  drawButtons();
 
}

void drawButtons(void) {

  buttonWidth = tft.width() / NUM_BUTTONS;
  buttonTop = tft.height() - buttonHeight;
  
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

  // Display the sensor readings
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
    // rotate coordinate system
    int y = p.x;
    int x = p.y;

    if (y < buttonHeight) {
      int index = x / buttonWidth;
      buttons[index].state = !buttons[index].state;
      digitalWrite(buttons[index].pin, buttons[index].state);
      drawButtons();
    }
  }

}
