/*
      MIDI2CV_Poly
      Copyright (C) 2020 Craig Barnes

      A big thankyou to Elkayem for his midi to cv code
      A big thankyou to ElectroTechnique for his polyphonic tsynth that I used for the poly notes routine

      This program is free software: you can redistribute it and/or modify
      it under the terms of the GNU General Public License as published by
      the Free Software Foundation, either version 3 of the License, or
      (at your option) any later version.

      This program is distributed in the hope that it will be useful,
      but WITHOUT ANY WARRANTY; without even the implied warranty of
      MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
      GNU General Public License <http://www.gnu.org/licenses/> for more details.
*/

#include <SPI.h>
#include <Wire.h>
#include <Adafruit_SSD1306.h>
#include <EEPROM.h>
#include <Bounce2.h>
#include <MIDI.h>
#include <USBHost_t36.h>

// OLED I2C is used on pins 18 and 19 for Teensy 3.x

// Voices available
#define  NO_OF_VOICES 6
#define trigTimeout 20

//Note DACS
#define DAC_NOTE1 7
#define DAC_NOTE2 8
#define DAC_NOTE3 9

//Autotune MUX

#define MUX_S0 10
#define MUX_S1 24
#define MUX_S2 25
#define MUX_S3 26

#define MUX_ENABLE 3
#define MUX_OUT 4

//Trig outputs
#define TRIG_NOTE1  32
#define TRIG_NOTE2  31
#define TRIG_NOTE3  30
#define TRIG_NOTE4  29
#define TRIG_NOTE5  28
#define TRIG_NOTE6  27

//Gate outputs
#define GATE_NOTE1  33
#define GATE_NOTE2  34
#define GATE_NOTE3  35
#define GATE_NOTE4  36
#define GATE_NOTE5  37
#define GATE_NOTE6  38

//Encoder or buttons
#define ENC_A   14
#define ENC_B   15
#define ENC_BTN 16
#define AUTOTUNE 17

#define UNISON_ON 2


// Scale Factor will generate 0.5v/octave
// 4 octave keyboard on a 3.3v powered DAC
#define NOTE_SF 547.00f
#define VEL_SF 256.0

#define OLED_RESET 17
#define SCREEN_WIDTH 128 // OLED display width, in pixels
#define SCREEN_HEIGHT 64 // OLED display height, in pixels
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

int encoderPos, encoderPosPrev;
Bounce encButton = Bounce();
Bounce encoderA = Bounce();
Bounce encoderB = Bounce();

enum Menu {
  SETTINGS,
  KEYBOARD_MODE_SET_CH,
  MIDI_CHANNEL_SET_CH,
  TRANSPOSE_SET_CH,
  OCTAVE_SET_CH,
  SCALE_FACTOR,
  SCALE_FACTOR_SET_CH
} menu;

char gateTrig[] = "TTTTTT";


float sfAdj[6];

uint8_t pitchBendChan;
uint8_t ccChan;
int masterChan;
int masterTran;
int previousMode;
int transpose;
int8_t d2, i;
int noteMsg;
int keyboardMode;
int octave;
int realoctave;

float noteTrig[6];
float monoTrig;
float unisonTrig;

struct VoiceAndNote {
  int note;
  int velocity;
  long timeOn;
};

struct VoiceAndNote voices[NO_OF_VOICES] = {
  { -1, -1, 0},
  { -1, -1, 0},
  { -1, -1, 0},
  { -1, -1, 0},
  { -1, -1, 0},
  { -1, -1, 0}
};

boolean voiceOn[NO_OF_VOICES] = {false, false, false, false, false, false};
int voiceToReturn = -1;//Initialise to 'null'
long earliestTime = millis();//For voice allocation - initialise to now
int  prevNote = 0;//Initialised to middle value
bool notes[88] = {0}, initial_loop = 1;
int8_t noteOrder[40] = {0}, orderIndx = {0};
bool S1, S2;
unsigned long trigTimer = 0;

// MIDI setup

//USB HOST MIDI Class Compliant
USBHost myusb;
USBHub hub1(myusb);
USBHub hub2(myusb);
MIDIDevice midi1(myusb);

MIDI_CREATE_INSTANCE(HardwareSerial, Serial1, MIDI);
const int channel = 1;

// EEPROM Addresses

#define ADDR_GATE_TRIG     6
#define ADDR_PITCH_BEND    12
#define ADDR_CC            13
#define ADDR_SF_ADJUST     14
#define ADDR_MASTER_CHAN   20
#define ADDR_TRANSPOSE     26
#define ADDR_REAL_TRANSPOSE 27
#define ADDR_OCTAVE 28
#define ADDR_REALOCTAVE 29
#define ADDR_KEYBOARD_MODE 30


uint32_t int_ref_on_flexible_mode = 0b00001001000010100000000000000000;   // { 0000 , 1001 , 0000 , 1010000000000000 , 0000 }

uint32_t sample_data = 0b00000000000000000000000000000000;
uint32_t channel_a   = 0b00000010000000000000000000000000;
uint32_t channel_b   = 0b00000010000100000000000000000000;
uint32_t channel_c   = 0b00000010001000000000000000000000;
uint32_t channel_d   = 0b00000010001100000000000000000000;
uint32_t channel_e   = 0b00000010010000000000000000000000;
uint32_t channel_f   = 0b00000010010100000000000000000000;
uint32_t channel_g   = 0b00000010011000000000000000000000;
uint32_t channel_h   = 0b00000010011100000000000000000000;

bool highlightEnabled = false;  // Flag indicating whether highighting should be enabled on menu
#define HIGHLIGHT_TIMEOUT 20000  // Highlight disappears after 20 seconds.  Timer resets whenever encoder turned or button pushed
unsigned long int highlightTimer = 0;

