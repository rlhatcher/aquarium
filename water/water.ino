#include <Adafruit_FT6206.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>

#define TFT_CS 10  // chip select pin for the TFT
#define TFT_DC 9   // data pin for the TFT

#define NUM_CONTROLS 3  // number of push buttons
#define NUM_SENSORS 2   // number of flow sensors
#define NUM_ALARMS 1    // number of alarms
#define NUM_STATES 5    // number of system states
#define NUM_TIMERS 5    // number of event timers
#define NUM_EVENTS 8    // number of events

#define AVERAGE_PERIOD 60  // number of seconds for flow average

#define ILI9341_GREY 0x2104  // Dark grey 16 bit colour
#define MILLI_HOUR 3600000   // One hour in milliseconds
#define MILLI_MINUTE 60000   // One minute in milliseconds

// The state machine is defined using arrays of structures that
// define the system states, events, and timers. Enumerations are
// used to index the arrays.
enum event {
  RINSE_NEEDED,  // The system has idled for too long
  WARMED,        // The warming function is complete
  RINSED,        // The rinsing function is complete
  PAUSE_BTN,     // The pause button was pressed
  PLAY_BTN,      // The play button was pressed
  MAX_RUN,       // The maximum run time has been reached
  TANK_CLEAR,    // The tank is now empty
  TANK_CHECK     // Check if the tank is now empty
};
enum event_time {
  IDLE_TIME,      // RINSE_NEEDED timer
  WARM_TIME,      // WARMED timer
  RINSE_TIME,     // RINSED timer
  RUN_TIME,       // MAX_RUN timer
  CHECK_INTERVAL  // TANK_CLEAR check timer
};
enum control {
  FEED,   // Water feed valve from pump
  PURGE,  // Purge valve for RO membrane
  PUMP    // Water pump
};
enum state { WAITING, WARMING, RINSING, RUNNING, FULL };
enum alarm { TANK_FULL };

// event timers manage the transitions between states.
// the event_times array holds the duration to wait and the
// event to send
typedef struct event_timer {
  unsigned long end_millis;
  unsigned long duration;
  event event;
};

event_timer event_times[NUM_TIMERS] = {
    {0, 6 * MILLI_HOUR, RINSE_NEEDED},  // idle_time
    {0, 1 * MILLI_MINUTE, WARMED},      // prime_time
    {0, 1 * MILLI_MINUTE, RINSED},      // rinse_time
    {0, 4 * MILLI_HOUR, MAX_RUN},       // run_time
    {0, 1 * MILLI_MINUTE, TANK_CHECK}   // tank_time
};

// System states manage the control states, display settings,
// and associated timers. The states array defines the state model
typedef struct system_state {
  boolean feed;  // control states
  boolean purge;
  boolean pump;
  uint16_t colour;  // display settings
  state icon;
  char *label;
  event_time timer;  // associated timer
};

system_state states[NUM_STATES] = {
    {false, true, false, 0xFFE0, WAITING, "Waiting", IDLE_TIME},    // waiting
    {true, true, false, 0x07FF, WARMING, "Warming", WARM_TIME},     // priming
    {true, true, true, 0xAFE5, RINSING, "Rinsing", RINSE_TIME},     // rinsing
    {true, false, true, 0x07E0, RUNNING, "Running", RUN_TIME},      // running
    {false, true, false, 0xF800, FULL, "**Tank**", CHECK_INTERVAL}  // full
};

state stateNow = WAITING;  // TODO: change to local scope?

// system_events are used to trigger state transitions. The events array
// defines the events that are active and the target state.
typedef struct system_event {
  boolean active;
  state targetState;
};

system_event events[NUM_EVENTS] = {
    {false, WARMING},  // rinse_needed
    {false, RINSING},  // warmed
    {false, RUNNING},  // rinsed
    {false, RINSING},  // pause_btn
    {false, WARMING},  // play_btn
    {false, RINSING},  // max_run
    {false, WAITING},  // tank_clear
    {false, FULL}      // tank_check
};

// Push button controls
typedef struct btn_control {
  char *label;
  int pin;
  boolean state;
};

btn_control controls[NUM_CONTROLS] = {
    {"Feed", 6, false}, {"Purge", 7, false}, {"Pump", 5, true}};

// Alarm inputs
typedef struct alarm_input {
  char *label;
  int pin;
  boolean state;
};

alarm_input alarms[NUM_ALARMS] = {{"Tank Full", 8, false}};

// Flow sensors use interrupts to count pulses that are stored in
// volatile global variables
volatile unsigned long productFlowCounter = 0;
volatile unsigned long wasteFlowCounter = 0;

void prodFlowIrq() { productFlowCounter++; }
void wasteFlowIrq() { wasteFlowCounter++; }

