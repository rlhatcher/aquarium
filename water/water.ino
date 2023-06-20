#include <Adafruit_FT6206.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>

#define TFT_CS 10  // chip select pin for the TFT
#define TFT_DC 9   // data pin for the TFT

#define NUM_CONTROLS 3  // number of push buttons
#define NUM_SENSORS 2   // number of flow sensors

#define AVERAGE_PERIOD 60  // number of seconds for flow average

#define ILI9341_GREY 0x2104  // Dark grey 16 bit colour

// Flow sensor control setup
int prd_flow_pin = 2;
int wst_flow_pin = 3;

Adafruit_ILI9341 tft = Adafruit_ILI9341(TFT_CS, TFT_DC);
Adafruit_FT6206 cts = Adafruit_FT6206();

// Event variables
boolean start = false;


// Push button control setup
int btnH = 40;
int btnW = 0;
int btnT = 0;

enum control { FEED, PURGE, PUMP };
typedef struct buttonCtrl {
  String label;
  int pin;
  boolean state;
};

buttonCtrl controls[NUM_CONTROLS] = {
    {"Feed", 6, false}, {"Purge", 5, true}, {"Pump", 7, false}};

volatile unsigned long productFlowCounter = 0;
volatile unsigned long wasteFlowCounter = 0;

typedef struct sensor {
  String label;
  volatile unsigned long *pulseCount;
  unsigned long oldPulseCount;
  unsigned long lastMillis;
  float flowRate;
  float averageFlowRate;
  float flowRateSum;
  float flowRates[AVERAGE_PERIOD];  // buffer to store flow rates for averaging
  int bufferIndex;
};

sensor sensors[NUM_SENSORS] = {
    {"Product", &productFlowCounter, 0, 0, 0.0, 0.0, 0.0, {0}, 0},
    {" Waste ", &wasteFlowCounter, 0, 0, 0.0, 0.0, 0.0, {0}, 0}};

enum state { STANDBY, RUNNING, PRIME, RINSE_START, RINSE_STOP };
typedef struct systemState {
  boolean feed;
  boolean purge;
  boolean pump;
  uint16_t colour;
  state icon;
  unsigned int timer;
};
systemState states[5] = {{false, true, false, ILI9341_YELLOW, RUNNING, 0},
                         {true, false, true, ILI9341_GREEN, STANDBY, 0},
                         {true, true, false, ILI9341_BLUE, PRIME, 0},
                         {true, true, true, ILI9341_CYAN, RUNNING, 0},
                         {true, true, true, ILI9341_CYAN, RUNNING, 0}};

state stateNow = STANDBY;
boolean stateChanged = false;

enum event { T_PRIME, T_RINSE_START, T_RINSE_STOP, T_RUN, T_PLAY, T_PAUSE };
typedef struct eventType {
  event event;
  boolean active;
  state targetState;
};

eventType events[6] = {{T_PRIME, 0, false, RINSE_START},
                       {T_RINSE_START, 0, false, RUNNING},
                       {T_RINSE_STOP, 0, false, STANDBY},
                       {T_RUN, 0, false, RINSE_START},
                       {T_PLAY, 0, false, PRIME},
                       {T_PAUSE, 0, false, RINSE_STOP}};

void prodFlowIrq() { productFlowCounter++; }
void wasteFlowIrq() { wasteFlowCounter++; }

