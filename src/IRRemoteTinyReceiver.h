#ifndef IRRemoteTinyReceiver_h
#define IRRemoteTinyReceiver_h

#include <stdint.h>

/*
 * Helper macro for getting a macro definition as string
 */
#if !defined(STR_HELPER)
#define STR_HELPER(x) #x
#define STR(x) STR_HELPER(x)
#endif

#define IR_RECEIVE_PIN    32

class IRRemoteTinyReceiver
{

public:
  enum KeyResult
  {
    KEY_NULL,        ///< No key press
    KEY_DOWN,        ///< Switch is detected as active (down)
    KEY_UP,          ///< Switch is detected as inactive (up)
    KEY_PRESS,       ///< Simple press, or a repeated press sequence if enableRepeatResult(false) (default)
    KEY_DPRESS,      ///< Double press
    KEY_LONGPRESS,   ///< Long press
    KEY_RPTPRESS     ///< Repeated key press (only if enableRepeatResult(true))
  };

  typedef struct
  {
    uint16_t  address_;
    uint8_t   command_;
    uint8_t   value_;        ///< Identifier for this key, returned using getKey()
  } IRRemoteRxKeyValue;

  IRRemoteTinyReceiver (IRRemoteRxKeyValue* kv, uint8_t kvSize);

  static void Init();
  void Update();

  KeyResult read();
  uint8_t getKey();

  uint8_t FindKey (uint16_t  address, uint8_t command);

private:
  IRRemoteRxKeyValue* kv_;
  uint8_t kvSize_;
public:
  static KeyResult keyResult_;
  static uint8_t   lastKey_;
};

#endif // IRRemoteTinyReceiver_h