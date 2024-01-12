// Play a file from the SD card in looping mode, from the SD card.
// Example program to demonstrate the use of the MIDFile library
//
// Hardware required:
//  SD card interface - change SD_SELECT for SPI comms

#include <SdFat.h>
#include <MD_MIDIFile.h>
#include <LiquidCrystal.h>
#include "IRRemoteTinyReceiver.h"
#include "Debug_def.h"

#include <BLEMIDI_Transport.h>
#include <hardware/BLEMIDI_Client_ESP32.h>



// BLEMIDI_CREATE_DEFAULT_INSTANCE(); //Connect to first server found

BLEMIDI_NAMESPACE::BLEMIDI_Transport<BLEMIDI_NAMESPACE::BLEMIDI_Client_ESP32> BLEMIDI("Performer");
MIDI_NAMESPACE::MidiInterface<BLEMIDI_NAMESPACE::BLEMIDI_Transport<BLEMIDI_NAMESPACE::BLEMIDI_Client_ESP32>, BLEMIDI_NAMESPACE::MySettings> MIDI((BLEMIDI_NAMESPACE::BLEMIDI_Transport<BLEMIDI_NAMESPACE::BLEMIDI_Client_ESP32> &)BLEMIDI);

//BLEMIDI_CREATE_INSTANCE("",MIDI)                  //Connect to the first server found
//BLEMIDI_CREATE_INSTANCE("f2:c1:d9:36:e7:6b",MIDI) //Connect to a specific BLE address server
//BLEMIDI_CREATE_INSTANCE("MyBLEserver",MIDI)       //Connect to a specific name server

#ifndef LED_BUILTIN
#define LED_BUILTIN 2 //modify for match with yout board
#endif

void Serial2WriteData(byte* data, int length);
void ReadCB(void *parameter);       //Continuos Read function (See FreeRTOS multitasks)

unsigned long t0 = millis();
bool isConnected = false;




// SD Hardware defines ---------
// SPI select pin for SD card (SPI comms).
// Default SD chip select is the SPI SS pin (10 on Uno, 53 on Mega).
const uint8_t SD_SELECT = SS;

// LCD display defines ---------
const uint8_t LCD_ROWS = 2;
const uint8_t LCD_COLS = 16;

// LCD user defined characters
char PAUSE = '\1';
uint8_t cPause[8] = { 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x00 };

// LCD Shield pin definitions ---------
// These need to be modified for the LCD hardware setup
const uint8_t LCD_RS = 13;
const uint8_t LCD_ENA = 14;
const uint8_t LCD_D4 = 25;
const uint8_t LCD_D5 = 26;
const uint8_t LCD_D6 = 27;
const uint8_t LCD_D7 = 33;

// Define the key table for the analog keys
IRRemoteTinyReceiver::IRRemoteRxKeyValue kv[] =
{
  { 0x0, 0x07, 'S' },  // Select
  { 0x0, 0x03, 'U' },  // Up
  { 0x0, 0x02, 'D' },  // Down
  { 0x0, 0x0E, 'L' },  // Left
  { 0x0, 0x1A, 'R' },  // Right
};

// Library objects -------------
LiquidCrystal LCD(LCD_RS, LCD_ENA, LCD_D4, LCD_D5, LCD_D6, LCD_D7);
SDFAT SD;
MD_MIDIFile SMF;
IRRemoteTinyReceiver irRx_(kv, ARRAY_SIZE(kv));

// Playlist handling -----------
const uint8_t FNAME_SIZE = 13;               // file names 8.3 to fit onto LCD display
const char* PLAYLIST_FILE = "PLAYLIST.TXT"; // file of file names
const char* MIDI_EXT = ".MID";               // MIDI file extension
uint16_t  plCount = 0;
char fname[FNAME_SIZE+1];

// Enumerated types for the FSM(s)
enum lcd_state  { LSBegin, LSSelect, LSShowFile };
enum midi_state { MSBegin, MSLoad, MSOpen, MSProcess, MSClose };
enum seq_state  { LCDSeq, MIDISeq };

// MIDI callback functions for MIDIFile library ---------------

void midiCallback(midi_event *pev)
// Called by the MIDIFile library when a file event needs to be processed
// thru the midi communications interface.
// This callback is set up in the setup() function.
{
#if !DEBUG_ON
  if ((pev->data[0] >= 0x80) && (pev->data[0] <= 0xe0))
  {
    Serial.write(pev->data[0] | pev->channel);
    Serial.write(&pev->data[1], pev->size-1);
  }
  else
    Serial.write(pev->data, pev->size);
#endif
  DEBUG("\nM T", pev->track);
  DEBUG(":  Ch ", pev->channel+1);
  DEBUGS(" Data");
  for (uint8_t i = 0; i <= pev->size; i++)
    DEBUGX(" ", pev->data[i]);
}

