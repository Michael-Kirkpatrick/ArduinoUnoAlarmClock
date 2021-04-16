#include <LiquidCrystal.h>
#include <Servo.h>

// Pin 8 is J3 pin 1, or RS
// Pin A2 is J3 pin 2, or Enable
// Pins 4-7 are D4-7, or J4 pins 5-8, and used to interact with the display
LiquidCrystal lcd(8, A2, 4, 5, 6, 7);

Servo myservo;

//-----------------------------
// CONSTANTS
//-----------------------------

// Pin declarations
const int servoPin = 2;
const int ledPin = 13;
const int backlightPin = A1;
const int pingPin = 12;
const int triggerPin = 11;

// Second unit conversions
const int seconds_in_hour = 3600;
const int seconds_in_minute = 60;
const long seconds_in_day = 86400;
const long seconds_in_ten_hours = 36000;
const int seconds_in_ten_minutes = 600;
const long digitConversions[6] = {seconds_in_ten_hours, seconds_in_hour, seconds_in_ten_minutes, seconds_in_minute, 10, 1};
const int secondsInTwoMinutes = 120;

// Ultrasonic distance sensor
const int sensorTicksTop = 3;
const int sensorMaxTriggerDistance = 30;
const int sensorBacklightTicksTop = 50;
const int distanceConst = 58;

// Constants for reading button presses
const int adc_key_val[8] = {630, 710, 745, 780, 820, 870, 910, 950};
const int NUM_KEYS = 8;
const int selectKey = 0;
const int leftAndRightKeys = 1;
const int rightAndDownKeys = 2;
const int leftAndDownKeys = 3;
const int rightKey = 4;
const int leftKey = 5;
const int downKey = 6;
const int upKey = 7;

// Constants for setting the day/night indicator char array
const char day_string[6] = "Day  ";
const char night_string[6] = "Night";


//-----------------------------
// ENUMS
//-----------------------------

enum alarmMode {
  disabled,
  enabled,
  active
};

enum timeSetMode {
  standard,
  alarm,
  lower,
  upper
};


//-----------------------------
// VARIABLES
//-----------------------------

// Main clock control
int interrupts_per_second = 62;
int interrupt_counter = 0;
bool clock_display_control_taken = false; // Used to stop the interrupt from updating clock display when user takes control to set any time, the time is still counting in background though so once user releases control, clock will update to correct time.
bool clock_time_control_taken = false; // Used to stop the interrupt from updating the global time variable, gives user control of the time when they are setting it.

// Time tracking
long current_time_s = 0;
long alarm_time_s = 0;

// Servo
int servo_pos = 0;
bool servo_pos_rising;

// Ultrasonic distance sensor
int sensor_tick_counter = 0;
bool sensor_backlight_triggered = false;
int sensor_backlight_tick_counter = 0;

// LCD Display writing
int cursor_position_col;
int cursor_position_row;

// Variables for reading button presses
int adc_key_in;
int key=-1;
int oldkey=-1;

// Day/night tracking
long day_lower_bound_s = 21600; // default 06:00:00
long day_upper_bound_s = 64800; // default 18:00:00
bool day;
char day_night_indicator[6];

bool daylight_savings;

alarmMode alarm_status;

