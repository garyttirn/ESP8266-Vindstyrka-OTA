/**
 * ESP8266 SEN54/IKEA Vindstyrka collectd integration.
 *
 * Partly based on
 *  https://github.com/techniker/sen54_mqtt
 *  https://github.com/Sensirion/arduino-i2c-sen5x/blob/master/examples/exampleUsage/exampleUsage.ino
 *  
 * Uses Sensirion I2C SEN5X Arduino Library 
 *   https://github.com/Sensirion/arduino-i2c-sen5x
 *
 * To be powered by the Vindtyrka
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

// WebSerial
// Initiate HTTP server
AsyncWebServer server(80);

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

//Read voltage
ADC_MODE(ADC_VCC);

unsigned long lastReadTime = 0;

String ProgramVersion  = "0.1";

double massConcentrationPm1p0=0.0, massConcentrationPm2p5=0.0, massConcentrationPm4p0=0.0, massConcentrationPm10p0=0.0;
double ambientHumidity=0.0, ambientTemperature=0.0, vocIndex=0.0;
double glb_vcc=0.0, glb_rssi=0.0;

void printModuleVersions() {
    uint16_t error;
    char errorMessage[256];

    unsigned char productName[32];
    uint8_t productNameSize = 32;

    error = sen5x.getProductName(productName, productNameSize);

    if (error) {
        Serial.print("Error trying to execute getProductName(): ");
        errorToString(error, errorMessage, 256);
        SerPrintfLn(errorMessage);
    } else {
        Serial.print("ProductName:");
        SerPrintfLn((char*)productName);

    }

    uint8_t firmwareMajor;
    uint8_t firmwareMinor;
    bool firmwareDebug;
    uint8_t hardwareMajor;
    uint8_t hardwareMinor;
    uint8_t protocolMajor;
    uint8_t protocolMinor;

    error = sen5x.getVersion(firmwareMajor, firmwareMinor, firmwareDebug,
                             hardwareMajor, hardwareMinor, protocolMajor,
                             protocolMinor);
    if (error) {
        errorToString(error, errorMessage, 256);
        SerPrintfLn("Error trying to execute getVersion(): " + String((char*) errorMessage));
    } else {
        SerPrintfLn("Firmware: " + String(firmwareMajor) + "." + String(firmwareMinor));
        SerPrintfLn("Hardware: " + String(hardwareMajor) + "." + String(hardwareMinor));
    }
}

void printSerialNumber() {
    uint16_t error;
    char errorMessage[256];
    unsigned char serialNumber[32];
    uint8_t serialNumberSize = 32;

    error = sen5x.getSerialNumber(serialNumber, serialNumberSize);
    if (error) {
        errorToString(error, errorMessage, 256);
        SerPrintfLn("Error trying to execute getSerialNumber(): " + String((char*) errorMessage));
    } else {
        SerPrintfLn("SerialNumber:" + String((char*)serialNumber));
    }
}

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

int ReadSensor() {

  bool status = false;
  uint16_t error; 

  float t_pm1 = 0.0f, t_pm2_5 = 0.0f, t_pm4 = 0.0f, t_pm10 = 0.0f;
  float t_hum = 0.0f, t_temp = 0.0f, t_voc = 0.0f, t_nox = 0.0f;

  //Check for data
  error =  sen5x.readDataReady(status);

  if (error && !status) {
    SerPrintfLn("Sensor data not ready to read sensor values");
    return 1;
  }

  // read data
  error = sen5x.readMeasuredValues(t_pm1, t_pm2_5, t_pm4, t_pm10, 
                                   t_hum, t_temp, t_voc, t_nox);
  if (error) {
    SerPrintfLn("Failed to read sensor values");
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

  return 0;
}

float getVccs(){

  glb_vcc=0.0;
  glb_vcc= (double) ESP.getVcc();
    
 SerPrintfLn("PSU VCC: " + String(glb_vcc*0.001)+ "V");

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

  //VCC
  collectd_add_string(packet, TYPE_TYPE_INSTANCE,(char*) "vcc");
  collectd_add_value(packet, COLLECTD_VALUETYPE_GAUGE, (double*) &glb_vcc);
  
  SerPrintfLn("Temp: " + String(ambientTemperature) + "  Hum: " + String(ambientHumidity) + "  VOC: " + String(vocIndex) \
  + "\nPM1.0: " + String(massConcentrationPm1p0) \
  + "  PM2.5: " + String(massConcentrationPm2p5) \
  + "  PM4.0: " + String(massConcentrationPm4p0) \
  + "  PM10.0: " + String(massConcentrationPm10p0));

  SerPrintfLn( "Sending packet to " + String(CollectdIP[0]) + "." + String(CollectdIP[1]) + "." + String(CollectdIP[2]) + "." + String(CollectdIP[3]) + ":" +  String(CollectdPort));

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

  //Init Sensirion
  Wire.begin();
  sen5x.begin(Wire);

  // Print SEN55 module information if i2c buffers are large enough
  #ifdef USE_PRODUCT_INFO
    printSerialNumber();
    printModuleVersions();
  #endif

  //Offset temperature reading to compensate for the ESP8266 produced heat in the Vindstyrka case
  uint16_t error = sen5x.setTemperatureOffsetSimple(tempOffset);
  if (error) {
     SerPrintfLn("Sensor temperature offset has failed");
  }
  
  error = sen5x.startMeasurement();
  if (error) {
        SerPrintfLn("Failed to start measurement");
  }

  SerPrintfLn("Read loop started");
}

void loop() {
    unsigned long currentMillis = millis();

    if (currentMillis - lastReadTime >= (readInterval + timingOffset)) {
        lastReadTime = currentMillis;

       // Read voltage and WIFi RSSI
       getVccs();
       getRSSI();

       //Read Sensirion and report at reportingInterval
        if ( ReadSensor() == 0 && currentMillis%reportingInterval < 2900) {
          send_data_to_collectd();
        }
    SerPrintfLn(String(ESPName) + " " + String(FWVersion) + " lastReadTime : " + String(lastReadTime) + " reportingInterval : " + String(lastReadTime%reportingInterval));
    }
    //Small sleep
    delay (100);
}