void setup()
{
  pinMode(GATE_NOTE1, OUTPUT);
  pinMode(GATE_NOTE2, OUTPUT);
  pinMode(GATE_NOTE3, OUTPUT);
  pinMode(GATE_NOTE4, OUTPUT);
  pinMode(GATE_NOTE5, OUTPUT);
  pinMode(GATE_NOTE6, OUTPUT);
  pinMode(TRIG_NOTE1, OUTPUT);
  pinMode(TRIG_NOTE2, OUTPUT);
  pinMode(TRIG_NOTE3, OUTPUT);
  pinMode(TRIG_NOTE4, OUTPUT);
  pinMode(TRIG_NOTE5, OUTPUT);
  pinMode(TRIG_NOTE6, OUTPUT);
  pinMode(DAC_NOTE1, OUTPUT);
  pinMode(DAC_NOTE2, OUTPUT);
  pinMode(DAC_NOTE3, OUTPUT);
  pinMode(MUX_S0, OUTPUT);
  pinMode(MUX_S1, OUTPUT);
  pinMode(MUX_S2, OUTPUT);
  pinMode(MUX_S3, OUTPUT);
  pinMode(ENC_A, INPUT_PULLUP);
  pinMode(ENC_B, INPUT_PULLUP);
  pinMode(ENC_BTN, INPUT_PULLUP);
  pinMode(UNISON_ON, INPUT_PULLUP);
  pinMode(AUTOTUNE, INPUT_PULLUP);
  pinMode(MUX_OUT, INPUT);
  pinMode(MUX_ENABLE, OUTPUT);

  digitalWrite(GATE_NOTE1, LOW);
  digitalWrite(GATE_NOTE2, LOW);
  digitalWrite(GATE_NOTE3, LOW);
  digitalWrite(GATE_NOTE4, LOW);
  digitalWrite(GATE_NOTE5, LOW);
  digitalWrite(GATE_NOTE6, LOW);
  digitalWrite(TRIG_NOTE1, LOW);
  digitalWrite(TRIG_NOTE2, LOW);
  digitalWrite(TRIG_NOTE3, LOW);
  digitalWrite(TRIG_NOTE4, LOW);
  digitalWrite(TRIG_NOTE5, LOW);
  digitalWrite(TRIG_NOTE6, LOW);
  digitalWrite(DAC_NOTE1, HIGH);
  digitalWrite(DAC_NOTE2, HIGH);
  digitalWrite(DAC_NOTE3, HIGH);
  digitalWrite(MUX_S0, LOW);
  digitalWrite(MUX_S1, LOW);
  digitalWrite(MUX_S2, LOW);
  digitalWrite(MUX_S3, LOW);
  digitalWrite(MUX_ENABLE, LOW);

  SPI.setDataMode(SPI_MODE1);
  SPI.begin();

  SPI.beginTransaction(SPISettings(1000000, MSBFIRST, SPI_MODE1));
  digitalWrite(DAC_NOTE1, LOW);
  delayMicroseconds(1);
  SPI.transfer(int_ref_on_flexible_mode >> 24);
  SPI.transfer(int_ref_on_flexible_mode >> 16);
  SPI.transfer(int_ref_on_flexible_mode >> 8);
  SPI.transfer(int_ref_on_flexible_mode);
  digitalWrite(DAC_NOTE1, HIGH);
  SPI.endTransaction();
  digitalWrite(DAC_NOTE2, LOW);
  delayMicroseconds(1);
  SPI.transfer(int_ref_on_flexible_mode >> 24);
  SPI.transfer(int_ref_on_flexible_mode >> 16);
  SPI.transfer(int_ref_on_flexible_mode >> 8);
  SPI.transfer(int_ref_on_flexible_mode);
  digitalWrite(DAC_NOTE2, HIGH);
  SPI.endTransaction();
  digitalWrite(DAC_NOTE3, LOW);
  delayMicroseconds(1);
  SPI.transfer(int_ref_on_flexible_mode >> 24);
  SPI.transfer(int_ref_on_flexible_mode >> 16);
  SPI.transfer(int_ref_on_flexible_mode >> 8);
  SPI.transfer(int_ref_on_flexible_mode);
  digitalWrite(DAC_NOTE3, HIGH);
  SPI.endTransaction();

  //USB HOST MIDI Class Compliant
  delay(300); //Wait to turn on USB Host
  myusb.begin();
  midi1.setHandleControlChange(myControlChange);
  midi1.setHandleNoteOff(myNoteOff);
  midi1.setHandleNoteOn(myNoteOn);
  midi1.setHandlePitchChange(myPitchBend);
  Serial.println("USB HOST MIDI Class Compliant Listening");

  //MIDI 5 Pin DIN
  MIDI.begin(masterChan);
  MIDI.setHandleNoteOn(myNoteOn);
  MIDI.setHandleNoteOff(myNoteOff);
  MIDI.setHandlePitchBend(myPitchBend);
  MIDI.setHandleControlChange(myControlChange);
  Serial.println("MIDI In DIN Listening");

  //USB Client MIDI
  usbMIDI.setHandleControlChange(myControlChange);
  usbMIDI.setHandleNoteOff(myNoteOff);
  usbMIDI.setHandleNoteOn(myNoteOn);
  usbMIDI.setHandlePitchChange(myPitchBend);
  Serial.println("USB Client MIDI Listening");

  display.begin(SSD1306_SWITCHCAPVCC, 0x3C); // OLED I2C Address, may need to change for different device,
  // Check with I2C_Scanner

  // Wire.setClock(100000L);  // Uncomment to slow down I2C speed

  // Read Settings from EEPROM
  for (int i = 0; i < 6; i++) {
    gateTrig[i] = (char)EEPROM.read(ADDR_GATE_TRIG + i);
    if (gateTrig[i] != 'G' || gateTrig[i] != 'T') EEPROM.write(ADDR_GATE_TRIG + i, 'T');
    gateTrig[i] = (char)EEPROM.read(ADDR_GATE_TRIG + i);
    EEPROM.get(ADDR_SF_ADJUST + i * sizeof(float), sfAdj[i]);
    if ((sfAdj[i] < 0.9f) || (sfAdj[i] > 1.1f) || isnan(sfAdj[i])) sfAdj[i] = 1.0f;
  }

  keyboardMode = (int)EEPROM.read(ADDR_KEYBOARD_MODE);
  previousMode = (int)EEPROM.read(ADDR_KEYBOARD_MODE);
  masterChan = (int)EEPROM.read(ADDR_MASTER_CHAN);
  masterTran = (int)EEPROM.read(ADDR_TRANSPOSE);
  transpose = (int)EEPROM.read(ADDR_REAL_TRANSPOSE);
  octave = (int)EEPROM.read(ADDR_OCTAVE);
  realoctave = (int)EEPROM.read(ADDR_REALOCTAVE);
  pitchBendChan = masterChan;
  ccChan = masterChan;

  // Set defaults if EEPROM not initialized
  if (keyboardMode > 6) keyboardMode = 0;
  if (masterTran > 25) masterTran = 13;
  if (masterChan > 15) masterChan = 0;
  if (octave > 3) octave = 3;
  if (octave == 0) realoctave = -36;
  if (octave == 1) realoctave = -24;
  if (octave == 2) realoctave = -12;
  if (octave == 3) realoctave = 0;
  if (pitchBendChan > 15) pitchBendChan = masterChan;
  if (ccChan > 15) ccChan = masterChan;


  encButton.attach(ENC_BTN);
  encButton.interval(5);  // interval in ms

  sample_data = ((channel_h & 0xFFF0000F) | ( 13180 & 0xFFFF) << 4);
  outputDAC(DAC_NOTE3, sample_data);
  sample_data = ((channel_g & 0xFFF0000F) | ( 0 & 0xFFFF) << 4);
  outputDAC(DAC_NOTE3, sample_data);

  menu = SETTINGS;
  updateSelection();
}

