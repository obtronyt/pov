#define TLC_CLK 14
#define TLC_DIN 13
#define TLC_EN 5
#define TLC_LATCH 12
#define BL_ADC A0
#define HE_SENS 4

#include <string.h>
#include "smiley.h"

#define SLICE_NUM 360UL   

volatile uint32_t isrLastPulse = 0;
volatile uint32_t isrMtrTimePeriod = 0;
volatile uint32_t mtrLastUpdate = 0, previousSlice = 0, lastSlideExecute = 0, lastLoopExecute =0, lastAnimateExecute = 0, now_milli=0 ;
uint8_t dispIndex = 0;
bool updated = false;
uint16_t frame[SLICE_NUM] = {0};
uint32_t dynamic_slice = SLICE_NUM;

const uint8_t alpha[59 * 5] PROGMEM = {
  0x00, 0x00, 0x00, 0x00, 0x00,// (space)
	0x00, 0x00, 0x5F, 0x00, 0x00,// !
	0x00, 0x07, 0x00, 0x07, 0x00,// "
	0x14, 0x7F, 0x14, 0x7F, 0x14,// #
	0x24, 0x2A, 0x7F, 0x2A, 0x12,// $
	0x23, 0x13, 0x08, 0x64, 0x62,// %
	0x36, 0x49, 0x55, 0x22, 0x50,// &
	0x00, 0x05, 0x03, 0x00, 0x00,// '
	0x00, 0x1C, 0x22, 0x41, 0x00,// (
	0x00, 0x41, 0x22, 0x1C, 0x00,// )
	0x08, 0x2A, 0x1C, 0x2A, 0x08,// *
	0x08, 0x08, 0x3E, 0x08, 0x08,// +
	0x00, 0x50, 0x30, 0x00, 0x00,// ,
	0x08, 0x08, 0x08, 0x08, 0x08,// -
	0x00, 0x60, 0x60, 0x00, 0x00,// .
	0x20, 0x10, 0x08, 0x04, 0x02,// /
	0x3E, 0x51, 0x49, 0x45, 0x3E,// 0
	0x00, 0x42, 0x7F, 0x40, 0x00,// 1
	0x42, 0x61, 0x51, 0x49, 0x46,// 2
	0x21, 0x41, 0x45, 0x4B, 0x31,// 3
	0x18, 0x14, 0x12, 0x7F, 0x10,// 4
	0x27, 0x45, 0x45, 0x45, 0x39,// 5
	0x3C, 0x4A, 0x49, 0x49, 0x30,// 6
	0x01, 0x71, 0x09, 0x05, 0x03,// 7
	0x36, 0x49, 0x49, 0x49, 0x36,// 8
	0x06, 0x49, 0x49, 0x29, 0x1E,// 9
	0x00, 0x36, 0x36, 0x00, 0x00,// :
	0x00, 0x56, 0x36, 0x00, 0x00,// ;
	0x00, 0x08, 0x14, 0x22, 0x41,// <
	0x14, 0x14, 0x14, 0x14, 0x14,// =
	0x41, 0x22, 0x14, 0x08, 0x00,// >
	0x02, 0x01, 0x51, 0x09, 0x06,// ?
	0x32, 0x49, 0x79, 0x41, 0x3E,// @
  0x7E, 0x11, 0x11, 0x11, 0x7E, // A
  0x7F, 0x49, 0x49, 0x49, 0x36, // B
  0x3E, 0x41, 0x41, 0x41, 0x22, // C
  0x7F, 0x41, 0x41, 0x22, 0x1C, // D
  0x7F, 0x49, 0x49, 0x49, 0x41, // E
  0x7F, 0x09, 0x09, 0x01, 0x01, // F
  0x3E, 0x41, 0x41, 0x51, 0x32, // G
  0x7F, 0x08, 0x08, 0x08, 0x7F, // H
  0x00, 0x41, 0x7F, 0x41, 0x00, // I
  0x20, 0x40, 0x41, 0x3F, 0x01, // J
  0x7F, 0x08, 0x14, 0x22, 0x41, // K
  0x7F, 0x40, 0x40, 0x40, 0x40, // L
  0x7F, 0x02, 0x04, 0x02, 0x7F, // M
  0x7F, 0x04, 0x08, 0x10, 0x7F, // N
  0x3E, 0x41, 0x41, 0x41, 0x3E, // O
  0x7F, 0x09, 0x09, 0x09, 0x06, // P
  0x3E, 0x41, 0x51, 0x21, 0x5E, // Q
  0x7F, 0x09, 0x19, 0x29, 0x46, // R
  0x46, 0x49, 0x49, 0x49, 0x31, // S
  0x01, 0x01, 0x7F, 0x01, 0x01, // T
  0x3F, 0x40, 0x40, 0x40, 0x3F, // U
  0x1F, 0x20, 0x40, 0x20, 0x1F, // V
  0x7F, 0x20, 0x18, 0x20, 0x7F, // W
  0x63, 0x14, 0x08, 0x14, 0x63, // X
  0x03, 0x04, 0x78, 0x04, 0x03, // Y
  0x61, 0x51, 0x49, 0x45, 0x43  // Z
};

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

void load_string(char *s, uint8_t start_pos = 0){
  uint8_t start_pos_ = start_pos >= SLICE_NUM ? SLICE_NUM - 1 : start_pos;
  uint16_t * temp = frame+start_pos_;
  uint8_t cols[5];
  uint8_t string_len = strlen(s);
  if(s == NULL)
    return;
  memset(frame, 0, sizeof(frame));
  for(int i = 0; i< string_len; i++){
    memcpy_P(cols, &alpha[(s[i] - ' ') * 5], 5);
    for(int  j = 0; j< 5; j++){
      *temp = (uint16_t)cols[j];
       temp++;
       if(temp == frame + SLICE_NUM)
        temp = frame;
    }
  }
}

void sliding_string(char *s, uint32_t slide_time_ms){
  static uint8_t slider_pos = 0;
  if((now_milli - lastSlideExecute) > slide_time_ms){
    load_string(s, slider_pos);
    slider_pos =  (slider_pos + 1) % dynamic_slice;
    lastSlideExecute = now_milli;
  }
}

uint8_t compute_mid(char * s){
  uint8_t mid = strlen(s) / 2;
  return dynamic_slice - (mid*5);
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
void loop() {
  char *s[]={"H E L L O !", "D Y N A M I C", "P O V", ":)", ":D"};
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
  uint32_t currentSlice = (loopElapsed * dynamic_slice) /localMtrTimePeriod;
  if(now_milli - lastLoopExecute > 3000){
    dispIndex++;
    lastLoopExecute = now_milli;
  }
  switch(dispIndex){
    case 0:
      dynamic_slice = 100;
      if(!updated){
        load_string(s[dispIndex], compute_mid(s[dispIndex]));
        updated = true;
      }
    break;
    case 1:
    case 2:
    case 3:
    case 4:
      dynamic_slice = 100;
      sliding_string(s[dispIndex], 100);
    break;
    case 10:
      updated = false;
      dispIndex = 0;
    break;
    default:
      dynamic_slice = SLICE_NUM;
      smiley_animate();
  }

  if(currentSlice != previousSlice){
    tlc_write_12_bits(frame[currentSlice % SLICE_NUM]);
    previousSlice = currentSlice;
  }
}
