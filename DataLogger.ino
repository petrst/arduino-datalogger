#include <avr/interrupt.h>
#include <avr/sleep.h>
#include <avr/wdt.h>
#include <LiquidCrystal.h>
#include <EEPROM.h>
#include <I2C_eeprom.h>
#include <Wire.h>
#include "DataLogger.h"


// HARDWARE ///////////////////////////////////////////
const int BUTTON_PIN = 2;
const int STATUS_LED = 13;
const int SERIAL_LED = 12;
const int MEM_FULL_LED = 8;
const int TEMP_PIN = 0;

// initialize the library with the numbers of the interface pins
LiquidCrystal lcd(12, 11, 5, 4, 3, 6);


// MODE SWITCHING /////////////////////////////////////
const int WAKE_MODE =  1;
const int SLEEP_MODE = 3;
volatile int mode = WAKE_MODE;
const long NO_ACTIVITY_DELAY = 30 * 60 * 1000;


// DISPLAY MODE //////////////////////////////////////
const int TEMP_MODE  =  1;
const int MEM_MODE  =  2;
const int INTERVAL_MODE  =  3;
const int OFF_MODE  =  4;

volatile int disp_mode = TEMP_MODE;


// BUTTON HANDLING ////////////////////////////////////
const int NO_EVENT = 0;
const int RELEASE = 1;
const int LONG_PRESS =2;
const long LONG_PRESS_DELAY = 3000;
const unsigned long DEBOUCE_DELAY = 200;

int btnLast = HIGH;
bool wasLongPress = false;
long btnClickTime = 0;
bool ignoreBtn = false;

// MEASUREMENT ////////////////////////////////////////
const long MEASURE_PERIOD = 3000; //8*60*60*1000;
const int INTERVALS[] = {1, 4, 8, 80, 240, 480 };
volatile int interval_index = 0;
int m_counter = 0;

float tempC;
int reading;
float ALPHA = 0.1;
const int nrReadings = 5;
float temp[nrReadings];
long measureLastTime=0;

volatile bool do_measure = false;

// STORAGE ///////////////////////////////////////////
int index = 0;
float total = 0.0;
int mem_index;
bool mem_full=false;
const int MEM_SIZE=512;
I2C_eeprom ee(0x50);



//////////////////////////////////////////////////////
// INTERRUPT HANDLERS ////////////////////////////////

// Wake up by button
void pinInterrupt()  
{  
    detachInterrupt(0);  
    //attachInterrupt(0, pinInterrupt, HIGH); 
    mode = WAKE_MODE; 
    disp_mode = TEMP_MODE;
    
    // Ignore btn release after wake up
    ignoreBtn = true;
}


// Wake up by watchdog
ISR(WDT_vect){  
  if (m_counter == (INTERVALS[interval_index]-1)){
    do_measure = true;
    m_counter = 0;
  } else {
    m_counter++;
  }
}


//////////////////////////////////////////////////////


 

void setup() {
  Serial.begin(9600);
  pinMode(STATUS_LED, OUTPUT);
  pinMode(SERIAL_LED, OUTPUT);
  pinMode(MEM_FULL_LED, OUTPUT);
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  analogReference(INTERNAL);
  //attachInterrupt(0, handleButton, FALLING);
  //attachInterrupt(0, INT_Button, CHANGE);
  // set up the LCD's number of columns and rows:
  lcd.begin(16, 2);
  // Print a message to the LCD.
  for (int i=0;i<nrReadings;i++){
    temp[i]=0.0;
  }   
 digitalWrite(STATUS_LED,LOW); 
 
     noInterrupts();
    MCUSR = 0;
    WDTCSR |= 0b00011000;
    WDTCSR =  0b01100001;
    wdt_reset();
    interrupts();
    
    byte b1 = ee.readByte(0);
    ee.writeByte(0, 128);
    byte b2 = ee.readByte(0);
    int ss= ee.determineSize();   
}



long get_time(){
  return millis();
}


float onedigit(float in){
  return round(in*10)/10.0;
}

float measure(){
  reading = analogRead(TEMP_PIN);
  total = total - temp[index];
  temp[index] = reading;
  total = total + temp[index];
  index++;
  if (index>=nrReadings){
    index=0;
  }  
  reading = total / nrReadings; 
  return reading / 9.31;  
}


void store_mem(float temp){
  
  //EEPROM.write(mem_index,temp);
  //mem_index += 4;
  StorageRecord rec = {get_time(),temp};
  EEPROM.put(mem_index, rec);
  mem_index += sizeof(rec);
  
  if ( mem_index + sizeof(rec)>MEM_SIZE ) {
    mem_full = true;
    digitalWrite(MEM_FULL_LED, HIGH);
  }
  
}


StorageRecord read_mem(int index){
  StorageRecord rec;
  EEPROM.get( index, rec);
 return rec; 
}


void mem_reset(){
  mem_index = 0;
  mem_full = false;
  digitalWrite(MEM_FULL_LED, LOW);
}


void dispMem(){
  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print("Memory: ");  
  lcd.print(mem_index);
  lcd.print("b");
}


void do_measure_and_store(){
   Serial.print("MEASURE");
   digitalWrite(STATUS_LED,HIGH);
   
   for (int i=0;i<nrReadings;i++){
     tempC = measure();
   }
   store_mem(tempC);
   measureLastTime = millis();
   delay(200);
   digitalWrite(STATUS_LED,LOW);
}


bool in_sleep_mode(){
  return mode == SLEEP_MODE;
}


bool time_to_sleep(){
  long now = millis();
  if ( now - btnClickTime > NO_ACTIVITY_DELAY ){
    return true;
  } else { 
    return false;
  }
}

