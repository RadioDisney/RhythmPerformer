// Play a file from the SD card in looping mode, from the SD card.
// Example program to demonstrate the use of the MIDFile library
//
// Hardware required:
//  SD card interface - change SD_SELECT for SPI comms

#include <SdFat.h>
#include <MD_MIDIFile.h>

#include <Arduino.h>
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

#define USE_MIDI  1  // set to 1 for MIDI output, 0 for debug output

#if USE_MIDI // set up for direct MIDI serial output

#define DEBUGS(s)
#define DEBUG(s, x)
#define DEBUGX(s, x)
#define SERIAL_RATE 31250   // MIDI RATE

#define SERIAL2_RATE 115200   // 

#else // don't use MIDI to allow printing debug statements

#define DEBUGS(s)     do { Serial.print(s); } while(false)
#define DEBUG(s, x)   do { Serial.print(F(s)); Serial.print(x); } while(false)
#define DEBUGX(s, x)  do { Serial.print(F(s)); Serial.print(x, HEX); } while(false)
#define SERIAL_RATE 115200

#endif // USE_MIDI


// SD chip select pin for SPI comms.
// Default SD chip select is the SPI SS pin (10 on Uno, 53 on Mega).
const uint8_t SD_SELECT = SS;  

#define ARRAY_SIZE(a) (sizeof(a)/sizeof((a)[0]))

// The files in the tune list should be located on the SD card 
// or an error will occur opening the file and the next in the 
// list will be opened (skips errors).
const char *loopfile = "LOOPDEMO.MID";  // simple and short file

SDFAT	SD;
MD_MIDIFile SMF;

void midiCallback(midi_event *pev)
// Called by the MIDIFile library when a file event needs to be processed
// thru the midi communications interface.
// This callback is set up in the setup() function.
{
#if USE_MIDI
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
  for (uint8_t i=0; i<pev->size; i++)
  {
    DEBUGX(" ", pev->data[i]);
  }
}

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

  pinMode(4, OUTPUT);
  digitalWrite(4, HIGH);

  Serial.begin(SERIAL_RATE);
  Serial2.begin(SERIAL2_RATE, SERIAL_8E1);

  DEBUGS("\n[MidiFile Looper]");

  // Initialize SD
  if (!SD.begin(SD_SELECT, SPI_QUARTER_SPEED))
  {
    DEBUGS("\nSD init fail!");
    while (true) ;
  }

  initBLEMIDI();

  // Initialize MIDIFile
  SMF.begin(&SD);
  SMF.setMidiHandler(midiCallback);
  SMF.looping(true);

  // use the next file name and play it
  DEBUG("\nFile: ", loopfile);
  err = SMF.load(loopfile);
  if (err != MD_MIDIFile::E_OK)
  {
    DEBUG("\nSMF load Error ", err);
    while (true);
  }
}

byte serial2ReadBuffer[80];
size_t serial2ReadLenght;
char deviceAddr[24];

char myBLEAddString[24];

void loop(void)
{
  // play the file
  if (!SMF.isEOF())
  {
    SMF.getNextEvent();
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
      strncpy(deviceAddr, (char *)(serial2ReadBuffer + 1), serial2ReadLenght - 1);
      BLEMIDI.setName(deviceAddr);

      MIDI.begin(MIDI_CHANNEL_OMNI);

      BLEAddress myBLEAddr = BLEDevice::getAddress();
      sprintf(myBLEAddString, "\xF0%s\xF7", myBLEAddr.toString().c_str());

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