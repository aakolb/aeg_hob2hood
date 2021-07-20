#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <ESP8266HTTPUpdateServer.h>
#include <EEPROM.h>
#include "Adafruit_MQTT.h"
#include "Adafruit_MQTT_Client.h"
#include "mqtt.h"
#include "wifi.h"
#include "vars.h"

#include <IRrecv.h>
#include <IRutils.h>


WiFiClient client;
IRrecv irrecv(INPUT_IR_PIN);
decode_results results;

ESP8266WebServer httpServer(80);
ESP8266HTTPUpdateServer httpUpdater;

// IR commands from hob2hood device
const uint64_t IRReceiveVentLevel1 = 0xE3C01BE2;
const uint64_t IRReceiveVentLevel2 = 0xD051C301;
const uint64_t IRReceiveVentLevel3 = 0xC22FFFD7;
const uint64_t IRReceiveVentLevel4 = 0xB9121B29;
const uint64_t IRReceiveVentOff = 0x55303A3;
const uint64_t IRReceiveLightOn = 0xE208293C;
const uint64_t IRReceiveLightOff = 0x24ACF947;

Adafruit_MQTT_Client mqtt(&client, AIO_SERVER, AIO_SERVERPORT, AIO_CID, AIO_USERNAME, AIO_KEY);
Adafruit_MQTT_Subscribe cmnd = Adafruit_MQTT_Subscribe(&mqtt, MQTTSET);
Adafruit_MQTT_Publish state = Adafruit_MQTT_Publish(&mqtt, MQTTSTATE);
Adafruit_MQTT_Publish life = Adafruit_MQTT_Publish(&mqtt, MQTTLIFE);
Adafruit_MQTT_Publish pubip = Adafruit_MQTT_Publish(&mqtt, MQTTIP);

String read_String(char add, int laenge) // read from EEPROM
{
  char data[100]; //Max 100 Bytes
  uint16_t len = 0;
  unsigned char k;
  k = EEPROM.read(add);
  while (k != '\0' && len < laenge) // Read until null character
  {
    k = EEPROM.read(add + len);
    data[len] = k;
    len++;
  }
  data[len] = '\0';
  return String(data);
}

void writeString(char add, String data) // write to EEPROM
{
  uint16_t _size = data.length();
  uint16_t i;
  for (i = 0; i < _size; i++)
  {
    EEPROM.write(add + i, data[i]);
  }
  EEPROM.write(add + _size, '\0'); // Add termination null character for String Data
  EEPROM.commit();
}

/*
   --------------- Set outputs  ---------------
*/
void setOuput(uint8_t ventilationStatus, bool lightSatus) //
{
  switch (ventilationStatus)
  {
  case 1: // Ventilator ON. Low speed
    digitalWrite(outputRealy2, LOW);
    digitalWrite(outputRealy3, LOW);
    delay(100);
    digitalWrite(outputRealy1, HIGH);

    break;
  case 2: // Ventilator ON. Middle speed
    digitalWrite(outputRealy1, LOW);
    digitalWrite(outputRealy3, LOW);
    delay(100);
    digitalWrite(outputRealy2, HIGH);
    break;
  case 3: // Ventilator ON. High speed
    digitalWrite(outputRealy1, LOW);
    digitalWrite(outputRealy2, LOW);
    delay(100);
    digitalWrite(outputRealy3, HIGH);
    break;
  case 4: // Ventilator ON. High speed (ventilationStatus = 3)
    digitalWrite(outputRealy1, LOW);
    digitalWrite(outputRealy2, LOW);
    delay(100);
    digitalWrite(outputRealy3, HIGH);
    break;
  case 0: // Ventilator OFF.
    digitalWrite(outputRealy1, LOW);
    delay(50);
    digitalWrite(outputRealy2, LOW);
    delay(50);
    digitalWrite(outputRealy3, LOW);
    break;

  default: // Ventilator OFF.
    digitalWrite(outputRealy1, LOW);
    delay(50);
    digitalWrite(outputRealy2, LOW);
    delay(50);
    digitalWrite(outputRealy3, LOW);

    break;
  }

  if (lightSatus) // turn Relay on/off (Light)
  {
    digitalWrite(outputRealy4, HIGH);
  }
  else
  {
    digitalWrite(outputRealy4, LOW);
  }
  sendStatus = String(ventilationStatus) + ":" + String(lightStatus);
  state.publish((char *)sendStatus.c_str());
}


