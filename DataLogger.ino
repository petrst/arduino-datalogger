#include <avr/interrupt.h>
#include <avr/sleep.h>
#include <avr/wdt.h>
#include <LiquidCrystal.h>
#include <EEPROM.h>
#include <I2C_eeprom.h>
#include <Wire.h>
#include "DataLogger.h"
#include "EEPROMAnything.h"
#include "RTClib.h"


// HARDWARE ///////////////////////////////////////////
const int BUTTON_PIN = 3;
const int LCD_SWITCH = 7;
const int STATUS_LED = 6;
const int SERIAL_LED = 13;
const int MEM_FULL_LED = 6;
const int TEMP_PIN = 0;

// initialize the library with the numbers of the interface pins
//LiquidCrystal lcd(12, 11, 5, 4, 3, 6);
LiquidCrystal lcd(4, 5, 9, 10, 11, 12);


// MODE SWITCHING /////////////////////////////////////
const int WAKE_MODE =  1;
const int SLEEP_MODE = 3;
volatile int mode = WAKE_MODE;
const long NO_ACTIVITY_DELAY = 30 * 60 * 1000;
//const long NO_ACTIVITY_DELAY = 10 * 60 * 1000;


// DISPLAY MODE //////////////////////////////////////
const int TEMP_MODE  =  1;
const int MEM_MODE  =  2;
const int INTERVAL_MODE  =  3;
const int OFF_MODE  =  4;
const int TRANSFER_MODE = 5;

volatile int disp_mode = TEMP_MODE;


// BUTTON HANDLING ////////////////////////////////////
const int NO_EVENT = 0;
const int RELEASE = 1;
const int LONG_PRESS = 2;
const long LONG_PRESS_DELAY = 3000;
const unsigned long DEBOUCE_DELAY = 200;

int btnLast = HIGH;
bool wasLongPress = false;
long btnClickTime = 0;
bool ignoreBtn = false;

// TIME ////////////////////////////////////////
RTC_DS1307 rtc;

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
long measureLastTime = 0;

volatile bool do_measure = false;

// STORAGE ///////////////////////////////////////////
int index = 0;
float total = 0.0;
int mem_index;
bool mem_full = false;
const int MEM_SIZE = 8192;
I2C_eeprom ee(0x50);



#define NO_CMD 0
#define DOWNLOAD_CMD 1
#define TIMESYNC_CMD 2

//////////////////////////////////////////////////////
// INTERRUPT HANDLERS ////////////////////////////////

// Wake up by button
void pinInterrupt()
{
  detachInterrupt(digitalPinToInterrupt(BUTTON_PIN));
  //attachInterrupt(0, pinInterrupt, HIGH);
  mode = WAKE_MODE;
  disp_mode = TEMP_MODE;

  // Ignore btn release after wake up
  ignoreBtn = true;

  digitalWrite(STATUS_LED, LOW);
  lcd_on();
}