void myPitchBend(byte channel, int bend) {
  if ((MIDI.getChannel() == pitchBendChan) || (pitchBendChan == 0 )) {
    d2 = MIDI.getData2(); // d2 from 0 to 127, mid point = 64
    sample_data = (channel_h & 0xFFF0000F) | (((int(bend * 1.605) + 13180) & 0xFFFF) << 4);
    outputDAC(DAC_NOTE3, sample_data);
  }
}

void myControlChange(byte channel, byte number, byte value) {
  if ((MIDI.getChannel() == ccChan || ccChan == 0)) {
    if ( number == 1 )
    {
      sample_data = (channel_g & 0xFFF0000F) | (((int(value * 207)) & 0xFFFF) << 4);
      outputDAC(DAC_NOTE3, sample_data);
    }
    else
    {
      MIDI.sendControlChange(number, value, channel);
    }
  }
}

void commandTopNote()
{
  int topNote = 0;
  bool noteActive = false;

  for (int i = 0; i < 88; i++)
  {
    if (notes[i]) {
      topNote = i;
      noteActive = true;
    }
  }

  if (noteActive)
    commandNote(topNote);
  else // All notes are off, turn off gate
    digitalWrite(GATE_NOTE1, LOW);
}

void commandBottomNote()
{
  int bottomNote = 0;
  bool noteActive = false;

  for (int i = 87; i >= 0; i--)
  {
    if (notes[i]) {
      bottomNote = i;
      noteActive = true;
    }
  }

  if (noteActive)
    commandNote(bottomNote);
  else // All notes are off, turn off gate
    digitalWrite(GATE_NOTE1, LOW);
}

void commandLastNote()
{

  int8_t noteIndx;

  for (int i = 0; i < 40; i++) {
    noteIndx = noteOrder[ mod(orderIndx - i, 40) ];
    if (notes[noteIndx]) {
      commandNote(noteIndx);
      return;
    }
  }
  digitalWrite(GATE_NOTE1, LOW); // All notes are off
}

void commandNote(int noteMsg) {
  unsigned int mV = (unsigned int) ((float) (noteMsg + transpose + realoctave) * NOTE_SF * sfAdj[0] + 0.5);
  sample_data = (channel_a & 0xFFF0000F) | (((int(mV)) & 0xFFFF) << 4);
  outputDAC(DAC_NOTE1, sample_data);
  outputDAC(DAC_NOTE2, sample_data);
  digitalWrite(GATE_NOTE1, HIGH);
  digitalWrite(TRIG_NOTE1, HIGH);
  trigTimer = millis();
  while (millis() < trigTimer + trigTimeout) {
    // wait 50 milliseconds
  }
  digitalWrite(TRIG_NOTE1, LOW);

}

void commandTopNoteUni()
{
  int topNote = 0;
  bool noteActive = false;

  for (int i = 0; i < 88; i++)
  {
    if (notes[i]) {
      topNote = i;
      noteActive = true;
    }
  }

  if (noteActive)
    commandNoteUni(topNote);
  else // All notes are off, turn off gate
    digitalWrite(GATE_NOTE1, LOW);
  digitalWrite(GATE_NOTE2, LOW);
  digitalWrite(GATE_NOTE3, LOW);
  digitalWrite(GATE_NOTE4, LOW);
  digitalWrite(GATE_NOTE5, LOW);
  digitalWrite(GATE_NOTE6, LOW);
}

void commandBottomNoteUni()
{
  int bottomNote = 0;
  bool noteActive = false;

  for (int i = 87; i >= 0; i--)
  {
    if (notes[i]) {
      bottomNote = i;
      noteActive = true;
    }
  }

  if (noteActive)
    commandNoteUni(bottomNote);
  else // All notes are off, turn off gate
    digitalWrite(GATE_NOTE1, LOW);
  digitalWrite(GATE_NOTE2, LOW);
  digitalWrite(GATE_NOTE3, LOW);
  digitalWrite(GATE_NOTE4, LOW);
  digitalWrite(GATE_NOTE5, LOW);
  digitalWrite(GATE_NOTE6, LOW);
}

void commandLastNoteUni()
{

  int8_t noteIndx;

  for (int i = 0; i < 40; i++) {
    noteIndx = noteOrder[ mod(orderIndx - i, 40) ];
    if (notes[noteIndx]) {
      commandNoteUni(noteIndx);
      return;
    }
  }
  digitalWrite(GATE_NOTE1, LOW);
  digitalWrite(GATE_NOTE2, LOW);
  digitalWrite(GATE_NOTE3, LOW);
  digitalWrite(GATE_NOTE4, LOW);
  digitalWrite(GATE_NOTE5, LOW);
  digitalWrite(GATE_NOTE6, LOW);// All notes are off
}

