#include <Wire.h>
#include <Adafruit_DRV2605.h>  
#include <math.h>
#include <nvs_flash.h>
#include <esp_wifi.h>
#include "I2SManager.h"

//AUDIO FILES
#include "ExcuseMe.h"
#include "ThankYou.h"
#include "BluetoothC.h"
#include "Disconnected.h"
#include "PressAgain.h"
#include "Pairing.h"
#include "RequestSent.h"
#include "RequestReceived.h"
#include "Emergency.h"

Adafruit_DRV2605 drv;
I2SManager i2s;

// PIN VARIABLES
const uint8_t ButtonPin = 1;
const uint8_t MuteToggle = 2;
const uint8_t ModeToggle = 10;
const uint8_t AmpSD = 3;

// I2S AND I2C 
#define SDA_PIN 8
#define SCL_PIN 9

// BUTTON VARIABLES
bool butPressed = false;
bool longPressTriggered = false; 
bool debouncePass = false;
bool butState = false;

//SWITCH VARIABLES
bool muteMode = true;
bool buttonMode = false;
bool justSwitched=false;
bool justConnected=false;

//CACHED VARIABLES
bool cancelation=false;
bool advertised = false;
bool isBuzzing=false;
unsigned long prevTime = 0;


// CONSTANT VARIABLES
const unsigned long shortThreshold = 600;
const unsigned long longPressThreshold = 1650;
const unsigned long debounce = 50;
const uint8_t shortBuzz = 14;
const uint8_t mediumBuzzRamp = 70;
const uint8_t mediumBuzzCut = 64;
const uint8_t blueToothWaiting=52;


// Non-blocking Audio State
bool isFilePlaying = false;
bool alreadyPlayed = false;
const uint8_t* currentAudioData = NULL;
uint32_t currentAudioLen = 0;
uint32_t currentAudioOffset = 0;

// CONFIRMATION STATE 
unsigned long intervalTrack=0;
unsigned long pressTracker=0;
const unsigned long voiceNoteThreshold=5000;
const unsigned long emergencyThreshold=3750;

const unsigned long shortInterval=350;
const unsigned long mediumInterval=750;
const unsigned long longInterval=1500;

const unsigned long stopBuzzing=60000;
unsigned long buzzingStart=0;

//FUNCTION DECLERATIONS
void vibration(uint8_t effect);
void playAudioNonBlock(const uint8_t* data, uint32_t len);
void stopAudio();
void playAudioBlock(const uint8_t* audioData, uint32_t audioLen);

void pulsingBuzz(uint8_t effect, unsigned long interval, uint8_t multiple);
void updateBuzz(uint8_t effect, unsigned long interval, uint8_t multiple);
void vibration(uint8_t effect);




//BLUETOOTH SETUP
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

BLEServer *pServer = NULL;
BLECharacteristic *pCharacteristic = NULL;
bool deviceConnected = false;

// Unique IDs for the Service and Characteristic
#define SERVICE_UUID           "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
#define CHARACTERISTIC_UUID_TX "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"
#define CHARACTERISTIC_UUID_RX "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"

class MyServerCallbacks: public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) { 
        deviceConnected = true;
        pServer->updateConnParams(pServer->getConnId(), 0x06, 0x12, 0, 100);
        pServer->getAdvertising()->stop();
    }
    void onDisconnect(BLEServer* pServer) { 
        deviceConnected = false;
        advertised = false;
    }
};

class MyCallbacks: public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic *pCharacteristic) {
        String rxValue = pCharacteristic->getValue().c_str();
        if (rxValue == "Accepted") {
          if(muteMode){
            pulsingBuzz(shortBuzz,shortInterval,3);
          }
          else{
            vibration(shortBuzz);
            playAudioNonBlock(requestR_data,requestR_len);
          }
            
        }
        else if (rxValue == "Nearby") {
            vibration(shortBuzz);
        }
    }
};