// Board setup
void setup() {
  Serial.begin(115200);

  for (int i = 0; i < NUM_CONTROLS; i++) {
    pinMode(controls[i].pin, OUTPUT);
    digitalWrite(controls[i].pin, controls[i].state);
  }

  // Attach the interrupts to the flow sensor pins
  attachInterrupt(digitalPinToInterrupt(prd_flow_pin), prodFlowIrq, RISING);
  attachInterrupt(digitalPinToInterrupt(wst_flow_pin), wasteFlowIrq, RISING);

  // Start the TFT and rotate 90 degrees
  tft.begin();
  tft.setRotation(3);
  tft.fillScreen(ILI9341_BLACK);
  btnW = tft.width() / NUM_CONTROLS;
  btnT = tft.height() - btnH;

  // Start the touch screen with 'sensitivity' coefficient
  Serial.println((cts.begin(40)) ? "TS started" : "TS failed");

  // send our first event to start the state machine
  start = true;
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

// Perform state transitions
void processEvents(void) {
  if (start) {
    stateNow = STANDBY;
    stateChanged = true;
    start = false;
  }

  // Transition to target state if event is active
  for (int i = 0; i < 6; i++) {
    if (events[i].active) {
      stateNow = events[i].targetState;
      stateChanged = true;
      events[i].active = false;
    }
  }
}

void drawStatus(uint16_t fg_colour, String status) {
  if (stateChanged) {
    tft.setTextSize(3);
    tft.fillRoundRect(0, 145, tft.width(), 48, 5, states[stateNow].colour);
    for (int i = 0; i < NUM_CONTROLS; i++) {
      int padding = (controls[i].label.length() == 4) ? 20 : 10;
      uint16_t color = controls[i].state ? ILI9341_DARKGREEN : ILI9341_RED;
      tft.drawRoundRect(i * btnW, btnT, btnW, btnH, 10, ILI9341_WHITE);
      tft.fillRoundRect(i * btnW + 1, btnT + 1, btnW - 2, btnH - 2, 10, color);
      tft.setCursor(i * btnW + padding, btnT + 10);
      tft.setTextColor(ILI9341_WHITE, color);
      tft.println(controls[i].label);
      digitalWrite(controls[i].pin, controls[i].state);
    }
    tft.setCursor(10, 160);
    tft.setTextColor(fg_colour, states[stateNow].colour);
    tft.print(status);
  }
}

void drawControl(state theState) {
  if (stateChanged) {
    state icon = states[theState].icon;
    tft.fillCircle(250, 70, 60, states[icon].colour);
    tft.drawCircle(250, 70, 60, ILI9341_WHITE);
    tft.drawCircle(250, 70, 59, ILI9341_BLACK);
    if (icon == RUNNING) {
      tft.fillTriangle(220, 110, 220, 30, 300, 70, ILI9341_BLACK);
    }
    if (icon == STANDBY) {
      tft.fillRoundRect(220, 30, 20, 80, 5, ILI9341_BLACK);
      tft.fillRoundRect(260, 30, 20, 80, 5, ILI9341_BLACK);
    }
  }
}

void loop() {
  // touch screen variables
  int x, y;
  boolean touch = false;
  touch = getTouch(&x, &y);

  // Perform state transitions
  processEvents();

  // Update the control settings for the current state
  controls[FEED].state = states[stateNow].feed;
  controls[PURGE].state = states[stateNow].purge;
  controls[PUMP].state = states[stateNow].pump;

  // State specific processing
  switch (stateNow) {
    case STANDBY:
      drawStatus(ILI9341_BLACK, "Standby");
      drawControl(stateNow);
      if (states[STANDBY].timer == 0) {
        Serial.println("Entering Standby");
        states[STANDBY].timer = millis();
      } else {
        if (millis() - states[STANDBY].timer > 500) {
          if (touch) {
            if (y < 340) {
              events[T_PLAY].active = true;
            }
          }
          states[STANDBY].timer = 0;
        }
      }
      break;
    case RUNNING:
      drawStatus(ILI9341_BLACK, " Running ");
      drawControl(stateNow);
      if (events[T_PAUSE].millis == 0) {
        Serial.println("Entering Running");
        events[T_PAUSE].millis = millis();
      } else {
        if (millis() - events[T_PAUSE].millis > 500) {
          if (touch) {
            if (y < 340) {
              events[T_PAUSE].millis = true;
            }
          }
          events[T_PAUSE].millis = 0;
        }
      }
      break;
    case PRIME:
      drawStatus(ILI9341_BLACK, "  Prime  ");
      if (timerEvents[T_PRIME] == 0) {
        Serial.println("Entering Prime");
        timerEvents[T_PRIME] = millis();
      } else if (millis() - timerEvents[T_PRIME] > 10000) {
        transitionEvents[T_PRIME]= true;
        timerEvents[T_PRIME] = 0;
      }
      break;
    case RINSE:
      drawStatus(ILI9341_BLACK, "  Rinse  ");
      if (stateLast == RUNNING) {
        if (timerEvents[T_RINSE_STOP] == 0) {
          Serial.println("Entering Rinse Stop");
          timerEvents[T_RINSE_STOP] = millis();
        } else {
          if (millis() - timerEvents[T_RINSE_STOP] > 10000) {
            transitionEvents[T_RINSE_STOP] = true;
            timerEvents[T_RINSE_STOP] = 0;
          }
        }
      } else {
        if (timerEvents[T_RINSE_START] == 0) {
          Serial.println("Entering Rinse Start");
          timerEvents[T_RINSE_START] = millis();
        } else {
          if (millis() - timerEvents[T_RINSE_START] > 10000) {
            transitionEvents[T_RINSE_START] = true;
            timerEvents[T_RINSE_START] = 0;
          }
        }
      }
      break;
  }

  stateChanged = false;

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
  if (cts.touched()) {
    TS_Point p = cts.getPoint();
    // flip coordinate system to match display
    *y = p.x;
    *x = p.y;
    return true;
  } else {
    return false;
  }
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
    } else {
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