bool time_to_measure(){
  // TODO: change implementation to RTC reading
  long now = millis();
  if (now - measureLastTime > MEASURE_PERIOD ){
    return true;
  } else {
    return false;
  }
}

  

void on_click(){

  if (disp_mode==TEMP_MODE) disp_mode = MEM_MODE;
  else if (disp_mode==MEM_MODE) disp_mode = INTERVAL_MODE;
  else if (disp_mode==INTERVAL_MODE) disp_mode = TEMP_MODE;
  lcd.clear();
}

void on_long_press(){
  if (disp_mode==INTERVAL_MODE){
    interval_index = (interval_index + 1) % (sizeof(INTERVALS)/sizeof(int));
  } else if ( disp_mode == MEM_MODE ){
    lcd.clear();
    lcd.setCursor(0,5);
    lcd.print("RESET");
    delay(500);
    mem_reset();
  }
}

void handle_button(){
   int event = read_button_event();
   
   if ( event == LONG_PRESS && !ignoreBtn) { 
      on_long_press();      
   } else if ( event == RELEASE && !ignoreBtn ){
     on_click();

   }    
   
   if (event == RELEASE && ignoreBtn){
          ignoreBtn = false;
   }
}

int read_button_event(){
   int btnState = digitalRead(BUTTON_PIN);
   long now = millis();  
   
   // PRESS  - record time and do nothing
   if ( btnState == LOW && btnLast ==HIGH ){
     Serial.println("PRESS");
     btnClickTime = now;
     btnLast = btnState;   
     
   // RELEASE  (but not after LONG_PRESS )    
   } else if (  (btnState == HIGH && btnLast == LOW) && !wasLongPress ){
     Serial.println("RELEASE");
     btnLast = btnState;
     Serial.println(">RELEASE");
     return RELEASE;

   // RELEASE AFTER LONG_PRESS
   } else if (  (btnState == HIGH && btnLast == LOW) && wasLongPress ){
     Serial.println("RELEASE AFTER LONG");
     btnLast = btnState;
     wasLongPress = false;
     Serial.println(">NO_EVENT");
     return NO_EVENT;
    
   // LONG_PRESS 
   } else if ( ((btnState == LOW && btnLast == LOW) && ( now - btnClickTime >  LONG_PRESS_DELAY )) && !wasLongPress){
     Serial.println(">LONG");
     wasLongPress = true;
     return LONG_PRESS;
   }   

   btnLast = btnState;
   return NO_EVENT;
}
     

String calc_interval(){
  int secs = INTERVALS[interval_index]*8;
  if ( secs < 60 ){
     return String(String( secs )+"s  ");
  } else if ( secs < 3600 ){
    return String(String(secs / 60)+"m  ");
  } else {
    return String(String(secs/3600)+"h  ");
  }
}


void handle_display(){
 if (disp_mode==TEMP_MODE){
    lcd.setCursor(0, 0);
    lcd.print(F("Teplota: "));
    lcd.setCursor(9, 0);
    lcd.print(onedigit(tempC));
  } else if (disp_mode==MEM_MODE){
    dispMem();
  } else if ( disp_mode == INTERVAL_MODE){
    lcd.setCursor(0, 0);
    lcd.print(F("Interval: "));
    lcd.setCursor(10, 0);
    lcd.print( calc_interval());
    //lcd.setCursor(0, 1);
    //lcd.print( MEM_SIZE / (3600 / (prescale * 8) * sizeof(StorageRecord)));
  } else {
    lcd.clear();
    lcd.print("OFF");
  }
}


void enable_watchdog(){
  noInterrupts();
  MCUSR = 0;
  WDTCSR |= 0b00011000;
  WDTCSR =  0b01100001;
  wdt_reset();
  interrupts();
} 

void go_sleep(){
    // TODO: change implementation to a real sleep mode
    Serial.println("GO SLEEP");
    mode = SLEEP_MODE;
    disp_mode = OFF_MODE;
    handle_display();    
    //digitalWrite(STATUS_LED,LOW);
           

    
    // Choose our preferred sleep mode:  
    set_sleep_mode(SLEEP_MODE_PWR_SAVE);  
    //  
    interrupts();  
    
    // Set pin 2 as interrupt and attach handler:  
    attachInterrupt(0, pinInterrupt, LOW);  
    
    // Set sleep enable (SE) bit:  
    sleep_enable();  
    //  
    // Put the device to sleep:  
    //digitalWrite(13,LOW);   // turn LED off to indicate sleep
    sleep_mode();  
    
    //  
    // Upon waking up, sketch continues from this point.  
    sleep_disable();  
    
    //digitalWrite(13,HIGH);   // turn LED on to indicate awake
    
}

bool serial_cmd_recognized(){
  if (!Serial.available()) return false;

  char a = Serial.read();
  if ( a=='D' ) return true;
    return false;
 
}

void sendData(){
  
  StorageRecord rec;
  Serial.println("Sending");
  for(int i=0; i<mem_index;){
    rec = read_mem(i);
    Serial.print(i,DEC);
    Serial.print(" ");
    Serial.print(rec.date);
    Serial.print("-");
    Serial.println(rec.temp);
    i+=sizeof(rec);
  }
  
  Serial.print("done");
}

void loop(){

   if (serial_cmd_recognized()) {
     digitalWrite(SERIAL_LED, HIGH);
     sendData();
   } else digitalWrite(SERIAL_LED, LOW);
  
   if ( !in_sleep_mode() ){
     handle_button();
     handle_display();
   }
   
   //if ( time_to_measure() ){
   if ( do_measure && !mem_full){
     do_measure_and_store();     
     do_measure = false;
   }
   
   if (in_sleep_mode() || (!in_sleep_mode() && time_to_sleep())){
     go_sleep();
   } else {  
     delay(100);
   }
}