void setup() {
  cli(); // Disabling interrupts for setup

  pinMode(ledPin, OUTPUT); // Used to toggle the LED, on is standard time, off is daylight savings time.
  daylight_savings = false; 
  set_led(false); // By default we are not in daylight savings, so indicator (onboard LED) is off.
  
  pinMode(backlightPin, OUTPUT); // Will connect to D10 (J3 pin 3), to control the backlight on the display.
  set_backlight(false); // Turn backlight off by default

  myservo.attach(servoPin); // Attach servo to pin 2
  myservo.write(servo_pos); // Set initial position

  // Initialize the pins for the distance sensor
  pinMode(pingPin, INPUT); 
  pinMode(triggerPin, OUTPUT);

  // Initialize the display
  lcd.begin(16,2);
  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print(F("00:00:00 - Night")); // Time by default is unset and begins at 0, that is night
  lcd.setCursor(0,1);
  lcd.print(F("Alarm: 00:00:00|")); // Alarm by default is off and unset ('|' is off, '-' is on)
  alarm_status = disabled;
  day = false;
  set_day_night_indicator(false);

  // Initialize timer2 to be suited to handling the main clock's timing.
  TCCR2A = 0b00000010; // Enable WGM21 for CTC mode
  OCR2A = 251; // 16MHz / 1024 prescaler / (251 OCR2A + 1) = 62.004 Hz interupts. The closest we can get to a whole number with a low frequency on 8bit timer. Now we just track 62 interrupts per second incremented.
  TCNT2 = 0;
  TCCR2B = 0b00000111; // Prescaler of 1024
  TIMSK2 = 0b00000010; // Enable timer compare interrupt on OCR2A

  // Initialize Serial output for test purposes
  Serial.begin(9600);
  while(!Serial); // Wait for serial to be ready
  Serial.println(F("Project Phase 3 Testing Output Ready"));
  
  sei(); // Re-enable interrupts
}

// Looks like a lot going on, tested that in worst case it takes about 950 microseconds, so always less than a millisecond. Usually just 4 micros for incrementing the counter. Runs every 16ms so not eating up too much total time.
ISR(TIMER2_COMPA_vect) {
  if (!clock_time_control_taken) {
    interrupt_counter += 1;
    if (interrupt_counter >= interrupts_per_second) {
      interrupt_counter = 0;
      current_time_s += 1;
      if (current_time_s >= seconds_in_day) {
        current_time_s = 0;
      }
      if (alarm_status == enabled && current_time_s == alarm_time_s) {
        alarm_status = active;
        set_backlight(true);
        servo_pos = 0;
        servo_pos_rising = true;
      }
      if (!clock_display_control_taken) {
        update_clock_display(false);
      }
    }
  }
}

void loop() {
  adc_key_in = analogRead(0);    // read the value from the sensor, button presses are sent through A0 and read through the arduinos A0 as well
  key = get_key(adc_key_in);  // convert into key press
  if (key != oldkey) {  // if keypress is detected
    delay(50);  // wait for debounce time
    adc_key_in = analogRead(0);    // read the value from the sensor 
    key = get_key(adc_key_in);    // convert into key press
    if (key != oldkey) {   
      oldkey = key;
      if (key >=0) {
          switch(key) {
            case selectKey:
              toggle_daylight_savings();  
              break;
            case leftAndRightKeys:
              clock_time_control_taken = true; // User now has control of the time
              clock_display_control_taken = true; // User now has control of display
              set_time_mode(false); // Set the current time
              clock_time_control_taken = false; // User releases control over the time, interrupt can start incrementing it
              clock_display_control_taken = false; // Interrupt can now update clock display
              break;
            case leftAndDownKeys:
              clock_display_control_taken = true;
              set_time_mode(true); // Set the alarm time
              clock_display_control_taken = false;
              break;
            case rightAndDownKeys: 
              clock_display_control_taken = true;
              set_day_bounds(true);
              set_day_bounds(false);
              lcd.noCursor();
              lcd.noBlink();
              update_clock_display(true); // Update alarm display to show the alarm again.
              clock_display_control_taken = false;
              break;
            case leftKey:
              if (alarm_status == active) {
                turn_off_alarm();
              } else {
                toggle_alarm();
              }
              break;
            case downKey:
              if (day) {
                toggle_backlight();
              }
              break;
            case upKey:
              toggle_speed_mode();
              break;
            default:
              break;
          }
      }
    }
  }
  delay(100);
  check_distance_sensor();

  if (alarm_status == active) {
    ring_servo();
  }
}

