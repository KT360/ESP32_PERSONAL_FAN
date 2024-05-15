#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <DHT.h>
#include <Wifi.h>
#include <HTTPClient.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <stdio.h> 

#include <sstream>
#include <iomanip>
#include <string>

#include "driver/timer.h"
#include <stdlib.h>

#define SCREEN_WIDTH 128 // OLED display width, in pixels
#define SCREEN_HEIGHT 64 // OLED display height, in pixels
#define OLED_RESET     -1 // Reset pin # (or -1 if sharing Arduino reset pin)

#define DHTPIN 23         // Digital pin connected to the DHT sensor
#define DHTTYPE DHT22    // DHT 22 (AM2302), AM2321


#define PWM_CHANNEL 0   // ESP32 has 16 channels which can generate 16 independent waveforms
#define PWM_FREQ  25000    //25kHz for Noctua fans
#define PWM_RESOLUTION 8


#define PWM_OUTPUT_PIN 32

#define PWM_DELAY 4


const int MAX_DUTY_CYCLE = (int)(pow(2, PWM_RESOLUTION) - 1);


Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

DHT dht(DHTPIN, DHTTYPE);

String URL = "http://Path/temp_sensor/post_data.php";

const char* ssid = "";
const char* password = "";

float temperature;
float humidity;

unsigned long previousMillis = 0;
const long interval = 1800000;

int CYCLE = 100;

float disp_rpm = CYCLE*1500/MAX_DUTY_CYCLE;

//TODO: SPEND SOME TIME ON THE DISCONNECTION CODE
//Error handling for RPM values being passed
//Comments
//0 RPM on display glitch

// BLE SECTION
BLEServer *pServer = NULL;

BLECharacteristic *message_characteristic = NULL;
BLECharacteristic *rpm_characteristic = NULL;

#define SERVICE_UUID "f158a25a-f800-41af-9193-e80ef1a1d3e7"

#define MESSAGE_CHARACTERISTIC_UUID "7b8d3c84-0efa-405d-9f9d-6d1b73fea6d6"
#define RPM_CHARACTERISTIC_UUID "dedf926b-600b-4dad-815a-1364e9dd0422"


std::string getString(double number, int precision = 1)
{
  std::ostringstream stream;
  stream << std::fixed << std::setprecision(precision) << number;  // Set precision to 1 decimal place
  std::string temp_string = stream.str();

  return temp_string;
}

class MyServerCallbacks : public BLEServerCallbacks
{
  void onConnect(BLEServer *pServer)
  {
    Serial.println("Connected");
  };

  void onDisconnect(BLEServer *pServer)
  {
    Serial.println("Disconnected");


    // Start advertising again
    pServer->getAdvertising()->start();

    message_characteristic->setValue("Temperature");

    std::string rpm_message = getString((double) CYCLE/MAX_DUTY_CYCLE*1500, 0);

    rpm_characteristic->setValue(rpm_message);

    Serial.println("Waiting for a client connection to notify...");
  }
};

class CharacteristicsCallbacks : public BLECharacteristicCallbacks
{
  //When notified of New RPM convert that to new cycle signal, assuming a inear relationship between the current resolution (8bits = 256 levels)
  void onWrite(BLECharacteristic *pCharacteristic)
  {
    std::string value = pCharacteristic->getValue().c_str();

    Serial.print("Value Written ");
    Serial.println(pCharacteristic->getValue().c_str());

    if(pCharacteristic == rpm_characteristic)
    {
      int rpm = std::stoi(value);

      disp_rpm = rpm;

      CYCLE = (int) rpm*255/1500;

      ledcWrite(PWM_CHANNEL,CYCLE);      
    }

    /*
    if (pCharacteristic == box_characteristic)
    {
      boxValue = pCharacteristic->getValue().c_str();
      box_characteristic->setValue(const_cast<char *>(boxValue.c_str()));
      box_characteristic->notify();
    }    if (pCharacteristic == box_characteristic)
    {
      boxValue = pCharacteristic->getValue().c_str();
      box_characteristic->setValue(const_cast<char *>(boxValue.c_str()));
      box_characteristic->notify();
    }
    */
  }
};




void connectWiFi(const char* ssid, const char* password)
{
  WiFi.mode(WIFI_OFF);

  delay(1000);

  WiFi.mode(WIFI_STA);

  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi");

  while(WiFi.status() != WL_CONNECTED)//Show loading
  {
    delay(500);
    Serial.println(".");
  }

  Serial.print("connected to: ");
  Serial.println(ssid);
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());

}


