/*
Multi BLE Sensor - Richard Hedderly 2019

Based on heart sensor code by Andreas Spiess which was based on a Neil
Kolban example.

Based on Neil Kolban example for IDF:
https://github.com/nkolban/esp32-snippets/blob/master/cpp_utils/tests/BLE%20Tests/SampleServer.cpp
Ported to Arduino ESP32 by Evandro Copercini
updates by chegewara
heavily modified by Alexander Pruss
*/

#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEServer.h>
#include <BLE2902.h> 
#include "debounce.h"

#define POWER
#define CADENCE
#undef TEST

#ifndef TEST
const uint32_t rotationDetectPin = 23;
#endif
const uint32_t ledPin = 2;
const uint32_t incPin = 0;
Debounce incButton(incPin, LOW);
const uint8_t defaultRotationValue = 1;
const uint32_t rotationDebounceTime = 100;
const uint32_t minimumUpdateTime = 250;
uint32_t lastRotationDuration = 0;

uint32_t lastReportedRotationTime = 0;
uint16_t lastCrankRevolution = 0; // in 1024ths of a second!
uint16_t crankRevolution = 0;

uint16_t prevRotationDetect = 0;
uint32_t lastRotationDetectTime = 0;
uint32_t lastUpdateTime = 0;

#define NUM_FRICTIONS 8
// friction model: force = -frictionCoeff * angularVelocity
uint32_t frictionCoeffX10[] = { 141, 191, 203, 220, 284, 370, 382, 384 };

const uint32_t flashPauseDuration = 200;
const uint32_t flashPlayDuration = 200;
const uint32_t flashDelayDuration = 2000;

char* flashPattern = NULL;
uint32_t flashStartTime = 0;

char flashPatterns[][NUM_FRICTIONS*2+2] = { "10D", "1010D", "101010D", "10101010D", "1010101010D", "101010101010D", "10101010101010D", "1010101010101010D" };

byte frictionValue = 5;
#define RADIUSX1000 145 // radius of crank in meters * 1000 (= radius of crank in mm)

byte cscmeasurement[5] = { 2 };
byte powermeasurement[6] = { 0x20 }; // include crank revolution data
byte cscfeature = 2;
byte powerfeature = 8; // crank revolution
byte powerlocation = 6; // right crank

bool _BLEClientConnected = false;

#define ID(x) (BLEUUID((uint16_t)(x)))

#ifdef CADENCE
#define speedService ID(0x1816)
BLECharacteristic cscMeasurementCharacteristics(ID(0x2A5B), BLECharacteristic::PROPERTY_NOTIFY);
BLECharacteristic cscFeatureCharacteristics(ID(0x2A5C), BLECharacteristic::PROPERTY_READ);

BLEDescriptor cscMeasurementDescriptor(ID(0x2901));
BLEDescriptor cscFeatureDescriptor(ID(0x2901));
#endif

#ifdef POWER
#define powerService ID(0x1818)
BLECharacteristic powerMeasurementCharacteristics(ID(0x2A63), BLECharacteristic::PROPERTY_NOTIFY);
BLECharacteristic powerFeatureCharacteristics(ID(0x2A65), BLECharacteristic::PROPERTY_READ);
BLECharacteristic powerSensorLocationCharacteristics(ID(0x2A5D), BLECharacteristic::PROPERTY_READ);

BLEDescriptor powerMeasurementDescriptor(ID(0x2901));
BLEDescriptor powerFeatureDescriptor(ID(0x2901));
BLEDescriptor powerSensorLocationDescriptor(ID(0x2901));
#endif

class MyServerCallbacks:public BLEServerCallbacks
{
  void onConnect(BLEServer* pServer)
  {
    Serial.println("connected");
    _BLEClientConnected = true;
  };

  void onDisconnect(BLEServer* pServer)
  {
    Serial.println("disconnected");
    _BLEClientConnected = false;
  }
};

