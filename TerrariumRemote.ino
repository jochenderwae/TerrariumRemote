#include <ESP8266WiFi.h>
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <WiFiManager.h>

#include <Adafruit_MQTT.h>
#include <Adafruit_MQTT_Client.h>

#include <Adafruit_ST7789.h>
#include <Adafruit_ST77xx.h>

#include <Adafruit_GFX.h>
#include <Adafruit_SPITFT.h>
#include <Adafruit_SPITFT_Macros.h>
#include <gfxfont.h>

// images
extern const uint16_t thermometerBitmap [] PROGMEM;
extern const uint8_t  thermometerMask [] PROGMEM;
extern const uint16_t nowifi [] PROGMEM;
extern const uint16_t wifi [] PROGMEM;
extern const uint16_t waterlevelBitmap [] PROGMEM;
extern const uint8_t waterlevelMask [] PROGMEM;

#define TFT_DC    D1     // TFT DC  pin is connected to NodeMCU pin D1 (GPIO5)
#define TFT_RST   D2     // TFT RST pin is connected to NodeMCU pin D2 (GPIO4)
#define TFT_CS    D8     // TFT CS  pin is connected to NodeMCU pin D8 (GPIO15)


#define BUTTON_RAIN D8
#define BUTTON_FOG  D6
#define LED_RAIN    D0
#define LED_FOG     D3


#define MQTT_SERVER     "192.168.0.2"
#define MQTT_SERVERPORT 1883

#define COLOR_DISABLED 0x632C

WiFiManager wifiManager;
WiFiClient client;
Adafruit_MQTT_Client *mqtt;//(&client,  MQTT_SERVER, MQTT_SERVERPORT, "", "");
Adafruit_MQTT_Publish *publishRain;
Adafruit_MQTT_Publish *publishFog;
Adafruit_MQTT_Subscribe *subscribeHeat;
Adafruit_MQTT_Subscribe *subscribeLight1;
Adafruit_MQTT_Subscribe *subscribeLight2;
Adafruit_MQTT_Subscribe *subscribeRain;
Adafruit_MQTT_Subscribe *subscribeFog;
Adafruit_MQTT_Subscribe *subscribeFan;
Adafruit_MQTT_Subscribe *subscribeTemp;
Adafruit_MQTT_Subscribe *subscribeHumid;
Adafruit_MQTT_Subscribe *subscribeLastRain;
Adafruit_MQTT_Subscribe *subscribeLastFog;
Adafruit_MQTT_Subscribe *subscribeFreshWaterLevel;
Adafruit_ST7789 tft = Adafruit_ST7789(TFT_CS, TFT_DC, TFT_RST);
GFXcanvas16 backbuffer = GFXcanvas16(120, 120);


bool rainOn = false;
bool prevRainOn = false;
bool fogOn = false;
bool prevFogOn = false;
bool heaterOn = false;
bool prevHeaterOn = false;
bool light1On = false;
bool prevLight1On = false;
bool light2On = false;
bool prevLight2On = false;
bool fanOn = false;
bool prevFanOn = false;
float temperature = 0;
float prevTemperature = 0;
int humidity = 0;
int prevHumidity = 0;
int hoursSinceRain = 0;
int lastHoursSinceRain = 0;
int hoursSinceFog = 0;
int lastHoursSinceFog = 0;
int freshWaterLevel = 0;
int lastFreshWaterLevel = 0;

int prevRainButton = LOW;
int prevFogButton = LOW;
bool invalidateScreen = true;

boolean connected = false;
boolean prevConnected = false;
boolean connectedBlink = false;

long lastUpdateTime = 0;
long lastRepaintTime = 0;

void MQTT_connect();

