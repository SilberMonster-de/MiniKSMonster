/*******************************************

  Name.......:  MiniKSMonster
  Description:  Arduino sketch for the kolloidal silver generator Mini KS Monster is a fork of https://github.com/AgH2O/ks_shield
  Project....:  https://www.silbermonster.de
  License....:  This work is licensed under the Creative Commons Attribution-NonCommercial-ShareAlike 3.0 Unported License.
                To view a copy of this license, visit http://creativecommons.org/licenses/by-nc-sa/3.0/ or send a letter to
                Creative Commons, 171 Second Street, Suite 300, San Francisco, California, 94105, USA.

********************************************/

#include "SSD1306Ascii.h"                 // ascii library for Oled
#include "SSD1306AsciiAvrI2c.h"

#include <EEPROM.h>                       // EEPROM library

#define I2C_ADDRESS 0x3C                  // 0X3C+SA0 - 0x3C or 0x3D

SSD1306AsciiAvrI2c oled;                  // create short alias

#define SWL 6                             // switch def. left, middle, right
#define SWM 7
#define SWR 8

#define START 9                           // control line def.
#define AUDIO 5
#define POLW  10

#define TIMER_START TCCR1B |= (1 << CS12) | (0 << CS11) | (0 << CS10); // set bits
#define TIMER_STOP  TCCR1B &= ~((1 << CS12) | (1 << CS11) | (1 << CS10)); // deletes bits

float liter = 0.25;                       // set some start values
float ppm = 50;                           // wished ppm

boolean polaritaet = true;
boolean wassertest = false;               // water quality test default is disable
boolean display = true;                   // display enable
float akt_ppm = 0;
char text[32];

unsigned int taste, i, eine_minute, Position, adc_wert, adc_wert_a1;
unsigned int polwechselzeit1 = 15;
unsigned int polwechselzeit2 = 10;
unsigned int polwechselzeit;
float polwechselschwelle = 2.5;
float spannung;
float strom_mess;
float strom_wassertest;                   // current measurement for water quality test
float schwellenwert_wassertest = 1.5;     // <1.5 water quality test passed, >1.5 water quality test failed
float shunt = 47;                         // 47 Ohm 0.1%

float Q_gesamt = 0;                       // new variables for ppm method
float Q_messung = 0;
float faktor = 0.001118083;               // = M / z * F , 107,8782/1*96485
float mah = 0;
float schrittweite_adc = 2.490;
float zielmasse;
float masse;
unsigned int intervall = 1;               // measure every 1 sec.
long unsigned int sek = 0;
char stringbuf[16];
int stunde, minute, sekunde;
int b;

unsigned long previousMillis = 0;
int interval = 5000;                      // set the interval for the operation in milliseconds
unsigned long previousCounter = 0;
int intervaldisplay = 60000;              // set the interval for display protection in seconds

int filterfactor;
float filter;

