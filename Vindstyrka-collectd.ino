/**
 * ESP8266 SEN54/IKEA Vindsyrka + Sensirion SCD41 (CO2) +  MICS-5524 (CO) with collectd integration.
 *
 * Partly based on
 *  https://github.com/techniker/sen54_mqtt
 *  
 * Uses Sensirion I2C SEN5X Library
 *   https://github.com/Sensirion/arduino-i2c-sen5x
 *
 * Uses Sensirion I2C SCD4X Library
 *   https://github.com/Sensirion/arduino-i2c-scd4x
 *
 * Uses DFRobot MICS Library
 *   https://github.com/DFRobot/DFRobot_MICS/
 *
 * ESP8266 and all sensors powered by the Vindtyrkan
 * 
 **/

#include <Arduino.h>
#include <Ticker.h>
#include <ArduinoOTA.h>
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <ESP8266httpUpdate.h>
#include <SensirionI2CSen5x.h>
#include <Wire.h>
#include <WiFiUdp.h>
#include "collectd-protocol.h"

//Sensirion SCD41
#include <SensirionI2cScd4x.h>

//DFRobot MiCS-5524 Library
#include "DFRobot_MICS.h"

#define CALIBRATION_TIME   2    // Default calibration time is three minutes
#define ADC_PIN            A0   // Analog pin connected to the sensor's analog output
#define POWER_PIN          2    // Digital pin to power the sensor

//WebSerial
#include <ESPAsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <WebSerialLite.h>

// * Include settings
#include "config.h"

//Larger Serial buffer
#define SERIAL_BUFFER_SIZE 1024

// * Initiate WIFI client
WiFiClient client;

// * Initiate collectd UDP client
WiFiUDP udp_handler;

SensirionI2CSen5x sen5x;

SensirionI2cScd4x scd4x;

// WebSerial
// Initiate HTTP server
AsyncWebServer server(80);

//Read CO
DFRobot_MICS_ADC mics(ADC_PIN, 0);

unsigned long lastReadTime = 0;

String ProgramVersion  = "0.2";

double massConcentrationPm1p0=0.0, massConcentrationPm2p5=0.0, massConcentrationPm4p0=0.0, massConcentrationPm10p0=0.0;
double ambientHumidity=0.0, ambientTemperature=0.0, vocIndex=0.0;
double co2Concentration=0.0; //SCD41
double coConcentration=0.0; //MICS
double glb_rssi=0.0;

void SerPrintf(const char *format, ...) {
  char buffer[256]; 
  
  va_list args;
  va_start(args, format);
  vsnprintf(buffer, sizeof(buffer), format, args);
  va_end(args);

  Serial.print(buffer);
  WebSerial.print(buffer);
}

void SerPrintfLn(const char *format, ...) {
  char buffer[256]; 
  
  va_list args;
  va_start(args, format);
  vsnprintf(buffer, sizeof(buffer), format, args);
  va_end(args);

  // Uses the native println methods of each object
  Serial.println(buffer);
  WebSerial.println(buffer);
}

void SerPrintfLn(const String &message) {
  Serial.println(message);
  WebSerial.println(message);
}

/* Message callback of WebSerial */
void recvMsg(uint8_t *data, size_t len){
  WebSerial.println("Received Data...");
  String d = "";
  for(int i=0; i < len; i++){
    d += char(data[i]);
  }
  WebSerial.println(d);

  if (d == "REBOOT" ) {
    SerPrintfLn("Rebooting.");
    delay (500);
    ESP.restart();
  }
}

// The used commands use up to 48 bytes. On some Arduino's the default buffer
// space is not large enough
#define MAXBUF_REQUIREMENT 48

#if (defined(I2C_BUFFER_LENGTH) &&                 \
     (I2C_BUFFER_LENGTH >= MAXBUF_REQUIREMENT)) || \
    (defined(BUFFER_LENGTH) && BUFFER_LENGTH >= MAXBUF_REQUIREMENT)
#define USE_PRODUCT_INFO
#endif

//OTA callback update_started()
void update_started() {
  SerPrintfLn("HTTP update process started");
}

