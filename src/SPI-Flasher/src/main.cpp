#include <MD5Builder.h>
#include <SPIMemory.h>
#include "base64.hpp"

typedef int32_t messagelen_t;  // NOTE: Sign is needed for -1 output by Serial.read()
const uint16_t DATA_CHUNK_SIZE = 2048;
const messagelen_t MESSAGE_MAX_SIZE = (int)(DATA_CHUNK_SIZE / .75) + 5;  // n b64 chars <= .75n bytes
const unsigned long INITIAL_SERIAL_BAUD_RATE = 9600;

// ESP -> Host prefixes: ! = Error | @ = MD5 hash to verify | # = Information

// Baud = ! | Erase = @ | Write = # | File Size = $ | Flash Data = % | Do Erase = ^ | Do Flash = & | Reset State = * | Send Flash Info = (
enum states { NONE, SET_BAUD, SET_ERASE, SET_WRITE, SET_FILE_SIZE, RECV_FLASH_DATA, DO_ERASE, DO_FLASH, RESET_STATE, SEND_FLASH_INFO };
states state = NONE;

// ----
// Function signatures
void resetState();

void handleSerialMessage();
void handleData();

void handleGetFlashInfo();
void handleSetBaud();
void handleSetErase();
void handleSetWrite();
void handleSetFileSize();
void handleDoFlash();

void flashChip(uint32_t fileSize, bool doMock);
void eraseChip();
void writeData(byte data[], messagelen_t dataLength);

String md5(byte byteArray[], uint32_t len);
uint32_t byteArrayToInt(byte byteArray[], messagelen_t length);
void byteArrayToHex(byte array[], unsigned int length, char output[]);
unsigned int b64ToBytes(unsigned char * toDecode, unsigned int length, byte * output);
uint32_t b64ToInt(byte * toDecode, unsigned int length, byte * buffer);

// ----
// Internal objects and variables
MD5Builder md5Builder;
SPIFlash flash;
uint32_t flashSize;
uint32_t currentFlashOffset = 0;

bool shouldDoErase;
bool shouldDoWrite;
uint32_t fileSize;

byte receivedMessage[MESSAGE_MAX_SIZE];
messagelen_t messageLength = 0;
messagelen_t currRecvDataPos = 0;
bool dataNeedsHandling = false;

byte dataBuffer[DATA_CHUNK_SIZE];
uint32_t dataLength = 0;

// ------------
void setup() {
  Serial.begin(INITIAL_SERIAL_BAUD_RATE);

  while (!Serial) { delay(5); }

  flash.begin();
  flashSize = flash.getCapacity();
}

// ----
void loop() {
  handleSerialMessage();

  if (dataNeedsHandling) {
    handleData();
  }

  delay(1);  // ESP beauty rest; they REALLY do not like busy loops
}

void resetState() {
  delay(1000);  // If it takes the host longer than one second to read remaining messages, oh well!

  Serial.end();
  Serial.begin(INITIAL_SERIAL_BAUD_RATE);
  state = NONE;
  shouldDoErase = false;
  shouldDoWrite = false;
  fileSize = 0;
  currRecvDataPos = 0;
  messageLength = 0;
  dataNeedsHandling = false;
}

// ----
void handleSerialMessage() {
  const static char endMarker = '\n';
  int_least16_t rcvData;  // Signed to make sure we can read -1

  while (Serial.available() > 0) {
    rcvData = Serial.read();

    switch (rcvData) {
      case -1: break;  // Nothing received; this should never happen

      case '!': state = SET_BAUD; break;
      case '@': state = SET_ERASE; break;
      case '#': state = SET_WRITE; break;
      case '$': state = SET_FILE_SIZE; break;
      case '%': state = RECV_FLASH_DATA; break;
      case '^': state = DO_ERASE; break;
      case '&': state = DO_FLASH; break;
      case '*': state = RESET_STATE; break;
      case '(': state = SEND_FLASH_INFO; break;

      case endMarker:
        messageLength = currRecvDataPos;
        currRecvDataPos = 0;
        dataNeedsHandling = true;
        break;

      default:
        receivedMessage[currRecvDataPos] = rcvData;
        currRecvDataPos++;

        if (currRecvDataPos > MESSAGE_MAX_SIZE) {
          Serial.println(F("!ERROR: Message overflowed buffer; did you mean to send '&' (DO_FLASH)?"));
          resetState();
        }
    }
  }
}

// ----
void handleData() {
  switch (state) {
    case SET_BAUD: handleSetBaud(); break;
    case SET_ERASE: handleSetErase(); break;
    case SET_WRITE: handleSetWrite(); break;
    case SET_FILE_SIZE: handleSetFileSize(); break;

    case RECV_FLASH_DATA:
      dataLength = b64ToBytes(receivedMessage, messageLength, dataBuffer);

      if (dataLength == 0) {
        Serial.println("!ERROR: Data length was 0 after conversion from base64");
        resetState();
        return;
      }

      Serial.println('@' + md5(dataBuffer, dataLength));
      break;

    case DO_ERASE: eraseChip(); break;
    case DO_FLASH: handleDoFlash(); break;
    
    case RESET_STATE: resetState(); break;
    case SEND_FLASH_INFO: handleGetFlashInfo(); break;
    
    case NONE: break;
  }

  messageLength = 0;
  dataNeedsHandling = false;
}