void commandNoteUni(int noteMsg) {

  unsigned int mV1 = (unsigned int) ((float) (noteMsg + transpose + realoctave) * NOTE_SF * sfAdj[0] + 0.5);
  sample_data = (channel_a & 0xFFF0000F) | (((int(mV1)) & 0xFFFF) << 4);
  outputDAC(DAC_NOTE1, sample_data);
  outputDAC(DAC_NOTE2, sample_data);
  unsigned int mV2 = (unsigned int) ((float) (noteMsg + transpose + realoctave) * NOTE_SF * sfAdj[1] + 0.5);
  sample_data = (channel_b & 0xFFF0000F) | (((int(mV2)) & 0xFFFF) << 4);
  outputDAC(DAC_NOTE1, sample_data);
  outputDAC(DAC_NOTE2, sample_data);
  unsigned int mV3 = (unsigned int) ((float) (noteMsg + transpose + realoctave) * NOTE_SF * sfAdj[2] + 0.5);
  sample_data = (channel_c & 0xFFF0000F) | (((int(mV3)) & 0xFFFF) << 4);
  outputDAC(DAC_NOTE1, sample_data);
  outputDAC(DAC_NOTE2, sample_data);
  unsigned int mV4 = (unsigned int) ((float) (noteMsg + transpose + realoctave) * NOTE_SF * sfAdj[3] + 0.5);
  sample_data = (channel_d & 0xFFF0000F) | (((int(mV4)) & 0xFFFF) << 4);
  outputDAC(DAC_NOTE1, sample_data);
  outputDAC(DAC_NOTE2, sample_data);
  unsigned int mV5 = (unsigned int) ((float) (noteMsg + transpose + realoctave) * NOTE_SF * sfAdj[4] + 0.5);
  sample_data = (channel_e & 0xFFF0000F) | (((int(mV5)) & 0xFFFF) << 4);
  outputDAC(DAC_NOTE1, sample_data);
  outputDAC(DAC_NOTE2, sample_data);
  unsigned int mV6 = (unsigned int) ((float) (noteMsg + transpose + realoctave) * NOTE_SF * sfAdj[5] + 0.5);
  sample_data = (channel_f & 0xFFF0000F) | (((int(mV6)) & 0xFFFF) << 4);
  outputDAC(DAC_NOTE1, sample_data);
  outputDAC(DAC_NOTE2, sample_data);

  digitalWrite(TRIG_NOTE1, HIGH);
  digitalWrite(GATE_NOTE1, HIGH);
  digitalWrite(TRIG_NOTE2, HIGH);
  digitalWrite(GATE_NOTE2, HIGH);
  digitalWrite(TRIG_NOTE3, HIGH);
  digitalWrite(GATE_NOTE3, HIGH);
  digitalWrite(TRIG_NOTE4, HIGH);
  digitalWrite(GATE_NOTE4, HIGH);
  digitalWrite(TRIG_NOTE5, HIGH);
  digitalWrite(GATE_NOTE5, HIGH);
  digitalWrite(TRIG_NOTE6, HIGH);
  digitalWrite(GATE_NOTE6, HIGH);

  trigTimer = millis();
  while (millis() < trigTimer + trigTimeout) {
    // wait 50 milliseconds
  }

  digitalWrite(TRIG_NOTE1, LOW);
  digitalWrite(TRIG_NOTE2, LOW);
  digitalWrite(TRIG_NOTE3, LOW);
  digitalWrite(TRIG_NOTE4, LOW);
  digitalWrite(TRIG_NOTE5, LOW);
  digitalWrite(TRIG_NOTE6, LOW);


}

void myNoteOn(byte channel, byte note, byte velocity) {
  //Check for out of range notes
  if (note < 0 || note > 127) return;

  prevNote = note;
  if (keyboardMode == 0) {
    switch (getVoiceNo(-1)) {
      case 1:
        voices[0].note = note;
        voices[0].velocity = velocity;
        voices[0].timeOn = millis();
        updateVoice1();

        digitalWrite(GATE_NOTE1, HIGH);
        digitalWrite(TRIG_NOTE1, HIGH);
        noteTrig[0] = millis();
        while (millis() < noteTrig[0] + trigTimeout) {
          // wait 50 milliseconds
        }
        digitalWrite(TRIG_NOTE1, LOW); // Set trigger low after 20 msec
        voiceOn[0] = true;
        break;
      case 2:
        voices[1].note = note;
        voices[1].velocity = velocity;
        voices[1].timeOn = millis();
        updateVoice2();

        digitalWrite(GATE_NOTE2, HIGH);
        digitalWrite(TRIG_NOTE2, HIGH);
        noteTrig[1] = millis();
        while (millis() < noteTrig[1] + trigTimeout) {
          // wait 50 milliseconds
        }
        digitalWrite(TRIG_NOTE2, LOW);
        voiceOn[1] = true;
        break;
      case 3:
        voices[2].note = note;
        voices[2].velocity = velocity;
        voices[2].timeOn = millis();
        updateVoice3();

        digitalWrite(GATE_NOTE3, HIGH);
        digitalWrite(TRIG_NOTE3, HIGH);
        noteTrig[2] = millis();
        while (millis() < noteTrig[2] + trigTimeout) {
          // wait 50 milliseconds
        }
        digitalWrite(TRIG_NOTE3, LOW);
        voiceOn[2] = true;
        break;
      case 4:
        voices[3].note = note;
        voices[3].velocity = velocity;
        voices[3].timeOn = millis();
        updateVoice4();

        digitalWrite(GATE_NOTE4, HIGH);
        digitalWrite(TRIG_NOTE4, HIGH);
        noteTrig[3] = millis();
        while (millis() < noteTrig[3] + trigTimeout) {
          // wait 50 milliseconds
        }
        digitalWrite(TRIG_NOTE4, LOW);
        voiceOn[3] = true;
        break;
      case 5:
        voices[4].note = note;
        voices[4].velocity = velocity;
        voices[4].timeOn = millis();
        updateVoice5();

        digitalWrite(GATE_NOTE5, HIGH);
        digitalWrite(TRIG_NOTE5, HIGH);
        noteTrig[4] = millis();
        while (millis() < noteTrig[4] + trigTimeout) {
          // wait 50 milliseconds
        }
        digitalWrite(TRIG_NOTE5, LOW);
        voiceOn[4] = true;
        break;
      case 6:
        voices[5].note = note;
        voices[5].velocity = velocity;
        voices[5].timeOn = millis();
        updateVoice6();

        digitalWrite(GATE_NOTE6, HIGH);
        digitalWrite(TRIG_NOTE6, HIGH);
        noteTrig[5] = millis();
        while (millis() < noteTrig[5] + trigTimeout) {
          // wait 50 milliseconds
        }
        digitalWrite(TRIG_NOTE6, LOW);
        voiceOn[5] = true;
        break;
    }
  }
  else if (keyboardMode == 4 || keyboardMode == 5 || keyboardMode == 6)
  {
    if (keyboardMode == 4)
    {
      S1 = 1;
      S2 = 1;
    }
    if (keyboardMode == 5)
    {
      S1 = 0;
      S2 = 1;
    }
    if (keyboardMode == 6)
    {
      S1 = 0;
      S2 = 0;
    }
    noteMsg = note;

    if (velocity == 0)  {
      notes[noteMsg] = false;
    }
    else {
      notes[noteMsg] = true;
    }

    unsigned int velmV = ((unsigned int) ((float) velocity) * VEL_SF);
    sample_data = (channel_a & 0xFFF0000F) | (((int(velmV)) & 0xFFFF) << 4);
    outputDAC(DAC_NOTE3, sample_data);
    if (S1 && S2) { // Highest note priority
      commandTopNote();
    }
    else if (!S1 && S2) { // Lowest note priority
      commandBottomNote();
    }
    else { // Last note priority
      if (notes[noteMsg]) {  // If note is on and using last note priority, add to ordered list
        orderIndx = (orderIndx + 1) % 40;
        noteOrder[orderIndx] = noteMsg;
      }
      commandLastNote();
    }
  }
  else if (keyboardMode == 1 || keyboardMode == 2 || keyboardMode == 3)
  {
    if (keyboardMode == 1)
    {
      S1 = 1;
      S2 = 1;
    }
    if (keyboardMode == 2)
    {
      S1 = 0;
      S2 = 1;
    }
    if (keyboardMode == 3)
    {
      S1 = 0;
      S2 = 0;
    }
    noteMsg = note;

    if (velocity == 0)  {
      notes[noteMsg] = false;
    }
    else {
      notes[noteMsg] = true;
    }

    unsigned int velmV = ((unsigned int) ((float) velocity) * VEL_SF);
    sample_data = (channel_a & 0xFFF0000F) | (((int(velmV)) & 0xFFFF) << 4);
    outputDAC(DAC_NOTE3, sample_data);
    sample_data = (channel_b & 0xFFF0000F) | (((int(velmV)) & 0xFFFF) << 4);
    outputDAC(DAC_NOTE3, sample_data);
    sample_data = (channel_c & 0xFFF0000F) | (((int(velmV)) & 0xFFFF) << 4);
    outputDAC(DAC_NOTE3, sample_data);
    sample_data = (channel_d & 0xFFF0000F) | (((int(velmV)) & 0xFFFF) << 4);
    outputDAC(DAC_NOTE3, sample_data);
    sample_data = (channel_e & 0xFFF0000F) | (((int(velmV)) & 0xFFFF) << 4);
    outputDAC(DAC_NOTE3, sample_data);
    sample_data = (channel_f & 0xFFF0000F) | (((int(velmV)) & 0xFFFF) << 4);
    outputDAC(DAC_NOTE3, sample_data);
    if (S1 && S2) { // Highest note priority
      commandTopNoteUni();
    }
    else if (!S1 && S2) { // Lowest note priority
      commandBottomNoteUni();
    }
    else { // Last note priority
      if (notes[noteMsg]) {  // If note is on and using last note priority, add to ordered list
        orderIndx = (orderIndx + 1) % 40;
        noteOrder[orderIndx] = noteMsg;
      }
      commandLastNoteUni();
    }
  }
}