void setup() {
  
                                          // Interrupt Routine Setup Timer1 1 Hz
                                          // TIMER 1 for interrupt frequency 1 Hz:
  cli();                                  // stop interrupts
  TCCR1A = 0;                             // set entire TCCR1A register to 0
  TCCR1B = 0;                             // same for TCCR1B
  TCNT1  = 0;                             // initialize counter value to 0
                                          // set compare match register for 1 Hz increments
  OCR1A = 62499;                          // = 16000000 / (256 * 1) - 1 (must be <65536)
  TCCR1B |= (1 << WGM12);                 // turn on CTC mode
                                          // Set CS12, CS11 and CS10 bits for 256 prescaler
  TCCR1B |= (1 << CS12) | (0 << CS11) | (0 << CS10);
  TIMSK1 |= (1 << OCIE1A);                // enable timer compare interrupt
  // TIMSK1 |= (0 << OCIE1A);             // disable timer compare interrupt 
  sei();                                  // allow interrupts
  TIMER_STOP

  pinMode(SWL, INPUT);                    // setup Switches + PullDown
  digitalWrite(SWL, LOW);
  pinMode(SWM, INPUT);
  digitalWrite(SWM, LOW);
  pinMode(SWR, INPUT);
  digitalWrite(SWR, LOW);

  pinMode(START, OUTPUT);                 // setup control lines
  digitalWrite(START, HIGH);              // start value high == stop
  pinMode(AUDIO, OUTPUT);
  pinMode(POLW, OUTPUT);
  digitalWrite(POLW, LOW);                // start value low

  oled.begin(&Adafruit128x64, I2C_ADDRESS);// setup Oled
  oled.setContrast(100);                  // contrast adjustment
  oled.clear();
  oled.set2X();
  oled.setFont(X11fixed7x14B);
  oled.println(" Mini KS");               // start message
  oled.println(" Monster");
  oled.set1X();
  oled.setFont(fixed_bold10x15);
  delay(2000);                            // 2 sec. are enough

  analogReference(EXTERNAL);              // analog Reference external 2.5V

  int eeAddress = 1;                      // read EEPROM
  int eepromtest = 0;
  eepromtest = EEPROM.read(0);
  if (eepromtest == 1) {
    liter = EEPROM.get(eeAddress, liter);
    eeAddress += sizeof(float); //Move address to the next byte after float 'liter'.
    ppm = EEPROM.get(eeAddress, ppm);
    eeAddress += sizeof(float); //Move address to the next byte after float 'ppm'.
    polwechselzeit1 = EEPROM.get(eeAddress, polwechselzeit1);
    eeAddress += sizeof(unsigned int); //Move address to the next byte after unsigned int 'polwechselzeit1'.
    polwechselzeit2 = EEPROM.get(eeAddress, polwechselzeit2);
    eeAddress += sizeof(unsigned int); //Move address to the next byte after unsigned int 'polwechselzeit2'.
    polwechselschwelle = EEPROM.get(eeAddress, polwechselschwelle);
  }
}

uint8_t lese_tasten(void) {               // function reads switches and returns 1-7
  uint8_t zwi_speich = 0;
  if (digitalRead(SWL) == HIGH)
    zwi_speich = 1;
  if (digitalRead(SWM) == HIGH)
    zwi_speich += 2;
  if (digitalRead(SWR) == HIGH)
    zwi_speich += 4;
  return zwi_speich;

}

void print_wassermenge(float liter) {     // 1. display output - amount of water
  oled.clear();
  oled.setCursor(3, 0);
  oled.print("Wassermenge");
  oled.setCursor(8, 4);
  oled.print(liter);
  oled.print(" Liter");

}

void print_ppm(float ppm) {               // 2. display output - wished ppm
  oled.clear();
  oled.setCursor(9, 0);
  oled.print("KS Staerke");
  oled.setCursor(29, 4);
  oled.print((int)ppm);
  oled.print(" ppm ");
}

void print_polw1(unsigned int polwechselzeit1) {
  oled.clear();                           // 3. display output - change polarioty time
  oled.setCursor(4, 0);
  oled.print("Umpolzeit 1");
  oled.setCursor(26, 4);
  oled.print(polwechselzeit1);
  oled.print(" Sek.");
}

void print_schwelle(float polwechselschwelle) {
  oled.clear();                           // 4. display output - change polarioty time
  oled.setCursor(4, 0);
  oled.print(" Schwelle");
  oled.setCursor(26, 4);
  oled.print(polwechselschwelle);
  oled.print(" mA");
}

void print_polw2(unsigned int polwechselzeit2) {
  oled.clear();                           // 5. display output - change polarioty time
  oled.setCursor(4, 0);
  oled.print("Umpolzeit 2");
  oled.setCursor(26, 4);
  oled.print(polwechselzeit2);
  oled.print(" Sek.");
}
void print_wassertest(void) {             // 6. display output - water quality test and Start question
  oled.clear();

  if (wassertest) {
    oled.setCursor(0, 0);
    oled.print("Wassertest: ");
    digitalWrite(START, LOW);
    delay(200);
    adc_wert_a1 = analogRead(1);
    strom_wassertest = (float) (adc_wert_a1 * schrittweite_adc);
    delay(200);
    oled.setCursor(0, 2);
    if (strom_wassertest < schwellenwert_wassertest) {
      oled.print("OK");
    } else {
      oled.print("Mangelhaft");
    }
    delay(200);
    digitalWrite(START, HIGH);
    oled.setCursor(0, 4);
    oled.set2X();
    oled.print("Start?");
    oled.set1X();
  } else {
    oled.setCursor(0, 2);
    oled.set2X();
    oled.print("Start?");
    oled.set1X();
  }
}

