#pragma once

#define DEBUG_ON  0

#if DEBUG_ON

#define DEBUG(s, x)  do { Serial.print(F(s)); Serial.print(x); } while(false)
#define DEBUGX(s, x) do { Serial.print(F(s)); Serial.print(F("0x")); Serial.print(x, HEX); } while(false)
#define DEBUGS(s)    do { Serial.print(F(s)); } while (false)
#define SERIAL_RATE 115200

#else

#define DEBUG(s, x)
#define DEBUGX(s, x)
#define DEBUGS(s)
#define SERIAL_RATE 31250

#endif

#define SERIAL2_RATE 115200