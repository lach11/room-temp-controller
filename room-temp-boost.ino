/*
 * @script        Room Temperature Control Unit
 * @date          14 July 2014
 * @version       1.0
 * @copyright
 * @liability
 * @circuit
 */

// include the library code:
#include <ShiftLCD.h>
#include "DHT.h"
#include <Wire.h>
#include "RTClib.h"
#include <EEPROM.h> //memory library

#define DHTPIN 10     // what pin we're connected to
#define DHTTYPE DHT22
#define MENU_ROLLOVER    10

// initialize the library with the numbers of the interface pins
ShiftLCD lcd(7, 5, 6);
DHT dht(DHTPIN, DHTTYPE);

/* real time clock variables */
RTC_DS1307 RTC; //clock object
DateTime now;
DateTime b_now;
DateTime t_now;
DateTime saved_time; //saving time at start of each new temp cycle
DateTime time_diff; //get diff b/w saved and current time - for calculating minute markers
DateTime now1;
int the_num_diff; //gets diff as int for testing against operator
int previous_min;
int previous_day;

/* button pins */
int modeBtn = 4;
int upBtn = 3;
int downBtn = 2;

/* remote pins */
int heater_on = 8; //remote on button via opto
int heater_off = 9; //remote off button via opto
int remote_delay = 600;

/* temperature variables */
int delay_minutes = 10; //set number of minutes for cycle
int cycle_count = 0; 
int minute_counter = 0; //for counting minutes passed
int minute_mode = 0; //ensures only one cycle at the minute mark is used - fast cycles means multiple readings qualify for mod 60 test
int std_temp = 23; //base temperature 
int light_on_time = 10000;

/* room measurement variables */
int current_temp = 0; //get current temp
int current_humidity = 0;
int accum_temp = 0; //accumulate temp samples for each defined cycle
int average_temp; //aveerage temp for each cycle
int temp_threshold; 
int compare_temp;

/* set boost variables */
int boost_hour = 3;
int boost_minute = 30;
int boost_temp = 25;
int boost_rst_hour = 7;
int boost_rst_minute = 15;
boolean boost_switch = false; //test for determining if in boost heating period - default is false or off

/* menu variables */
int menu_mode = 1;
int saved_menu_mode;
int button_return_val;

/* general variables */
int eeprom_pos = 0;
int on_off_status = 2; //var for status of the whole unit i.e. monitoring or not
char save_status_state; //save state for current unit status
int switch_status = 0;
int sleep_mode = 1;

/* backlight variables */
int backlightMode = 0;
DateTime lightTime;
int lightSeconds = 30; 

/**************************************************************************************************************************************/

void setup() {
  // set up the LCD's number of rows and columns: 
  lcd.begin(16, 2);
  // Print a message to the LCD.
  pinMode(modeBtn, INPUT);
  pinMode(upBtn, INPUT);
  pinMode(downBtn, INPUT);
  pinMode(heater_on, OUTPUT);
  pinMode(heater_off, OUTPUT);
  dht.begin();
  Wire.begin();
  RTC.begin();
  //RTC.adjust(DateTime(__DATE__, __TIME__));
  current_temp = take_temperature();
  current_humidity = get_humidity();
  check_if_boost_time();
  compare_temp = (boost_switch) ? boost_temp : std_temp;
  
  //if(boost_switch == true) { //if in boost time period
    //compare_temp = boost_temp;
  //} else { //std time period
    //compare_temp = std_temp;
  //} //end if
}


/**************************************************************************************************************************************/