void check_distance_sensor() {
  sensor_tick_counter += 1;
  if (sensor_tick_counter >= sensorTicksTop) {
    sensor_tick_counter = 0;
    PORTB |= B00001000; // Set trigger pin high
    delayMicroseconds(10);
    PORTB &= B11110111; // Set trigger pin low
    long sensor_duration = pulseIn(pingPin, HIGH);
    
    int sensor_distance = sensor_duration / distanceConst;
    if (sensor_distance < sensorMaxTriggerDistance) {
      if (!day) {
        set_backlight(true);
        sensor_backlight_triggered = true;
        sensor_backlight_tick_counter = 0;
      }
      
      if (alarm_status == active) {
        alarm_status = enabled;
        alarm_time_s += secondsInTwoMinutes;
        if (alarm_time_s > seconds_in_day) { // Cover case where snoozing pushes clock into next day.
          alarm_time_s = alarm_time_s - seconds_in_day;
        }
        update_clock_display(true); // Update alarm display
        set_backlight(false);
      }
      // Hitting around here seems to be the issue right now. May be a memory leak or some other memory issue. If we just don't call update_alarm_display, there is no issue though, so it has to be related to that.
    }
  }

  if (sensor_backlight_triggered) {
    sensor_backlight_tick_counter += 1;
    if (sensor_backlight_tick_counter >= sensorBacklightTicksTop) {
      sensor_backlight_tick_counter = 0;
      sensor_backlight_triggered = false;
      set_backlight(false);
    }
  }
}

void ring_servo() {
  if (servo_pos_rising) {
    if (servo_pos < 180) {
      servo_pos += 30;
      myservo.write(servo_pos);
    } else {
      servo_pos_rising = false;
    }
  } else {
    if (servo_pos > 0) {
      servo_pos -= 30;
      myservo.write(servo_pos);
    } else {
      servo_pos_rising = true;
    }
  }
}

void toggle_alarm() {
  alarm_status = (alarm_status == disabled) ? enabled : disabled;
  lcd.setCursor(15,1);
  if (alarm_status == enabled) {
    lcd.print(F("-"));
  } else if (alarm_status == disabled) {
    lcd.print(F("|"));
  }
}

void turn_off_alarm() {
  alarm_status = disabled;
  lcd.setCursor(15,1);
  lcd.print(F("|"));
  set_backlight(false);
}

void toggle_daylight_savings() {
  if (daylight_savings) {
    set_led(false);
    long new_time = current_time_s - seconds_in_hour;
    if (new_time < 0) {
      new_time = (seconds_in_day - (seconds_in_hour - current_time_s));
    } 
    current_time_s = new_time;
    daylight_savings = false;
  } else {
    set_led(true);
    long new_time = current_time_s + seconds_in_hour;
    if (new_time > seconds_in_day) {
      new_time = current_time_s + seconds_in_hour - seconds_in_day;
    }
    current_time_s = new_time;
    daylight_savings = true;
  }
}

void update_clock_display(bool alarm_mode) {
  long mode_time = alarm_mode ? alarm_time_s : current_time_s;
  
  long hours = mode_time / seconds_in_hour; // long required here to enable the arithmetic to complete when intermediate steps exceed 2 byte capacity
  int minutes = (mode_time - (hours * seconds_in_hour)) / seconds_in_minute;
  int seconds = mode_time - (hours * seconds_in_hour) - (minutes * seconds_in_minute);

  char formatted_time[16];
  
  if (alarm_mode) {
    char alarm_status_indicator = (alarm_status == disabled) ? '|' : '-';
    sprintf(formatted_time, "Alarm: %02d:%02d:%02d%c", int(hours), int(minutes), int(seconds), alarm_status_indicator);
    lcd.setCursor(0,1);
  } else {
    if (current_time_s >= day_lower_bound_s && current_time_s < day_upper_bound_s && !day) {
      set_day_night_indicator(true);
      day = true;
      set_backlight(false);
    } else if ((current_time_s < day_lower_bound_s || current_time_s >= day_upper_bound_s) && day) {
      set_day_night_indicator(false);
      day = false;
      set_backlight(false); // Turn it off in case it was toggled on during day.
    }
    sprintf(formatted_time, "%02d:%02d:%02d - %-5s", int(hours), int(minutes), int(seconds), day_night_indicator);
    lcd.setCursor(0,0);
  }
  
  lcd.print(formatted_time);
}