void setup() {
  Serial.begin(9600);
  
  // if the display has CS pin try with SPI_MODE0
  tft.init(240, 240, SPI_MODE2);    // Init ST7789 display 240x240 pixel

  // if the screen is flipped, remove this command
  tft.setRotation(0);

  Serial.println(F("Initialized"));

  tft.fillScreen(ST77XX_BLACK);
  tft.setCursor(0, 0);
  tft.setTextColor(ST77XX_WHITE);
  tft.setTextWrap(true);

  pinMode(BUTTON_RAIN, INPUT);
  pinMode(BUTTON_FOG, INPUT);
  pinMode(LED_RAIN, OUTPUT);
  pinMode(LED_FOG, OUTPUT);

  wifiManager.setAPCallback(configModeCallback);
  wifiManager.autoConnect("MQTT_BUTTON");

  mqtt                     = new Adafruit_MQTT_Client(&client,  MQTT_SERVER, MQTT_SERVERPORT, "", "");
  publishRain              = new Adafruit_MQTT_Publish(mqtt,   "terrarium/cmnd/rain");
  publishFog               = new Adafruit_MQTT_Publish(mqtt,   "terrarium/cmnd/fog");
  subscribeRain            = new Adafruit_MQTT_Subscribe(mqtt, "terrarium/stat/rain");
  subscribeFog             = new Adafruit_MQTT_Subscribe(mqtt, "terrarium/stat/fog");
  subscribeHeat            = new Adafruit_MQTT_Subscribe(mqtt, "terrarium/stat/heater");
  subscribeLight1          = new Adafruit_MQTT_Subscribe(mqtt, "terrarium/stat/light1");
  subscribeLight2          = new Adafruit_MQTT_Subscribe(mqtt, "terrarium/stat/light2");
  subscribeFan             = new Adafruit_MQTT_Subscribe(mqtt, "terrarium/stat/fan");
  subscribeTemp            = new Adafruit_MQTT_Subscribe(mqtt, "terrarium/stat/insideTemperature");
  subscribeHumid           = new Adafruit_MQTT_Subscribe(mqtt, "terrarium/stat/insideHumidity");
  subscribeLastRain        = new Adafruit_MQTT_Subscribe(mqtt, "terrarium/stat/lastRain");
  subscribeLastFog         = new Adafruit_MQTT_Subscribe(mqtt, "terrarium/stat/lastFog");
  subscribeFreshWaterLevel = new Adafruit_MQTT_Subscribe(mqtt, "terrarium/stat/freshWaterLevel");

  mqtt->subscribe(subscribeHeat);
  mqtt->subscribe(subscribeLight1);
  mqtt->subscribe(subscribeLight2);
  mqtt->subscribe(subscribeRain);
  mqtt->subscribe(subscribeFog);
  mqtt->subscribe(subscribeFan);
  mqtt->subscribe(subscribeTemp);
  mqtt->subscribe(subscribeHumid);
  mqtt->subscribe(subscribeLastRain);
  mqtt->subscribe(subscribeLastFog);
  mqtt->subscribe(subscribeFreshWaterLevel);
  
  tft.println("Setup done.");

  lastUpdateTime = millis();
}

/*
 * loop()
 * 
 * Main loop:
 *  - read and process incoming MQTT messages
 *  - write MQTT messages based on button presses
 *  - 
 */