void InitBLE ()
{
  BLEDevice::init("Exercise Bike Sensor");
  BLEServer *pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());

#ifdef CADENCE
  BLEService *pSpeed = pServer->createService(speedService);
  pSpeed->addCharacteristic(&cscMeasurementCharacteristics);
  pSpeed->addCharacteristic(&cscFeatureCharacteristics);

  cscMeasurementDescriptor.setValue("CSC Measurement");
 // cscMeasurementCharacteristics.addDescriptor(&cscMeasurementDescriptor);
  cscMeasurementCharacteristics.addDescriptor(new BLE2902());

  cscFeatureDescriptor.setValue("CSC Feature");
  cscFeatureCharacteristics.addDescriptor(&cscFeatureDescriptor);

  pServer->getAdvertising()->addServiceUUID(speedService);

  pSpeed->start();
#endif

#ifdef POWER
  BLEService *pPower = pServer->createService(powerService);
  pPower->addCharacteristic(&powerMeasurementCharacteristics);
  pPower->addCharacteristic(&powerFeatureCharacteristics);
  pPower->addCharacteristic(&powerSensorLocationCharacteristics);

  powerMeasurementDescriptor.setValue("Power Measurement");
 // powerMeasurementCharacteristics.addDescriptor(&powerMeasurementDescriptor);
  powerMeasurementCharacteristics.addDescriptor(new BLE2902());

  powerFeatureDescriptor.setValue("Power Feature");
  powerFeatureCharacteristics.addDescriptor(&powerFeatureDescriptor);

  powerSensorLocationDescriptor.setValue("Power Sensor Location");
  powerSensorLocationCharacteristics.addDescriptor(&powerSensorLocationDescriptor);

  pServer->getAdvertising()->addServiceUUID(powerService);

  pPower->start();
#endif

  pServer->getAdvertising()->setScanResponse(true);
  pServer->getAdvertising()->setMinPreferred(0x06);  // functions that help with iPhone connections issue
  pServer->getAdvertising()->setMinPreferred(0x12);
  pServer->getAdvertising()->start();

  
}

void setFriction(uint32_t value) {  
    frictionValue = value;
    flashPattern = flashPatterns[value];
    flashStartTime = millis();
}

void flashPlay() {
  if (flashPattern == NULL) {
    digitalWrite(ledPin, 0);
    return;
  }
  char* p = flashPattern;
  uint32_t currentTime = millis() - flashStartTime;
  bool active = false;
  uint32_t duration = 0;
  uint32_t t = 0;
  while(*p) {
    switch(*p) {
      case '0':
        active = false;
        duration = flashPauseDuration;
        break;
      case '1':
        active = true;
        duration = flashPlayDuration;
        break;
      case 'D':
        active = false;
        duration = flashDelayDuration;
        break;
      default:
        duration = 0;
        active = false;
    }
    if (currentTime < t + duration) {
      digitalWrite(ledPin, active);
      return;
    }
    t += duration;
    p++;
  }  
  flashStartTime = millis(); // rewind
}

void setup ()
{
  Serial.begin(115200);
  Serial.println("BLEBike start");
  InitBLE();
#ifndef TEST  
  pinMode(rotationDetectPin, INPUT); 
#endif  
  pinMode(ledPin, OUTPUT);
  digitalWrite(ledPin, 1);
  pinMode(0, INPUT);
  setFriction(0);
}