// Wake up by watchdog
ISR(WDT_vect) {
  if (m_counter == (INTERVALS[interval_index] - 1)) {
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
  pinMode(LCD_SWITCH, OUTPUT);
  analogReference(INTERNAL);
  //attachInterrupt(0, handleButton, FALLING);
  //attachInterrupt(0, INT_Button, CHANGE);
  // set up the LCD's number of columns and rows:


  //lcd.begin(16, 2);
  lcd.print("Startuji ...");
  lcd_on();

  // Print a message to the LCD.
  for (int i = 0; i < nrReadings; i++) {
    temp[i] = 0.0;
  }


  digitalWrite(STATUS_LED, LOW);

  noInterrupts();
  MCUSR = 0;
  WDTCSR |= 0b00011000;
  WDTCSR =  0b01100001;
  wdt_reset();
  interrupts();


  if (! rtc.begin()) {
    //Serial.println("Couldn't find RTC");
    digitalWrite(STATUS_LED, HIGH);
    while (1);
  }


  if (! rtc.isrunning()) {
    Serial.println("RTC is NOT running!");
    digitalWrite(STATUS_LED, HIGH);
    // following line sets the RTC to the date & time this sketch was compiled
    // rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
    // This line sets the RTC with an explicit date & time, for example to set
    // January 21, 2014 at 3am you would call:
    rtc.adjust(DateTime(2015, 12, 28, 10, 20, 0));
    Serial.println("Time set!");
  }
  //rtc.adjust(DateTime(2015, 12, 28, 10, 20, 0));
   

}


void lcd_on() {
  //digitalWrite(LCD_SWITCH, HIGH);
  delay(100);
  lcd.begin(16, 2);
  lcd.display();
}


void lcd_off() {
  //digitalWrite(LCD_SWITCH, LOW);
  lcd.noDisplay();
}


DateTime get_time() {
  //return millis();
  return rtc.now();
}


float onedigit(float in) {
  return round(in * 10) / 10.0;
}

/*
float measure() {
  reading = analogRead(TEMP_PIN);
  total = total - temp[index];
  temp[index] = reading;
  total = total + temp[index];
  index++;
  if (index >= nrReadings) {
    index = 0;
  }
  reading = total / nrReadings;
  return reading / 9.31;
}
*/


#define NUM_READS 100
float measure(){
   // read multiple values and sort them to take the mode
   int sortedValues[NUM_READS];
   for(int i=0;i<NUM_READS;i++){
     int value = analogRead(TEMP_PIN);
     int j;
     if(value<sortedValues[0] || i==0){
        j=0; //insert at first position
     }
     else{
       for(j=1;j<i;j++){
          if(sortedValues[j-1]<=value && sortedValues[j]>=value){
            // j is insert position
            break;
          }
       }
     }
     for(int k=i;k>j;k--){
       // move all values higher than current reading up one position
       sortedValues[k]=sortedValues[k-1];
     }
     sortedValues[j]=value; //insert current reading
   }
   //return scaled mode of 10 values
   float returnval = 0;
   for(int i=NUM_READS/2-5;i<(NUM_READS/2+5);i++){
     returnval +=sortedValues[i];
   }
   returnval = returnval/10;
   return returnval/9.31;
}




void store_mem(float temp) {

  //EEPROM.write(mem_index,temp);
  //mem_index += 4;
  StorageRecord rec = {get_time(), temp};
  //EEPROM.put(mem_index, rec);
  //EEPROM_writeAnything(mem_index, rec);
  ee.writeBlock(mem_index, (unsigned char*)&rec, sizeof(rec));

  mem_index += sizeof(rec);

  if ( mem_index + sizeof(rec) > MEM_SIZE ) {
    mem_full = true;
    digitalWrite(MEM_FULL_LED, HIGH);
  }

}


StorageRecord read_mem(int index) {
  StorageRecord rec;
  //EEPROM.get( index, rec);
  //EEPROM_readAnything(index, rec);
  ee.readBlock(index, (unsigned char*)&rec, sizeof(rec));
  Serial.print("INDEX=");
  Serial.println(index,DEC);
  Serial.print("DAYREAD=");
  Serial.println(rec.date.day());
  return rec;
}


void mem_reset() {
  mem_index = 0;
  mem_full = false;
  digitalWrite(MEM_FULL_LED, LOW);
}


void dispMem() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Pamet: ");
  lcd.print(mem_index);
  lcd.print("b");
}


void do_measure_and_store() {
  digitalWrite(STATUS_LED, HIGH);

  for (int i = 0; i < nrReadings; i++) {
    tempC = measure();
  }
  store_mem(tempC);
  measureLastTime = millis();
  delay(200);
  digitalWrite(STATUS_LED, LOW);
}


bool in_sleep_mode() {
  return mode == SLEEP_MODE;
}


bool time_to_sleep() {
  long now = millis();
  if ( now - btnClickTime > NO_ACTIVITY_DELAY ) {
    return true;
  } else {
    return false;
  }
}

bool time_to_measure() {
  // TODO: change implementation to RTC reading
  long now = millis();
  if (now - measureLastTime > MEASURE_PERIOD ) {
    return true;
  } else {
    return false;
  }
}