void loop() {
  boolean dataUpdated = false;
  MQTT_connect();
  if(connected) {
    Adafruit_MQTT_Subscribe *subscription = mqtt->readSubscription(50);
    if(subscription) {
      char *topic = ((char *)subscription->topic);
      char *value = ((char *)subscription->lastread);
  
      Serial.print("Received: ");
      Serial.print(topic);
      Serial.print(" - ");
      Serial.println(value);
      
      if (subscription == subscribeRain) {
        tft.println("Rain");
        if(strcmp(value, "ON") == 0) {
          digitalWrite(LED_RAIN, HIGH);
          rainOn = true;
        }
        if(strcmp(value, "OFF") == 0) {
          digitalWrite(LED_RAIN, LOW);
          rainOn = false;
        }
        if(prevRainOn != rainOn) {
          prevRainOn = rainOn;
          dataUpdated = true;
        }
      } else if (subscription == subscribeFog) {
        if(strcmp(value, "ON") == 0) {
          digitalWrite(LED_FOG, HIGH);
          fogOn = true;
        }
        if(strcmp(value, "OFF") == 0) {
          digitalWrite(LED_FOG, LOW);
          fogOn = false;
        }
        if(prevFogOn != fogOn) {
          prevFogOn = fogOn;
          dataUpdated = true;
        }
      } else if (subscription == subscribeHeat) {
        if(strcmp(value, "ON") == 0) {
          heaterOn = true;
        }
        if(strcmp(value, "OFF") == 0) {
          heaterOn = false;
        }
        if(prevHeaterOn != heaterOn) {
          prevHeaterOn = heaterOn;
          dataUpdated = true;
        }
      } else if (subscription == subscribeLight1) {
        if(strcmp(value, "ON") == 0) {
          light1On = true;
        }
        if(strcmp(value, "OFF") == 0) {
          light1On = false;
        }
        if(prevLight1On != light1On) {
          prevLight1On = light1On;
          dataUpdated = true;
        }
      } else if (subscription == subscribeLight2) {
        if(strcmp(value, "ON") == 0) {
          light2On = true;
        }
        if(strcmp(value, "OFF") == 0) {
          light2On = false;
        }
        if(prevLight2On != light2On) {
          prevLight2On = light2On;
          dataUpdated = true;
        }
      } else if (subscription == subscribeFan) {
        if(strcmp(value, "ON") == 0) {
          fanOn = true;
        }
        if(strcmp(value, "OFF") == 0) {
          fanOn = false;
        }
        if(prevFanOn != fanOn) {
          prevFanOn = fanOn;
          dataUpdated = true;
        }
      } else if(subscription == subscribeTemp) {
        temperature = atof(value);
        if(prevTemperature != temperature) {
          prevTemperature = temperature;
          dataUpdated = true;
        }
      } else if(subscription == subscribeHumid) {
        humidity = atoi(value);
        if(prevHumidity != humidity) {
          prevHumidity = humidity;
          dataUpdated = true;
        }
      } else if(subscription == subscribeLastRain) {
        hoursSinceRain = atoi(value);
        if(lastHoursSinceRain != hoursSinceRain) {
          lastHoursSinceRain = hoursSinceRain;
          dataUpdated = true;
        }
      } else if(subscription == subscribeLastFog) {
        hoursSinceFog = atoi(value);
        if(lastHoursSinceFog != hoursSinceFog) {
          lastHoursSinceFog = hoursSinceFog;
          dataUpdated = true;
        }
      } else if(subscription == subscribeFreshWaterLevel) {
        freshWaterLevel = atoi(value);
        if(lastFreshWaterLevel != freshWaterLevel) {
          lastFreshWaterLevel = freshWaterLevel;
          dataUpdated = true;
        }
      }
    }
  }

  int rainButton = digitalRead(BUTTON_RAIN);
  if(prevRainButton != rainButton && rainButton == HIGH) {
    if(connected) {
      publishRain->publish("ON");
    }
  }
  prevRainButton = rainButton;
  
  int fogButton = digitalRead(BUTTON_FOG);
  if(prevFogButton != fogButton && fogButton == HIGH) {
    if(connected) {
      publishFog->publish("ON");
    }
  }
  prevRainButton = rainButton;

  if(!connected) {
   /*
    rainOn = (random(1000) % 2) == 0;
    fogOn = (random(1000) % 2) == 0;
    heaterOn = (random(1000) % 2) == 0;
    light1On = (random(1000) % 2) == 0;
    light2On = (random(1000) % 2) == 0;
    fanOn = (random(1000) % 2) == 0;
    
    temperature++;
    if(temperature > 50) {
      temperature = 10;
    }
    humidity++;
    if(humidity > 100) {
      humidity = 0;
    }
    hoursSinceRain++;
    if(hoursSinceRain > 24) {
      hoursSinceRain = 0;
    }
    hoursSinceFog++;
    if(hoursSinceFog > 120) {
      hoursSinceFog = 0;
    }
    freshWaterLevel++;
    if(freshWaterLevel > 100) {
      freshWaterLevel = 0;
    }*/
    dataUpdated = true;
  }

  if(dataUpdated) {
    invalidateScreen = true;
    lastUpdateTime = millis();
  }

  // repaint if data changed and the last change is 100ms old or the last repaint is more then a second ago
  if(invalidateScreen && (abs(lastUpdateTime - millis()) > 100 || abs(lastRepaintTime - millis()) > 1000)) {
    invalidateScreen = false;
    repaintScreen();
    lastRepaintTime = millis();
  }
}