void sendData()
{
  //Post data to sql database @ local host
  String postData = "temperature="+String(temperature)+"&humidity="+String(humidity);

  HTTPClient http;
  http.begin(URL);

  http.addHeader("Content-Type", "application/x-www-form-urlencoded");

  int httpCode = http.POST(postData);

  String payload = http.getString();
  

  if(httpCode > 0)
  {
    if(httpCode == HTTP_CODE_OK || httpCode == HTTP_CODE_CREATED)
    {
      Serial.println("Recieved payload");
      Serial.println(payload);
    }else
    {
      Serial.println("POST failed , http code: ");
      Serial.print(httpCode);
      Serial.println(payload);
    }
  }else
  {
    Serial.print("Error sending POST: ");
    Serial.println(http.errorToString(httpCode).c_str());
    Serial.println(payload);
  }
}




void setup() {

  Serial.begin(115200);

  ledcSetup(PWM_CHANNEL, PWM_FREQ, PWM_RESOLUTION);
  ledcAttachPin(PWM_OUTPUT_PIN, PWM_CHANNEL);

  ledcWrite(PWM_CHANNEL,CYCLE);

  connectWiFi(ssid, password);

  dht.begin();

  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) { //display's I2C address
    Serial.println(F("SSD1306 allocation failed"));
    for(;;); // Don't proceed, loop forever
  }

  display.clearDisplay();
  display.setTextSize(1);      // Normal 1:1 pixel scale
  display.setTextColor(SSD1306_WHITE); // Draw white text
  display.setCursor(0,0);     // Start at top-left corner


  // Create the BLE Device
  BLEDevice::init("ESPFAN");
  // Create the BLE Server
  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());
  // Create the BLE Service
  BLEService *pService = pServer->createService(SERVICE_UUID);
  delay(100);

  // Create a BLE Characteristic
  message_characteristic = pService->createCharacteristic(
      MESSAGE_CHARACTERISTIC_UUID,
      BLECharacteristic::PROPERTY_READ |
          BLECharacteristic::PROPERTY_WRITE |
          BLECharacteristic::PROPERTY_NOTIFY |
          BLECharacteristic::PROPERTY_INDICATE);

  rpm_characteristic = pService->createCharacteristic(
      RPM_CHARACTERISTIC_UUID,
      BLECharacteristic::PROPERTY_READ |
          BLECharacteristic::PROPERTY_WRITE |
          BLECharacteristic::PROPERTY_NOTIFY |
          BLECharacteristic::PROPERTY_INDICATE);

  // Start the BLE service
  pService->start();

  // Start advertising
  pServer->getAdvertising()->start();

  message_characteristic->setValue("Temperature");
  message_characteristic->setCallbacks(new CharacteristicsCallbacks());

  std::string rpm_message = getString((double) CYCLE/MAX_DUTY_CYCLE*1500, 0);

  rpm_characteristic->setValue(rpm_message);
  rpm_characteristic->setCallbacks(new CharacteristicsCallbacks());

  Serial.println("Waiting for a client connection to notify...");
}


void loop() {

  while(WiFi.status() != WL_CONNECTED)
  {
    connectWiFi(ssid, password);
  }


  humidity = dht.readHumidity(); // Read humidity
  temperature = dht.readTemperature(); // Read temperature as Celsius
  
  if (isnan(humidity) || isnan(temperature)) {
    display.println(F("Failed to read from DHT sensor!"));
  } else {
    display.clearDisplay();
    display.setCursor(0,0);     // Start at top-left corner
    display.print("Humidity: ");
    display.print(humidity);
    display.println("%");
    
    display.print("Temp: ");
    display.print(temperature);
    display.println(" C");
  }

  display.print(disp_rpm); //1500 rpm is max for fan
  display.println(" RPM");

  
  display.display(); // Show the new data on the OLED

  double disp_temp = (double) temperature;

  std::string temperature_msg = getString(disp_temp) + " °C";
  
  message_characteristic->setValue(temperature_msg);
  message_characteristic->notify();

  unsigned long currentMillis = millis();


  /*
    if(currentMillis - previousMillis >= interval)
    {
      previousMillis = currentMillis;

      sendData();
    }
  
  */

  delay(2000);

}