IRAM_ATTR void ISR() // Interrupt Service Routine 
{
  hoodStateChanged = true;
  if (ventilationStatus <= 2)
  {
    ventilationStatus++;
  }
  else
  {
    ventilationStatus = 0;
  }
}

void WIFIconnect(void) // Connect WiFi
{
  while (WiFi.status() != WL_CONNECTED)
  {
    delay(5000);
  }
  CounterMQTTReconnectLastWiFi = 0;
  CounterWiFiReconnect++;
}

void MQTT_connect()
{
  int8_t ret;

  if (mqtt.connected()) // Stop if already connected.
  {
    return;
  }

  uint8_t retries = 15;

  while ((ret = mqtt.connect()) != 0) // connect will return 0 for connected
  {
    mqtt.disconnect();
    delay(5000); // wait 5 seconds
    retries--;
    if (retries == 0) // basically die and wait for WDT to reset me
    {
      while (1)
        ;
    }
  }

  CounterMQTTReconnect++;
  CounterMQTTReconnectLastWiFi++;
  if (mqttConnectAfterRestart == false)
  {
    life.publish("MQTT connected after restart");
    mqttConnectAfterRestart = true;
  }
  else
  {
    life.publish("MQTT connected after connection lost");
  }
}

/*
   --------------- LIVE MSG ---------------
*/
void SendCountersMsg()
{
  String lifeMSG = "Life " + String(CounterWork) + " / WIFI: " + String(CounterWiFiReconnect) + " / MQTT(lastWIFI): " + String(CounterMQTTReconnect) + "(" + String(CounterMQTTReconnectLastWiFi) + ")";
  life.publish((char *)lifeMSG.c_str());
  CounterWork++;
}

/*
   ---------------------------------- Setup ----------------------------------
*/
void setup()
{
  EEPROM.begin(512);
  attachInterrupt(digitalPinToInterrupt(inputButtonVentilator), ISR, FALLING);
  pinMode(outputRealy1, OUTPUT);
  pinMode(outputRealy2, OUTPUT);
  pinMode(outputRealy3, OUTPUT);
  pinMode(outputRealy4, OUTPUT);

  s_delayButton = read_String(delayButton_addr, delayButton_len);
  if (s_delayButton.toInt() <= 0)
  {
    i_delayButton = 50;
  }
  else
  {
    i_delayButton = s_delayButton.toInt();
  }

  s_readSubscription = read_String(readSubscription_addr, readSubscription_len);
  if (s_readSubscription.toInt() <= 0)
  {
    i_readSubscription = 100;
  }
  else
  {
    i_readSubscription = s_readSubscription.toInt();
  }

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  WiFi.hostname(moduleName);

  while (WiFi.status() != WL_CONNECTED)
  {
    delay(1000);
  }

  httpUpdater.setup(&httpServer, update_path, update_username, update_password);
  httpServer.begin();
  mqtt.subscribe(&cmnd);
  irrecv.enableIRIn(); // Start the receiver
}