typedef struct sensor {
  char *label;
  volatile unsigned long *pulseCount;
  unsigned long last_count;
  unsigned long last_milli;
  float flow;
  float flow_target;
  float flow_sum;
  float flow_rates[AVERAGE_PERIOD];  // buffer to store flow rates for averaging
  int buffer_idx;
};

sensor sensors[NUM_SENSORS] = {
    {" Clean ", &productFlowCounter, 0, 0, 0.0, 250.0, 0.0, {0}, 0},
    {" Waste ", &wasteFlowCounter, 0, 0, 0.0, 800.0, 0.0, {0}, 0}};

// TFT and touch screen
Adafruit_ILI9341 tft = Adafruit_ILI9341(TFT_CS, TFT_DC);
Adafruit_FT6206 cts = Adafruit_FT6206();

// Event variables
boolean start = false;  // Used to start the state machine
boolean play = true;    // Tracks the play/pause button state

// Push button control setup
int btnH = 40;
int btnW = 0;
int btnT = 0;

void setup() {
  Serial.begin(115200);

  // Flow sensor pins
  int prd_flow_pin = 2;
  int wst_flow_pin = 3;

  // Attach the interrupts to the flow sensor pins
  attachInterrupt(digitalPinToInterrupt(prd_flow_pin), prodFlowIrq, RISING);
  attachInterrupt(digitalPinToInterrupt(wst_flow_pin), wasteFlowIrq, RISING);

  // Initialise controls to default state
  for (int i = 0; i < NUM_CONTROLS; i++) {
    pinMode(controls[i].pin, OUTPUT);
    digitalWrite(controls[i].pin, controls[i].state);
  }

  // Initialise alarms to current value
  for (int i = 0; i < NUM_ALARMS; i++) {
    pinMode(alarms[i].pin, INPUT);
    alarms[i].state = digitalRead(alarms[i].pin);
  }

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

void loop() {
  boolean stateChanged = false;

  // Handle our start event as a special case
  if (start) {
    stateNow = WAITING;
    stateChanged = true;
    start = false;
  }

  // External event processing

  // Check for any physical alarm changes and one-time trip them
  // since we only have 1, we can just check the pin
  if (digitalRead(alarms[TANK_FULL].pin) && !alarms[TANK_FULL].state) {
    Serial.println("Tank full");
    alarms[TANK_FULL].state = true;
    stateNow = FULL;
    stateChanged = true;
  } else {
    // Touchscreen presses are ignored if we have an alarm tripped
    if (getTouch()) {
      events[(play) ? PLAY_BTN : PAUSE_BTN].active = true;
    }
  }

  // Handle the rinsed transition guard conditions
  if (events[RINSED].active) {
    stateNow = (play) ? RUNNING : WAITING;
    stateChanged = true;
    events[RINSED].active = false;
  }

  // Handle the tank_check transition
  if (events[TANK_CHECK].active) {
    if (digitalRead(alarms[TANK_FULL].pin) == false) {
      stateNow = WAITING;
      stateChanged = true;
      alarms[TANK_FULL].state = false;
      events[TANK_CHECK].active = false;
    }
  }

  // Transition active events to their target state
  // TODO: this will only transition to the last event in the array
  for (int i = 0; i < NUM_EVENTS; i++) {
    if (events[i].active) {
      stateNow = events[i].targetState;
      stateChanged = true;
      events[i].active = false;
    }
  }

  // Update the control settings for the current state
  controls[FEED].state = states[stateNow].feed;
  controls[PURGE].state = states[stateNow].purge;
  controls[PUMP].state = states[stateNow].pump;

  // Manage state entry
  if (stateChanged) {
    for (int i = 0; i < NUM_CONTROLS; i++) {
      digitalWrite(controls[i].pin, controls[i].state);
    }
    draw_statechanged(ILI9341_BLACK, states[stateNow].label);
    event_times[states[stateNow].timer].end_millis =
        event_times[states[stateNow].timer].duration + millis();
    stateChanged = false;
  }

  // Send any events that are ready
  unsigned long now = millis();

  if (now > event_times[states[stateNow].timer].end_millis) {
    events[event_times[states[stateNow].timer].event].active = true;
    event_times[states[stateNow].timer].end_millis = 0;
  } else {
    draw_timechanged(event_times[states[stateNow].timer].end_millis - now);
  }

  // Draw flow meter readings
  draw_sensors();
}

// Updates the display when the state changes to avoid flicker
void draw_statechanged(uint16_t fg_colour, char *status) {
  // Status Bar
  tft.setTextSize(3);
  tft.fillRoundRect(0, 145, tft.width(), 48, 5, states[stateNow].colour);
  tft.setCursor(10, 160);
  tft.setTextColor(fg_colour, states[stateNow].colour);
  tft.print(status);

  // Button controls
  for (int i = 0; i < NUM_CONTROLS; i++) {
    int padding = (strlen(controls[i].label) == 4) ? 20 : 10;
    uint16_t colour = controls[i].state ? ILI9341_DARKGREEN : ILI9341_RED;
    tft.drawRoundRect(i * btnW, btnT, btnW, btnH, 10, ILI9341_WHITE);
    tft.fillRoundRect(i * btnW + 1, btnT + 1, btnW - 2, btnH - 2, 10, colour);
    tft.setCursor(i * btnW + padding, btnT + 10);
    tft.setTextColor(ILI9341_WHITE, colour);
    tft.println(controls[i].label);
  }

  // Play / Pause Button
  state icon = states[stateNow].icon;
  tft.fillCircle(250, 70, 60, states[icon].colour);
  tft.drawCircle(250, 70, 60, ILI9341_WHITE);
  tft.drawCircle(250, 70, 59, ILI9341_BLACK);
  switch (icon) {
    case WAITING:
      tft.fillTriangle(220, 110, 220, 30, 300, 70, ILI9341_BLACK);
      play = true;
      break;
    case RUNNING:
      tft.fillRoundRect(220, 30, 20, 80, 5, ILI9341_BLACK);
      tft.fillRoundRect(260, 30, 20, 80, 5, ILI9341_BLACK);
      play = false;
      break;
    default:
      tft.fillTriangle(220, 110, 250, 50, 280, 110, ILI9341_BLACK);
      tft.fillTriangle(220, 30, 250, 90, 280, 30, ILI9341_BLACK);
      tft.fillTriangle(223, 107, 250, 55, 277, 107, ILI9341_LIGHTGREY);
      tft.fillTriangle(223, 33, 250, 85, 277, 33, ILI9341_LIGHTGREY);
  }
}

// Updates the display when the time changes
void draw_timechanged(unsigned long millis) {
  unsigned long seconds = millis / 1000;
  unsigned long minutes = seconds / 60;
  unsigned long hours = minutes / 60;

  // Calculate the remaining minutes and seconds
  minutes %= 60;
  seconds %= 60;

  // Format the time
  char buffer[9];
  snprintf(buffer, sizeof(buffer), "%02lu:%02lu:%02lu", hours, minutes,
           seconds);

  tft.setTextSize(3);
  tft.setCursor(165, 160);
  tft.setTextColor(ILI9341_BLACK, states[stateNow].colour);
  tft.print(buffer);
}

boolean getTouch(void) {
  if (cts.touched()) {
    TS_Point p = cts.getPoint();

    // Check the play/pause button was pressed
    if (p.y > 190 && p.x > 85) {
      return true;
    }
  }
  return false;
}

// Reads the flow sensors and updates the flow rates. A circular buffer is used
// to calculate the average flow rate over AVERAGE_PERIOD of seconds.
void draw_sensors(void) {
  int xp = 0, yp = 5, gap = 5, rad = 40;

  for (int i = 0; i < NUM_SENSORS; i++) {
    // Disable the interrupts while we read the counter value
    noInterrupts();
    unsigned long pulseCount = *(sensors[i].pulseCount);
    interrupts();

    unsigned long now = millis();
    unsigned long dCount = pulseCount - sensors[i].last_count;
    unsigned long dTime = now - sensors[i].last_milli;

    if (dTime >= 1000) {
      sensors[i].flow = dCount;
      sensors[i].flow_rates[sensors[i].buffer_idx] = sensors[i].flow;
      sensors[i].flow_sum = 0;
      for (int j = 0; j < AVERAGE_PERIOD; j++) {
        sensors[i].flow_sum += sensors[i].flow_rates[j];
      }
      int flow_avg = sensors[i].flow_sum / AVERAGE_PERIOD;
      sensors[i].buffer_idx = (sensors[i].buffer_idx + 1) % AVERAGE_PERIOD;
      sensors[i].last_count = pulseCount;
      sensors[i].last_milli = now;

      tft.setCursor(xp, yp + (rad + gap) * 2);
      tft.setTextColor(ILI9341_BLACK, ILI9341_WHITE);
      tft.setTextSize(2);
      tft.print(sensors[i].label);

      xp = gap + ringMeter(flow_avg, 0, sensors[i].flow_target, xp, yp, rad,
                           "ml/min");
    }
  }
}

int ringMeter(int value, int vmin, int vmax, int x, int y, int r, char *units) {
  x += r;
  y += r;  // Calculate coords of centre of ring

  int w = r / 4;        // Width of outer ring is 1/4 of radius
  int angle = 150;      // Half the sweep angle of meter (300 degrees)
  int text_colour = 0;  // To hold the text colour
  int v = map(value, vmin, vmax, -angle, angle);  // Map the value to an angle v
  byte seg = 5;   // Segments are 5 degrees wide = 60 segments for 300 degrees
  byte inc = 15;  // Draw segments every 5 deg, increase to 10 for segments

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
    char valuestr[4];
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
  }

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
