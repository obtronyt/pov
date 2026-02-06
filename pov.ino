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
#define ALPHA_SLICE_NUM 120UL 
#define ANIMATE_SLICE_NUM 60UL 

volatile uint32_t isrLastPulse = 0;
volatile uint32_t isrMtrTimePeriod = 0;
volatile uint32_t mtrLastUpdate = 0, previousSlice = 0, lastSlideExecute = 0, lastLoopExecute =0, lastAnimateExecute = 0, now_milli=0;
uint8_t dispIndex = 0;
uint8_t battCheckCount = 0;
bool updated = false;
uint16_t frame[SLICE_NUM] = {0};
uint32_t dynamicSlice = SLICE_NUM;
bool animationInProgress =false;



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


void load_kicker(uint16_t animate_speed = 200)
{
  static uint16_t kickerId = 0, ballFall = 0, ballFalling = 0x3;
  char hit_index = 2;
  uint16_t *temp = NULL;
  if(now_milli - lastAnimateExecute < animate_speed)
    return;
  switch(kickerId){
  // stand
  case 0:
      animationInProgress = true;
      memset(frame, 0, sizeof(frame));
      memcpy(frame, kicker, sizeof(uint16_t)*10);
      kickerId++;
      break;
  // ball fall
  case 1:
      memset(frame, 0, sizeof(frame));
      memcpy(frame, kicker, sizeof(uint16_t)*10);
      frame[8] |= ballFalling<<ballFall;
      frame[9] |= ballFalling<<ballFall;
      ballFall++;
      if(ballFall >= 10)
        kickerId++;
      break;
  case 2:
  case 3:
      memset(frame, 0, sizeof(frame));
      memcpy(frame, kicker+(10*(kickerId-1)), sizeof(uint16_t)*10);
      kickerId++;
      ballFall = 10;
      break;
  case 4:
      // ball moving
      if(ballFall == hit_index)
        kickerId++;
      else{
      memset(frame, 0, sizeof(frame));
      memcpy(frame, walker+((ballFall%4)*10), sizeof(uint16_t)*10);
      uint16_t ball_bounce = ballFall%4+5; 
      temp = frame+ballFall;
      *temp = ballFalling << ball_bounce;
      ballFall = (ballFall+1)%dynamicSlice;
      temp = frame+ballFall;
      *temp = ballFalling << ball_bounce;
      }
      break;
  // GUy falling
  case 5:
  case 6:
  case 7:
      memset(frame, 0 ,sizeof(frame));
      memcpy(frame, kicker+(10*(kickerId-2)), sizeof(uint16_t)*10);
      kickerId++;
      break;
  case 8:
      ballFall = 0;
      kickerId = 0;
      memset(frame, 0 ,sizeof(frame));
      memcpy(frame, kicker+(10*(kickerId-3)), sizeof(uint16_t)*10);
      animationInProgress = false;
      dispIndex++;
      break;
  }
  lastAnimateExecute = now_milli;
}

void load_walker(uint16_t animate_speed = 200){
  static uint16_t walkerId = 0, xOffset = 0;
  uint16_t *temp = NULL;
  if(now_milli - lastAnimateExecute < animate_speed)
    return;
  memset(frame, 0 , sizeof(frame));
  temp = frame + xOffset;
  for(char i=0 ; i < 10; i++)
  {
    *temp = walker[walkerId*10 + i];
    temp++;
    if(temp == frame+dynamicSlice)
      temp = frame;
  }
  walkerId = (walkerId + 1)%4;
  xOffset = (xOffset + 1)%dynamicSlice;
  lastAnimateExecute = now_milli;
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
  char o[] = "O U C H !";
  uint32_t localMtrTimePeriod, localLastPulse;
  volatile uint16_t animationTime = 3000;
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
  if(now_milli - lastLoopExecute > animationTime){
    if(!animationInProgress){
      dispIndex++;
      updated = false;
    }
    lastLoopExecute = now_milli;
  }
  switch(dispIndex){ 
    case 0:
    case 1:
    case 2:
      dynamicSlice = ALPHA_SLICE_NUM;
      if(!updated){
        load_string(s[dispIndex], compute_mid(s[dispIndex]));
        updated = true;
      }
    break;
    case 3:
    case 4:
      dynamicSlice = ALPHA_SLICE_NUM;
      sliding_string(s[dispIndex], 100);
    break;
    case 7:
      if(!updated){
        dynamicSlice = ALPHA_SLICE_NUM;
        show_battery_level();
        updated = true;
      }
      break;
    case 8:
      dynamicSlice = ANIMATE_SLICE_NUM;
      load_kicker(220);
      break;
    case 9:
      dynamicSlice = ANIMATE_SLICE_NUM;
      if(!updated){
      memset(frame, 0, sizeof(frame));
      memcpy(frame, kicker+(5*10), sizeof(uint16_t)*10);
      updated = true;
      }
      break;
    case 10:
      dynamicSlice = ALPHA_SLICE_NUM;
      sliding_string(o, 100);
      break;
    case 11:
      dispIndex = 0;
      updated = 0;
      animationInProgress=false;
      animationTime = 3000;
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
