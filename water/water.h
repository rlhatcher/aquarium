#define TFT_CS 10  // chip select pin for the TFT
#define TFT_DC 9   // data pin for the TFT

#define NUM_CONTROLS 3  // number of push buttons
#define NUM_SENSORS 2   // number of flow sensors

#define AVERAGE_PERIOD 60  // number of seconds for flow average

enum state { STANDBY, RUNNING, PRIME, RINSE, CONFIRM_START, CONFIRM_STOP };
enum control { FEED, PURGE, PUMP };
typedef struct buttonCtrl {
  String label;
  int pin;
  boolean state;
};

typedef struct sensorCtrl {
  String label;
  volatile unsigned long* pulseCount;
  unsigned long oldPulseCount;
  unsigned long lastMillis;
  float flowRate;
  float averageFlowRate;
  float flowRateSum;
  float flowRates[AVERAGE_PERIOD];  // buffer to store flow rates for averaging
  int bufferIndex;
};

typedef struct systemState {
  state name;
  boolean feed;
  boolean purge;
  boolean pump;
};
