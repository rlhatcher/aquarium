#include <Adafruit_FT6206.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>

#include "water.h"

#define ILI9341_GREY 0x2104  // Dark grey 16 bit colour

Adafruit_ILI9341 tft = Adafruit_ILI9341(TFT_CS, TFT_DC);
Adafruit_FT6206 cts = Adafruit_FT6206();

state stateNow = STANDBY;
state stateLast = STANDBY;
boolean stateChanged = false;

// Event variables
boolean start_declined = false;
boolean stop_declined = false;
boolean start_accepted = false;
boolean stop_accepted = false;
boolean t_prime = false;
boolean t_rinse_start = false;
boolean t_rinse_stop = false;
boolean t_run = false;
boolean touch_run = false;
boolean touch_standby = false;

// Push button control setup
int buttonHeight = 40;
int buttonWidth = 0;
int buttonTop = 0;

buttonCtrl controls[NUM_CONTROLS] = {
    {"Feed", 6, false}, {"Purge", 5, true}, {"Pump", 7, false}};

// Flow sensor control setup
int productFlowPin = 2;
int wasteFlowPin = 3;

volatile unsigned long productFlowCounter = 0;
volatile unsigned long wasteFlowCounter = 0;

sensorCtrl sensors[NUM_SENSORS] = {
    {"Product", &productFlowCounter, 0, 0, 0.0, 0.0, 0.0, {0}, 0},
    {" Waste ", &wasteFlowCounter, 0, 0, 0.0, 0.0, 0.0, {0}, 0}};

void productFlowInterrupt() { productFlowCounter++; }

void wasteFlowInterrupt() { wasteFlowCounter++; }

// Board setup
void setup() {
  Serial.begin(115200);

  for (int i = 0; i < NUM_CONTROLS; i++) {
    pinMode(controls[i].pin, OUTPUT);
    digitalWrite(controls[i].pin, controls[i].state);
  }

  // Attach the interrupts to the flow sensor pins
  attachInterrupt(digitalPinToInterrupt(productFlowPin), productFlowInterrupt,
                  RISING);
  attachInterrupt(digitalPinToInterrupt(wasteFlowPin), wasteFlowInterrupt,
                  RISING);

  // Start the TFT and rotate 90 degrees
  tft.begin();
  tft.setRotation(3);
  buttonWidth = tft.width() / NUM_CONTROLS;
  buttonTop = tft.height() - buttonHeight;

  tft.fillScreen(ILI9341_BLACK);

  // Start the touch screen with 'sensitivity' coefficient
  Serial.println((cts.begin(40)) ? "Touchscreen started."
                                 : "Unable to start touchscreen.");

  drawState();
  drawButtons();
}