void loop() {
  
  //////////////////////////////////////////////////////////////////////////
  
  /* get current time */
  now = RTC.now();
  
  //////////////////////////////////////////////////////////////////////////
  
  /* if user presses menu button increment menu mode counter to update display */
  if(digitalRead(modeBtn) == HIGH) { //if menu change button pressed
    menu_mode++; //increment menu tracker
    backlight_init();
    if(menu_mode == MENU_ROLLOVER) { //if top menu int exceeded then reset to 1
       menu_mode = 1; 
    } //end if
  } //end if
  //check
  //////////////////////////////////////////////////////////////////////////
  
  /* read any user input and process */
  button_return_val = register_button_press(); //check for btn press
  //check
  //////////////////////////////////////////////////////////////////////////
  
  /* if the up or down button have been press then action input */
  if(button_return_val != 0)  user_input_actions(button_return_val); //process user input
  
  //////////////////////////////////////////////////////////////////////////
  
  /* if menu has changed or btn has been pressed, update display */
  if((menu_mode != saved_menu_mode) || (button_return_val != 0)) render_display();

  //////////////////////////////////////////////////////////////////////////
  
  //arbitrary reset number to stop eventual int overflow
  if(on_off_status == 10) on_off_status = 2;

  //////////////////////////////////////////////////////////////////////////
  
  //start cycle and work out time elapsed
  if(cycle_count == 0) saved_time = now; //time variable for start of each monitoring cycle
  
  cycle_count++;
  
  /* calculate time elapsed */
  time_diff = now.unixtime() - saved_time.unixtime(); //how much time has elapsed in cycle
  the_num_diff = time_diff.second(); //get number of seconds passed
  
  //////////////////////////////////////////////////////////////////////////
  
  //at the end of each minute irrespective of operation status
  if(the_num_diff%60 == 0) { //a minute has elapsed
    minute_mode++;
  } else {
    minute_mode = 0;
  } //end if
  
  //just want to capture one temp sample per minute -> stops multiple samples taken across multiple clock cycles
  if(minute_mode == 1) { 
     minute_counter++; //keep track of elapsed minutes
     current_temp = take_temperature(); //get temp sample every minute
     current_humidity = get_humidity();
     if(menu_mode == 1) {
       update_temp(); //update main menu if active with current temperature and humidity 
     } //end if
     accum_temp += current_temp; //accumulate readings for cycle
   } //end if
   
   //////////////////////////////////////////////////////////////////////////
   
  //at the end of each cycle 
  if(minute_counter == delay_minutes) {
    cycle_count = 0;
    minute_counter = 0;
    if(on_off_status%2 != 0) {
      average_temp = accum_temp/delay_minutes; //get average temp for cycle
      
      check_if_boost_time(); //check if in boost temperature time periodr
      
      if(boost_switch == true) { //if in boost time period
        compare_temp = boost_temp;
      } else { //std time period
        compare_temp = std_temp;
      } //end if
      
      if(average_temp < compare_temp) {
        turn_heater_on();
      } else {
        turn_heater_off();
      } //end if
      
      accum_temp = 0;
      update_temp();
    } //end if
  } //end if           

  saved_menu_mode = menu_mode;
  backlight_decision();
  
  delay(150);
  
   //////////////////////////////////////////////////////////////////////////

} //end loop


/**************************************************************************************************************************************/

/******************************************************/
/* menu functions                                     */
/******************************************************/
 
//render lcd display
 