//OTA callback update_finished()
void update_finished() {
  SerPrintfLn("HTTP update process finished");
}

//OTA callback update_progress()
void update_progress(int cur, int total) {
  SerPrintf("HTTP update process at %d of %d bytes...\n", cur, total);
}

//OTA callback update_error()
void update_error(int err) {
  SerPrintf("HTTP update fatal error code %d\n", err);
}

//OTA CHeck
int checkForUpdates() {
    ESPhttpUpdate.setLedPin(LED_BUILTIN, LOW);

    // Add optional callback notifiers
    ESPhttpUpdate.onStart(update_started);
    ESPhttpUpdate.onEnd(update_finished);
    ESPhttpUpdate.onProgress(update_progress);
    ESPhttpUpdate.onError(update_error);
    
    SerPrintfLn("\nChecking for update from " + String(UpdateURL) + ". Current FW version " + String(FWVersion) + "\n");

    t_httpUpdate_return ret = ESPhttpUpdate.update(client, String(UpdateURL),String(FWVersion));

    switch (ret) {
      case HTTP_UPDATE_FAILED:
        SerPrintf("HTTP_UPDATE_FAILED Error (%d): %s\n", ESPhttpUpdate.getLastError(), ESPhttpUpdate.getLastErrorString().c_str());
        break;

      case HTTP_UPDATE_NO_UPDATES:
        SerPrintf("HTTP_UPDATE_NO_UPDATES");
        break;

      case HTTP_UPDATE_OK:
        SerPrintf("HTTP_UPDATE_OK");
        delay(5000);
        ESP.restart();
        break;
    }

return 0;
}

int connect_wifi (){
  int retries = 0;
  int wifiStatus = WiFi.status();

  WiFi.persistent(true);
  WiFi.mode(WIFI_STA);
  WiFi.begin( WifiSSID, WifiPass );

  while( wifiStatus != WL_CONNECTED ) {
    retries++;
    delay( 500 );
    wifiStatus = WiFi.status();
  }

 //Light sleep for WiFi
  WiFi.setSleepMode(WIFI_LIGHT_SLEEP);

return wifiStatus;
}

int ReadSens5x() {

  bool status = false;
  uint16_t error;

  float t_pm1 = 0.0f, t_pm2_5 = 0.0f, t_pm4 = 0.0f, t_pm10 = 0.0f;
  float t_hum = 0.0f, t_temp = 0.0f, t_voc = 0.0f, t_nox = 0.0f;

  SerPrintfLn("\nReading SENS54 sensor...");

  //Check for data
  error =  sen5x.readDataReady(status);

  if (error && !status) {
    SerPrintfLn("SENS54 data not ready to read sensor values");
    return 1;
  }

  // read data
  error = sen5x.readMeasuredValues(t_pm1, t_pm2_5, t_pm4, t_pm10, 
                                   t_hum, t_temp, t_voc, t_nox);
  if (error) {
    SerPrintfLn("Failed to read SENS54 values");
    return 1;
  }

  //floats to double as collectd expects
  massConcentrationPm1p0=(double) t_pm1;
  massConcentrationPm2p5=(double)t_pm2_5;
  massConcentrationPm4p0=(double)t_pm4;
  massConcentrationPm10p0=(double)t_pm10;
  ambientHumidity=(double)t_hum;
  ambientTemperature=(double)t_temp;
  vocIndex=(double)t_voc;

  SerPrintfLn("Temp: " + String(ambientTemperature) + " Hum: " + String(ambientHumidity) + " VOC: " + String(vocIndex) \
  + "\nPM1.0: " + String(massConcentrationPm1p0) \
  + " PM2.5: " + String(massConcentrationPm2p5) \
  + " PM4.0: " + String(massConcentrationPm4p0) \
  + " PM10.0: " + String(massConcentrationPm10p0));

  return 0;
}