void drawButtons(void) {
  tft.setTextSize(3);

  for (int i = 0; i < NUM_CONTROLS; i++) {
    int padding = (controls[i].label.length() == 4) ? 20 : 10;
    uint16_t color = controls[i].state ? ILI9341_DARKGREEN : ILI9341_RED;

    tft.drawRoundRect(i * buttonWidth, buttonTop, buttonWidth, buttonHeight, 10,
                      ILI9341_WHITE);
    tft.fillRoundRect(i * buttonWidth + 1, buttonTop + 1, buttonWidth - 2,
                      buttonHeight - 2, 10, color);
    tft.setCursor(i * buttonWidth + padding, buttonTop + 10);
    tft.setTextColor(ILI9341_WHITE, color);
    tft.println(controls[i].label);
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

// Display the status
void drawState() {
  tft.fillRect(0, 150, tft.width(), tft.height() - 189, ILI9341_DARKCYAN);

  tft.setCursor(10, 165);
  tft.setTextColor(ILI9341_WHITE, ILI9341_DARKCYAN);
  tft.setTextSize(3);
  tft.print("Status ");

  switch (stateNow) {
    case STANDBY:
      tft.setTextColor(ILI9341_BLACK, ILI9341_YELLOW);
      tft.print(" Standby ");
      tft.fillRoundRect(180, 240, 80, 100, 10, ILI9341_WHITE);
      break;
    case RUNNING:
      tft.setTextColor(ILI9341_BLACK, ILI9341_GREEN);
      tft.print(" Running ");
    case PRIME:
      tft.setTextColor(ILI9341_BLACK, ILI9341_RED);
      tft.print("  Fault  ");
    case RINSE:
      tft.setTextColor(ILI9341_BLACK, ILI9341_CYAN);
      tft.print("  Rinse  ");

    default:
      break;
  }

  tft.setTextColor(ILI9341_WHITE, ILI9341_DARKCYAN);
  tft.println(" ");
}

// Perform state transitions
void processEvents(void) {
  if (start_declined) {
    if (stateNow != CONFIRM_START) {
      Serial.println("Start declined - not in confirm start state");
    }
    stateLast = stateNow;
    stateNow = STANDBY;
    stateChanged = stateLast != stateNow;
    start_declined = false;
  }
  if (start_accepted) {
    if (stateNow != CONFIRM_START) {
      Serial.println("Start declined - not in confirm start state");
    }
    stateLast = stateNow;
    stateNow = PRIME;
    stateChanged = stateLast != stateNow;
    start_declined = false;
  }
  if (stop_declined) {
    if (stateNow != CONFIRM_STOP) {
      Serial.println("Stop declined - not in confirm stop state");
    }
    stateLast = stateNow;
    stateNow = RUNNING;
    stateChanged = stateLast != stateNow;
    stop_declined = false;
  }
  if (stop_accepted) {
    if (stateNow == CONFIRM_STOP) {
      stateLast = stateNow;
      stateNow = RINSE;
      stateChanged = stateLast != stateNow;
      stop_accepted = false;
    } else {
      Serial.println("Stop accepted - not in confirm stop state");
    }
  }
  if (t_prime) {
    stateLast = stateNow;
    stateNow = RINSE;
    stateChanged = stateLast != stateNow;
    t_prime = false;
  }
  if (t_rinse_start) {
    stateLast = stateNow;
    stateNow = RUNNING;
    stateChanged = stateLast != stateNow;
    t_rinse_start = false;
  }
  if (t_rinse_stop) {
    stateLast = stateNow;
    stateNow = STANDBY;
    stateChanged = stateLast != stateNow;
    t_rinse_stop = false;
  }
  if (t_run) {
    stateLast = stateNow;
    stateNow = RINSE;
    stateChanged = stateLast != stateNow;
    t_run = false;
  }
  if (touch_run) {
    stateLast = stateNow;
    stateNow = CONFIRM_STOP;
    stateChanged = stateLast != stateNow;
    touch_run = false;
  }
  if (touch_standby) {
    stateLast = stateNow;
    stateNow = CONFIRM_START;
    stateChanged = stateLast != stateNow;
    touch_standby = false;
  }
}

void loop() {

  // touch screen variables
  int x, y;
  boolean touch = false;
  touch = getTouch(&x, &y);

  // Perform state transitions
  processEvents();

  switch (stateNow) {
    case STANDBY:
      controls[FEED].state = false;
      controls[PURGE].state = true;
      controls[PUMP].state = false;
      if (touch) {
        if (x > 0 && x < 80 && y > 240 && y < 340) {
          touch_standby = true;
        }
      }
      break;
    case PRIME:
      controls[FEED].state = true;
      controls[PURGE].state = true;
      controls[PUMP].state = false;
      break;
    case RINSE:
      controls[FEED].state = true;
      controls[PURGE].state = true;
      controls[PUMP].state = true;
      break;
    case RUNNING:
      controls[FEED].state = true;
      controls[PURGE].state = false;
      controls[PUMP].state = true;
      break;
  }

  if (stateChanged) {
    for (int i = 0; i < NUM_CONTROLS; i++) {
      digitalWrite(controls[i].pin, controls[i].state);
    }
    drawState();
    drawButtons();
    stateChanged = false;
  }

  if (readSensors()) {
    int xpos = 0, ypos = 5, gap = 5, radius = 40;

    for (int i = 0; i < NUM_SENSORS; i++) {
      tft.setCursor(xpos, ypos + (radius + gap) * 2);
      tft.setTextColor(ILI9341_WHITE, ILI9341_PURPLE);
      tft.setTextSize(2);
      tft.print(sensors[i].label);

      xpos = gap + ringMeter(sensors[i].averageFlowRate, 0, 40, xpos, ypos,
                             radius, "ml/min");
    }
  }
}

boolean getTouch(int *x, int *y) {
  if (!cts.touched()) return false;

  TS_Point p = cts.getPoint();

  // flip coordinate system to match display
  *y = p.x;
  *x = p.y;

  return true;
}

int ringMeter(int value, int vmin, int vmax, int x, int y, int r, char *units) {
  x += r;
  y += r;  // Calculate coords of centre of ring

  int w = r / 4;        // Width of outer ring is 1/4 of radius
  int angle = 150;      // Half the sweep angle of meter (300 degrees)
  int text_colour = 0;  // To hold the text colour
  int v = map(value, vmin, vmax, -angle, angle);  // Map the value to an angle v
  byte seg = 5;  // Segments are 5 degrees wide = 60 segments for 300 degrees
  byte inc =
      10;  // Draw segments every 5 degrees, increase to 10 for segmented ring

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

    if (i < v) {  // Fill in coloured segments with 2 triangles
      tft.fillTriangle(x0, y0, x1, y1, x2, y2, colour);
      tft.fillTriangle(x1, y1, x2, y2, x3, y3, colour);
      text_colour = colour;  // Save the last colour drawn
    } else                   // Fill in blank segments
    {
      tft.fillTriangle(x0, y0, x1, y1, x2, y2, ILI9341_GREY);
      tft.fillTriangle(x1, y1, x2, y2, x3, y3, ILI9341_GREY);
    }
  }
  char valuestr[10];
  sprintf(valuestr, "%03d", value);
  int pad = 5;
  int xoffset = x - ((r / 2) - pad);

  tft.setCursor(xoffset, y - pad);
  tft.setTextColor(ILI9341_WHITE, ILI9341_BLACK);
  tft.setTextSize(2);
  tft.print(valuestr);  // Print the value

  tft.setCursor(xoffset, y + r);
  tft.setTextColor(ILI9341_WHITE, ILI9341_BLACK);
  tft.setTextSize(1);
  tft.print(units);  // Print the units

  // Calculate and return right hand side x coordinate
  return x + r;
}

// #########################################################################
// Return a 16 bit rainbow colour
// #########################################################################
unsigned int rainbow(byte value) {
  // Value is expected to be in range 0-127
  // The value is converted to a spectrum colour from 0 = blue through to 127 =
  // red

  byte red = 0;    // Red is the top 5 bits of a 16 bit colour value
  byte green = 0;  // Green is the middle 6 bits
  byte blue = 0;   // Blue is the bottom 5 bits

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