void render_display() {
 
  lcd.clear();
  switch(menu_mode) {
   
    case 1: //main menu -> current time | current temp | current status
      rendaer_main_menu(); //external function as it is called by two separate functions
      break;
     
    case 2: //set temp
      lcd.setCursor(0, 0);
      lcd.print("Set Temperature");
      lcd.setCursor(0, 1);
      lcd.print(std_temp);
      lcd.print(" C");
      break;
     
    case 3: //manual status
      lcd.setCursor(0, 0);
      lcd.print("Operation Status");
      lcd.setCursor(0, 1);
      if(on_off_status%2 == 0) {
        lcd.print("OFF");
      } else {
        lcd.print("ON ");
      } //end if
      break;
     
    case 4: //remote function
      lcd.setCursor(0, 0);
      lcd.print("Remote Operation");
      lcd.setCursor(0, 1);
      lcd.print("UP=ON   DOWN=OFF");
      break;
     
    case 5: //set delay
      lcd.setCursor(0, 0);
      lcd.print("Set Period");
      lcd.setCursor(0, 1);
      lcd.print(delay_minutes);
      lcd.print(" min");
      break;
     
    case 6: //set backlight time
      lcd.setCursor(0, 0);
      lcd.print("Set Light Time");
      lcd.setCursor(0, 1);  
      lcd.print(lightSeconds);
      lcd.print(" sec");
      break;
      
    case 7: //boost temperature
      lcd.setCursor(0, 0);
      lcd.print("Set Boost Temp");
      lcd.setCursor(0, 1);  
      lcd.print(boost_temp);
      lcd.print(" C");
      break;
      
    case 8: //boost time
      lcd.setCursor(0, 0);
      lcd.print("Set Boost Time");
      lcd.setCursor(0, 1);  
      lcd.print(boost_hour);
      lcd.print(":");
      if(boost_minute<10) {
        lcd.print("0");
        lcd.print(boost_minute);
      } else {
        lcd.print(boost_minute);
      } //end if
      
      break;
      
    case 9: //boost reset time
      lcd.setCursor(0, 0);
      lcd.print("Boost Rst Time");
      lcd.setCursor(0, 1);  
      lcd.print(boost_rst_hour);
      lcd.print(":");
      if(boost_rst_minute<10) {
        lcd.print("0");
        lcd.print(boost_rst_minute);
      } else {
        lcd.print(boost_rst_minute);
      } //end if
      
      break;
     
    default: break;
   
  } //end switch
 
} //end function


void rendaer_main_menu() {
  //output time
      lcd.setCursor(0, 0);
      lcd.print("T:");
      if(boost_switch == true) { //if in boost time period
        compare_temp = boost_temp;
      } else { //std time period
        compare_temp = std_temp;
      } //end if
      lcd.print(compare_temp);
      lcd.print("C");
      //lcd.print(the_num_diff);
      
      t_now = RTC.now();
      lcd.print(" " );
      lcd.print(t_now.hour());
      lcd.print(":");
      if(t_now.minute()<10) {
        lcd.print("0");
        lcd.print(t_now.minute());
      } else {
        lcd.print(t_now.minute());
      }
      
      
      //output temp
      lcd.setCursor(0, 1);
      lcd.print("CT:");
      lcd.print(current_temp);
      //lcd.print(accum_temp);
      lcd.print("C");
      
      //output humidity
      lcd.setCursor(10, 1);
      lcd.print("CH:");
      lcd.print(current_humidity);
      lcd.print("%");
      
      //output status
      lcd.setCursor(13, 0);
      if(switch_status == 0) {
        lcd.print("OFF");
      } else {
        lcd.print("ON ");
      } //end if
} //end function

/******************************************************/
/* user panel functions                               */
/******************************************************/
 
//determine if and which button has been pressed by the user
int register_button_press() {
  if(digitalRead(upBtn) == HIGH) {
    return 1;
  } else if(digitalRead(downBtn) == HIGH) {
    return 2;
  } else {
    return 0;
  } //end if
} //end function


