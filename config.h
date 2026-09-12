/* Vindstyrkan ESP8266 Config*/

const char* WifiSSID      = "SSID";
const char* WifiPass      = "PASSWORD";

const long readInterval = 10000; // Read sensor data every 10 seconds
const long timingOffset = 2500; // Offset to stagger the reading
const long reportingInterval = 30000; // Report measurements to Vera every 30s
const long tempOffset = -5; //ESP8266 Seems to cause elevated temperature measurements

const char* ESPName = "Vindstyrka";
const IPAddress CollectdIP = {192,168,1,3};
const char* CollectdPort = "25826";
const char* UpdateURL = "http://192.168.1.2:4080/8266OTA.php";
const char* FWVersion = "12092026";