uint32_t calculatePower(uint32_t revTimeMillis) {
  // typical: angularVelocity = 6 rad / sec
  //          distance = angularVelocity * r * time = 0.87m
  //          frictionalForce = 6 * 25 = 150N
  //          workPerSec = 150 * 0.87 = 130W
  //
  // angularVelocity = 2 * pi / revTime
  // distance = angularVelocity * r * dt
  // force = angularVelocity * frictionCoeff
  // power = force * distance / dt
  //       = angularVelocity * frictionCoeff * distance / dt
  //       = (2 * pi)^2 * frictionCoeff * r / revTime^2 
  // power = (uint32_t)( (2 * PI) * (2 * PI) * RADIUSX1000 + 0.5) * frictionCoeffX10[frictionValue] / 10000 * 1000^2 / revTimeMillis^2
  if (revTimeMillis == 0)
    return 0;
  return (uint32_t)( (2 * PI) * (2 * PI) * RADIUSX1000 * 100 + 0.5) * frictionCoeffX10[frictionValue] / revTimeMillis / revTimeMillis;
}

inline uint16_t getTime1024ths(uint32_t ms) 
{
  // TODO: there will be a glitch every 4.66 hours
  ms &= 0x00FFFFFFul;
  return ms * 128/125;
}

void loop ()
{
  uint8_t rotationDetect;
  uint32_t ms;
  uint8_t needUpdate;
  uint32_t fromLastRotation;

  ms = millis();

#ifdef TEST
  rotationDetect = (ms % 1000) < 200;
#else  
  rotationDetect = digitalRead(rotationDetectPin) ^ defaultRotationValue;
#endif
  if (incButton.getEvent() == DEBOUNCE_PRESSED) {
    setFriction((frictionValue + 1) % NUM_FRICTIONS);
  }
  flashPlay();

  needUpdate = 0;

  uint32_t curRotationDuration = ms - lastReportedRotationTime;
  fromLastRotation = ms - lastRotationDetectTime;

  if (rotationDetect && ! prevRotationDetect && fromLastRotation >= rotationDebounceTime) {
    Serial.println("rotation detected at "+String(fromLastRotation));
    lastRotationDuration = curRotationDuration;
    lastReportedRotationTime = ms;
    crankRevolution++;
    lastCrankRevolution = getTime1024ths(ms);
    needUpdate = 1;
  }
  else {
    if (lastRotationDuration < curRotationDuration)
      lastRotationDuration = curRotationDuration;
  }
  if (rotationDetect)
    lastRotationDetectTime = ms;

  prevRotationDetect = rotationDetect;

  uint32_t power = crankRevolution >= 2 ? calculatePower(lastRotationDuration) : 0;

  if (power > 0x7FFF)
    power = 0x7FFF;

  if (ms - lastUpdateTime >= minimumUpdateTime) 
    needUpdate = 1;

  if (! needUpdate)
    return;

  if ( 0 && !_BLEClientConnected)
    return;

  Serial.print(power);
  Serial.print(" ");
  Serial.print(lastRotationDuration);
  Serial.print(" ");
  Serial.print(crankRevolution);
  Serial.print(" ");
  Serial.println(lastCrankRevolution); 

  lastUpdateTime = ms;

  cscmeasurement[1] = crankRevolution;
  cscmeasurement[2] = crankRevolution >> 8;

  cscmeasurement[3] = lastCrankRevolution;
  cscmeasurement[4] = lastCrankRevolution >> 8;

#ifdef CADENCE
  cscFeatureCharacteristics.setValue(&cscfeature, 1);

  cscMeasurementCharacteristics.setValue(cscmeasurement, sizeof(cscmeasurement));
  cscMeasurementCharacteristics.notify();
#endif  

  powermeasurement[1] = 0; 
  powermeasurement[2] = power;
  powermeasurement[3] = power >> 8;
  powermeasurement[4] = crankRevolution;
  powermeasurement[5] = crankRevolution >> 8;

#ifdef POWER
  powerFeatureCharacteristics.setValue(&powerfeature, 1);
//  powerFeatureCharacteristics.notify();
  powerSensorLocationCharacteristics.setValue(&powerlocation, 1);  
//  powerSensorLocationCharacteristics.notify(); 

  powerMeasurementCharacteristics.setValue(powermeasurement, sizeof(powermeasurement));
  powerMeasurementCharacteristics.notify();
#endif  
}

