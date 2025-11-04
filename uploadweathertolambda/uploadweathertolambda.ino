#include <WiFi.h>
#include <Wire.h>
#include <BME280I2C.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <DS18B20.h>
#include <vars.h>

//Define sensors
BME280I2C bme;
DS18B20 ds(15);

#define uS_TO_S_FACTOR 1000000ULL /* Conversion factor for micro seconds to seconds */
#define TIME_TO_SLEEP 60          /* Time ESP32 will go to sleep (in seconds) */

int LED_BUILTIN = 2;

const char* ssid = WIFI_SSID;
const char* password = WIFI_PASSWORD;
String hostname = "ESP32WeatherStation";

bool metric = false;
int live = 2;     //Live flag - if 0, does not post to AWS. If 1, posts to AWS only. If 2, sends to wunderground

const char* lambdaurl = LAMBDA_URL;

/*
Method to print the reason by which ESP32
has been awaken from sleep
*/
void print_wakeup_reason() {
  esp_sleep_wakeup_cause_t wakeup_reason;

  wakeup_reason = esp_sleep_get_wakeup_cause();

  switch (wakeup_reason) {
    case ESP_SLEEP_WAKEUP_EXT0:     Serial.println("Wakeup caused by external signal using RTC_IO"); break;
    case ESP_SLEEP_WAKEUP_EXT1:     Serial.println("Wakeup caused by external signal using RTC_CNTL"); break;
    case ESP_SLEEP_WAKEUP_TIMER:    Serial.println("Wakeup caused by timer"); break;
    case ESP_SLEEP_WAKEUP_TOUCHPAD: Serial.println("Wakeup caused by touchpad"); break;
    case ESP_SLEEP_WAKEUP_ULP:      Serial.println("Wakeup caused by ULP program"); break;
    default:                        Serial.printf("Wakeup was not caused by deep sleep: %d\n", wakeup_reason); break;
  }

  pinMode (LED_BUILTIN, OUTPUT);
}

void doWeather()
{
  analogWrite(LED_BUILTIN, 5);
  WiFi.mode(WIFI_STA);
  WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE, INADDR_NONE);
  WiFi.setHostname(hostname.c_str());
  WiFi.begin(ssid,password);
  Serial.print("Connecting to WiFi ..");
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print('.');
    delay(50);
  }
  Serial.println(WiFi.localIP());
  Serial.print(WiFi.SSID());
  Serial.print(" RSSI ");
  Serial.print(WiFi.RSSI());
  Serial.println("");

  //Do weather stuff here and upload
  Wire.begin();
  bme.begin();
  switch(bme.chipModel())
  {
     case BME280::ChipModel_BME280:
       Serial.println("Found BME280 sensor! Success.");
       break;
     case BME280::ChipModel_BMP280:
       Serial.println("Found BMP280 sensor! No Humidity available.");
       break;
     default:
       Serial.println("Found UNKNOWN sensor! Error!");
  }

  float tempBME(NAN), hum(NAN), pres(NAN), tempDS(NAN);  
  BME280::TempUnit tempUnit(BME280::TempUnit_Celsius);
  BME280::PresUnit presUnit(BME280::PresUnit_inHg);
  bme.read(pres, tempBME, hum, tempUnit, presUnit);

  Serial.println("-- Default Test --");


  Serial.println();

  Serial.print("Temperature = ");
  Serial.print(tempBME);
  Serial.println(" *C");
  
  // Convert temperature to Fahrenheit
  Serial.print("Temperature = ");
  Serial.print(1.8 * tempBME + 32);
  Serial.println(" *F");
  
  Serial.print("Pressure = ");
  Serial.print(pres);
  Serial.println(" inHG");

  Serial.print("Humidity = ");
  Serial.print(hum);
  Serial.println(" %");

  while (ds.selectNext()) {
    uint8_t address[8];
    ds.getAddress(address);

    Serial.print("DS18B20 Address:");
    for (uint8_t i = 0; i < 8; i++) {
      Serial.print(" ");
      Serial.print(address[i]);
    }
    Serial.print(" ");
    Serial.print(ds.getTempC());
    tempDS = ds.getTempC();
    Serial.println();
  }

  JsonDocument doc;

  doc["auth"]["ApiKey"] = LAMBDA_APIKEY;

  JsonObject data = doc["data"].to<JsonObject>();
  data["TemperatureBME"] = tempBME;
  data["TemperatureDS"] = tempDS;
  data["Pressure"] = pres;
  data["Humidity"] = hum;
  data["isLive"] = live;


  // Serialize the JSON document to a String
  String jsonPayload;
  serializeJson(doc, jsonPayload);

  Serial.println("Generated JSON Payload:");
  Serial.println(jsonPayload);

  if(live > 0)
  {
    NetworkClientSecure client;
    client.setInsecure(); 
    HTTPClient http;
    http.begin(client, lambdaurl);
    http.addHeader("Content-Type", "application/json");
    int httpResponseCode = http.POST(jsonPayload);
    Serial.print("HTTP Response code: ");
    Serial.println(httpResponseCode);
    String resp = http.getString();
    Serial.println("Payload:");
    Serial.println(resp);
    // Free resources
    http.end();
  }

  WiFi.disconnect();
  analogWrite(LED_BUILTIN, LOW);
}

void setup() {
  Serial.begin(115200);
  pinMode(15, INPUT_PULLUP);
  delay(50);  //Take some time to open up the Serial Monitor
    
    esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH,   ESP_PD_OPTION_OFF);
    esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_SLOW_MEM, ESP_PD_OPTION_OFF);
    esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_FAST_MEM, ESP_PD_OPTION_OFF);
    esp_sleep_pd_config(ESP_PD_DOMAIN_XTAL,         ESP_PD_OPTION_OFF);


  //Print the wakeup reason for ESP32
  print_wakeup_reason();


  //Do work here
  doWeather();

  esp_sleep_enable_timer_wakeup(TIME_TO_SLEEP * uS_TO_S_FACTOR);
  Serial.println("Setup ESP32 to sleep for every " + String(TIME_TO_SLEEP) + " Seconds");

  Serial.println("Going to sleep now");
  Serial.flush();
  esp_deep_sleep_start();
  Serial.println("This will never be printed");
}

void loop() {
  //This is not going to be called
}