// SETUP ----------------------------------------------------------------------------------------------------------------------------------------------
void setup() {

  setCpuFrequencyMhz(80);

  esp_wifi_stop();
  esp_wifi_deinit();

  //GENERAL SETUP
  pinMode(ButtonPin, INPUT); //Button Input
  pinMode(MuteToggle, INPUT); //Mute Toggle
  pinMode(AmpSD, OUTPUT); //Amp switch 
  pinMode(ModeToggle, INPUT); //Mode Toggle
  Serial.begin(115200);

  if (!drv.begin()) {
    Serial.println("Could not find DRV2605");
    while (1) delay(10);
  }
  drv.selectLibrary(1);
  drv.setMode(DRV2605_MODE_INTTRIG); 
  
  i2s.setupSpeaker();
  i2s_channel_disable(i2s.tx_handle);

  //BLUETOOTH SETUP  
  
  esp_err_t ret = nvs_flash_init(); 
  
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    nvs_flash_erase();
    nvs_flash_init();
  }


  BLEDevice::init("C3_Supermini_BLE"); // Create the BLE Device
  BLEDevice::setMTU(247);
  pServer = BLEDevice::createServer(); // Create the BLE Server
  pServer->setCallbacks(new MyServerCallbacks());
  BLEService *pService = pServer->createService(SERVICE_UUID); // Create the BLE Service

  pCharacteristic = pService->createCharacteristic(
                      CHARACTERISTIC_UUID_TX,
                      BLECharacteristic::PROPERTY_NOTIFY
                    );

  BLECharacteristic *pRxCharacteristic = pService->createCharacteristic(
                                         CHARACTERISTIC_UUID_RX,
                                         BLECharacteristic::PROPERTY_WRITE
                                       );  
  
  pRxCharacteristic->setCallbacks(new MyCallbacks());
  
  pService->start(); // Start the service
  Serial.println("Waiting for a client connection...");
}


// MAIN LOOP ------------------------------------------------------------------------------------------------------------------------------------------

void loop() {
  buttonMode=digitalRead(ModeToggle);
  

  //APP
  if (buttonMode==false) {

    if(!advertised){
       pServer->getAdvertising()->start();
       advertised=true;
    }
    
    if(!justSwitched){
      buzzingStart=millis();
      justSwitched=true;
      alreadyPlayed=false;
    }

    appMode();
  }

  //STANDALONE
  else {

    if(justSwitched){
      vibration(mediumBuzzCut);
      justSwitched=false;
      justConnected=false;
      alreadyPlayed=false;

      pServer->getAdvertising()->stop();
      advertised=false;
      stopAudio();
    }

    if(deviceConnected){
      pServer->disconnect(pServer->getConnId());
      advertised=false;

      if(!muteMode){
        playAudioBlock(disconnected_data, disconnected_len);
      }
      deviceConnected=false;
    }
    standaloneMode();
  }

  //Main Loop only checks the mode switch
  delay(5);
}


// STANDALONE ------------------------------------------------------------------------------------------------------------------------------------------
void standaloneMode(){

  butPressed = digitalRead(ButtonPin);
  
  if (butPressed == true && butState == false) {
      prevTime = millis();
      butState = true;
      longPressTriggered = false; // Reset for this new press cycle
  }

  //LONG PRESS CHECK
  else if (butPressed == true && butState == true) {
    if (!longPressTriggered && (millis() - prevTime > shortThreshold)) {
      vibration(shortBuzz);
      longPressTriggered = true;
      playAudioBlock(thank_data, thank_len);
    }

    else if (!longPressTriggered && (millis() - prevTime > debounce)) {
      debouncePass= true;
    }
  }

  //RELEASE
  else if (butPressed == false && butState == true) {
    if (!longPressTriggered && debouncePass) {
      vibration(shortBuzz);
      playAudioBlock(excuse_data, excuse_len);
      }
      butState = false;
      debouncePass=false;
      prevTime = 0;
    }
  
  //esp_sleep_enable_timer_wakeup(50000); UNCOMMENT ONCE FINISHED
  //esp_light_sleep_start();
}

