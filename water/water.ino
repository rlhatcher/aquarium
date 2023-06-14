#include <Adafruit_FT6206.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>

#include "water.h"

#define ILI9341_GREY 0x2104 // Dark grey 16 bit colour

Adafruit_ILI9341 tft = Adafruit_ILI9341(TFT_CS, TFT_DC);
Adafruit_FT6206 cts = Adafruit_FT6206();

// Push button control setup
int buttonHeight = 40;
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
  {" Waste ", &wasteFlowCounter, 0, 0, 0.0, 0.0, 0.0, {0}, 0}
};

void productFlowInterrupt() {
  productFlowCounter++;
}

void wasteFlowInterrupt() {
  wasteFlowCounter++;
}

// Board setup
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

    tft.drawRoundRect(i * buttonWidth, buttonTop, buttonWidth, buttonHeight, 10, ILI9341_WHITE);
    tft.fillRoundRect(i * buttonWidth + 1, buttonTop + 1, buttonWidth-2, buttonHeight-2, 10, color);
    tft.setCursor(i * buttonWidth + padding, buttonTop + 10);
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
      sensors[i].flowRate = ((deltaCount / (float)deltaTime) * 60000) / 58.800;
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

    int xpos = 0, ypos = 5, gap = 5, radius = 40;

    for (int i = 0; i < NUM_SENSORS; i++) {
      
      tft.setCursor(xpos, ypos+(radius+gap)*2);
      tft.setTextColor(ILI9341_WHITE, ILI9341_PURPLE);
      tft.setTextSize(2);
      tft.print(sensors[i].label);
      
      xpos = gap + ringMeter(sensors[i].averageFlowRate, 0, 40, xpos, ypos, radius, "ml/min");
    }
  }

  // Display the status
  tft.setCursor(0, 160);
  tft.setTextColor(ILI9341_WHITE, ILI9341_DARKCYAN);
  tft.setTextSize(3);
  tft.print("Status ");
  tft.setTextColor(ILI9341_BLACK, ILI9341_YELLOW);
  tft.print(" Standby ");
  // tft.setTextColor(ILI9341_BLACK, ILI9341_GREEN);
  // tft.print(" Running ");
  // tft.setTextColor(ILI9341_BLACK, ILI9341_RED);
  // tft.print("  Fault  ");
  // tft.setTextColor(ILI9341_BLACK, ILI9341_CYAN);
  // tft.print("  Rinse  ");
  tft.setTextColor(ILI9341_WHITE, ILI9341_DARKCYAN);
  tft.println(" ");

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

int ringMeter(int value, int vmin, int vmax, int x, int y, int r, char *units) {
  
  x += r; y += r;   // Calculate coords of centre of ring

  int w = r / 4;    // Width of outer ring is 1/4 of radius
  int angle = 150;  // Half the sweep angle of meter (300 degrees)
  int text_colour = 0; // To hold the text colour
  int v = map(value, vmin, vmax, -angle, angle); // Map the value to an angle v
  byte seg = 5; // Segments are 5 degrees wide = 60 segments for 300 degrees
  byte inc = 10; // Draw segments every 5 degrees, increase to 10 for segmented ring

  // Draw colour blocks every inc degrees
  for (int i = -angle; i < angle; i += inc) {

    int colour = rainbow(map(i, -angle, angle, 127, 63));

    // Calculate pair of coordinates for segment start
    float sx = cos((i - 90) * 0.0174532925);
    float sy = sin((i - 90) * 0.0174532925);
    uint16_t x0 = sx * (r - w) + x;
    uint16_t y0 = sy * (r - w) + y;
    uint16_t x1 = sx * r + x;
    uint16_t y1 = sy * r + y;

    // Calculate pair of coordinates for segment end
    float sx2 = cos((i + seg - 90) * 0.0174532925);
    float sy2 = sin((i + seg - 90) * 0.0174532925);
    int x2 = sx2 * (r - w) + x;
    int y2 = sy2 * (r - w) + y;
    int x3 = sx2 * r + x;
    int y3 = sy2 * r + y;

    if (i < v) { // Fill in coloured segments with 2 triangles
      tft.fillTriangle(x0, y0, x1, y1, x2, y2, colour);
      tft.fillTriangle(x1, y1, x2, y2, x3, y3, colour);
      text_colour = colour; // Save the last colour drawn
    }
    else // Fill in blank segments
    {
      tft.fillTriangle(x0, y0, x1, y1, x2, y2, ILI9341_GREY);
      tft.fillTriangle(x1, y1, x2, y2, x3, y3, ILI9341_GREY);
    }
  }
  char valuestr[10];
  sprintf(valuestr, "%03d", value);
  int pad = 5;
  int xoffset = x-((r/2)-pad);

  tft.setCursor(xoffset, y-pad);
  tft.setTextColor(ILI9341_WHITE, ILI9341_BLACK);
  tft.setTextSize(2);
  tft.print(valuestr); // Print the value 

  tft.setCursor(xoffset, y+r);
  tft.setTextColor(ILI9341_WHITE, ILI9341_BLACK);
  tft.setTextSize(1);
  tft.print(units); // Print the units


  // Calculate and return right hand side x coordinate
  return x + r;
}

// #########################################################################
// Return a 16 bit rainbow colour
// #########################################################################
unsigned int rainbow(byte value)
{
  // Value is expected to be in range 0-127
  // The value is converted to a spectrum colour from 0 = blue through to 127 = red

  byte red = 0; // Red is the top 5 bits of a 16 bit colour value
  byte green = 0;// Green is the middle 6 bits
  byte blue = 0; // Blue is the bottom 5 bits

  byte quadrant = value / 32;

  if (quadrant == 0) {
    blue = 31;
    green = 2 * (value % 32);
    red = 0;
  }
  if (quadrant == 1) {
    blue = 31 - (value % 32);
    green = 63;
    red = 0;
  }
  if (quadrant == 2) {
    blue = 0;
    green = 63;
    red = value % 32;
  }
  if (quadrant == 3) {
    blue = 0;
    green = 63 - 2 * (value % 32);
    red = 31;
  }
  return (red << 11) + (green << 5) + blue;
}