void erste_zeile_clean() {                // Clean first line
  oled.home();
  oled.print("                 ");
}

void zweiSekunden(void) {                 // 2 sec. delay but reads left and right switch
  for (i = 0; i < 100; i ++)
    if ((lese_tasten() == 4) || (lese_tasten() == 1) )
      break;
    else
      delay(20);
}

void software_Reset() {                   // Restarts program from beginning but
  //  asm volatile ("  jmp 0");           // does not reset the peripherals and registers
}
                                          // function calculates target mass
float errechne_zielmasse(float ppm, float wasser) {
  return (wasser * 1000 / 1000000) * ppm; // mass = (water * 1000 (gramm)/ 1000000) * ppm
}
                                          // function calculates ppm from mass
float masse2ppm(float masse, float liter) {
  return (masse / (liter * 1000 / 1000000));
}

void sek2hhmmss(long int zeit) {          // format clock/counter
  if (zeit > 59) {
    minute++;
    sek = 0;
  }
  if (minute > 59) {
    stunde++;
    minute = 0;
  }
  if (stunde > 9) {                       // because 1 digit 9 max.
    stunde = 0;
  }
}

ISR(TIMER1_COMPA_vect) {                  // Interrupt Routine every 1 sec

  adc_wert = analogRead(0);               // measure U on voltage divider
  adc_wert_a1 = analogRead(1);            // measure I on shunt 47 ohm

  filterfactor = 10;                      // filter factor
  filter = ((filter * filterfactor) + adc_wert_a1) / (filterfactor + 1);

  spannung = (float) adc_wert * 0.00244;  // 0.00244140625 (2,5V /1024) accurate value important
                                          // Ushunt * 1000 = mV
  strom_mess = (float) (filter * schrittweite_adc);
  strom_mess = (strom_mess / shunt) ;     // I = Ushunt / Rshunt

  Q_messung =  strom_mess  * intervall;   // Coulomb = I * t
  Q_gesamt = Q_gesamt + Q_messung;        // Q added
  masse = (Q_gesamt / 1000) * faktor;     // Qgesamt from mC to C
  mah = (Q_gesamt / 1000) * 0.2795476873690739;
  sek2hhmmss(sek);
  akt_ppm = masse2ppm(masse, liter);

  ppm = masse2ppm(masse, liter);
  
  if (display) {
    oled.setCursor(18, 0);                 // Oled 1. row
    sprintf(stringbuf, "%01d:%02d:%02d", stunde, minute, sek);
    oled.print(stringbuf);
    oled.setCursor(18, 2);                 // Oled 2. row
    oled.print(ppm);
    oled.print(" ppm    ");
    oled.setCursor(18, 4);                 // Oled 3. row
    if (lese_tasten() == 2) {
      oled.print((int)(spannung * 67.76)); // factor measured voltage divider
      oled.print(" Volt      ");

    } else {
      oled.print(mah);
      oled.print(" mAh       ");
    }
    oled.setCursor(18, 6);                 // Oled 4. row
    oled.print(strom_mess);
    oled.print(" mA ");

  } else {
    oled.clear();
  }
  unsigned long currentCounter = i;        // automatic display shutdown
  if ((unsigned long)(currentCounter - previousCounter) >= intervaldisplay) {
    display = false;
    if (lese_tasten() >= 1) {
      previousCounter = currentCounter;
    }
  } else {
    display = true;
  }
  if (strom_mess < polwechselschwelle)
    {
      polwechselzeit = polwechselzeit1;
    }
    else
    {
      polwechselzeit = polwechselzeit2;
    }
  if (!(i % (polwechselzeit)))             // Polarity change every 15 sec./ basis time
  { polaritaet = !polaritaet;
    digitalWrite(START, HIGH);
    delay(500);
    digitalWrite(POLW, polaritaet);
    delay(500);
    digitalWrite(START, LOW);
    if (display) {
      if (polaritaet) {
        oled.setCursor(105, 0); oled.print("-");
      } else {
        oled.setCursor(105, 0); oled.print("+");
      }
    }
  }
  sek++;
  i++;                                     // intervall x i = total time
}

