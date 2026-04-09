#define TLC_CLK 14
#define TLC_DIN 13
#define TLC_EN 5
#define TLC_LATCH 12
#define BL_ADC A0
#define HE_SENS 4

#include <string.h>
#include "smiley.h"
#include "alpha.h"
#include "math.h"

#define SLICE_NUM 360UL 
#define ALPHA_SLICE_NUM 120UL 
#define ANIMATE_SLICE_NUM 60UL 

volatile uint32_t isrLastPulse = 0;
volatile uint32_t isrMtrTimePeriod = 0;
volatile uint32_t mtrLastUpdate = 0, previousSlice = 0, lastSlideExecute = 0, lastLoopExecute =0, lastAnimateExecute = 0, now_milli=0, nextBlink =0;
uint8_t dispIndex = 0;
uint8_t battCheckCount = 0;
bool updated = false;
uint16_t frame[SLICE_NUM] = {0};
uint16_t random_index[SLICE_NUM] = {0};
uint16_t sine_wave[SLICE_NUM] = {0};
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

void populate_sine_wave(){
    static float phase = 0.2;
    for(int i = 0; i < SLICE_NUM; i++){
      float val = sin((2 * PI * i * 6) / SLICE_NUM + phase);
      phase+=0.2; 
      val = (val + 1.0f) * 0.5f;                 
      uint16_t index = (uint16_t)(val * 11 + 0.5f); // 0–11
      sine_wave[i] = 1 << index;
  }
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
      updated = false;
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

void sliding_string(char *s, uint32_t slide_time_ms, bool merge=false){
  static uint8_t slider_pos = 0;
  if((now_milli - lastSlideExecute) > slide_time_ms){
    load_string(s, slider_pos, merge);
    slider_pos =  (slider_pos + 1) % dynamicSlice;
    lastSlideExecute = now_milli;
  }
}

uint16_t compute_mid(char * s, bool reverse = false){
  uint16_t mid = strlen(s) / 2;
  if(!reverse)
    return dynamicSlice - (mid*5);
  else
    return (dynamicSlice/2) - (mid*5);
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

// 10*8 rectangel with 3*3 eye ball void
void generate_eye(uint16_t start_col, uint8_t eye_ball_x, uint8_t eye_ball_y, char blink){
  uint16_t eye_frame[8]={0};
  uint8_t e_col=8;
  uint8_t e_row=10;
  for(int i=0;i<(e_row);i++){
    for(int j=0; j< e_col; j++){
      if(blink){
        if(i==5 || i ==6)
          eye_frame[j] |= 1<<(i);
      }
      else{
        if(!(i >= eye_ball_y && (i < eye_ball_y+3) && j >= eye_ball_x && (j < eye_ball_x+3)))
        {
          eye_frame[j]|=1<<(i+1);
        }
      }
    }
  }

  for(int i=0;i<8;i++){
    uint16_t ind = (start_col+i)%dynamicSlice;
    frame[ind] = eye_frame[i];
  }

}

void eye_animation(){
  static uint16_t curr_frame = 0;
  static uint32_t startedBlink =0;
  long r = 0;
  if(nextBlink == 0){
  memset(frame, 0 ,sizeof(frame));
  nextBlink = now_milli + 1000;
  }
  switch(curr_frame){
      case 0:
        if(!updated){
        load_string("---------", (dynamicSlice/2) - 5, 0, true);

        // left eye
        generate_eye(dynamicSlice-10, 3,3,0);
        // right eye
        generate_eye(10, 3,3,0);
        updated = true;
        }
        if(now_milli > nextBlink ){
          curr_frame++;
          startedBlink = now_milli;
          updated = false;
        }
      break;
      default:
                // left eye
        if(!updated){
        generate_eye(dynamicSlice-10, 3,3,1);
        // right eye
        generate_eye(10, 3,3,1);
        updated = true;
        }
        if(now_milli - startedBlink > 400)
        {
          curr_frame = 0;
          r = random(800,1200);
          nextBlink = now_milli + r;
          updated = false;
        }
    }

}

void eye_animation_pattern(){
  static uint8_t state = 0;
  static uint32_t lastMove = 0;
  static uint8_t step = 0;

  static int8_t dx = 3;
  static int8_t dy = 3;

  if(now_milli - lastMove < 150) return;
  lastMove = now_milli;

  switch(state){

    case 0: // center to right
      dx = 3 + step;
      dy = 3;
      step++;
      if(step > 2){
        step = 0;
        state = 1;
      }
    break;

    case 1: // center
      dx = 3;
      dy = 3;
      state = 2;
    break;

    case 2: // center to diagonal right
      dx = 3 + step;
      dy = 3 + step;
      step++;
      if(step > 2){
        step = 0;
        state = 3;
      }
    break;

    case 3: // center
      dx = 3;
      dy = 3;
      state = 4;
    break;

    case 4: // center to left
      dx = 3 - step;
      dy = 3;
      step++;
      if(step > 2){
        step = 0;
        state = 5;
      }
    break;

    case 5: // center
      dx = 3;
      dy = 3;
      state = 6;
    break;

    case 6: // center to diagonal left
      dx = 3 - step;
      dy = 3 - step;
      step++;
      if(step > 2){
        step = 0;
        state = 0;
      }
    break;
  }

  generate_eye(dynamicSlice-10, dx, dy, 0);
  generate_eye(10, dx, dy, 0);
}
void set_pixel(uint16_t x, uint16_t y, bool on=true){
  if(on)
    frame[x%dynamicSlice]|=1<<(y%12);
  else
    frame[x%dynamicSlice]&= (1<<(y%12))^(0xFFFF) ;
}

void randomize_array(){
  for(int i = 0; i< 360; i++)
  {
    random_index[i]=i;
  }
  for(int i=0; i < 360; i++){
    int j = ((int)random(360));
    uint16_t temp = random_index[j];
    random_index[j] = random_index[i];
    random_index[i] = temp;
  }
}

void random_sparkle(){
  static uint16_t current_pixel = 0;
  static uint32_t animateTime = 0;
  static uint8_t pixel_life[360][12] = {0};
  if(updated) return;
  if(!animationInProgress){
    current_pixel = 0;
    randomize_array();
    animationInProgress = true;
    memset(frame, 0, sizeof(frame));
    memset(pixel_life, 0 ,sizeof(pixel_life));
    animateTime = now_milli;
  }
  if(now_milli - lastAnimateExecute < 1) return;
  lastAnimateExecute = now_milli;

  for(int i = 0; i < 360; i++){
    for(int j =0 ; j<12; j++){
      if(pixel_life[i][j] > 0){
        pixel_life[i][j]-=1;
        if(pixel_life[i][j] == 0){
          set_pixel(i, j, false);
        }
      }
    }
  }
  for(int m = 0 ;m <5;m++){

    uint16_t rRow = random(12);
    set_pixel(random_index[current_pixel], rRow);
    pixel_life[random_index[current_pixel]][rRow] = random(5,15);
    current_pixel ++;
    if(current_pixel >= 360)
    { randomize_array();
      current_pixel = 0;
    }
  }

  if(now_milli - animateTime > 7000){
    updated = true;
    animationInProgress = false;
  }

}
void animate_sine_wave(bool reset = false){
  static uint16_t index = 0;
  static uint16_t trail[6] = {0};

  if(reset){
    index = 0;
    memset(trail, 0, sizeof(trail));
    return;
  }

  if(now_milli - lastAnimateExecute < 50) return;
  lastAnimateExecute = now_milli;

  if(index == 0){
    memset(frame, 0, sizeof(frame));
    for(int i = 0; i < 6; i++) trail[i] = 0;
  }

  for(int i = 5; i > 0; i--){
    trail[i] = trail[i-1];
  }
  trail[0] = index;
  memset(frame, 0, sizeof(frame));
  for(int i = 0; i < 6; i++){
    uint16_t pos = trail[i];
    frame[pos] = sine_wave[pos];
  }
  index++;
  if(index >= SLICE_NUM){
    index = 0;
  }
}


void setup() {
  randomSeed(millis());
  populate_sine_wave();

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
  char *s[]={"P O V   D I S P", "L O A D I N G"};
  char fin[]  = "- - F I N - -";
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
      dynamicSlice = ALPHA_SLICE_NUM;
      if(!updated){
        load_string(s[dispIndex], compute_mid(s[dispIndex]));
        updated = true;
      }
    break;
    case 1:
      sliding_string(s[dispIndex], 100);
    break;
    case 2:
    case 3:
      animate_sine_wave();
    break;
    case 4:
    case 5:
      dynamicSlice = ALPHA_SLICE_NUM;
      eye_animation();
    break;
    case 6:
    case 7:
      dynamicSlice = ALPHA_SLICE_NUM;
      eye_animation_pattern();
    break;
    case 11:
      if(!updated){
        dynamicSlice = ALPHA_SLICE_NUM;
        show_battery_level();
        updated = true;
      }
      break;
    case 12:
      dynamicSlice = ANIMATE_SLICE_NUM;
      load_kicker(220);
      break;
    case 13:
    case 14:
      dynamicSlice = 100;
      if(!updated){
      char *st = "O U C H";
      memset(frame, 0, sizeof(frame));
      memcpy(frame, kicker+(5*10), sizeof(uint16_t)*10);
      load_string(st, compute_mid(st, true), 0, true);
      updated = true;
      }
    break;
    case 15:
      dynamicSlice = SLICE_NUM;
      random_sparkle();
    break;
    case 16:
      dynamicSlice = ALPHA_SLICE_NUM;
      if(!updated){
        load_string(fin, compute_mid(fin));
        updated = true;
      }
    break;
    case 17:
      dispIndex = 0;
      updated = 0;
      animationInProgress=false;
      animationTime = 3000;
      nextBlink = 0;
      animate_sine_wave(true);
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