/*
   ---------------------------------- Loop ----------------------------------
*/
void loop()
{

  if (WiFi.status() != WL_CONNECTED)
  {
    WIFIconnect();
  }
  if (mqtt.connected())
  {
  }
  else
  {
    delay(2000);
  }

  /****
    //  OTA
  ****/

  httpServer.handleClient();

  /****
    // Stop if already connected.
  ****/
  if (mqtt.connected() == false)
  {
    if (millis() - mqttConnectInterval >= 10000)
    {
      MQTT_connect();
    }
  }

  /****
    // Send life Message (Interval 2 minute)
  ****/
  if ((unsigned long)(millis() - waitUntil) >= interval)
  {
    SendCountersMsg();
    setOuput(ventilationStatus, lightStatus);
    waitUntil = waitUntil + interval;
  }

  if (hoodStateChanged)
  {
    setOuput(ventilationStatus, lightStatus);
    hoodStateChanged = false;
  }

  /****
    // IR listener
  ****/

  if (irrecv.decode(&results))
  {
    serialPrintUint64(results.value, HEX);
    irrecv.resume(); // Receive the next value
    sendStatus = "";
    switch (results.value)
    {
    case IRReceiveLightOn:
      sendStatus = "Light ON";
      lightStatus = true;

      break;

    case IRReceiveLightOff:
      sendStatus = "Light OFF";
      lightStatus = false;

      break;

    case IRReceiveVentLevel1:
      sendStatus = "VL 1"; // Ventilator Level 1
      ventilationStatus = 1;

      break;

    case IRReceiveVentLevel2:
      sendStatus = "VL 2"; // Ventilator Level 2
      ventilationStatus = 2;

      break;

    case (IRReceiveVentLevel3):
      sendStatus = "VL 3"; // Ventilator Level 3
      ventilationStatus = 3;

      break;

    case (IRReceiveVentLevel4):
      sendStatus = "VL 3"; // Ventilator Level 3 (The same Level as 3 because Hood only 3 Level)
      ventilationStatus = 3;

      break;

    case IRReceiveVentOff:
      sendStatus = "Ventilator OFF";
      ventilationStatus = 0;

      break;

    default:
      sendStatus = "Unknown cmd";

      break;
    }
    state.publish((char *)sendStatus.c_str());
    hoodStateChanged = true;
  }

  /****
    // MQTT listen and action
  ****/
  Adafruit_MQTT_Subscribe *subscription;

  while ((subscription = mqtt.readSubscription(i_readSubscription)))
  {
    if (subscription == &cmnd)
    {
      message = (char *)cmnd.lastread;

      String setting = message.substring(0, 6);

      if (setting.equals("button"))
      {
        if (message.substring(6).length() <= 0)
        {
          String time_debounce = "Button debounce: " + String(i_delayButton);
          state.publish((char *)time_debounce.c_str());
        }
        else
        {
          i_delayButton = message.substring(6).toInt();
          writeString(delayButton_addr, message.substring(6));
        }
      }
      else if (setting.equals("t_mqtt"))
      {
        if (message.substring(6).length() <= 0)
        {
          String read_mqtt = "ReadMQTT: " + String(i_readSubscription);
          state.publish((char *)read_mqtt.c_str());
        }
        else
        {
          i_readSubscription = message.substring(6).toInt();
          writeString(readSubscription_addr, message.substring(6));
        }
      }
      if (message.charAt(0) == 'v' || message.charAt(0) == 'l')
      {
        if (message.equals("v1_on"))
        {
          ventilationStatus = 1;
        }
        else if (message.equals("v2_on"))
        {
          ventilationStatus = 2;
        }
        else if (message.equals("v3_on"))
        {
          ventilationStatus = 3;
        }
        else if (message.equals("v_off"))
        {
          ventilationStatus = 0;
        }
        else if (message.equals("l_on"))
        {
          lightStatus = true;
        }
        else if (message.equals("l_off"))
        {
          lightStatus = false;
        }
        hoodStateChanged = true;
      }
      else if (message.equals("resetcounterwifi"))
      {
        CounterWiFiReconnect = 0;
      }
      else if (message.equals("resetcountermqtt"))
      {
        CounterMQTTReconnect = 0;
        CounterMQTTReconnectLastWiFi = 0;
      }
      else if (message.equals("resetcounterloop"))
      {
        CounterWork = 0;
      }
      else if (message.equals("resetcounter"))
      {
        CounterWiFiReconnect = 0;
        CounterMQTTReconnect = 0;
        CounterMQTTReconnectLastWiFi = 0;
        CounterWork = 0;
      }
      else if (message.equals("help"))
      {
        state.publish("resetcounter/resetcounter -mqtt/-loop/-wifi,t_mqttxxx(t_mqtt100),buttonxxx(button50),reboot");
      }
      else if (message.equals("reboot"))
      {
        ESP.restart();
      }
      else if (message.equals("ip"))
      {
        String iPAndTopic = "" + WiFi.localIP().toString() + ": " MQTTSET;
        pubip.publish((char *)iPAndTopic.c_str());
      }
    }
  }
}