void sysexCallback(sysex_event *pev)
// Called by the MIDIFile library when a System Exclusive (sysex) file event needs 
// to be processed thru the midi communications interface. Most sysex events cannot 
// really be processed, so we just ignore it here.
// This callback is set up in the setup() function.
{
  DEBUG("\nS T", pev->track);
  DEBUGS(": Data");
  for (uint8_t i = 0; i < pev->size; i++)
    DEBUGX(" ", pev->data[i]);
}

void midiSilence(void)
// Turn everything off on every channel.
// Some midi files are badly behaved and leave notes hanging, so between songs turn
// off all the notes and sound
{
  midi_event ev;

  // All sound off
  // When All Sound Off is received all oscillators will turn off, and their volume
  // envelopes are set to zero as soon as possible.
  ev.size = 0;
  ev.data[ev.size++] = 0xb0;
  ev.data[ev.size++] = 120;
  ev.data[ev.size++] = 0;

  for (ev.channel = 0; ev.channel < 16; ev.channel++)
    midiCallback(&ev);
}

// LCD Message Helper functions -----------------
void LCDMessage(uint8_t r, uint8_t c, const char *msg, bool clrEol = false)
// Display a message on the LCD screen with optional spaces padding the end
{
  LCD.setCursor(c, r);
  LCD.print(msg);
  if (clrEol)
  {
    c += strlen(msg);
    while (c++ < LCD_COLS)
      LCD.write(' ');
  }
}

void LCDErrMessage(const char *msg, bool fStop)
{
  LCDMessage(1, 0, msg, true);
  DEBUG("\nLCDErr: ", msg);
  while (fStop) { yield(); }; // stop here (busy loop) if told to
  delay(2000);      // if not stop, pause to show message
}

// Create list of files for menu --------------

uint16_t createPlaylistFile(void)
// create a play list file on the SD card with the names of the files.
// This will then be used in the menu.
{
  SDFILE    plFile;   // play list file
  SDFILE    mFile;    // MIDI file
  SDDIR     dir;      // directory folder
  uint16_t  count = 0;// count of files
  char      fname[FNAME_SIZE+1];

  // Open/create the display list and directory files
  // Errors will stop execution...
  if (!dir.open("/", O_READ))
  {
    DEBUGX("\nDir open fail, err ", dir.getError());
    LCDErrMessage("Dir open fail", true);
  }

  if (!plFile.open(PLAYLIST_FILE, O_CREAT | O_WRITE))
  {
    DEBUGX("\nPL Create fail, err ", plFile.getError());
    LCDErrMessage("PL create fail", true);
  }

  while (mFile.openNext(&dir, O_READ))
  {
    mFile.getName(fname, FNAME_SIZE);

    DEBUG("\n", count);
    DEBUG(" ", fname);

    if (mFile.isFile() && !mFile.isHidden())
    {
      // only include files with MIDI extension
      if (strcasecmp(MIDI_EXT, &fname[strlen(fname) - strlen(MIDI_EXT)]) == 0)
      {
        DEBUGS(" -> W");
        if (plFile.write(fname, FNAME_SIZE) == 0)
          DEBUGS(" error");
        else
          DEBUGS(" OK");
        count++;
      }
      else
        DEBUGS(" -> not MIDI");
    }
    else
      DEBUGS(" -> folder/hidden");

    mFile.close();

  }
  // close the control files
  plFile.close();
  dir.close();
  DEBUGS("\nList completed");

  return(count);
}

// FINITE STATE MACHINES -----------------------------

seq_state lcdFSM(seq_state curSS)
// Handle selecting a file name from the list (user input)
{
  static lcd_state s = LSBegin;
  static uint8_t plIndex = 0;
  static SDFILE plFile;  // play list file

  // LCD state machine
  switch (s)
  {
  case LSBegin:
    LCDMessage(0, 0, "Select play:", true);
    if (!plFile.isOpen())
    {
      if (!plFile.open(PLAYLIST_FILE, O_READ))
        LCDErrMessage("PL file no read open", true);
    }
    s = LSShowFile;
    break;

  case LSShowFile:
    plFile.seekSet(FNAME_SIZE * plIndex);
    plFile.read(fname, FNAME_SIZE);

    LCDMessage(1, 0, fname, true);
    LCD.setCursor(LCD_COLS-2, 1);
    LCD.print(plIndex == 0 ? ' ' : '<');
    LCD.print(plIndex == plCount-1 ? ' ' : '>');
    s = LSSelect;
    break;

  case LSSelect:
    if (irRx_.read() == IRRemoteTinyReceiver::KEY_PRESS)
    {
      switch (irRx_.getKey())
        // Keys are mapped as follows:
        // Select:  move on to the next state in the state machine
        // Left:    use the previous file name (move back one file name)
        // Right:   use the next file name (move forward one file name)
        // Up:      move to the first file name
        // Down:    move to the last file name
      {
      case 'S': // Select
        DEBUGS("\n>Play");
        curSS = MIDISeq;    // switch mode to playing MIDI in main loop
        s = LSBegin;        // reset for next time
        break;

      case 'L': // Left
        DEBUGS("\n>Previous");
        if (plIndex != 0)
          plIndex--;
        s = LSShowFile;
        break;

      case 'U': // Up
        DEBUGS("\n>First");
        plIndex = 0;
        s = LSShowFile;
        break;

      case 'D': // Down
        DEBUGS("\n>Last");
        plIndex = plCount - 1;
        s = LSShowFile;
        break;

      case 'R': // Right
        DEBUGS("\n>Next");
        if (plIndex != plCount - 1)
          plIndex++;
        s = LSShowFile;
        break;
      }
    }
    break;

  default:
    s = LSBegin;        // reset for next time
    break;
  }  

  return(curSS);
}