void on_click() {

  if (disp_mode == TEMP_MODE) disp_mode = MEM_MODE;
  else if (disp_mode == MEM_MODE) disp_mode = INTERVAL_MODE;
  else if (disp_mode == INTERVAL_MODE) disp_mode = TEMP_MODE;
  lcd.clear();
}

void on_long_press() {
  if (disp_mode == INTERVAL_MODE) {
    interval_index = (interval_index + 1) % (sizeof(INTERVALS) / sizeof(int));
  } else if ( disp_mode == MEM_MODE ) {
    lcd.clear();
    lcd.setCursor(0, 5);
    lcd.print("RESET");
    delay(500);
    mem_reset();
  }
}

void handle_button() {
  int event = read_button_event();

  if ( event == LONG_PRESS && !ignoreBtn) {
    on_long_press();
  } else if ( event == RELEASE && !ignoreBtn ) {
    on_click();

  }

  if (event == RELEASE && ignoreBtn) {
    ignoreBtn = false;
  }
}

int read_button_event() {
  int btnState = digitalRead(BUTTON_PIN);
  long now = millis();

  // PRESS  - record time and do nothing
  if ( btnState == LOW && btnLast == HIGH ) {
    //Serial.println("PRESS");
    btnClickTime = now;
    btnLast = btnState;

    // RELEASE  (but not after LONG_PRESS )
  } else if (  (btnState == HIGH && btnLast == LOW) && !wasLongPress ) {
    //Serial.println("RELEASE");
    btnLast = btnState;
    //Serial.println(">RELEASE");
    return RELEASE;

    // RELEASE AFTER LONG_PRESS
  } else if (  (btnState == HIGH && btnLast == LOW) && wasLongPress ) {
    //Serial.println("RELEASE AFTER LONG");
    btnLast = btnState;
    wasLongPress = false;
    //Serial.println(">NO_EVENT");
    return NO_EVENT;

    // LONG_PRESS
  } else if ( ((btnState == LOW && btnLast == LOW) && ( now - btnClickTime >  LONG_PRESS_DELAY )) && !wasLongPress) {
    //Serial.println(">LONG");
    wasLongPress = true;
    return LONG_PRESS;
  }

  btnLast = btnState;
  return NO_EVENT;
}


String calc_interval() {

  int secs = INTERVALS[interval_index] * 8;
  if ( secs < 60 ) {
    return String(String( secs ) + "s  ");
  } else if ( secs < 3600 ) {
    return String(String(secs / 60) + "m  ");
  } else {
    return String(String(secs / 3600) + "h  ");
  }
}


/*
char* calc_interval(){
  char rv[4];
  int secs = INTERVALS[interval_index]*8;
  if ( secs < 60 ){
     //return String(String( secs )+"s  ");
     sprintf(rv,"%ds", secs);
  } else if ( secs < 3600 ){
     sprintf(rv,"%dm", int(secs/60));
  } else {
     sprintf(rv,"%dh", int(secs/3600));
  }
  return rv;
}
*/

void handle_display() {
  
  if (disp_mode == TEMP_MODE) {
    lcd.setCursor(0, 0);
    lcd.print(F("Teplota: "));
    lcd.setCursor(9, 0);
    lcd.print(onedigit(tempC), 1);
    lcd.setCursor(13, 0);
    lcd.print("\xDF" "C");
    
    
    DateTime t = rtc.now();
    lcd.setCursor(0, 1);
    if (t.hour()<10) lcd.print(0);
    lcd.print(t.hour());
    //lcd.setCursor(3, 1);
    lcd.print(":");
    if (t.minute()<10) lcd.print(0);
    //lcd.setCursor(4, 1);
    lcd.print(t.minute());    
    //lcd.setCursor(6, 1);
    lcd.print(":");
    //lcd.setCursor(7, 1);
    if (t.second()<10) lcd.print(0);
    lcd.print(t.second());    
    

    int mem_occupation = 100 *( 1.0 * mem_index / MEM_SIZE );
    if ( mem_index + sizeof(StorageRecord) > MEM_SIZE ) mem_occupation = 100;
    
    lcd.setCursor(12, 1);
    lcd.print(mem_occupation);
    lcd.print("%");

  } else if (disp_mode == MEM_MODE) {
    dispMem();
  } else if ( disp_mode == INTERVAL_MODE) {
    lcd.setCursor(0, 0);
    lcd.print(F("Interval: "));
    lcd.setCursor(10, 0);
    lcd.print( calc_interval());
    //lcd.setCursor(0, 1);
    //lcd.print( MEM_SIZE / (3600 / (prescale * 8) * sizeof(StorageRecord)));
  } else if (disp_mode == TRANSFER_MODE) {
    lcd.clear();
    lcd.setCursor(3, 0);
    lcd.print( "Posilam...");
  } else {
    lcd.clear();
    lcd.print("OFF");
  }
  
}