//action any user button press(es)
void user_input_actions(int button_num) {
 
  switch(menu_mode) {
   
    case 1: //main menu -> current time | current temp | current status
      //nothing to action on the main menu regarding up and down button
      break;
     
    case 2: //set temp
      if(button_num == 1) { //up btn
        std_temp += 1;
      } //end if
       
      if(button_num == 2) { //down btn
        std_temp -= 1;
        if(std_temp == -1) {
          std_temp = 0;
        } //end if
      } //end if
      break;
     
    case 3: //manual status
      if((button_num == 1) || (button_num == 2)) { //up or down btn
        on_off_status += 1;
      } //end if
      break;
     
    case 4: //remote function
      if(button_num == 1) { //up btn
        turn_heater_on();
      } //end if
       
      if(button_num == 2) { //down btn
        turn_heater_off();
      } //end if
      break;
     
    case 5: //set delay
      if(button_num == 1) { //up btn
        delay_minutes++;
      } //end if
       
      if(button_num == 2) { //down btn
        delay_minutes--;
        if(delay_minutes == 0) {
          delay_minutes = 1; //set 1 minute minimum
        } //end if
      } //end if
      break;
     
    case 6: //set backlight
      if(button_num == 1) {
        lightSeconds++;
      } //end if
      
      if(button_num == 2) {
        lightSeconds--;
        if(lightSeconds == -1) {
          lightSeconds = 0;
        } //end if
      } //end if
      break; 
      
    case 7: //set boost temp
      if(button_num == 1) { //up btn
        boost_temp++;
      } //end if
       
      if(button_num == 2) { //down btn
        boost_temp--;
        if(boost_temp <= 0) { //reset if set below zero
          boost_temp = 1; //set 1 minute minimum
        } //end if
      } //end if
      break;
      
    case 8: //set boost time
      if(button_num == 1) { //up btn
        boost_minute+=5;
        if(boost_minute >= 60) {
          boost_hour++;
          boost_minute=0; 
        } //end if
        if(boost_hour==24) {
          boost_hour=0;
        } //end if
      } //end if
       
      if(button_num == 2) { //down btn
        boost_minute-=5;
        if(boost_minute < 0) {
          boost_minute=55;
          boost_hour--;
        } //end if
        if(boost_hour<0) {
          boost_hour=23;
        } //end if
      } //end if
      break;
      
      case 9: //set boost reset time
      if(button_num == 1) { //up btn
        boost_rst_minute+=5;
        if(boost_rst_minute >= 60) {
          boost_rst_hour++;
          boost_rst_minute=0; 
        } //end if
        if(boost_rst_hour==24) {
          boost_rst_hour=0;
        } //end if
      } //end if
       
      if(button_num == 2) { //down btn
        boost_rst_minute-=5;
        if(boost_rst_minute < 0) {
          boost_rst_minute=45;
          boost_rst_hour--;
        } //end if
        if(boost_rst_hour<0) {
          boost_rst_hour=23;
        } //end if
      } //end if
      break;
     
    default: break;
   
  } //end switch
 
} //end function

/****************************************************/
/* room climate functions */
/****************************************************/

//function to take temp reading and return celsius value
float take_temperature() {
  float t = dht.readTemperature();
  return t;
} //end function

float get_humidity() {
  float h = dht.readHumidity();
  return h;
} //end function


/****************************************************/
/* remote control functions */
/****************************************************/

//function to activate on button on the remote control
void turn_heater_on() {
  digitalWrite(heater_on, HIGH);
  delay(remote_delay);
  digitalWrite(heater_on, LOW);
  switch_status = 1;
} //end function

//function to activate the off button on the remote control
void turn_heater_off() { 
  digitalWrite(heater_off, HIGH);
  delay(remote_delay);
  digitalWrite(heater_off, LOW);
  switch_status = 0;
} //end function


void update_temp() {
  rendaer_main_menu();
} //end function

/****************************************************/
/* boost functions */
/****************************************************/

void check_if_boost_time(void) {
  
  b_now = RTC.now(); //get current time
  
  if((b_now.hour()==boost_hour) && (b_now.minute()==boost_minute)) boost_switch = true;
  if((b_now.hour()==boost_rst_hour) && (b_now.minute()==boost_rst_minute)) boost_switch=false;
  
  rendaer_main_menu();
  
} //end function


/****************************************************/
/* backlight functions */
/****************************************************/


void backlight_init() {
  backlightMode++;
  lightTime = now.unixtime() + lightSeconds;
} //end function


void backlight_decision() {
  int diff;
  diff = lightTime.unixtime() - now.unixtime();
  if(backlightMode == 0) {
    lcd.backlightOff();
  } else {
    if(diff == 0) {
      lcd.backlightOff();
      backlightMode = 0;
    } else {
      lcd.backlightOn();
    } //end if
  } //end if
} //end function

