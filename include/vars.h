#define outputRealy1 16 
#define outputRealy2 14 
#define outputRealy3 12 
#define outputRealy4 13 
#define inputButtonVentilator  5 // the number of the pushbutton pin
const uint16_t INPUT_IR_PIN = 4;
const char *update_path = "/update"; // http adress for firmware update (http://192.168.***.***/update)
const char *update_username = "admin";
const char *update_password = "admin";

// General
bool mqttConnectAfterRestart = false, lightStatus = false;

volatile uint8_t ventilationStatus = 0; 
volatile bool hoodStateChanged = false;
uint32_t CounterWiFiReconnect = 0, CounterMQTTReconnect = 0, CounterMQTTReconnectLastWiFi = 0, CounterWork = 0;

long mqttConnectInterval = 0;

unsigned long interval = 120000, waitUntil =0;

String sendStatus = "";
String message = "";

String s_delayButton;
uint8_t delayButton_addr = 0;
uint8_t delayButton_len = 4;
uint8_t i_delayButton = 50;

String s_readSubscription;
uint8_t readSubscription_addr = 5;
uint8_t readSubscription_len = 4;
uint8_t i_readSubscription = 100;