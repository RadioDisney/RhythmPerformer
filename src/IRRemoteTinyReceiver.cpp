#include "Debug_def.h"
#include "IRRemoteTinyReceiver.h"



// IR Remote ***********************************************************

#include "TinyIRReceiver.hpp" 
volatile struct TinyIRReceiverCallbackDataStruct sCallbackData;

IRRemoteTinyReceiver::KeyResult IRRemoteTinyReceiver::keyResult_ = IRRemoteTinyReceiver::KEY_NULL;
uint8_t IRRemoteTinyReceiver::lastKey_ = 0;

IRRemoteTinyReceiver::IRRemoteTinyReceiver (IRRemoteRxKeyValue* kv, uint8_t kvSize)
:kv_(kv), kvSize_(kvSize)
{};

void IRRemoteTinyReceiver::Init ()
{
      // Enables the interrupt generation on change of IR input signal
      DEBUG("\n", "*****************************");
  if (!initPCIInterruptForTinyReceiver()) {
      DEBUG("\n", F("No interrupt available for pin " STR(IR_RECEIVE_PIN))); // optimized out by the compiler, if not required :-)
  }

#if defined(USE_FAST_PROTOCOL)
    DEBUG("\n", F("Ready to receive Fast IR signals at pin " STR(IR_RECEIVE_PIN)));
#else
    DEBUG("\n", F("Ready to receive NEC IR signals at pin " STR(IR_RECEIVE_PIN)));
#endif
}

void IRRemoteTinyReceiver::Update ()
{
      if (sCallbackData.justWritten) {
        sCallbackData.justWritten = false;
        keyResult_ = IRRemoteTinyReceiver::KEY_PRESS;
        IRRemoteTinyReceiver::lastKey_ = FindKey (sCallbackData.Address, sCallbackData.Command);

#if defined(USE_FAST_PROTOCOL)
        DEBUG("", F("Command=0x"));
#else
        DEBUG("", F("Address=0x"));
        DEBUG(sCallbackData.Address, HEX);
        DEBUG("", F(" Command=0x"));
#endif
        DEBUG(sCallbackData.Command, HEX);
        if (sCallbackData.Flags == IRDATA_FLAGS_IS_REPEAT) {
            DEBUG("", F(" Repeat"));
        }
        if (sCallbackData.Flags == IRDATA_FLAGS_PARITY_FAILED) {
            DEBUG("", F(" Parity failed"));
        }
        DEBUG("\n", "");

        
    }
}

uint8_t IRRemoteTinyReceiver::FindKey (uint16_t  address, uint8_t command)
{
  for (IRRemoteRxKeyValue* p_kv = kv_; p_kv < kv_ + kvSize_; p_kv++)
  {
    if (p_kv -> address_ == address && p_kv -> command_ == command)
    {
      return p_kv -> value_;
    }
  }

  return 0;
}

IRRemoteTinyReceiver::KeyResult IRRemoteTinyReceiver::read()
{
  KeyResult keyResult = keyResult_;
  keyResult_ = KEY_NULL;
  return keyResult;
}

uint8_t IRRemoteTinyReceiver::getKey()
{
  return lastKey_;
}

#if defined(USE_FAST_PROTOCOL)
void handleReceivedTinyIRData(uint8_t aCommand, uint8_t aFlags)
#elif defined(USE_ONKYO_PROTOCOL)
void handleReceivedTinyIRData(uint16_t aAddress, uint16_t aCommand, uint8_t aFlags)
#else
void handleReceivedTinyIRData(uint8_t aAddress, uint8_t aCommand, uint8_t aFlags)
#endif
{
    DEBUG("\n", "handleReceivedTinyIRData");
#if defined(ARDUINO_ARCH_MBED) || defined(ESP32)
    // Copy data for main loop, this is the recommended way for handling a callback :-)
#  if !defined(USE_FAST_PROTOCOL)
    sCallbackData.Address = aAddress;
#  endif
    sCallbackData.Command = aCommand;
    sCallbackData.Flags = aFlags;
    sCallbackData.justWritten = true;
#else
    /*
     * Printing is not allowed in ISR context for any kind of RTOS
     * For Mbed we get a kernel panic and "Error Message: Semaphore: 0x0, Not allowed in ISR context" for Serial.print()
     * for ESP32 we get a "Guru Meditation Error: Core  1 panic'ed" (we also have an RTOS running!)
     */
    // Print only very short output, since we are in an interrupt context and do not want to miss the next interrupts of the repeats coming soon
#  if defined(USE_FAST_PROTOCOL)
    printTinyReceiverResultMinimal(&Serial, aCommand, aFlags);
#  else
    printTinyReceiverResultMinimal(&Serial, aAddress, aCommand, aFlags);
#  endif
#endif
}