void myNoteOff(byte channel, byte note, byte velocity) {
  if (keyboardMode == 0) {
    switch (getVoiceNo(note)) {
      case 1:
        digitalWrite(GATE_NOTE1, LOW);
        voices[0].note = -1;
        voiceOn[0] = false;
        break;
      case 2:
        digitalWrite(GATE_NOTE2, LOW);
        voices[1].note = -1;
        voiceOn[1] = false;
        break;
      case 3:
        digitalWrite(GATE_NOTE3, LOW);
        voices[2].note = -1;
        voiceOn[2] = false;
        break;
      case 4:
        digitalWrite(GATE_NOTE4, LOW);
        voices[3].note = -1;
        voiceOn[3] = false;
        break;
      case 5:
        digitalWrite(GATE_NOTE5, LOW);
        voices[4].note = -1;
        voiceOn[4] = false;
        break;
      case 6:
        digitalWrite(GATE_NOTE6, LOW);
        voices[5].note = -1;
        voiceOn[5] = false;
        break;
    }
  }
  else if (keyboardMode == 4 || keyboardMode == 5 || keyboardMode == 6)
  {
    if (keyboardMode == 4)
    {
      S1 = 1;
      S2 = 1;
    }
    if (keyboardMode == 5)
    {
      S1 = 0;
      S2 = 1;
    }
    if (keyboardMode == 6)
    {
      S1 = 0;
      S2 = 0;
    }
    noteMsg = note;

    if (velocity == 0)  {
      notes[noteMsg] = false;
    }
    else {
      notes[noteMsg] = true;
    }

    // Pins NP_SEL1 and NP_SEL2 indictate note priority
    unsigned int velmV = ((unsigned int) ((float) velocity) * VEL_SF);
    sample_data = (channel_a & 0xFFF0000F) | (((int(velmV)) & 0xFFFF) << 4);
    outputDAC(DAC_NOTE3, sample_data);
    if (S1 && S2) { // Highest note priority
      commandTopNote();
    }
    else if (!S1 && S2) { // Lowest note priority
      commandBottomNote();
    }
    else { // Last note priority
      if (notes[noteMsg]) {  // If note is on and using last note priority, add to ordered list
        orderIndx = (orderIndx + 1) % 40;
        noteOrder[orderIndx] = noteMsg;
      }
      commandLastNote();
    }
  } else if (keyboardMode == 1 || keyboardMode == 2 || keyboardMode == 3)
  {
    if (keyboardMode == 1)
    {
      S1 = 1;
      S2 = 1;
    }
    if (keyboardMode == 2)
    {
      S1 = 0;
      S2 = 1;
    }
    if (keyboardMode == 3)
    {
      S1 = 0;
      S2 = 0;
    }
    noteMsg = note;

    if (velocity == 0)  {
      notes[noteMsg] = false;
    }
    else {
      notes[noteMsg] = true;
    }

    unsigned int velmV = ((unsigned int) ((float) velocity) * VEL_SF);
    sample_data = (channel_a & 0xFFF0000F) | (((int(velmV)) & 0xFFFF) << 4);
    outputDAC(DAC_NOTE3, sample_data);
    sample_data = (channel_b & 0xFFF0000F) | (((int(velmV)) & 0xFFFF) << 4);
    outputDAC(DAC_NOTE3, sample_data);
    sample_data = (channel_c & 0xFFF0000F) | (((int(velmV)) & 0xFFFF) << 4);
    outputDAC(DAC_NOTE3, sample_data);
    sample_data = (channel_d & 0xFFF0000F) | (((int(velmV)) & 0xFFFF) << 4);
    outputDAC(DAC_NOTE3, sample_data);
    sample_data = (channel_e & 0xFFF0000F) | (((int(velmV)) & 0xFFFF) << 4);
    outputDAC(DAC_NOTE3, sample_data);
    sample_data = (channel_f & 0xFFF0000F) | (((int(velmV)) & 0xFFFF) << 4);
    outputDAC(DAC_NOTE3, sample_data);
    if (S1 && S2) { // Highest note priority
      commandTopNoteUni();
    }
    else if (!S1 && S2) { // Lowest note priority
      commandBottomNoteUni();
    }
    else { // Last note priority
      if (notes[noteMsg]) {  // If note is on and using last note priority, add to ordered list
        orderIndx = (orderIndx + 1) % 40;
        noteOrder[orderIndx] = noteMsg;
      }
      commandLastNoteUni();
    }
  }
}