// APP ------------------------------------------------------------------------------------------------------------------------------------------
void appMode(){

  muteMode = digitalRead(MuteToggle);

  //SPEAKER AUDIO UPDATER
  updateAudio();
  updateBuzz(0,0,0);

  if (muteMode && isFilePlaying) {
    stopAudio();
  }

  //DEVICE CONNECTION

  if (deviceConnected) {
    //Just Connected
    if(!justConnected){
      justConnected=true;
      vibration(shortBuzz);

      if(!muteMode){
        playAudioNonBlock(bluetoothC_data, bluetoothC_len);
      }
    }

    butPressed = digitalRead(ButtonPin);

    // Initial Press
    if (butPressed && !butState && !cancelation) {
      prevTime = millis();
      butState = true;
      longPressTriggered = false; // Reset for this new press cycle
      }

    //LONG PRESS CHECK
    else if (butPressed && butState && !cancelation) {
      if (!longPressTriggered && (millis() - prevTime > longPressThreshold)) {
        longPressTriggered = true;
        longPress();
      }

      else if (!longPressTriggered && (millis() - prevTime > debounce)) {
        debouncePass= true;
      }
    }

    //RELEASE
    else if (!butPressed && butState) {
      if(cancelation){
        cancelation=false;
      }
      butState = false;
      prevTime = 0;

      if (!longPressTriggered && debouncePass) {
          shortPress();
      }
      debouncePass=false;
    }     
  }

  else if(justConnected && !deviceConnected){
    justConnected=false;
    vibration(mediumBuzzCut);
    buzzingStart=millis();

    if(!muteMode){
      playAudioNonBlock(disconnected_data,disconnected_len); 
    }

  }

  //APP MODE BUT NOT CONNECTED
  else{

    justConnected=false;


    if(millis()-buzzingStart<stopBuzzing){
      if(muteMode || (alreadyPlayed && !isFilePlaying)){
        pulsingBuzz(blueToothWaiting,longInterval,0);
      }
      else if(!muteMode && !isFilePlaying && !alreadyPlayed){
        playAudioNonBlock(pairing_data,pairing_len);
        alreadyPlayed=true;
      }
    }

    else{
      butPressed=digitalRead(ButtonPin);
      if(butPressed){
        buzzingStart=millis();
      }
    }


  }
}



//BUTTON PRESSES APP MODE ----------------------------------------------------------------------------------------------------------------------------------
void shortPress() {

  stopAudio();
  pressTracker=millis();
  
  bool alreadyPressed=false;

  while(millis()-pressTracker<voiceNoteThreshold){
    if(millis()-pressTracker>500){
      butPressed=digitalRead(ButtonPin);
    }
    else{
      butPressed=false;
      longPressTriggered=false;
    }

    updateAudio();

    if(muteMode || (alreadyPressed && !isFilePlaying)){
      pulsingBuzz(shortBuzz,mediumInterval,0);
    }
    else if(!muteMode && !isFilePlaying && !alreadyPressed){
      vibration(shortBuzz);
      playAudioNonBlock(pressAgain_data, pressAgain_len);
      alreadyPressed=true;
    }


    //BUTTON-------------------------------------------------
    if (butPressed == true && butState == false) {
      prevTime = millis();
      butState = true;
      longPressTriggered = false;
    }

    //LONG PRESS CHECK
    else if (butPressed == true && butState == true) {
      if (!longPressTriggered && (millis() - prevTime > shortThreshold)) {
        longPressTriggered = true;
        stopAudio();
        break;
        //break out of calling help mode
      }

      else if (!longPressTriggered && (millis() - prevTime > debounce)) {
        debouncePass= true;
      }
    }

    //RELEASE
    else if (butPressed == false && butState == true) {
      if (!longPressTriggered && debouncePass) {
        debouncePass=false;
        stopAudio();
        recordAudio();
        break;
      }
    }
  }


  if(!longPressTriggered){
    drv.stop();
    pCharacteristic->setValue("Request Sent");
    pCharacteristic->notify();

    if(muteMode){
      vibration(mediumBuzzRamp); // Feedback: sent
    }
    else{
      vibration(shortBuzz);
      playAudioNonBlock(request_data,request_len);
    }

  }

  else{
    cancelation=true;
  }
}