seq_state midiFSM(seq_state curSS)
// Handle playing the selected MIDI file
{
  static midi_state s = MSBegin;
  char  sBuf[10];
  switch (s)
  {
  case MSBegin:
    // Set up the LCD 
    LCDMessage(0, 0, "Player:> +-", true);
    LCDMessage(1, 0, "<:K  >:\xdb", true);   // string of user defined characters
    s = MSLoad;
    break;

  case MSLoad:
    // Load the current file in preparation for playing
    {
      int  err;

      // Attempt to load the file
      if ((err = SMF.load(fname)) == MD_MIDIFile::E_OK)
        s = MSProcess;
      else
      {
        char aErr[16];

        sprintf(aErr, "SMF error %03d", err);
        LCDErrMessage(aErr, false);
        s = MSClose;
      }
    }
    break;

  case MSProcess:
    // Play the MIDI file
    if (!SMF.isEOF())
    {
      if (SMF.getNextEvent())
      {
        sprintf(sBuf, "T:%3d", SMF.getTempo());
        LCDMessage(0, LCD_COLS-strlen(sBuf), sBuf, true);
        sprintf(sBuf, "S:%d/%d", SMF.getTimeSignature()>>8, SMF.getTimeSignature() & 0xf);
        LCDMessage(1, LCD_COLS-strlen(sBuf), sBuf, true);
      };
    }    
    else
      s = MSClose;

    // check the keys
    if (irRx_.read() == IRRemoteTinyReceiver::KEY_PRESS)
    {
      switch (irRx_.getKey())
      {
      case 'L': midiSilence();  SMF.restart();    break;  // Rewind
      case 'R': midiSilence();  s = MSClose;                      break;  // Stop
      case 'U':
          {
            if (!SMF.isEOF())
            {
              if (SMF.getNextEvent())
              {
                SMF.setTempo(SMF.getTempo()+1);
                sprintf(sBuf, "T:%3d", SMF.getTempo());
                LCDMessage(0, LCD_COLS-strlen(sBuf), sBuf, true);
              };
            }  
          }   
          break;
      case 'D':
          {
            if (!SMF.isEOF())
            {
              if (SMF.getNextEvent())
              {
                SMF.setTempo(SMF.getTempo()-1);
                sprintf(sBuf, "T:%3d", SMF.getTempo());
                LCDMessage(0, LCD_COLS-strlen(sBuf), sBuf, true);
              };
            } 
          }
          break; 
      case 'S': 
          {
            SMF.pause(!SMF.isPaused());
            if (SMF.isPaused())
              midiSilence();
            sprintf(sBuf, "%c", SMF.isPaused()?'\1':'>');
            LCDMessage(0, 7, sBuf);
          }    
          break;  // Pause or Play
      }
    }
    break;

  case MSClose:
    // close the file and switch mode to user input
    SMF.close();
    midiSilence();
    curSS = LCDSeq;
    // fall through to default state

  default:
    s = MSBegin;
    break;
  }

  return(curSS);
}



//******************************************************************


#define ARRAY_SIZE(a) (sizeof(a)/sizeof((a)[0]))