int getVoiceNo(int note) {
  voiceToReturn = -1;//Initialise to 'null'
  earliestTime = millis();//Initialise to now
  if (note == -1) {
    //NoteOn() - Get the oldest free voice (recent voices may be still on release stage)
    for (int i = 0; i <  NO_OF_VOICES; i++) {
      if (voices[i].note == -1) {
        if (voices[i].timeOn < earliestTime) {
          earliestTime = voices[i].timeOn;
          voiceToReturn = i;
        }
      }
    }
    if (voiceToReturn == -1) {
      //No free voices, need to steal oldest sounding voice
      earliestTime = millis();//Reinitialise
      for (int i = 0; i <  NO_OF_VOICES; i++) {
        if (voices[i].timeOn < earliestTime) {
          earliestTime = voices[i].timeOn;
          voiceToReturn = i;
        }
      }
    }
    return voiceToReturn + 1;
  } else {
    //NoteOff() - Get voice number from note
    for (int i = 0; i <  NO_OF_VOICES; i++) {
      if (voices[i].note == note) {
        return i + 1;
      }
    }
  }
  //Shouldn't get here, return voice 1
  return 1;
}

void updateVoice1()
{
  unsigned int mV = (unsigned int) ((float) (voices[0].note + transpose + realoctave) * NOTE_SF * sfAdj[0] + 0.5);
  sample_data = (channel_a & 0xFFF0000F) | (((int(mV)) & 0xFFFF) << 4);
  outputDAC(DAC_NOTE1, sample_data);
  outputDAC(DAC_NOTE2, sample_data);
  unsigned int velmV = ((unsigned int) ((float) voices[0].velocity) * VEL_SF);
  sample_data = (channel_a & 0xFFF0000F) | (((int(velmV)) & 0xFFFF) << 4);
  outputDAC(DAC_NOTE3, sample_data);
}

void updateVoice2()
{
  unsigned int mV = (unsigned int) ((float) (voices[1].note + transpose + realoctave) * NOTE_SF * sfAdj[1] + 0.5);
  sample_data = (channel_b & 0xFFF0000F) | (((int(mV)) & 0xFFFF) << 4);
  outputDAC(DAC_NOTE1, sample_data);
  outputDAC(DAC_NOTE2, sample_data);
  unsigned int velmV = ((unsigned int) ((float) voices[1].velocity) * VEL_SF);
  sample_data = (channel_b & 0xFFF0000F) | (((int(velmV)) & 0xFFFF) << 4);
  outputDAC(DAC_NOTE3, sample_data);
}

void updateVoice3()
{
  unsigned int mV = (unsigned int) ((float) (voices[2].note + transpose + realoctave) * NOTE_SF * sfAdj[2] + 0.5);
  sample_data = (channel_c & 0xFFF0000F) | (((int(mV)) & 0xFFFF) << 4);
  outputDAC(DAC_NOTE1, sample_data);
  outputDAC(DAC_NOTE2, sample_data);
  unsigned int velmV = ((unsigned int) ((float) voices[2].velocity) * VEL_SF);
  sample_data = (channel_c & 0xFFF0000F) | (((int(velmV)) & 0xFFFF) << 4);
  outputDAC(DAC_NOTE3, sample_data);
}

void updateVoice4()
{
  unsigned int mV = (unsigned int) ((float) (voices[3].note + transpose + realoctave) * NOTE_SF * sfAdj[3] + 0.5);
  sample_data = (channel_d & 0xFFF0000F) | (((int(mV)) & 0xFFFF) << 4);
  outputDAC(DAC_NOTE1, sample_data);
  outputDAC(DAC_NOTE2, sample_data);
  unsigned int velmV = ((unsigned int) ((float) voices[3].velocity) * VEL_SF);
  sample_data = (channel_d & 0xFFF0000F) | (((int(velmV)) & 0xFFFF) << 4);
  outputDAC(DAC_NOTE3, sample_data);
}

void updateVoice5()
{
  unsigned int mV = (unsigned int) ((float) (voices[4].note + transpose + realoctave) * NOTE_SF * sfAdj[4] + 0.5);
  sample_data = (channel_e & 0xFFF0000F) | (((int(mV)) & 0xFFFF) << 4);
  outputDAC(DAC_NOTE1, sample_data);
  outputDAC(DAC_NOTE2, sample_data);
  unsigned int velmV = ((unsigned int) ((float) voices[4].velocity) * VEL_SF);
  sample_data = (channel_e & 0xFFF0000F) | (((int(velmV)) & 0xFFFF) << 4);
  outputDAC(DAC_NOTE3, sample_data);
}

void updateVoice6()
{
  unsigned int mV = (unsigned int) ((float) (voices[5].note + transpose + realoctave) * NOTE_SF * sfAdj[5] + 0.5);
  sample_data = (channel_f & 0xFFF0000F) | (((int(mV)) & 0xFFFF) << 4);
  outputDAC(DAC_NOTE1, sample_data);
  outputDAC(DAC_NOTE2, sample_data);
  unsigned int velmV = ((unsigned int) ((float) voices[5].velocity) * VEL_SF);
  sample_data = (channel_f & 0xFFF0000F) | (((int(velmV)) & 0xFFFF) << 4);
  outputDAC(DAC_NOTE3, sample_data);
}


void updateUnisonCheck()
{
  if (digitalRead(UNISON_ON) == 1 && keyboardMode == 0) //poly
  {
    allNotesOff();
    keyboardMode = 3;
  }

  if (digitalRead(UNISON_ON) == 1 && (keyboardMode == 4 || keyboardMode == 5 || keyboardMode == 6 )) // mono
  {
    allNotesOff();
    keyboardMode = 3;
  }

  if (digitalRead(UNISON_ON) == 0 && (keyboardMode == 1 || keyboardMode == 2 || keyboardMode == 3 )) //switch back from unison
  {
    allNotesOff();
    keyboardMode = previousMode;
  }
}

