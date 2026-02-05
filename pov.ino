#define TLC_CLK 14
#define TLC_DIN 13
#define TLC_EN 5
#define TLC_LATCH 12
#define BL_ADC A0
#define HE_SENS 4

#include <string.h>
#include "smiley.h"
#include "alpha.h"

#define SLICE_NUM 360UL 
#define ALPHA_SLICE_NUM 100UL  

volatile uint32_t isrLastPulse = 0;
volatile uint32_t isrMtrTimePeriod = 0;
volatile uint32_t mtrLastUpdate = 0, previousSlice = 0, lastSlideExecute = 0, lastLoopExecute =0, lastAnimateExecute = 0, now_milli=0;
uint8_t dispIndex = 10;
uint8_t battCheckCount = 0;
bool updated = false;
uint16_t frame[SLICE_NUM] = {0};
uint32_t dynamicSlice = SLICE_NUM;



void tlc_write_12_bits(uint16_t inp) {
  for (uint8_t i = 0; i < 12; i++) {
    digitalWrite(TLC_CLK, LOW);
    digitalWrite(TLC_DIN, (inp >> i) & 1);
    digitalWrite(TLC_CLK, HIGH);
  }
  digitalWrite(TLC_LATCH, HIGH);
  delayMicroseconds(1);
  digitalWrite(TLC_LATCH, LOW);
}

void IRAM_ATTR mIsr() {
  uint32_t now = micros();
  isrMtrTimePeriod = now - isrLastPulse;
  isrLastPulse = now;
}

// start_row = 0: D1
// start_pos = 0: 90 deg
void load_string(char *s, uint8_t start_pos = 0, uint8_t start_row = 0, bool merge = false){
  if(s == NULL)
    return;
  uint8_t start_pos_ = start_pos >= dynamicSlice ? dynamicSlice - 1 : start_pos;
  uint16_t * temp = frame+start_pos_;
  uint8_t cols[5];
  uint8_t string_len = strlen(s);
  if(!merge){
    memset(frame, 0, sizeof(frame));
  }
  // since minimum 5 rows requried 7+5 = 11 Max led row
  start_row = start_row > 7 ? 7 : start_row; 
  for(int i = 0; i< string_len; i++){
    memcpy_P(cols, &alpha[(s[i] - ' ') * 5], 5);
    for(int  j = 0; j< 5; j++){
      *temp = *temp | (((uint16_t)cols[j]) << start_row);
       temp++;
       if(temp == frame + dynamicSlice)
        temp = frame;
    }
  }
}

void sliding_string(char *s, uint32_t slide_time_ms){
  static uint8_t slider_pos = 0;
  if((now_milli - lastSlideExecute) > slide_time_ms){
    load_string(s, slider_pos);
    slider_pos =  (slider_pos + 1) % dynamicSlice;
    lastSlideExecute = now_milli;
  }
}

uint8_t compute_mid(char * s){
  uint8_t mid = strlen(s) / 2;
  return dynamicSlice - (mid*5);
}

void smiley_animate(){
  static uint8_t frame_count =0, skip = 0, c2=0;
  static int8_t rotate = -1;
  if(now_milli - lastAnimateExecute > 100){
    if(c2 > 10){
      skip = 0;
      c2=0;
    }
    if(!skip){
    memcpy_P(frame, smiley_face_animation[frame_count], sizeof(frame));
    lastAnimateExecute = now_milli;
    if(frame_count == 5 || frame_count == 0){
      rotate*=-1;
      
      skip = 1;
    }
    frame_count +=rotate;
    }
    else 
    c2++;
  }
}

void show_battery_level(){
  char s[] = "B A T T E R Y";
  float conversionFactor = 10.0F/(10.0F+100.0F);
  float lowLevel = 3.45F;
  float highLevel = 4.2F;
  int bl=0;
  for(int i =0; i< 5; i++){
    bl += analogRead(BL_ADC);
  }
  bl=bl/5;
  float battVol = (bl/1023.0F)/conversionFactor;
  float battLevel = max(((battVol - lowLevel)/(highLevel-lowLevel)), 0.0F);
  memset(frame, 0, sizeof(frame));
  uint8_t battLevelMapped = ALPHA_SLICE_NUM * battLevel;
  battLevelMapped = battLevelMapped > ALPHA_SLICE_NUM ? ALPHA_SLICE_NUM : battLevelMapped;
  for(int i = 0; i<battLevelMapped; i ++){
    //Turn on first 3 rows;
    frame[i] = (uint16_t)0x0007;
  }
  load_string(s, compute_mid(s), 4, true);
}

void setup() {
  pinMode(HE_SENS, INPUT);

  pinMode(TLC_CLK, OUTPUT);
  pinMode(TLC_EN, OUTPUT);
  pinMode(TLC_DIN, OUTPUT);
  pinMode(TLC_LATCH, OUTPUT);

  digitalWrite(TLC_EN, LOW);

  attachInterrupt(digitalPinToInterrupt(HE_SENS), mIsr, FALLING);
  previousSlice = SLICE_NUM;
}



void loop() {
  char *s[]={"H E L L O !", "P O V   B Y", "O B T R O N", ":)", ":D"};
  uint32_t localMtrTimePeriod, localLastPulse;
  noInterrupts();
  localMtrTimePeriod = isrMtrTimePeriod;
  localLastPulse = isrLastPulse;
  interrupts();


  uint32_t now = micros();
  now_milli = millis();
  // prevent div by 0
  if(localMtrTimePeriod == 0)
    return;
  // Guessing how much motor would have moved since the last pulse or start of rotation
  uint32_t loopElapsed = now - localLastPulse;
  // If loop() was running more than the one rotation of motor then we can wrap it.
  loopElapsed %= localMtrTimePeriod;
  uint32_t currentSlice = (loopElapsed * dynamicSlice) /localMtrTimePeriod;
  if(now_milli - lastLoopExecute > 3000){
    dispIndex++;
    lastLoopExecute = now_milli;
  }
  switch(dispIndex){
    case 0:
      dynamicSlice = ALPHA_SLICE_NUM;
      if(!updated){
        load_string(s[dispIndex], compute_mid(s[dispIndex]));
        updated = true;
      }
    break;
    case 1:
    case 2:
    case 3:
    case 4:
      dynamicSlice = ALPHA_SLICE_NUM;
      sliding_string(s[dispIndex], 100);
    break;
    case 10:
      if(!updated){
        dynamicSlice = ALPHA_SLICE_NUM;
        show_battery_level();
        updated = true;
      }
      break;
    case 11:
      updated = false;
      dispIndex = 0;  
    break;
    default:
      updated = false;
      dynamicSlice = SLICE_NUM;
      smiley_animate();
  }

  if(currentSlice != previousSlice){
    tlc_write_12_bits(frame[currentSlice % dynamicSlice]);
    previousSlice = currentSlice;
  }
}