int ReadScd4x() {

  bool status = false;
  uint16_t error; 

  uint16_t t_co2 = 0;
  float t_hum = 0.0f, t_temp = 0.0f;

  SerPrintfLn("\nReading SCD41 sensor...");

  error = scd4x.getDataReadyStatus(status);

  if (error && !status) {
    SerPrintfLn("SC41 Sensor data not ready to read sensor values");
    return 1;
  }

  error = scd4x.readMeasurement(t_co2, t_temp, t_hum);

  if (error) {
    SerPrintfLn("Failed to read SCD41 values");
    return 1;
  }

  SerPrintfLn("Temp: " + String(t_temp) + " Hum: " + String(ambientHumidity) + " CO2: " + String(t_co2) + "ppm");

  //floats to double as collectd expects
  co2Concentration=(double) t_co2;

  return 0;
}

int ReadMICS(){

  SerPrintfLn("\nReading MICS sensor...");

  mics.wakeUpMode();

  // Wait calibration time, do not care about millis overlow and few lost measurements
  if (lastReadTime < CALIBRATION_TIME*60000) {
    SerPrintfLn("MICS Sensor data not ready to read sensor values");
    return 1;
  }

  // Read raw ADC data from the sensor
  float t_adc = mics.getADCData(OX_MODE);

  // Read gas data from the sensor
  float t_co = mics.getGasData(CO);

  SerPrintfLn("RAW ADC: " + String(t_adc) + " CO: " + String(t_co) + "ppm\n");

  //floats to double as collectd expects
  coConcentration=(double) t_co;

  return 0; 
}

int getRSSI(){

  glb_rssi=0.0;
  glb_rssi= (double) WiFi.RSSI();
    
  SerPrintfLn("WiFi RSSI: " + String(glb_rssi) + "dBm");
   
  return 0; 
}

//send data to collectd
void send_data_to_collectd (void)
{
  //metric packet to send to collectd for detailed readings
  struct collectd_packet *packet = collectd_init_packet((char*)ESPName, 2048);

  SerPrintfLn("\nPreparing collected packet...");

  collectd_add_numeric(packet, TYPE_TIME, 0);
  collectd_add_string(packet, TYPE_PLUGIN,(char*) "sensor");
  collectd_add_string(packet, TYPE_TYPE,(char*) "gauge");

  //Temperature
  collectd_add_string(packet, TYPE_TYPE_INSTANCE, (char*) "temp");
  collectd_add_value(packet, COLLECTD_VALUETYPE_GAUGE, (double*) &ambientTemperature);

  //Humidity
  collectd_add_string(packet, TYPE_TYPE_INSTANCE, (char*) "hum");
  collectd_add_value(packet, COLLECTD_VALUETYPE_GAUGE,(double*) &ambientHumidity);

  //volatile organic compounds (VOCs) Index
  collectd_add_string(packet, TYPE_TYPE_INSTANCE, (char*) "voc");
  collectd_add_value(packet, COLLECTD_VALUETYPE_GAUGE, (double*) &vocIndex);

  //volatile organic compounds (VOCs) Index
  collectd_add_string(packet, TYPE_TYPE_INSTANCE, (char*) "voc");
  collectd_add_value(packet, COLLECTD_VALUETYPE_GAUGE, (double*) &vocIndex);

  //massConcentrationPm1p0
  collectd_add_string(packet, TYPE_TYPE_INSTANCE, (char*) "pm1p0");
  collectd_add_value(packet, COLLECTD_VALUETYPE_GAUGE, (double*) &massConcentrationPm1p0);

  //massConcentrationPm2p5
  collectd_add_string(packet, TYPE_TYPE_INSTANCE, (char*) "pm2p5");
  collectd_add_value(packet, COLLECTD_VALUETYPE_GAUGE, (double*) &massConcentrationPm2p5);

  //massConcentrationPm4p0
  collectd_add_string(packet, TYPE_TYPE_INSTANCE, (char*) "pm4p0");
  collectd_add_value(packet, COLLECTD_VALUETYPE_GAUGE,(double*)  &massConcentrationPm4p0);

    //massConcentrationPm1p0
  collectd_add_string(packet, TYPE_TYPE_INSTANCE, (char*) "pm10p0");
  collectd_add_value(packet, COLLECTD_VALUETYPE_GAUGE, (double*) &massConcentrationPm10p0);

  //RSSI
  collectd_add_string(packet, TYPE_TYPE_INSTANCE, (char*) "rssi");
  collectd_add_value(packet, COLLECTD_VALUETYPE_GAUGE, (double*) &glb_rssi);

  //CO2
  collectd_add_string(packet, TYPE_TYPE_INSTANCE,(char*) "co2");
  collectd_add_value(packet, COLLECTD_VALUETYPE_GAUGE, (double*) &co2Concentration);

  //CO
  collectd_add_string(packet, TYPE_TYPE_INSTANCE,(char*) "co");
  collectd_add_value(packet, COLLECTD_VALUETYPE_GAUGE, (double*) &coConcentration);

  SerPrintfLn( "Sending packet to " + String(CollectdIP[0]) + "." + String(CollectdIP[1]) + "." + String(CollectdIP[2]) + "." + String(CollectdIP[3]) + ":" +  String(CollectdPort));
  SerPrintfLn("Temp: " + String(ambientTemperature) + " Hum: " + String(ambientHumidity) + " VOC: " + String(vocIndex) \
  + "\nPM1.0: " + String(massConcentrationPm1p0) \
  + " PM2.5: " + String(massConcentrationPm2p5) \
  + " PM4.0: " + String(massConcentrationPm4p0) \
  + " PM10.0: " + String(massConcentrationPm10p0) \
  + " CO2: " + String(co2Concentration) \
  + " CO: " + String(coConcentration) \
  + " RSSI: " + String(glb_rssi) + "dBm" );

  udp_handler.beginPacket(CollectdIP, atoi(CollectdPort));
  udp_handler.write(packet->buffer, packet->current_offset);
  udp_handler.endPacket();
  
 collectd_reset_packet(packet,(char*)ESPName);
}