void allNotesOff() {
  digitalWrite(GATE_NOTE1, LOW);
  digitalWrite(GATE_NOTE2, LOW);
  digitalWrite(GATE_NOTE3, LOW);
  digitalWrite(GATE_NOTE4, LOW);
  digitalWrite(GATE_NOTE5, LOW);
  digitalWrite(GATE_NOTE6, LOW);

  voices[0].note = -1;
  voices[1].note = -1;
  voices[2].note = -1;
  voices[3].note = -1;
  voices[4].note = -1;
  voices[5].note = -1;

  voiceOn[0] = false;
  voiceOn[1] = false;
  voiceOn[2] = false;
  voiceOn[3] = false;
  voiceOn[4] = false;
  voiceOn[5] = false;
}

void loop()
{

  updateEncoderPos();
  updateEncoderPosB();
  encButton.update();
  updateUnisonCheck();

  if (encButton.fell()) {
    if (initial_loop == 1) {
      initial_loop = 0;  // Ignore first push after power-up
    }
    else {
      updateMenu();
    }
  }

  // Check if highlighting timer expired, and remove highlighting if so
  if (highlightEnabled && ((millis() - highlightTimer) > HIGHLIGHT_TIMEOUT)) {
    highlightEnabled = false;
    menu = SETTINGS;    // Return to top level menu
    updateSelection();  // Refresh screen without selection highlight
  }

  myusb.Task();
  midi1.read(masterChan);   //USB HOST MIDI Class Compliant
  MIDI.read(masterChan);//MIDI 5 Pin DIN
  usbMIDI.read(masterChan); //USB Client MIDI

}

void outputDAC(int CHIP_SELECT, uint32_t sample_data)
{
  SPI.beginTransaction(SPISettings(1000000, MSBFIRST, SPI_MODE1));
  digitalWrite(CHIP_SELECT, LOW);
  SPI.transfer(sample_data >> 24);
  SPI.transfer(sample_data >> 16);
  SPI.transfer(sample_data >> 8);
  SPI.transfer(sample_data);
  digitalWrite(CHIP_SELECT, HIGH);
  SPI.endTransaction();
}

void setVoltage(int dacpin, bool channel, bool gain, unsigned int mV)
{
  //  int command = channel ? 0x9000 : 0x1000;
  //
  //  command |= gain ? 0x0000 : 0x2000;
  //  command |= (mV & 0x0FFF);
  //
  //  SPI.beginTransaction(SPISettings(15000000, MSBFIRST, SPI_MODE0));
  //  digitalWrite(dacpin, LOW);
  //  SPI.transfer(command >> 8);
  //  SPI.transfer(command & 0xFF);
  //  digitalWrite(dacpin, HIGH);
  //  SPI.endTransaction();
}


void updateEncoderPos() {
  static int encoderA, encoderB, encoderA_prev;

  encoderA = digitalRead(ENC_A);

  if ((!encoderA) && (encoderA_prev)) { // A has gone from high to low
    if (highlightEnabled) { // Update encoder position
      encoderPosPrev = encoderPos;
      encoderB ? encoderPos++ : encoderPos--;
    }
    else {
      highlightEnabled = true;
      encoderPos = 0;  // Reset encoder position if highlight timed out
      encoderPosPrev = 0;
    }
    highlightTimer = millis();
    updateSelection();
  }
  encoderA_prev = encoderA;
}

void updateEncoderPosB() {
  static int encoderA, encoderB, encoderB_prev;

  encoderB = digitalRead(ENC_B);

  if ((!encoderB) && (encoderB_prev)) { // A has gone from high to low
    if (highlightEnabled) { // Update encoder position
      encoderPosPrev = encoderPos;
      encoderA ? encoderPos-- : encoderPos++;
    }
    else {
      highlightEnabled = true;
      encoderPos = 0;  // Reset encoder position if highlight timed out
      encoderPosPrev = 0;
    }
    highlightTimer = millis();
    updateSelection();
  }
  encoderB_prev = encoderB;
}

int setCh;
char setMode[6];

void updateMenu() {  // Called whenever button is pushed

  if (highlightEnabled) { // Highlight is active, choose selection
    switch (menu) {
      case SETTINGS:
        switch (mod(encoderPos, 5)) {
          case 0:
            menu = KEYBOARD_MODE_SET_CH;
            break;
          case 1:
            menu = MIDI_CHANNEL_SET_CH;
            break;
          case 2:
            menu = TRANSPOSE_SET_CH;
            break;
          case 3:
            menu = OCTAVE_SET_CH;
            break;
          case 4:
            menu = SCALE_FACTOR;
            break;
        }
        break;

      case KEYBOARD_MODE_SET_CH:  // Save keyboard mode setting to EEPROM
        menu = SETTINGS;
        EEPROM.write(ADDR_KEYBOARD_MODE, keyboardMode);
        break;

      case MIDI_CHANNEL_SET_CH:  // Save midi channel setting to EEPROM
        menu = SETTINGS;
        EEPROM.write(ADDR_MASTER_CHAN, masterChan);
        break;

      case TRANSPOSE_SET_CH:  // Save transpose setting to EEPROM
        menu = SETTINGS;
        EEPROM.write(ADDR_TRANSPOSE, masterTran);
        EEPROM.write(ADDR_REAL_TRANSPOSE, masterTran - 12);
        transpose = (masterTran - 12);
        break;

      case OCTAVE_SET_CH:  // Save octave adjust setting to EEPROM
        menu = SETTINGS;
        EEPROM.write(ADDR_OCTAVE, octave);
        if (octave == 0) realoctave = -36;
        if (octave == 1) realoctave = -24;
        if (octave == 2) realoctave = -12;
        if (octave == 3) realoctave = 0;
        EEPROM.write(ADDR_REALOCTAVE, realoctave);
        break;

      case SCALE_FACTOR:
        setCh = mod(encoderPos, 7);
        switch (setCh) {
          case 0:
          case 1:
          case 2:
          case 3:
          case 4:
          case 5:
            menu = SCALE_FACTOR_SET_CH;
            break;
          case 6:
            menu = SETTINGS;
            break;
        }
        break;

      case SCALE_FACTOR_SET_CH: // Save scale factor to EEPROM
        menu = SCALE_FACTOR;
        EEPROM.put(ADDR_SF_ADJUST + setCh * sizeof(float), sfAdj[setCh]);
        break;
    }
  }
  else { // Highlight wasn't visible, reinitialize highlight timer
    highlightTimer = millis();
    highlightEnabled = true;
  }
  encoderPos = 0;  // Reset encoder position
  encoderPosPrev = 0;
  updateSelection(); // Refresh screen
}