void set_time_mode(bool alarm_mode) {
  long mode_time = alarm_mode ? alarm_time_s : current_time_s;
  
  // Calculate all of the display digits current values as time is stopped to alter the time.
  long value_to_subtract = 0;
  int tens_hours_value = mode_time / seconds_in_ten_hours;
  value_to_subtract += tens_hours_value * seconds_in_ten_hours;
  int ones_hours_value = (mode_time - value_to_subtract) / seconds_in_hour;
  value_to_subtract += ones_hours_value * seconds_in_hour;
  int tens_minutes_value = (mode_time - value_to_subtract) / seconds_in_ten_minutes;
  value_to_subtract += tens_minutes_value * seconds_in_ten_minutes;
  int ones_minutes_value = (mode_time - value_to_subtract) / seconds_in_minute;
  value_to_subtract += ones_minutes_value * seconds_in_minute;
  int tens_seconds_value = (mode_time - value_to_subtract) / 10;
  value_to_subtract += tens_seconds_value * 10;
  int seconds_value = mode_time - value_to_subtract;
  int digit_values[6] = {tens_hours_value, ones_hours_value, tens_minutes_value, ones_minutes_value, tens_seconds_value, seconds_value};
  
  // Setup the cursor to indicate what digit the user is on
  cursor_position_col = alarm_mode ? 7 : 0;
  cursor_position_row = alarm_mode ? 1 : 0;
  lcd.setCursor(cursor_position_col, cursor_position_row);
  lcd.cursor();
  lcd.blink();

  for (;;) {
    adc_key_in = analogRead(0);   
    key = get_key(adc_key_in);  
    if (key != oldkey) {  
      delay(50);  
      adc_key_in = analogRead(0);    
      key = get_key(adc_key_in);    
      if (key != oldkey) {   
        oldkey = key;
        if (key >=0) {
          switch (key) {
            case selectKey:
              lcd.noCursor();
              lcd.noBlink();
              return;
              break;
            case rightKey:
              increment_cursor_col(alarm_mode);
              break;
            case leftKey:
              decrement_cursor_col(alarm_mode);
              break;
            case downKey:
              decrement_current_cursor_value(digit_values, alarm_mode ? alarm : standard); 
              break;
             case upKey:
              increment_current_cursor_value(digit_values, alarm_mode ? alarm : standard);
              break;
            default:
              break;
          }
        }
      }
    }
  }
}