void handleGetFlashInfo() {
  uint32_t JEDEC = flash.getJEDECID();
  if (!JEDEC) {
    Serial.println("!ERROR: Connection to flash failed; check wiring.");

  } else {
    Serial.print("#JEDEC ID: 0x"); Serial.println(JEDEC, HEX);
    Serial.print("#Man ID: 0x"); Serial.println(uint8_t(JEDEC >> 16), HEX);
    Serial.print("#Memory ID: 0x"); Serial.println(uint8_t(JEDEC >> 8), HEX);
    Serial.print("#Capacity: "); Serial.println(flashSize);
    Serial.print("#Max Pages: "); Serial.println(flash.getMaxPage());
  }
}

// ----
void handleSetBaud() {
  uint32_t baudRate = b64ToInt(receivedMessage, messageLength, dataBuffer);

    if (baudRate > 921600) {
      Serial.print("!ERROR: Invalid baudrate '");
      Serial.print(baudRate, HEX);
      Serial.println("'");

      resetState();
      return;
    }

    Serial.end();
    Serial.begin(baudRate);
}

void handleSetErase() { shouldDoErase = b64ToInt(receivedMessage, messageLength,  dataBuffer); }
void handleSetWrite() { shouldDoWrite = b64ToInt(receivedMessage, messageLength,  dataBuffer); }

void handleSetFileSize() {
  uint32_t readValue = b64ToInt(receivedMessage, messageLength, dataBuffer);

  if (readValue > flashSize) {
    Serial.println(F("!ERROR: File size exceeds flash size"));
    resetState();
    return;
  }

  fileSize = readValue;
}

void handleDoFlash() {
  writeData(dataBuffer, dataLength);
  dataLength = 0;
}

// ----
void eraseChip() {
  Serial.println(F("#Erasing chip..."));
  Serial.flush();

  int err;
  for (int i = 0; i < ceil(flashSize / 32768); i++) {
    // eraseBlock64K causes soft reset for some reason?
    flash.eraseBlock32K(32768 * i);

    err = flash.error();
    if (err != 0) {
      Serial.print(F("!ERROR: Flash error during erase in block at "));
      Serial.print(32768 * i);
      Serial.print(F(" | Err "));
      Serial.println(err);

      resetState();
      return;
    }

    delay(1);  // ESP beauty rest
  }

  Serial.println(F("#Chip erased"));
}

// ----
void writeData(byte data[], messagelen_t dataLength) {
  flash.writeByteArray(currentFlashOffset, data, dataLength);
  int flashErrNo = flash.error();

  if (flashErrNo != 0) {
    Serial.print(F("!ERROR: Flash error during write in page at "));
    Serial.print(currentFlashOffset);
    Serial.print(F(" : Err "));
    Serial.println(flashErrNo);

    resetState();
    return;
  
  } else {
    Serial.println(F("#W_OK"));
    Serial.flush();
    currentFlashOffset += dataLength;
  }

  return;
}

// ----
String md5(byte byteArray[], uint32_t len) {
  md5Builder.begin();

  uint8_t arrItem;
  for (uint32_t i = 0; i < len; i++) {
    arrItem = (uint8_t)byteArray[i];
    md5Builder.add(&arrItem, 1);
  }
  md5Builder.calculate();

  return md5Builder.toString();
}

// ----
uint32_t byteArrayToInt(byte byteArray[], messagelen_t length) {
  if (length == 0) { return 0; }

  uint32_t out = 0;
  for (messagelen_t i = 0; i < length; i++) {
    out += byteArray[i] << (i * 8);
  }

  return out;
}

// --
void byteArrayToHex(byte array[], unsigned int length, char output[]) {
  for (unsigned int i = 0; i < length; i++) {
        byte nib1 = (array[i] >> 4) & 0x0F;
        byte nib2 = (array[i] >> 0) & 0x0F;
        output[i*2 + 0] = nib1 < 0xA ? '0' + nib1  : 'A' + nib1  - 0xA;
        output[i*2 + 1] = nib2 < 0xA ? '0' + nib2  : 'A' + nib2  - 0xA;
    }
    output[length*2] = '\0';
}

// --
unsigned int b64ToBytes(unsigned char * toDecode, unsigned int length, byte * output) {
  return decode_base64(toDecode, length, output);
}

// --
uint32_t b64ToInt(unsigned char * toDecode, unsigned int length, byte buffer[]) {
  unsigned int outLength = decode_base64(toDecode, length, buffer);
  return byteArrayToInt(buffer, outLength);
}