void updateSelection() { // Called whenever encoder is turned
  display.clearDisplay();
  switch (menu) {
    case KEYBOARD_MODE_SET_CH:
      if (menu == KEYBOARD_MODE_SET_CH) keyboardMode = mod(encoderPos, 7);

    case MIDI_CHANNEL_SET_CH:
      if (menu == MIDI_CHANNEL_SET_CH) masterChan = mod(encoderPos, 17);

    case TRANSPOSE_SET_CH:
      if (menu == TRANSPOSE_SET_CH) masterTran = mod(encoderPos, 25);

    case OCTAVE_SET_CH:
      if (menu == OCTAVE_SET_CH) octave = mod(encoderPos, 4);

    case SETTINGS:
      display.setCursor(0, 0);
      display.setTextColor(WHITE, BLACK);
      display.print(F("SETTINGS"));
      display.setCursor(0, 16);

      if (menu == SETTINGS) setHighlight(0, 5);
      display.print(F("Keyboard Mode "));
      if (menu == KEYBOARD_MODE_SET_CH) display.setTextColor(BLACK, WHITE);
      if (keyboardMode == 0) display.print("Poly  ");
      if (keyboardMode == 1) display.print("Uni T");
      if (keyboardMode == 2) display.print("Uni B");
      if (keyboardMode == 3) display.print("Uni L");
      if (keyboardMode == 4) display.print("Mono T ");
      if (keyboardMode == 5) display.print("Mono B ");
      if (keyboardMode == 6) display.print("Mono L ");
      display.println(F(""));
      display.setTextColor(WHITE, BLACK);

      if (menu == SETTINGS) setHighlight(1, 5);
      display.print(F("Midi Channel  "));
      if (menu == MIDI_CHANNEL_SET_CH) display.setTextColor(BLACK, WHITE);
      if (masterChan == 0) display.print("Omni");
      else display.print(masterChan);
      display.println(F(" "));
      display.setTextColor(WHITE, BLACK);

      if (menu == SETTINGS) setHighlight(2, 5);
      display.print(F("Transpose     "));
      if (menu == TRANSPOSE_SET_CH) display.setTextColor(BLACK, WHITE);
      display.print(masterTran - 12);
      display.println(F(" "));
      display.setTextColor(WHITE, BLACK);

      if (menu == SETTINGS) setHighlight(3, 5);
      display.print(F("Octave Adjust "));
      if (menu == OCTAVE_SET_CH) display.setTextColor(BLACK, WHITE);
      if (octave == 0) display.print("-3 ");
      if (octave == 1) display.print("-2 ");
      if (octave == 2) display.print("-1 ");
      if (octave == 3) display.print(" 0 ");
      display.println(F(" "));
      display.setTextColor(WHITE, BLACK);

      if (menu == SETTINGS) setHighlight(4, 5);
      else display.setTextColor(WHITE, BLACK);
      display.println(F("Scale Factor     "));
      break;

    case SCALE_FACTOR_SET_CH:
      if ((encoderPos > encoderPosPrev) && (sfAdj[setCh] < 1.1))
        sfAdj[setCh] += 0.001f;
      else if ((encoderPos < encoderPosPrev) && (sfAdj[setCh] > 0.9))
        sfAdj[setCh] -= 0.001f;

    case SCALE_FACTOR:
      display.setCursor(0, 0);
      display.setTextColor(WHITE, BLACK);
      display.print(F("SET SCALE FACTOR"));
      display.setCursor(0, 8);

      if (menu == SCALE_FACTOR) setHighlight(0, 7);
      display.print(F("Note 1:    "));
      if ((menu == SCALE_FACTOR_SET_CH) && setCh == 0) display.setTextColor(BLACK, WHITE);
      display.println(sfAdj[0], 3);

      if (menu == SCALE_FACTOR) setHighlight(1, 7);
      else display.setTextColor(WHITE, BLACK);
      display.print(F("Note 2:    "));
      if ((menu == SCALE_FACTOR_SET_CH) && setCh == 1) display.setTextColor(BLACK, WHITE);
      display.println(sfAdj[1], 3);

      if (menu == SCALE_FACTOR) setHighlight(2, 7);
      else display.setTextColor(WHITE, BLACK);
      display.print(F("Note 3:    "));
      if ((menu == SCALE_FACTOR_SET_CH) && setCh == 2) display.setTextColor(BLACK, WHITE);
      display.println(sfAdj[2], 3);

      if (menu == SCALE_FACTOR) setHighlight(3, 7);
      else display.setTextColor(WHITE, BLACK);
      display.print(F("Note 4:    "));
      if ((menu == SCALE_FACTOR_SET_CH) && setCh == 3) display.setTextColor(BLACK, WHITE);
      display.println(sfAdj[3], 3);

      if (menu == SCALE_FACTOR) setHighlight(4, 7);
      else display.setTextColor(WHITE, BLACK);
      display.print(F("Note 5:    "));
      if ((menu == SCALE_FACTOR_SET_CH) && setCh == 4) display.setTextColor(BLACK, WHITE);
      display.println(sfAdj[4], 3);

      if (menu == SCALE_FACTOR) setHighlight(5, 7);
      else display.setTextColor(WHITE, BLACK);
      display.print(F("Note 6:    "));
      if ((menu == SCALE_FACTOR_SET_CH) && setCh == 5) display.setTextColor(BLACK, WHITE);
      display.println(sfAdj[5], 3);

      if (menu == SCALE_FACTOR) setHighlight(6, 7);
      else display.setTextColor(WHITE, BLACK);
      display.print(F("Return      "));

  }
  display.display();
}

void setHighlight(int menuItem, int numMenuItems) {
  if ((mod(encoderPos, numMenuItems) == menuItem) &&  highlightEnabled ) {
    display.setTextColor(BLACK, WHITE);
  }
  else {
    display.setTextColor(WHITE, BLACK);
  }
}

int mod(int a, int b)
{
  int r = a % b;
  return r < 0 ? r + b : r;
}