void biep(void) {
  tone(AUDIO, 2600);                       // beep
  delay(50);
  noTone(AUDIO);
}

void biep2(void) {
  tone(AUDIO, 2600);                       // beep2
  delay(1000);
  noTone(AUDIO);
  delay(400);
}

// *** M A I N L O O P ***
void loop() {
  do {                                     // back to loop
    print_wassermenge(liter);
    do {                                   // choose amout of water
      if (lese_tasten() == 1) {
        if (liter < 5.00)
          if (liter <= 0.79) {
            liter += 0.01 ;
          } else {
            liter += 0.05 ;
          }
        oled.setCursor(8, 4);
        oled.print(liter);
        biep();
      }
      if (lese_tasten() == 2) {
        if (liter > 0.05)
          if (liter < 0.80) {
            liter -= 0.01 ;
          } else {
            liter -= 0.05 ;
          }
        oled.setCursor(8, 4);
        oled.print(liter);
        biep();
      }
      unsigned long currentMillis = millis();
      if ((unsigned long)(currentMillis - previousMillis) >= interval) {
        delay(50);
        if (lese_tasten() == 0) {
          previousMillis = currentMillis;
        }
      } else {
        delay(300);
      }
    } while (lese_tasten() != 4);
    biep();

    print_ppm(ppm);
    do {
      if (lese_tasten() == 1) {
        if (ppm < 1000)
          if (ppm < 20) {
            ppm += 1 ;
          } else {
            ppm += 5 ;
          }
        oled.setCursor(29, 4);
        oled.print((int) ppm);
        oled.println(" ppm  ");
        biep();
      }
      if (lese_tasten() == 2) {
        if (ppm > 1)
          if (ppm < 20) {
            ppm -= 1 ;
          } else {
            ppm -= 5 ;
          }
        oled.setCursor(29, 4);
        oled.print((int) ppm);
        oled.println(" ppm  ");
        biep();
      }
      unsigned long currentMillis = millis();
      if ((unsigned long)(currentMillis - previousMillis) >= interval) {
        delay(50);
        if (lese_tasten() == 0) {
          previousMillis = currentMillis;
        }
      } else {
        delay(300);
      }
    } while (lese_tasten() != 4);
    biep();

print_polw1(polwechselzeit1);
    do {                                   // choose polarity change time
      if (lese_tasten() == 1) {
        if (polwechselzeit1 < 600)         // top max. 600 sec = 10 Min
          polwechselzeit1 += 1 ;
        oled.setCursor(26, 4);
        oled.print(polwechselzeit1);
        oled.print(" Sek. ");
        biep();
      }
      if (lese_tasten() == 2) {
        if (polwechselzeit1)               // bottom min.
          polwechselzeit1 -= 1 ;
        oled.setCursor(26, 4);
        oled.print(polwechselzeit1);
        oled.print(" Sek. ");
        biep();
      }
      unsigned long currentMillis = millis();
      if ((unsigned long)(currentMillis - previousMillis) >= interval) {
        delay(50);
        if (lese_tasten() == 0) {
          previousMillis = currentMillis;
        }
      } else {
        delay(300);
      }
    } while (lese_tasten() != 4);
    biep();
    
    print_schwelle(polwechselschwelle);
    do {                                   // choose polarity change time
      if (lese_tasten() == 1) {
        if (polwechselschwelle < 10)       // top max. 10mA
          polwechselschwelle += 0.1 ;
        oled.setCursor(26, 4);
        oled.print(polwechselschwelle);
        oled.print(" mA ");
        biep();
      }
      if (lese_tasten() == 2) {
        if (polwechselschwelle)            // bottom min.
          polwechselschwelle -= 0.1 ;
        oled.setCursor(26, 4);
        oled.print(polwechselschwelle);
        oled.print(" mA ");
        biep();
      }
      unsigned long currentMillis = millis();
      if ((unsigned long)(currentMillis - previousMillis) >= interval) {
        delay(50);
        if (lese_tasten() == 0) {
          previousMillis = currentMillis;
        }
      } else {
        delay(300);
      }
    } while (lese_tasten() != 4);
    biep();
    
    print_polw2(polwechselzeit2);
    do {                                  // choose polarity change time
      if (lese_tasten() == 1) {
        if (polwechselzeit2 < 600)        // top max. 600 sec = 10 Min
          polwechselzeit2 += 1 ;
        oled.setCursor(26, 4);
        oled.print(polwechselzeit2);
        oled.print(" Sek. ");
        biep();
      }
      if (lese_tasten() == 2) {
        if (polwechselzeit2)              // bottom min.
          polwechselzeit2 -= 1 ;
        oled.setCursor(26, 4);
        oled.print(polwechselzeit2);
        oled.print(" Sek. ");
        biep();
      }
      unsigned long currentMillis = millis();
      if ((unsigned long)(currentMillis - previousMillis) >= interval) {
        delay(50);
        if (lese_tasten() == 0) {
          previousMillis = currentMillis;
        }
      } else {
        delay(300);
      }
    } while (lese_tasten() != 4);
    biep();
    
    int eeAddress = 1;                    // write EEPROM
    EEPROM.write(0, 1);
    EEPROM.put(eeAddress, liter);
    eeAddress += sizeof(float); //Move address to the next byte after float 'liter'.
    EEPROM.put(eeAddress, ppm);
    eeAddress += sizeof(float); //Move address to the next byte after float 'ppm'.
    EEPROM.put(eeAddress, polwechselzeit1);
    eeAddress += sizeof(unsigned int); //Move address to the next byte after float 'polwechselzeit1'.
    EEPROM.put(eeAddress, polwechselzeit2);
    eeAddress += sizeof(unsigned int); //Move address to the next byte after float 'polwechselzeit2'.
    EEPROM.put(eeAddress, polwechselschwelle);
    
    oled.clear();
    delay(1000);
    do {                                  // last user check
      oled.setCursor(0, 0);
      oled.print((int)ppm);
      oled.print(" ppm");
      oled.setCursor(0, 2);
      oled.print(liter);
      oled.print(" Liter");
      oled.setCursor(0, 4);
      oled.print((int)ppm/5);
      oled.print(" mg/L");
      oled.setCursor(0, 6);
      oled.print("Wasser kalt");
      taste = lese_tasten();
      zweiSekunden();                     // delay against flickering display but read keys
    } while (taste != 4 && taste != 2 && taste != 1); //action if key hit, leave while loop
    biep();
  } while ( taste != 4 && taste != 2);    // only **not** back, if 4 == go further

  delay(1000);
  print_wassertest();                     // water quality test and Start question
  do {
    taste = lese_tasten();                // go futher, on every key
  } while (taste != 4);
  biep();

  switch (taste)
  {
    case 1:
    case 2:
    case 4:
      oled.clear();
      zielmasse = errechne_zielmasse(ppm, liter);
      digitalWrite(START, LOW);           // Start DC/DC Converter
      TIMER_START
      do {                                // ACTIONLOOP UNTIL KS FINISHED
        delay(100);                       // 0,1 sec.
        if (lese_tasten() == 7) {         // shall i do  a soft reset?
          biep();
          software_Reset();
        }
      } while (masse <= zielmasse);       // ACTIONLOOP END

      digitalWrite(START, HIGH);          // Stop DC/DC Converter
      TIMER_STOP
      digitalWrite(POLW, LOW);
      oled.setCursor(18, 0);              // Oled 1. row
      sprintf(stringbuf, "%01d:%02d:%02d", stunde, minute, sek);
      oled.print(stringbuf);
      oled.setCursor(18, 2);              // Oled 2. row
      oled.print(ppm);
      oled.print(" ppm    ");
      oled.setCursor(18, 4);              // Oled 3. row
      oled.print(mah);
      oled.print(" mAh       ");
      oled.setCursor(18, 6);              // Oled 4. row
      oled.print("KS fertig");
      for (b = 1; b <= 10; b++ ) {        // Finished and beep 10 times
        biep2();
      }
      break;
  }

  do {                                    // Wait loop until any key is pressed
    taste = lese_tasten();
  } while (taste != 4 && taste != 2 && taste != 1);
  Q_gesamt = 0; i = 0; sek = 0; stunde = 0; minute = 0; zielmasse = 0; masse = 0; display = true; // Reset counter
  delay(1000);
}