void enable_watchdog() {
  noInterrupts();
  MCUSR = 0;
  WDTCSR |= 0b00011000;
  WDTCSR =  0b01100001;
  wdt_reset();
  interrupts();
}

void go_sleep() {
  // TODO: change implementation to a real sleep mode
  //Serial.println("GO SLEEP");
  mode = SLEEP_MODE;
  disp_mode = OFF_MODE;
  handle_display();
  //digitalWrite(STATUS_LED,LOW);
  lcd_off();


  // Choose our preferred sleep mode:
  set_sleep_mode(SLEEP_MODE_PWR_SAVE);
  //
  interrupts();

  // Set pin 2 as interrupt and attach handler:
  attachInterrupt(digitalPinToInterrupt(BUTTON_PIN), pinInterrupt, LOW);

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

int serial_cmd_recognized() {
  if (!Serial.available()) return NO_CMD;
  //Serial.begin(9600);

  char a = Serial.read();
  if ( a == 'D' ) return DOWNLOAD_CMD;
  else if ( a == 'T' ) return TIMESYNC_CMD;
  return NO_CMD;

}

void sendData() {

  StorageRecord rec;
  
  //noInterrupts();
  //Serial.println("Sending");
  for (int i = 0; i < mem_index;) {
    rec = read_mem(i);
    Serial.print(i, DEC);
    Serial.print(", \"");

    Serial.print(rec.date.year(), DEC);
    Serial.print("/");
    Serial.print(rec.date.month(), DEC);
    Serial.print("/");
    Serial.print(rec.date.day(), DEC);
    Serial.print(" ");
    Serial.print(rec.date.hour(), DEC);
    Serial.print(":");
    Serial.print(rec.date.minute(), DEC);
    Serial.print(":");
    Serial.print(rec.date.second(), DEC);



    Serial.print("\", ");
    Serial.println(rec.temp);
    i += sizeof(rec);
  }

  Serial.print("\n");
  //interrupts();
}

void loop() {

  //Serial.println(mode);
  int cmd = serial_cmd_recognized();
  if (cmd==DOWNLOAD_CMD) {
    lcd.clear();
    lcd.setCursor(3, 0);
    lcd.print( "Posilam...");
    sendData();
  } else if ( cmd == TIMESYNC_CMD ){
    lcd.clear();
    lcd.setCursor(3, 0);
    lcd.print( "Synchronizuji ...");
    
    String instr = "";
    int hours = 0;
    int minutes = 0;
    int seconds = 0;
    
    instr += (char)Serial.read();
    instr += (char)Serial.read();
    hours = instr.toInt();
    
    instr = "";
    instr += (char)Serial.read();
    instr += (char)Serial.read();
    minutes = instr.toInt();
    
    instr = "";
    instr += (char)Serial.read();
    instr += (char)Serial.read();
    seconds = instr.toInt();
    
    rtc.adjust(DateTime(2016, 1, 1, hours, minutes, seconds));
    Serial.println("SYNC DONE");
    Serial.println(hours);
    
  } else digitalWrite(SERIAL_LED, LOW);

  if ( !in_sleep_mode() ) {
    tempC = measure();
    handle_button();
    handle_display();
  }

  //if ( time_to_measure() ){
  if ( do_measure && !mem_full) {
    do_measure_and_store();
    do_measure = false;
  }

  if (in_sleep_mode() || (!in_sleep_mode() && time_to_sleep())) {
    go_sleep();
  } else {
    delay(100);
  }
}