void initBLEMIDI()
{

  BLEMIDI.setHandleConnected([]()
                             {
                              //  Serial.println("---------CONNECTED---------");
                               isConnected = true;
                               digitalWrite(LED_BUILTIN, HIGH);
                             });

  BLEMIDI.setHandleDisconnected([]()
                                {
                                  // Serial.println("---------NOT CONNECTED---------");
                                  isConnected = false;
                                  digitalWrite(LED_BUILTIN, LOW);
                                });

  MIDI.setHandleNoteOn([](byte channel, byte note, byte velocity)
                       {
                         Serial.write(0x91);
                         Serial.write(note);
                         Serial.write(velocity);
                         digitalWrite(LED_BUILTIN, LOW);
                       });
  MIDI.setHandleNoteOff([](byte channel, byte note, byte velocity)
                        {
                         Serial.write(0x81);
                         Serial.write(note);
                         Serial.write(velocity);
                         digitalWrite(LED_BUILTIN, HIGH);
                        });

  xTaskCreatePinnedToCore(ReadCB,           //See FreeRTOS for more multitask info  
                          "MIDI-READ",
                          3000,
                          NULL,
                          1,
                          NULL,
                          1); //Core0 or Core1

    pinMode(LED_BUILTIN, OUTPUT);
    digitalWrite(LED_BUILTIN, LOW);
}

void setup(void)
{
  int  err;

  pinMode(4, OUTPUT);       // Hand Shake Pin
  digitalWrite(4, HIGH);

  Serial.begin(SERIAL_RATE);
  Serial2.begin(SERIAL2_RATE, SERIAL_8E1);

  DEBUGS("\n[Rhythm Performer]");
  
  // initialize LCD display
  pinMode(15, OUTPUT);      // LCD contrast
  digitalWrite(15, 0);
  LCD.begin(LCD_COLS, LCD_ROWS);
  LCD.clear();
  LCD.noCursor();
  LCDMessage(0, 0, "Rhythm Performer", false);
  LCDMessage(1, 0, "--(C)PopuMusic--", false);

    // Load characters to the LCD
  LCD.createChar(PAUSE, cPause);
  
  initBLEMIDI();

  // Initialize SD
  // initialize SDFat
  if (!SD.begin(SD_SELECT, SPI_DIV3_SPEED))
    LCDErrMessage("SD init fail!", true);

  plCount = createPlaylistFile();
  if (plCount == 0)
    LCDErrMessage("No files", true);

  // Initialize MIDIFile
  SMF.begin(&SD);
  SMF.setMidiHandler(midiCallback);
  SMF.setSysexHandler(sysexCallback);
  SMF.looping(true);

  delay(4000);   // allow the welcome to be read on the LCD

  IRRemoteTinyReceiver::Init();
}

byte serial2ReadBuffer[80];
size_t serial2ReadLenght;
char deviceAddr[24];
bool hasMidiBegin_ = false;

char myBLEAddString[24];

void loop(void)
{
  irRx_.Update();

  static seq_state s = LCDSeq;

  switch (s)
  {
    case LCDSeq:  s = lcdFSM(s);	break;
    case MIDISeq: s = midiFSM(s);	break;
    default: s = LCDSeq;
  }

  if (Serial2.available())
  {
    serial2ReadLenght = Serial2.readBytesUntil('\xF7', serial2ReadBuffer, 80);

    // if (serial2ReadLenght > 0)
    // {
    //   digitalWrite(LED_BUILTIN, HIGH);
    //   Serial2WriteData(serial2ReadBuffer, serial2ReadLenght);
    // }

    if (serial2ReadBuffer[0] == '\xF0')
    {
      digitalWrite(LED_BUILTIN, HIGH);
      if (hasMidiBegin_ == false)
      {
        hasMidiBegin_ = true;
        strncpy(deviceAddr, (char *)(serial2ReadBuffer + 1), serial2ReadLenght - 1);
        BLEMIDI.setName(deviceAddr);

        MIDI.begin(MIDI_CHANNEL_OMNI);

        BLEAddress myBLEAddr = BLEDevice::getAddress();
        sprintf(myBLEAddString, "\xF0%s\xF7", myBLEAddr.toString().c_str());
      }

      Serial2WriteData((byte*)myBLEAddString, strlen(myBLEAddString));
    }
  }
}

void Serial2WriteData(byte* data, int length)
{
  for (int i = 0; i < length; i++)
      Serial2.write(*(data+i));
}

/**
 * This function is called by xTaskCreatePinnedToCore() to perform a multitask execution.
 * In this task, read() is called every millisecond (approx.).
 * read() function performs connection, reconnection and scan-BLE functions.
 * Call read() method repeatedly to perform a successfull connection with the server 
 * in case connection is lost.
*/
void ReadCB(void *parameter)
{
//  Serial.print("READ Task is started on core: ");
//  Serial.println(xPortGetCoreID());
  for (;;)
  {
    MIDI.read(); 
    vTaskDelay(1 / portTICK_PERIOD_MS); //Feed the watchdog of FreeRTOS.
    //Serial.println(uxTaskGetStackHighWaterMark(NULL)); //Only for debug. You can see the watermark of the free resources assigned by the xTaskCreatePinnedToCore() function.
  }
  vTaskDelay(1);
}