/*
 * repaintScreen()
 * 
 * Redraw the whole screen
 */
void repaintScreen() {
  drawStatus();
  drawThermometer();
  drawStats();
  drawWaterLevel();
}

/*
 * drawStatus()
 * 
 * Draws the status of the output / actuators (heating, lighting, ...)
 */
void drawStatus() {
  int x, y, columnX;
  int16_t x1, y1;
  uint16_t w, h;

  backbuffer.fillScreen(ST77XX_BLACK);
  backbuffer.setTextColor(ST77XX_WHITE);
  backbuffer.setTextWrap(false);
  backbuffer.setTextSize(2);

  if(connected) {
    backbuffer.drawRGBBitmap(94, 0, wifi, 24, 24);
  } else if(connectedBlink) {
    backbuffer.drawRGBBitmap(94, 0, nowifi, 24, 24);
  }
  connectedBlink = !connectedBlink;

  
  /*
  * Draw heater status
  */
  x = 6;
  y = 24;
  backbuffer.getTextBounds("Heater", x, y, &x1, &y1, &w, &h);
  columnX = x + w + 32;

  backbuffer.setCursor(x, y);
  backbuffer.println("Heater");  
  if(heaterOn) {
    backbuffer.fillCircle(columnX, y1 + h / 2, 6, ST77XX_GREEN);
  } else {
    backbuffer.fillCircle(columnX, y1 + h / 2, 6, COLOR_DISABLED);
  }

  /*
  * Draw light status
  */

  y = y + h + 1;
  backbuffer.getTextBounds("Light", x, y, &x1, &y1, &w, &h);

  int lights = (light1On?1:0) + (light2On?2:0);
  backbuffer.setCursor(x, y);
  backbuffer.println("Light");
  switch(lights) {
    case 0:
      backbuffer.fillCircle(columnX - 32, y1 + h / 2, 6, COLOR_DISABLED);
      backbuffer.fillCircle(columnX - 16, y1 + h / 2, 6, COLOR_DISABLED);
      backbuffer.fillCircle(columnX,      y1 + h / 2, 6, COLOR_DISABLED);
      break;
    case 1:
      backbuffer.fillCircle(columnX - 32, y1 + h / 2, 6, ST77XX_GREEN);
      backbuffer.fillCircle(columnX - 16, y1 + h / 2, 6, COLOR_DISABLED);
      backbuffer.fillCircle(columnX,      y1 + h / 2, 6, COLOR_DISABLED);
      break;
    case 2:
      backbuffer.fillCircle(columnX - 32, y1 + h / 2, 6, ST77XX_GREEN);
      backbuffer.fillCircle(columnX - 16, y1 + h / 2, 6, ST77XX_GREEN);
      backbuffer.fillCircle(columnX,      y1 + h / 2, 6, COLOR_DISABLED);
      break;
    case 3:
      backbuffer.fillCircle(columnX - 32, y1 + h / 2, 6, ST77XX_GREEN);
      backbuffer.fillCircle(columnX - 16, y1 + h / 2, 6, ST77XX_GREEN);
      backbuffer.fillCircle(columnX,      y1 + h / 2, 6, ST77XX_GREEN);
      break;
  }

  /*
  * Draw rain status
  */
  
  y = y + h + 1;
  backbuffer.getTextBounds("Rain", x, y, &x1, &y1, &w, &h);
  
  backbuffer.setCursor(x, y);
  backbuffer.println("Rain");
  if(rainOn) {
    backbuffer.fillCircle(columnX, y1 + h / 2, 6, ST77XX_GREEN);
  } else {
    backbuffer.fillCircle(columnX, y1 + h / 2, 6, COLOR_DISABLED);
  }

  /*
  * Draw Fog status
  */
  
  y = y + h + 1;
  backbuffer.getTextBounds("Fog", x, y, &x1, &y1, &w, &h);
  
  backbuffer.setCursor(x, y);
  backbuffer.println("Fog");
  if(fogOn) {
    backbuffer.fillCircle(columnX, y1 + h / 2, 6, ST77XX_GREEN);
  } else {
    backbuffer.fillCircle(columnX, y1 + h / 2, 6, COLOR_DISABLED);
  }

  /*
  * Draw Fan status
  */
  
  y = y + h + 1;
  backbuffer.getTextBounds("Fan", x, y, &x1, &y1, &w, &h);
  
  backbuffer.setCursor(x, y);
  backbuffer.println("Fan");
  if(fanOn) {
    backbuffer.fillCircle(columnX, y1 + h / 2, 6, ST77XX_GREEN);
  } else {
    backbuffer.fillCircle(columnX, y1 + h / 2, 6, COLOR_DISABLED);
  }

  tft.drawRGBBitmap(120, 0, backbuffer.getBuffer(), 120, 120);
}