void set_day_bounds(bool setting_lower) {
  long mode_time = setting_lower ? day_lower_bound_s : day_upper_bound_s;
  
  long hours = mode_time / seconds_in_hour; // long required here to enable the arithmetic to complete when intermediate steps exceed 2 byte capacity
  int minutes = (mode_time - (hours * seconds_in_hour)) / seconds_in_minute;
  int seconds = mode_time - (hours * seconds_in_hour) - (minutes * seconds_in_minute);

  long value_to_subtract = 0;
  int tens_hours_value = mode_time / seconds_in_ten_hours;
  value_to_subtract += tens_hours_value * seconds_in_ten_hours;
  int ones_hours_value = (mode_time - value_to_subtract) / seconds_in_hour;
  value_to_subtract += ones_hours_value * seconds_in_hour;
  int tens_minutes_value = (mode_time - value_to_subtract) / seconds_in_ten_minutes;
  value_to_subtract += tens_minutes_value * seconds_in_ten_minutes;
  int ones_minutes_value = (mode_time - value_to_subtract) / seconds_in_minute;
  value_to_subtract += ones_minutes_value * seconds_in_minute;
  int tens_seconds_value = (mode_time - value_to_subtract) / 10;
  value_to_subtract += tens_seconds_value * 10;
  int seconds_value = mode_time - value_to_subtract;
  int digit_values[6] = {tens_hours_value, ones_hours_value, tens_minutes_value, ones_minutes_value, tens_seconds_value, seconds_value};
  
  lcd.clear();
  lcd.setCursor(0,0);
  if (setting_lower) {
    lcd.print(F("Day Lower Bound"));
  } else {
    lcd.print(F("Day Upper Bound"));
  }
  
  lcd.setCursor(0,1);

  char formatted_time[16];
  sprintf(formatted_time, "%02d:%02d:%02d", int(hours), int(minutes), int(seconds));
  lcd.print(formatted_time);

  cursor_position_row = 1;
  cursor_position_col = 0;
  lcd.setCursor(0,1);
  lcd.cursor();
  lcd.blink();

  for (;;) {
    adc_key_in = analogRead(0);   
    key = get_key(adc_key_in);  
    if (key != oldkey) {  
      delay(50);  
      adc_key_in = analogRead(0);    
      key = get_key(adc_key_in);    
      if (key != oldkey) {   
        oldkey = key;
        if (key >=0) {
          switch (key) {
            case selectKey:      
              return;
              break;
            case rightKey:
              increment_cursor_col(false);
              break;
            case leftKey:
              decrement_cursor_col(false);
              break;
            case downKey:
              decrement_current_cursor_value(digit_values, setting_lower ? lower : upper); 
              break;
             case upKey:
              increment_current_cursor_value(digit_values, setting_lower ? lower : upper);
              break;
            default:
              break;
          }
        }
      }
    }
  }
}

void increment_current_cursor_value(int digit_values[], timeSetMode time_set_mode) {
  int digit_values_index = get_digit_values_index(time_set_mode == alarm);
  int digit_max_value = get_digit_max_value(digit_values_index, digit_values);
  if (digit_values[digit_values_index] < digit_max_value) {
    if (time_set_mode == alarm) {
      alarm_time_s += digitConversions[digit_values_index];
    } else if (time_set_mode == standard) {
      current_time_s += digitConversions[digit_values_index];
    } else if (time_set_mode == lower) {
      day_lower_bound_s += digitConversions[digit_values_index];
    } else if (time_set_mode == upper) {
      day_upper_bound_s += digitConversions[digit_values_index];
    }
    digit_values[digit_values_index] += 1;
    lcd.print(digit_values[digit_values_index]);

    // Cover the case in which the tens of hours is taken from 1->2 and ones of hours is over 3 which would be an illegal value, revert hours ones to 0
    if (digit_values_index == 0 && digit_values[digit_values_index] == 2 && digit_values[1] > 3) { 
      if (time_set_mode == alarm) {
        alarm_time_s -= (seconds_in_hour * digit_values[1]);
      } else if (time_set_mode == standard) {
        current_time_s -= (seconds_in_hour * digit_values[1]);
      } else if (time_set_mode == lower) {
        day_lower_bound_s -= (seconds_in_hour * digit_values[1]);
      } else if (time_set_mode == upper) {
        day_upper_bound_s -= (seconds_in_hour * digit_values[1]);
      }
      digit_values[1] = 0;
      lcd.print(digit_values[1]);
    }
    
    lcd.setCursor(cursor_position_col, cursor_position_row); // Ensure the cursor does not move after the print
  }
}