void longPress(){

  pressTracker=millis();


  while(millis()-pressTracker<emergencyThreshold){
    if(millis()-pressTracker>500){
      butPressed=digitalRead(ButtonPin);
      if(!butPressed){
        butState=false;
      }
    }
    else{
      butPressed=false;
    }

    pulsingBuzz(mediumBuzzCut,mediumInterval,0);
    
    //BUTTON-------------------------------------------------
    if(butPressed && !butState){
      cancelation=true;
      pressTracker=0;
      butState=true;
      Serial.println("canceled");
      return;
    }
  }
  
  pCharacteristic->setValue("SOS_ALERT");
  pCharacteristic->notify();

  if(muteMode){
    drv.stop();
    pulsingBuzz(mediumBuzzRamp,longInterval,3);
  }

  else{
    playAudioNonBlock(emergency_data,emergency_len);
  }

}








//MIC ----------------------------------------------------------------------------------------------------------------------------------
void recordAudio(){

  i2s.switchToMic();
  const unsigned long recordMax = 10000;
  unsigned long recordStart = millis();

  static uint8_t chunkBuf[196];
  static int32_t raw[64];
  size_t chunkPos = 0;
  size_t bytes_read = 0;

  butState = false;
  prevTime = 0;

  pCharacteristic->setValue("REC_START");
  pCharacteristic->notify();

  while (millis() - recordStart < recordMax) {

    if (!deviceConnected) break;
    // Check for early stop press
    butPressed = digitalRead(ButtonPin);
    if (butPressed && !butState) {
      prevTime = millis();
      butState = true;
    } else if (!butPressed && butState) {
      butState = false;
      if (millis() - prevTime > debounce) {
        break; // Short press = stop early
      }
    }

    // Read from mic
    i2s_channel_read(i2s.rx_handle, raw, sizeof(raw), &bytes_read, 10);
    int samples = bytes_read / sizeof(int32_t);

    for (int i = 0; i < samples; i +=2) {  // step 2 instead of 1
      int16_t sample = (int16_t)(raw[i] >> 12);
      chunkBuf[chunkPos++] = sample & 0xFF;
      chunkBuf[chunkPos++] = (sample >> 8) & 0xFF;

      // Buffer full, send chunk
      if (chunkPos >= sizeof(chunkBuf)) {
        pCharacteristic->setValue(chunkBuf, sizeof(chunkBuf));
        pCharacteristic->notify();
        chunkPos = 0;
        delay(5);
      }
    }
  }

  // Signal end
  pCharacteristic->setValue("REC_END");
  pCharacteristic->notify();

  i2s.switchToSpeaker();

  butState = false;
  prevTime = 0;
  debouncePass = false;
  longPressTriggered = false;

}


//SPEAKER------------------------------------------------------------------------------------------------------------------------------------
void playAudioBlock(const uint8_t* audioData, uint32_t audioLen) {
  i2s_channel_enable(i2s.tx_handle);
  digitalWrite(AmpSD, HIGH);
  delay(25);

  const size_t chunkSize = 512;
  static int16_t boostBuf[chunkSize / 2];
  size_t offset = 44;

  while (offset < audioLen) {
    size_t toRead = min(chunkSize, (size_t)(audioLen - offset));
    size_t numSamples = toRead / 2;

    for (size_t i = 0; i < numSamples; i++) {
      int16_t sample;
      memcpy(&sample, audioData + offset + i * 2, 2);
      int32_t boosted = (int32_t)sample * 1.5;  // adjust multiplier for volume
      boostBuf[i] = (int16_t)constrain(boosted, -32768, 32767);
    }

    size_t bytes_written = 0;
    i2s_channel_write(i2s.tx_handle, boostBuf, numSamples * 2, &bytes_written, portMAX_DELAY);
    offset += toRead;
  }

  digitalWrite(AmpSD, LOW);
  i2s_channel_disable(i2s.tx_handle);
}