/*
 * drawThermometer()
 * 
 * Draws the thermometer in the top-left panel
 */
void drawThermometer() {
  int16_t x1, y1;
  uint16_t w, h;
  int charHeight = 0;
  
  backbuffer.fillScreen(ST77XX_BLACK);
  backbuffer.setTextColor(ST77XX_WHITE);
  backbuffer.setTextWrap(false);
  backbuffer.setTextSize(2);

  // temperature
  // humidity
  backbuffer.getTextBounds("024", 0, 0, &x1, &y1, &w, &h);
  charHeight = h / 2;
  
  float f = temperature / 50.0;
  uint16_t color = ((uint16_t)(f * 0b00011111)) << 11 | ((uint16_t)((1 - f) * 0b00011111));
  backbuffer.fillRect(31, 66 - temperature, 46, 42 + temperature, color);
  backbuffer.drawRGBBitmap(31, 10, thermometerBitmap, thermometerMask, 46, 98);
  backbuffer.drawFastHLine(72, 66 - temperature, 6, ST77XX_WHITE);
  backbuffer.setCursor(84, 66 - temperature - charHeight);
  if(temperature < 10) {
    backbuffer.print(" ");
  }
  backbuffer.println((uint8_t)round(temperature));

  backbuffer.drawFastHLine(28, 26, 6, ST77XX_WHITE);
  backbuffer.setCursor(0, 26 - charHeight);
  backbuffer.println("40");
  
  backbuffer.drawFastHLine(28, 46, 6, ST77XX_WHITE);
  backbuffer.setCursor(0, 46 - charHeight);
  backbuffer.println("20");
  
  backbuffer.drawFastHLine(28, 66, 6, ST77XX_WHITE);
  backbuffer.setCursor(0, 66 - charHeight);
  backbuffer.println(" 0");

  tft.drawRGBBitmap(0, 0, backbuffer.getBuffer(), 120, 120);
}

/*
 * drawStats()
 * 
 * Draws the buttom left quarter of the screen. Used to display key-value pairs (e.g. temperature, humidity, ...)
 */