void decrement_current_cursor_value(int digit_values[], timeSetMode time_set_mode) {
  int digit_values_index = get_digit_values_index(time_set_mode == alarm);
  if (digit_values[digit_values_index] > 0) {
    if (time_set_mode == alarm) {
      alarm_time_s -= digitConversions[digit_values_index];
    } else if (time_set_mode == standard) {
      current_time_s -= digitConversions[digit_values_index];
    } else if (time_set_mode == lower) {
      day_lower_bound_s -= digitConversions[digit_values_index];
    } else if (time_set_mode == upper) {
      day_upper_bound_s -= digitConversions[digit_values_index];
    }
    digit_values[digit_values_index] -= 1;
    lcd.print(digit_values[digit_values_index]);
    lcd.setCursor(cursor_position_col, cursor_position_row); // Ensure the cursor does not move after the print
  }
}

// Returns the maximum value the digit given by the digit_values_index can have.
int get_digit_max_value(int digit_values_index, int digit_values[]) {
  switch (digit_values_index) {
    case 0:
      return 2;
      break;
    case 1:
      if (digit_values[0] >= 2) { // Don't allow increment of ones hours past 3 unless tens hours is below 2
        return 3; 
      } else {
        return 9;
      }
      break;
    case 2:
    case 4:
      return 5;
      break;
    case 3:
    case 5:
      return 9;
      break;
    default:
      return 0;
      break;
  }
}

// Converts the cursor position into the correct index for accessing the digit values array.
int get_digit_values_index(bool alarm_mode) {
  int digit_values_index = alarm_mode ? cursor_position_col - 7 : cursor_position_col;
  if (digit_values_index > 2) {
    digit_values_index -= 1;
  }
  if (digit_values_index > 4) {
    digit_values_index -= 1;
  }
  return digit_values_index;
}

void increment_cursor_col(bool alarm_mode) {
  int cursor_pos = alarm_mode ? cursor_position_col - 7 : cursor_position_col;
  if (cursor_pos < 7) {
    cursor_position_col += 1;
    cursor_pos += 1;
    if (cursor_pos == 2 || cursor_pos == 5) { // Correct for colons
      cursor_position_col += 1;
    }
    lcd.setCursor(cursor_position_col, cursor_position_row);
  }
}

void decrement_cursor_col(bool alarm_mode) {
  int cursor_pos = alarm_mode ? cursor_position_col - 7 : cursor_position_col;
  if (cursor_pos > 0) {
    cursor_position_col -= 1;
    cursor_pos -= 1;
    if (cursor_pos == 2 || cursor_pos == 5) { // Correct for colons
      cursor_position_col -= 1;
    }
    lcd.setCursor(cursor_position_col, cursor_position_row);
  }
}

// Convert ADC value to key number
int get_key(unsigned int input)
{
    int k;
    for (k = 0; k < NUM_KEYS; k++)
    {
      if (input < adc_key_val[k])
      {
        return k;
      }
    }   
    if (k >= NUM_KEYS)k = -1;  // No valid key pressed
    return k;
}

void set_backlight(bool high) {
  high ? PORTC |= B00000010 : PORTC &= B11111101;
}

void toggle_backlight() {
  PORTC ^= B00000010; 
}  

void toggle_speed_mode() {
  interrupts_per_second = (interrupts_per_second == 62) ? 5 : 62;
}

void set_led(bool high) {
  high ? PORTB |= B00100000 : PORTB &= B11011111;
}

void toggle_led() {
  PORTB ^= B00100000;
}

void set_day_night_indicator(bool set_day) {
  for (int i = 0; i < 6; i++) {
    if (set_day) {
      day_night_indicator[i] = day_string[i];
    } else {
      day_night_indicator[i] = night_string[i];
    }
  }
}

// Find out how much free memory we have, useful for debugging as dynamic allocation rises in some areas.
unsigned int getFreeMemory()
{
 uint8_t* temp = (uint8_t*)malloc(16);    // assumes there are no free holes so this is allocated at the end
 unsigned int rslt = (uint8_t*)SP - temp;
 free(temp);
 return rslt;
}