//NON-BLOCKING AUDIO

void playAudioNonBlock(const uint8_t* data, uint32_t len) {
  if (isFilePlaying) stopAudio(); // Kill current sound if overlapping

  i2s_channel_enable(i2s.tx_handle);
  digitalWrite(AmpSD, HIGH);
  
  currentAudioData = data;
  currentAudioLen = len;
  currentAudioOffset = 44; // Skip WAV header
  isFilePlaying = true;
}

void stopAudio() {
    if (!isFilePlaying) return;

    isFilePlaying = false;
    // Order matters: disable data stream first, then pull SD low
    if (i2s.tx_handle != NULL) {
        i2s_channel_disable(i2s.tx_handle); 
    } 
    digitalWrite(AmpSD, LOW); 
    
    currentAudioData = NULL;
    currentAudioOffset = 0;
}

void updateAudio() {
    if (!isFilePlaying) return;

    const size_t chunkSize = 512;
    static int16_t boostBuf[chunkSize / 2]; // static = heap not stack
    
    size_t remaining = currentAudioLen - currentAudioOffset;
    
    if (remaining < 2) {
        stopAudio();
        return;
    }

    size_t toRead = min(chunkSize, remaining);
    size_t numSamples = toRead / 2;

    for (size_t i = 0; i < numSamples; i++) {
        int16_t sample;
        memcpy(&sample, currentAudioData + currentAudioOffset + i * 2, 2);
        int32_t boosted = (int32_t)sample * 1.5; 
        boostBuf[i] = (int16_t)constrain(boosted, -32768, 32767);
    }

    size_t bytes_written = 0;
    i2s_channel_write(i2s.tx_handle, boostBuf, numSamples * 2, &bytes_written, 0);
    currentAudioOffset += bytes_written;
}

// TRIGGER FUNCTIONS ----------------------------------------------------------------------------------------------------------------------------------

void pulsingBuzz(uint8_t effect, unsigned long interval, uint8_t multiple){

  static uint8_t lastEffect=effect;
  bool justBuzz=false;
  
  if(effect!=lastEffect){
    lastEffect=effect;
    justBuzz=true;
  }


  if(intervalTrack==0){
    intervalTrack=millis();
  }
  
  else if((millis()-intervalTrack>interval) || justBuzz){
    vibration(effect);
    intervalTrack=0;
  }

  if(multiple==0){
    isBuzzing=false;
    return;
  }

  isBuzzing=true;
  updateBuzz(effect,interval,multiple);

}

void updateBuzz(uint8_t effect, unsigned long interval, uint8_t multiple){
  if(!isBuzzing) return;

  static uint8_t counter=0;
  static uint8_t effectA=0;
  static unsigned long intervalA=0;
  static uint8_t multipleA=0; 

  if(multiple!=0){
    effectA=effect;
    intervalA=interval;
    multipleA=multiple-1;    
  }

  else{
    if(counter<multipleA){
      if(intervalTrack==0){
        intervalTrack=millis();
      } 

      else if(millis()-intervalTrack>intervalA){
        vibration(effectA);
        intervalTrack=0;
        counter++;
      }
    }

    else{
      intervalTrack=millis();
      counter=0;
      isBuzzing=false;
    }
  }
}



void vibration(uint8_t effect) {
  Serial.println("yes");
  drv.setWaveform(0, effect);
  drv.setWaveform(1, 0);
  drv.go();
}


/*NOTIFY PACKETS BEING SENT
"REC_START" — voice recording started
"REC_END" — voice recording finished
"Request Sent" — short press confirmed, help requested
"Long Pressed" — long press triggered (emergency)
Raw binary chunks — audio data during recording
*/