void drawStats() {
  int16_t x1, y1;
  uint16_t w, h;
  int charHeight = 0;
  
  backbuffer.fillScreen(ST77XX_BLACK);
  backbuffer.setTextColor(ST77XX_WHITE);
  backbuffer.setTextWrap(false);
  backbuffer.setTextSize(2);
  backbuffer.cp437();

  // temperature
  // humidity
  backbuffer.getTextBounds("Humid:", 0, 0, &x1, &y1, &w, &h);
  charHeight = h / 2;
  
  backbuffer.setCursor(0, 0);
  backbuffer.println("Temp:");
  backbuffer.println("Humid:");
  backbuffer.println("Rain:");
  backbuffer.println("Fog:");
  
  backbuffer.setCursor(w, 0);
  if(temperature < 10) {
    backbuffer.print(" ");
  }
  backbuffer.print((uint8_t)round(temperature));
  backbuffer.print((char)248);
  backbuffer.print("C");
  
  backbuffer.setCursor(w, h);
  if(humidity < 100) {
    backbuffer.print(" ");
  }
  if(humidity < 10) {
    backbuffer.print(" ");
  }
  backbuffer.print(humidity);
  backbuffer.print("%");
  
  backbuffer.setCursor(w, 2 * h);
  backbuffer.print(" ");
  if(hoursSinceRain < 10) {
    backbuffer.print(" ");
  }
  backbuffer.print(hoursSinceRain);
  backbuffer.print("h");
  
  backbuffer.setCursor(w, 3 * h);
  if(hoursSinceFog < 100) {
    backbuffer.print(" ");
  }
  if(hoursSinceFog < 10) {
    backbuffer.print(" ");
  }
  backbuffer.print(hoursSinceFog);
  backbuffer.print("h");

  tft.drawRGBBitmap(0, 120, backbuffer.getBuffer(), 120, 120);
}


/*
 * drawWaterLevel()
 * 
 * Draws the water level in the bottom-right panel
 */
void drawWaterLevel() {
  int16_t x1, y1;
  uint16_t w, h;
  
  backbuffer.fillScreen(ST77XX_BLACK);
  backbuffer.setTextColor(ST77XX_WHITE);
  backbuffer.setTextWrap(false);
  backbuffer.setTextSize(2);

  // temperature
  // humidity
  backbuffer.getTextBounds("0", 0, 0, &x1, &y1, &w, &h);
  int width = 2 * w;
  if(freshWaterLevel > 10) {
    width += w;
  }
  if(freshWaterLevel > 100) {
    width += w;
  }

  int levelPos = (int)round(freshWaterLevel * 60.0f / 100.0f);
  backbuffer.fillRect(60, 94 - levelPos, 60, levelPos, ST77XX_BLUE);
  backbuffer.drawRGBBitmap(60, 20, waterlevelBitmap, waterlevelMask, 60, 80);
  backbuffer.setCursor(90 - width / 2, 94 - levelPos - h - 2);
  backbuffer.print(freshWaterLevel);
  backbuffer.print("%");

  backbuffer.drawFastHLine(48, 34, 6, ST77XX_WHITE);
  backbuffer.setCursor(10, 34 - h / 2);
  backbuffer.println("100");
  
  backbuffer.drawFastHLine(48, 64, 6, ST77XX_WHITE);
  backbuffer.setCursor(10, 64 - h / 2);
  backbuffer.println(" 50");
  
  backbuffer.drawFastHLine(48, 94, 6, ST77XX_WHITE);
  backbuffer.setCursor(10, 94 - h / 2);
  backbuffer.println("  0");

  tft.drawRGBBitmap(120, 120, backbuffer.getBuffer(), 120, 120);
}

void configModeCallback (WiFiManager *myWiFiManager) {
  tft.println("Welcome to MQTT Button");
  tft.println("Entered config mode");
  tft.println(WiFi.softAPIP());
  tft.println(myWiFiManager->getConfigPortalSSID());
}

void MQTT_connect() {
  int8_t ret;

  // Stop if already connected.
  if (mqtt->connected()) {
    connected = true;
    return;
  }

  if (mqtt->connect() != 0) { // connect will return 0 for connected
    mqtt->disconnect();
    connected = false;
  }
}