void setup() {
  //Serial
  Serial.begin(115200);
  Serial.setDebugOutput(false);
  Serial.print("\n" + String(ESPName) + " " + String(FWVersion) + " started\n");

  // Connect WiFi
  connect_wifi();

  //WebSerial
  WebSerial.begin(&server);
  /* Attach Message Callback */
  WebSerial.onMessage(recvMsg);
  server.begin();

  WebSerial.print(F("IP address: "));
  WebSerial.println(WiFi.localIP().toString());

  //Check for OTA updates
  checkForUpdates();

  //Init MICS
  mics.begin();

  //Init Sensirion
  Wire.begin();
  sen5x.begin(Wire);
  scd4x.begin(Wire, SCD41_I2C_ADDR_62);

  //Offset temperature reading to compensate for the ESP8266 produced heat in the Vindstyrka case
  uint16_t error = sen5x.setTemperatureOffsetSimple(tempOffset);
  if (error) {
     SerPrintfLn("SENS54 temperature offset has failed");
  }
  
  error = sen5x.startMeasurement();
  if (error) {
        SerPrintfLn("Failed to start SENS54 measurement");
  }

  //Prepare SCD41
  error = scd4x.wakeUp();
  if (error) {
        SerPrintfLn("Failed to wake SCD41");
  }

  error = scd4x.stopPeriodicMeasurement();
  if (error) {
        SerPrintfLn("Failed to stop SCD41 periodic measurement");
  }

  error = scd4x.reinit();
  if (error) {
        SerPrintfLn("Failed to reinit SCD41");
  }

  error = scd4x.startPeriodicMeasurement();
  if (error) {
        SerPrintfLn("Failed to start SCD41 periodic measurement");
  }

  SerPrintfLn("Read loop started");
}

void loop() {
    unsigned long currentMillis = millis();

    if (currentMillis - lastReadTime >= (readInterval + timingOffset)) {
        lastReadTime = currentMillis;

       //Read Sensirion Sens5X and report at reportingInterval
        if ( ReadSens5x() == 0 && currentMillis%reportingInterval < 2900) {

          ReadScd4x();
          ReadMICS();

          // Read WIFi RSSI
          getRSSI();
          send_data_to_collectd();
        }

    // SerPrintfLn(String(ESPName) + " " + String(FWVersion) + " lastReadTime : " + String(lastReadTime) + " reportingInterval : " + String(lastReadTime%reportingInterval));
    }
    //Small sleep
    delay (100);
}
