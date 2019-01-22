/*WickerMan Version 0.1

https://github.com/PaulGarrett/WickerMan

WickerMan is a controller for garden wicking beds that:
	measures and reports soil moisture and temperature for an array of wicking beds
	allows remote refilling of each bed's water reservoir
	allows setting a soil moisture threshold for automatic refilling when the bed gets too dry
	has an air temperature and humidity sensor for each array of beds ("system")
	uses the mqtt protocol to allow all functions to be controlled remotely over wifi
	provides a graphical tool for managing your wicking beds via Node-red
	sends email alerts for critical failures

NOTE: This version (0.1) is for in-home demonstation only unless you secure your MQTT broker and Node_red server.

Hardware (for an array of two wicking beds):
	1 x Adafruit Feather HUZZAH ESP8266 or breakout board
	1 x Adafruit DS3231 RTC or equivalent or DS3232
	1 x DHT22 Humidity and Temperature Sensor
	2 x Adafruit STEMMA Soil Sensor - from Adafruit or your local agent
	2 x 12v normally closed Plastic Solenoid Valve - eg. From valves4projects on ebay
	2 x Float switches - ebay search for "Water Level Switch Liquid Level Sensor Plastic Vertical Float"
	2 x Rectifier Diode (across terminals of each solenoid)
	2 x Basic FET N-Channel (for activation of solenoids)
	4 x .25 watt 10k Resistors (pull downs for solenoid FETs and float swiches)
	(note: depending on user power requirements, add a DC-DC step down switching converter (12v to 5v), 
	such as the TSR-1 2450 and a 22uf capacitor, to power microcontroller and sensors from 
	same 12v 1A source as the solenoids)
	
MicroController Software
	ESP board settings etc for Arduino IDE
	ESP8266Wifi lib
	mqtt PubSubClient lib
	Adafruit seesaw lib
	others, see code below
	
Other
	Node-red Dashboard for control of settings and functions
	
Hardcoded settings
	This version of Wickerman uses the following hardcoded values which should be changed to suit the user

		int waterLevelPin[]={12,16}
		int fillSolenoidPin[]={13,15}
		i2c addresses for ss ssAddr[]= {0x36,0x37}
		fillingCheckInterval  = 2e3; //(2 seconds) - check waterLevelPin[] loop for when a bed is filling
		dawn=25771680; // an arbitrary date in unix "minutes" for the "beginning of time" (00:00 on 01/01/2019)
		ssid = "<insert your wifi ssid>";
		password = "<insert your wifi password>";
		mqtt_server = "<10.10.10.225>"; replace <> with the address of you mqtt server
		
*/

#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include "Adafruit_seesaw.h"
#include <avr/pgmspace.h>
#include <Wire.h>
#include "RTClib.h"
#include <EEPROM.h>
#include "DHT.h"

#define DHTPIN 14  

#define PROGMEM   ICACHE_RODATA_ATTR
#define FPSTR(pstr_pointer) (reinterpret_cast<const __FlashStringHelper *>(pstr_pointer))
#define F(string_literal) (FPSTR(PSTR(string_literal)))
#define countof(a) (sizeof(a) / sizeof(a[0]))

#define DHTTYPE DHT22   // DHT 22  (AM2302, AM2321)
DHT dht(DHTPIN, DHTTYPE);

RTC_DS3231 rtc; // i2c address is 0x68 but this is handled in the library. Note that the DS3232 also works fine with this library

Adafruit_seesaw ss; // for i2c soil sensors

// PROGMEM strings for mqtt topics and some text-heavy payloads

static const char string_0[] PROGMEM = "wickingBeds/";
static const char string_1[] PROGMEM = "airTemp";
static const char string_2[] PROGMEM = "humidity";
static const char string_3[] PROGMEM = "soilTemp";
static const char string_4[] PROGMEM = "soilMoisture";
static const char string_5[] PROGMEM = "lastFilledDateTime";
static const char string_6[] PROGMEM = "daysHoursSince";
static const char string_7[] PROGMEM = "init";
static const char string_8[] PROGMEM = "days";
static const char string_9[] PROGMEM = "hours";
static const char string_10[] PROGMEM = "currentTime";
static const char string_11[] PROGMEM = "fillNow"; //sub
static const char string_12[] PROGMEM = "auto"; //sub
static const char string_13[] PROGMEM = "threshold"; //sub
static const char string_14[] PROGMEM = "ALERT";  // _pub - for time over-runs / failures

//Timer Strings and alerts
static const char string_15[] PROGMEM = "minutes"; 
static const char string_16[] PROGMEM = "seconds"; 
static const char string_17[] PROGMEM = "refill timed out at"; 
static const char string_18[] PROGMEM = "auto off"; 
static const char string_19[] PROGMEM = "check bed"; 
static const char string_20[] PROGMEM = "took"; 
static const char string_21[] PROGMEM = "readingsInterval"; 
static const char string_22[] PROGMEM = "noRefillInterval"; 
static const char string_23[] PROGMEM = "maxTimeRefill"; 
static const char string_24[] PROGMEM = "msg";
static const char string_25[] PROGMEM = "resolved";
static const char string_26[] PROGMEM = "filledSuccess";
static const char string_27[] PROGMEM = "fillingFail";
const char* const string_table[] PROGMEM = {string_0, string_1, string_2, string_3, string_4, string_5, string_6, string_7, string_8, string_9, string_10, string_11,string_12,string_13,string_14,string_15,string_16,string_17,string_18,string_19,string_20, string_21, string_22, string_23, string_24, string_25, string_26, string_27};

char daysOfTheWeek[7][5] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};

String topic;

int ssAddr[]= {0x36,0x37}; // i2c addresses for Adafruit soil moisture sensors
//GPIO pin assignments
int waterLevelPin[]={12,16};  // - change if required
int fillSolenoidPin[]={13,15};// - change if required


char topic1[15];
char topic2[25];
char topic3[25];
char topic4[25];
char value[45];
char topicstring[55];
char alertmsg [55];

// TIME VARS
unsigned long previousReadingsSeconds= 0;
unsigned long previousFillingSeconds[3];
unsigned long readingsInterval = 2; // MINUTES change to higher value via node-Red / mqtt after initial setup
const long fillingCheckInterval  = 2e3; //2 seconds - change if required
unsigned long noRefillInterval[3]; // HOURS - change to higher value via node-Red / mqtt after initial setup
unsigned long lastautofill[3];// only used to check against noRefillInterval[]
unsigned long maxTimeRefill[3]={5,5,5}; // fill timeout in minutes for bed to refill - setable from node_red or mqtt
unsigned long fillStartTime[3]; // time refilling starts (unixtime)
unsigned long fillEndTime[3]; // time refilling stops (unixtime)
int overRun[3]={0,0,0}; // set to 1 by sendAlert() fn if filling timed out - reset to 0 when auto turned back to 1 or manual fill started
unsigned int fillDurationMinutes[] = {0,0,0};
unsigned int fillDurationSeconds[] = {0,0,0};


char datestring[25];
char hoursMinutesString[20];
long dawn=25771680; // prev 25761600 .unixtime in minutes at 00:00 25/12/2018 reset to 25771680 (00:00 1/1/2019)
uint16_t hoursSinceDawnNow;
uint16_t filledHoursSinceDawn[2];

 // change or set by running utility pgm Wman_setup.ino
uint8_t sysID;
uint8_t numBeds; // need to read this from EEPROM in setup()

float airTemp;
float soilTemp;
float humidity;
uint8_t soilMoisture;

int autoFill[3];
int threshold[3];
int fillNow[]={0,0,0};

int initialised=0; // flag to reset settings or upload new init data from EEPROM

int fillingNow[] ={0,0,0};
int fillFlag[]={0,0,0};

// network  -insert your network details here

const char* ssid = "yourNetworkssid";
const char* password = "yourNetworkPassword";
const char* mqtt_server = "local.ip.of.mqtt.server";

WiFiClient espClient;
PubSubClient client(espClient);

//**********************************************************************************************************************************
void setup_wifi() {

  delay(10);
  // We start by connecting to a WiFi network
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(ssid);

  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  randomSeed(micros());

  Serial.println("");
  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());
}

//**********************************************************************************************************************************
void callback(char* topic, byte* payload, unsigned int length) {  // this where we catch subscribed topics and deal with payloads

// Note: several users of the PubSubClient library report that this function MUST come higher than setup() and loop()
// That's why it is here. I also found that calls to other fns from here don't work.

Serial.println("CallBack");
 
	
	if ((char)topic[16]=='a'){ // topic includes "auto", 
		int bed = (int)topic[14]-48; // bedNum is topic[14] , convert ascii to int
		if ((int)payload[0] - 48 != autoFill[bed-1]){
		autoFill[bed-1] = (int)payload[0] - 48;
			EEPROM.write (10+15*(bed-1)+1,autoFill[bed-1] );
			EEPROM.commit();
			initialised =0;  // ensures main loop runs immediately and that new value is sent via mqtt to broker
		}
}

	if ((char)topic[16]=='t'){ // looking for "threshold"
		int bed = (int)topic[14]-48; // bedNum is topic[14] , convert ascii to int
		payload[length] = '\0'; // Make payload a string by NULL terminating it.
		threshold[bed-1] = atoi((char *)payload);; // 
		if (threshold[bed-1] != EEPROM.read(10+15*(bed-1)+2 )){
			EEPROM.write(10+15*(bed-1)+2,threshold[bed-1] );
			EEPROM.commit();
			initialised=0; // ensures main loop runs immediately and that new value is sent via mqtt to broker
		
			
			
			}
}

	if ((char)topic[16]=='m'){ // looking for "maxTimeRefill"
		int bed = (int)topic[14]-48; // bedNum is topic[14] , convert ascii to int
		payload[length] = '\0'; // Make payload a string by NULL terminating it.
		maxTimeRefill[bed-1] = atoi((char *)payload);; // 
		if (maxTimeRefill[bed-1] != EEPROM.read(10+15*(bed-1)+12)){
			EEPROM.write(10+15*(bed-1)+12,maxTimeRefill[bed-1] );
			EEPROM.commit();
			initialised=0; // ensures main loop runs immediately and that new value is sent via mqtt to broker
	}
}

	if ((char)topic[16]=='n'){ // looking for "noRefillInterval"
		int bed = (int)topic[14]-48; // bedNum is topic[14] , convert ascii to int
		payload[length] = '\0'; // Make payload a string by NULL terminating it.
		noRefillInterval[bed-1] = atoi((char *)payload);; // 
		if (noRefillInterval[bed-1] != EEPROM.read(10+15*(bed-1)+11)){
			EEPROM.write(10+15*(bed-1)+11,noRefillInterval[bed-1] );
			EEPROM.commit();
			initialised=0; // ensures main loop runs immediately and that new value is sent via mqtt to broker
	}
}
	if ((char)topic[14]=='r'){ // looking for "readingsInterval"
		payload[length] = '\0'; // Make payload a string by NULL terminating it
		readingsInterval = atoi((char *)payload);
		if (readingsInterval !=  EEPROM.read(2)){
			EEPROM.write(2,readingsInterval);
			EEPROM.commit();
			initialised=0; // ensures main loop runs immediately and that new value is sent via mqtt to broker
	}
}

	if ((char)topic[16]=='f'){// looking for "fillNow"
		int bed = (int)topic[14]-48; // bedNum is topic[14] , convert ascii to int
		if (fillingNow[bed-1]!=1){
			fillFlag[bed-1]=1; //  flag for main loop. 
			}
		if (fillingNow[bed-1]){
			Serial.println("Filling Already Happening");
		}

  }
  
}


//********************************************************************************************************************************
void reconnect() {
  // Loop until we're reconnected
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    // Create a random client ID
    String clientId = "ESP8266Client-";
    clientId += String(random(0xffff), HEX);
    // Attempt to connect
    if (client.connect(clientId.c_str())) {
      Serial.println("connected");
      // Once connected, publish an announcement...
      client.publish("wickingBeds/reconnected", "reconnected");    
	    
// ... and re/subscribe to auto, threshold and fillNow topics
strcpy_P(topic1, (PGM_P)string_table[0]); // 

for (int i=0; i< numBeds; i++){

		for (int s=11; s<14;s++){
	
strcpy_P(topic2, (PGM_P)string_table[s]);
	
    snprintf_P(topicstring, 
            countof(topicstring),
            PSTR("%s%1u/%1u/%s"),    
	topic1,
	sysID,
	i+1,
	topic2);


Serial.println (topicstring);

client.subscribe (topicstring);	
		}
//  add subs for noRefillInterval  and maxTimeRefill
	for (int s=22; s<24;s++){
	
strcpy_P(topic2, (PGM_P)string_table[s]);
	
    snprintf_P(topicstring, 
            countof(topicstring),
            PSTR("%s%1u/%1u/%s"),    
	topic1,
	sysID,
	i+1,
	topic2);

Serial.println (topicstring);

client.subscribe (topicstring);	
		}	
				
	}      
	
// ... and resubscribe to "/sysID/readingsInterval"
strcpy_P(topic2, (PGM_P)string_table[21]);
	
    snprintf_P(topicstring, 
            countof(topicstring),
            PSTR("%s%1u/%s"),    
	topic1,
	sysID,
	topic2);


Serial.println (topicstring);

client.subscribe (topicstring);	
		
Serial.println ("subscriptions done"); 

	
    } 
    else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      // Wait 5 seconds before retrying
      delay(5000);
    }
  }
}

//*****************************************************************************************************************************
void setup() {

  Serial.begin(115200);
  setup_wifi();
  client.setServer(mqtt_server, 1883);
  client.setCallback(callback);
EEPROM.begin(128);

sysID=EEPROM.read(0);
numBeds=EEPROM.read(1);
	
// if either of these is 0 or out of range, a fresh EEPROM set up is needed
	// stall the sketch here if 0 or out of range
// This version (0.1) uses Wman_setup.ino utility sketch
// to clear and populate EEPROM
// For v0.2, review rLoad() and process for setup from scratch,
// including whether this can be done from Node-red Dashboard
	if ((sysID<1)||(sysID>9)||(numBeds<1)||(numBeds>3)){
		Serial.println("SYSTEM EEPROM NOT SETUP - Use WMan_setup.ino before running");
	while (initialised==0){
	}
}
// pin modes
for(int i =0; i<numBeds;i++){
	  pinMode (waterLevelPin[i], INPUT);
	  pinMode (fillSolenoidPin[i], OUTPUT);
}
for(int i =0; i<numBeds;i++){
digitalWrite(fillSolenoidPin[i], LOW);
}
dht.begin();

if (! rtc.begin()) {
    Serial.println("Couldn't find RTC");
    while (1);
}
// reset rtc to compile time

rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
DateTime now = rtc.now();

}
//****************************************************************************************************************************
void loop() {

  if (!client.connected()) {
    reconnect();
  }
  client.loop();

// every 2 seconds, check any beds that are filling now   
for (int i = 0; i<numBeds; i++){
	if (fillingNow[i]==1 ){
		if (millis()-previousFillingSeconds[i]>= fillingCheckInterval) {
		checkFillingNow(i);
			previousFillingSeconds[i]=millis();
		}
	}
//  check whether "FillNow[] has been received
	if (fillFlag[i]==1) {
		startFilling(i);
	}
}  

// main loop for getting and publishing sensor data

DateTime now = rtc.now();

if((now.unixtime()>=previousReadingsSeconds+readingsInterval*60) || (initialised==0)){// start of sensor reading loop
 
	if (initialised==0){
	Serial.println("initialisation required");
	rLoad();		// if new boot or change to subscribed settings run rLoad() fn
	initialised=1; // sets initialised to 1 so it doesn't run again
}
	
pubCurrentTime(); // updates "Last Updated" timestamp on Overview page

// Note: for consistency, all mqtt payloads are published as strings

// airTemerature
 float airTemp = dht.readTemperature();
 
//get temp as <value> - a string with one decimal
dtostrf(airTemp,2, 1, value);
	
strcpy_P(topic1, (PGM_P)string_table[0]); // 
strcpy_P(topic2, (PGM_P)string_table[1]);

    snprintf_P(topicstring, 
            countof(topicstring),
            PSTR("%s%1u/%s"),    
      topic1,
      sysID,
      topic2);

// 	topicstring=wickingBeds/<sysID>/airTemp 

client.publish(topicstring, value);

Serial.print(topicstring);
Serial.print(" :");
Serial.println(value);

// humidity
	
float humidity = dht.readHumidity();
// humidity as <value> to a string

dtostrf(humidity,2, 0, value);

// strcpy_P(topic1, (PGM_P)string_table[0]); // 
strcpy_P(topic2, (PGM_P)string_table[2]);

    snprintf_P(topicstring, 
            countof(topicstring),
            PSTR("%s%1u/%s"),    
      topic1,
      sysID,
      topic2);

//topicstring=wickingBeds/<sysID>/humidity

client.publish(topicstring, value);

Serial.print(topicstring);
Serial.print(" :");
Serial.println(value);

// Check if any DHT reads failed and exit early (to try again).
  if (isnan(humidity) || isnan(airTemp)) {
    Serial.println("Failed to read from DHT sensor!");
  }

// get soil sensor readings for all beds

for (int j= 0; j < numBeds; j++) {

// catch errors first
if (!ss.begin(ssAddr[j])) {
    Serial.println("ERROR! sensor not found");
    Serial.println(ssAddr[j], HEX);
}

// soilTemperature

float soilTemp = ss.getTemp();

// soilTemp as <value> - a string with one decimal
dtostrf(soilTemp,2, 1, value);

// construct sprintf topicstring
//strcpy_P(topic1, (PGM_P)string_table[0]); // 
strcpy_P(topic2, (PGM_P)string_table[3]);

    snprintf_P(topicstring, 
            countof(topicstring),
            PSTR("%s%1u/%1u/%s"),    
      topic1,
      sysID,
	j+1,
      topic2);
	
//	topicstring= wickingBeds/<sysID>/<bedNumber>/soilTemp

client.publish(topicstring, value);
Serial.print(topicstring);
Serial.print(" :");
Serial.println(value);

// soilMoisture

uint16_t soilMoisture = ss.touchRead(0);

soilMoisture = map(soilMoisture, 30, 1024, 0, 100);

// soilMoisture as <value> as a string

itoa (soilMoisture,value,10);

//construct sprintf topicstring

//strcpy_P(topic1, (PGM_P)string_table[0]); // 
strcpy_P(topic2, (PGM_P)string_table[4]);

    snprintf_P(topicstring, 
            countof(topicstring),
            PSTR("%s%1u/%1u/%s"),    
      topic1,
      sysID,
	j+1,
      topic2);

//	topicstring= wickingBeds/<sysID/<bedNumber>/soilMoisture

client.publish(topicstring, value);

Serial.print(topicstring);
Serial.print(" :");
Serial.println(value);

//check to see if autoFill is necessary
			
DateTime now = rtc.now();

if ((now.unixtime() < noRefillInterval [j]*360 + lastautofill[j])&& (soilMoisture<=threshold[j])){
	Serial.print("To soon for auto refill of Bed ");
	Serial.println(j+1);
}
			
else if ((autoFill[j]==1) && (soilMoisture<=threshold[j]) && (fillingNow[j]!=1))  {
	Serial.print("Start Filling Triggered for Bed ");
	Serial.println (j+1);
	lastautofill[j]=now.unixtime();
	startFilling(j);
}

} // end soil sensor loop

//publish days/hours since previousFillHour

for (int j=0; j<numBeds; j++){
	if (fillingNow[j]!=1){
		timeSinceFilled(j);  // fn
	}
}
// reset previousReadingsSeconds

previousReadingsSeconds = now.unixtime();
} // end of sensor reading loop

} // end of main loop()

//***********************************************************
void startFilling(int i){ // tiggered from main loop() when filling required

	Serial.println();
	Serial.print("startFilling Function started for Bed ");
	Serial.print(i+1);
	Serial.println();
DateTime now = rtc.now();	
fillStartTime[i]=now.unixtime()	;
	
digitalWrite(fillSolenoidPin[i], HIGH); //open solenoid
	
fillingNow[i]=1; // flag
fillFlag[i]=0;
previousFillingSeconds[i] = millis();

char val1[]="Filling Now";
	
// create the topicstring
	strcpy_P(topic1, (PGM_P)string_table[0]); // 
	strcpy_P(topic2, (PGM_P)string_table[5]);

	snprintf_P(topicstring, 
		countof(topicstring),
		PSTR("%s%1u/%1u/%s"),    
	topic1,
	sysID,
	i+1,
	topic2);
	
client.publish(topicstring, val1);

// blank out the "daysHoursSince" value
char val2[] = " ";
//strcpy_P(topic1, (PGM_P)string_table[0]); // 
strcpy_P(topic2, (PGM_P)string_table[6]);

    snprintf_P(topicstring, 
            countof(topicstring),
            PSTR("%s%1u/%1u/%s"),    
      topic1,
      sysID,
	i+1,
      topic2);
//topic = "wickingBeds/<sysID>/<bedNumber>/daysHoursSince"

client.publish(topicstring, val2);

// zero for the "filledSuccess" graph

char val3[]="0";
// create the topicstring[]

strcpy_P(topic2, (PGM_P)string_table[26]); // 
    snprintf_P(topicstring, 
            countof(topicstring),
            PSTR("%s%1u/%1u/%s"),    
      topic1,
      sysID,
	i+1,
      topic2);


client.publish(topicstring, val3);
}
//***************************************************************************
void checkFillingNow (int i) {

// checks the state of the water-level float sensor and, if the reservoir
// is full, stop filling and save and publish the date and time. Otherwise, pass straight through

	Serial.println();
	Serial.print("Checking Filling Status for Bed ");
	Serial.println(i+1);
	Serial.print("waterLevel sensor reading is ");
	Serial.println(digitalRead(waterLevelPin[i]));

if (digitalRead(waterLevelPin[i]) == 0){// pin reads "0" when water-level sensor closes - ie reservoir is full
	
	DateTime now = rtc.now();  
	
	digitalWrite (fillSolenoidPin[i], LOW); // turn off solenoid
	initialised=0;
	
// write fill time details to EEPROM
	EEPROM.begin(128);
		EEPROM.write (10+15*i+3,now.minute());
		EEPROM.write (10+15*i+4,now.hour());
		EEPROM.write (10+15*i+5,now.day());
		EEPROM.write (10+15*i+6,now.month());
		EEPROM.write (10+15*i+7,now.year()-2000);
		
		hoursSinceDawnNow= (now.unixtime()/60-dawn)/60; //hours since 25/12/2018 for reference
	
		EEPROM.write (10+15*i+8,lowByte(hoursSinceDawnNow));
		EEPROM.write (10+15*i+9,highByte(hoursSinceDawnNow));
		EEPROM.commit();	// should this be repeated under each write above?
	
//construct datestring for now as "Fri 28/12 @ 16:45"

	snprintf_P(datestring, 
		countof(datestring),
		PSTR("%s %02u/%02u @ %02u:%02u"),    
	daysOfTheWeek[now.dayOfTheWeek()],
	now.day(),
	now.month(),
	now.hour(),
	now.minute());
	
		Serial.println();
		Serial.print("Filling finished at ");
		Serial.println(datestring);


// create the topicstring[] for wickingBeds/<sysID>/<bedNumber>/lastFilledDateTime
	strcpy_P(topic1, (PGM_P)string_table[0]); // 
	strcpy_P(topic2, (PGM_P)string_table[5]);

	snprintf_P(topicstring, 
		countof(topicstring),
		PSTR("%s%1u/%1u/%s"),    
	topic1,
	sysID,
	i+1,
	topic2);

client.publish(topicstring, datestring);

		Serial.print("published as ");
		Serial.println(topicstring);
		Serial.println(datestring);
		
// construct wickingBeds/<sysID>/<bedNumber>/daysHoursSince topic and publish with an initial value of "Just Now"

char val1[] = "Just Now";

strcpy_P(topic2, (PGM_P)string_table[6]);

    snprintf_P(topicstring, 
            countof(topicstring),
            PSTR("%s%1u/%1u/%s"),    
      topic1,
      sysID,
	i+1,
      topic2);

client.publish(topicstring, val1);

//*******************************************************************************
// send a"100" string to topic "wickingBeds/<sysID>/<bedNumber>/filledSuccess" for graphing

char val2[] = "100";
// create the topicstring[]
strcpy_P(topic2, (PGM_P)string_table[26]); // 

    snprintf_P(topicstring, 
            countof(topicstring),
            PSTR("%s%1u/%1u/%s"),    
      topic1,
      sysID,
	i+1,
      topic2);

client.publish(topicstring, val2);

// send a"0" string to topic "wickingBeds/<sysID>/<bedNumber>/filledSuccess" for neater graphing
char val3[]="0";

client.publish(topicstring, val3);
//*********************************************************************************

// reset fillingNow[] flag
fillingNow[i]=0;
fillEndTime[i] = now.unixtime();
fillDurationMinutes[i] = (fillEndTime[i]-fillStartTime[i])/60;
fillDurationSeconds[i] = (fillEndTime[i]-fillStartTime[i])%60;

//contruct msg message text with fill duration

strcpy_P(topic1, (PGM_P)string_table[20]);
strcpy_P(topic2, (PGM_P)string_table[15]);
strcpy_P(topic3, (PGM_P)string_table[16]);


   snprintf_P(alertmsg, 
            countof(alertmsg),
            PSTR("%s  %2u  %s  %2u %s"),    
      topic1,
      fillDurationMinutes[i],
      topic2,
      fillDurationSeconds[i], 
      topic3);

	
//construct msg topic
strcpy_P(topic1, (PGM_P)string_table[0]);
strcpy_P(topic2, (PGM_P)string_table[24]); //msg

snprintf_P(topicstring, 
            countof(topicstring),
            PSTR("%s%1u/%1u/%s"),    
      topic1,
      sysID,
	i+1,
      topic2);
      
client.publish(topicstring, alertmsg);

	} // end of "stop filling" loop
	
else{
char val1[]="Filling Now";
	
// create the topicstring
	strcpy_P(topic1, (PGM_P)string_table[0]); // 
	strcpy_P(topic2, (PGM_P)string_table[5]);

	snprintf_P(topicstring, 
		countof(topicstring),
		PSTR("%s%1u/%1u/%s"),    
	topic1,
	sysID,
	i+1,
	topic2);
	
client.publish(topicstring, val1);

// blank out the "daysHoursSince" value
char val2[] = " ";
//strcpy_P(topic1, (PGM_P)string_table[0]); // 
strcpy_P(topic2, (PGM_P)string_table[6]);

    snprintf_P(topicstring, 
            countof(topicstring),
            PSTR("%s%1u/%1u/%s"),    
      topic1,
      sysID,
	i+1,
      topic2);
//topic = "wickingBeds/<sysID>/<bedNumber>/daysHoursSince"

client.publish(topicstring, val2);

//Check for time elapsed
DateTime now = rtc.now();	
	if ((now.unixtime()-fillStartTime[i])/60 >= maxTimeRefill[i] ) {
		overRun[i]=sendAlert(i);
	}
}
	

/*Check for time elapsed
DateTime now = rtc.now();	
	if ((now.unixtime()-fillStartTime[i])/60 >= maxTimeRefill[i] ) {
		overRun[i]=sendAlert(i);
	}

*/
} // eof

//****************************************************************
void rLoad() { // called from top of loop() if initialised flag=0
	

Serial.println();
Serial.println ("initialisation activated.....");
Serial.println();	
int year[numBeds];
//initialises interface - gets data from EEPROM and publish[i+1] for threshold, 
//auto, lastFilled etc
DateTime now=rtc.now();
// ***********************
//check and initialise readingsInterval to default value (5 seconds) if EEPROM blank or > 60 minutes

if ((EEPROM.read(2) < 1) ||  (EEPROM.read(2) > 60)){
		readingsInterval=2;
		EEPROM.write (2, readingsInterval);
		EEPROM.commit();
}

else {
if (readingsInterval!=EEPROM.read(2)){
readingsInterval=EEPROM.read(2);
}
char valx[3];
itoa(readingsInterval, valx,10);
strcpy_P(topic1, (PGM_P)string_table[0]); // First occurance of string_table[0]) in this scope
strcpy_P(topic2, (PGM_P)string_table[7]);
strcpy_P(topic3, (PGM_P)string_table[21]);

    snprintf_P(topicstring, 
            countof(topicstring),
            PSTR("%s%1u/%s/%s"),    
      topic1,
      sysID,
      topic2,
      topic3);
client.publish(topicstring, valx);
}
//*********************
	
for (int i=0; i<numBeds;i++){

	
if (EEPROM.read(10+15*i+7)==0){ // following runs only if EEPROM value for year has been cleared (useful for total reset)
	
//re/initialise readingsInterval for system 
Serial.println();
Serial.print("Bed ");
Serial.print(i+1);	
Serial.println(" set up with default data");

char  val1[]= "NEVER";
   	
// construct lastFilled topicstring	
strcpy_P(topic1, (PGM_P)string_table[0]); // First occurance of string_table[0]) in this scope
strcpy_P(topic2, (PGM_P)string_table[5]);

    snprintf_P(topicstring, 
            countof(topicstring),
            PSTR("%s%1u/%1u/%s"),    
      topic1,
      sysID,
	i+1,
      topic2);
	
client.publish(topicstring, val1);	
// daysHoursSince = blank

char val2[] = " ";
	
// construct daysHoursSince topicstring
strcpy_P(topic1, (PGM_P)string_table[0]); 
strcpy_P(topic2, (PGM_P)string_table[6]);

    snprintf_P(topicstring, 
            countof(topicstring),
            PSTR("%s%1u/%1u/%s"),    
      topic1,
      sysID,
	i+1,
      topic2);
      
//topicstring = "wickingBeds/<sysID>/<bedNumber>/daysHoursSince"

client.publish(topicstring, val2);

// initialise "auto" to OFF
char val3[] = "0";
 
//construct ...../init/auto topic
strcpy_P(topic1, (PGM_P)string_table[0]); 
strcpy_P(topic2, (PGM_P)string_table[7]);
strcpy_P(topic3, (PGM_P)string_table[12]);

    snprintf_P(topicstring, 
            countof(topicstring),
            PSTR("%s%1u/%1u/%s/%s"),    
      topic1,
      sysID,
	i+1,
      topic2,
      topic3);
      
//topicstring = "wickingBeds/<sysID>/<bedNumber>/init/auto"

client.publish(topicstring, val3);
if (EEPROM.read (10+15*1+1) != 0){
EEPROM.write (10+15*i+1,0);
EEPROM.commit();
}
//construct /init/threshold topic and publish -m 25 -  a low value

char val4[] = "25";

// create the topicstring[]
strcpy_P(topic1, (PGM_P)string_table[0]); // First occurance of string_table[0]) in this scope
strcpy_P(topic2, (PGM_P)string_table[7]);
strcpy_P(topic3, (PGM_P)string_table[13]);

    snprintf_P(topicstring, 
            countof(topicstring),
            PSTR("%s%1u/%1u/%s/%s"),    
      topic1,
      sysID,
	i+1,
      topic2,
      topic3);
      
//topicstring = "wickingBeds/<sysID>/<bedNumber>/init/threshold"

client.publish(topicstring, val4);
if (EEPROM.read (10+15*1+2) != 25){
EEPROM.write (10+15*i+2,25);
EEPROM.commit();
}
//************************************
// initialise default values for noRefillInterval[] and maxTimeRefill[] for yr =0

char val5[]= "2"; // default 2hrs between refill attempts

//construct .....wickingBeds/<sysID>/<bedNumber>/init/noRefillInterval  topic
strcpy_P(topic1, (PGM_P)string_table[0]); // First occurance of string_table[0]) in this scope
strcpy_P(topic2, (PGM_P)string_table[7]);
strcpy_P(topic3, (PGM_P)string_table[22]);

    snprintf_P(topicstring, 
            countof(topicstring),
            PSTR("%s%1u/%1u/%s/%s"),    
      topic1,
      sysID,
	i+1,
      topic2,
      topic3);
      
//topicstring = "wickingBeds/<sysID>/<bedNumber>/init/noRefillInterval"

client.publish(topicstring, val5);
if (EEPROM.read (10+15*1+11) != 60){
EEPROM.write (10+15*i+11,60);
EEPROM.commit();
}


// initialise "maxTimeRefill" to 5
char val6[] = "5"; // default refill time-out to 5 minutes
 
//construct ...../init/maxTimeRefill  topic,
strcpy_P(topic1, (PGM_P)string_table[0]); // First occurance of string_table[0]) in this scope
strcpy_P(topic2, (PGM_P)string_table[7]);
strcpy_P(topic3, (PGM_P)string_table[23]);

    snprintf_P(topicstring, 
            countof(topicstring),
            PSTR("%s%1u/%1u/%s/%s"),    
      topic1,
      sysID,
	i+1,
      topic2,
      topic3);
      
//topicstring = "wickingBeds/<sysID>/<bedNumber>/init/maxTimeRefill"

client.publish(topicstring, val6);

if (EEPROM.read (10+15*1+12) != 5){
EEPROM.write (10+15*i+12,5);
EEPROM.commit();
}

// blank Alert and msg fields - val2[] already = ""

strcpy_P(topic1, (PGM_P)string_table[0]); // First occurance of string_table[0]) in this scope
strcpy_P(topic2, (PGM_P)string_table[14]);

snprintf_P(topicstring, 
            countof(topicstring),
            PSTR("%s%1u/%1u/%s"),    
      topic1,
      sysID,
	i+1,
      topic2);
      
client.publish(topicstring, val2);

//construct msg topic

strcpy_P(topic1, (PGM_P)string_table[0]);
strcpy_P(topic2, (PGM_P)string_table[24]);

snprintf_P(topicstring, 
            countof(topicstring),
            PSTR("%s%1u/%1u/%s"),    
      topic1,
      sysID,
	i+1,
      topic2);
      
client.publish(topicstring, val2);

//************** 
}
else { // if initialised set to zero because a setting has changed or initialised set to 0 on boot

Serial.println();
Serial.print("Bed ");
Serial.print(i+1);	
Serial.println(" set up with stored data");
	
//construct datestring for lastFilledDateTime[] from EEPROM as "28/12 @ 16:45"

	snprintf_P(datestring, 
		countof(datestring),
		PSTR("%02u/%02u @ %02u:%02u"),    
	EEPROM.read (10+15*i+5),
	EEPROM.read (10+15*i+6),
	EEPROM.read (10+15*i+4),
	EEPROM.read (10+15*i+3));
	
// create the topicstring
	strcpy_P(topic1, (PGM_P)string_table[0]); 
	strcpy_P(topic2, (PGM_P)string_table[5]);

	snprintf_P(topicstring, 
		countof(topicstring),
		PSTR("%s%1u/%1u/%s"),    
	topic1,
	sysID,
	i+1,
	topic2);
	
//topicstring= wickingBeds/<sysID>/<bedNumber>/lastFilledDateTime

if (EEPROM.read(10+15*i+7)==0){
client.publish(topicstring,"NEVER");
}
else {


client.publish(topicstring, datestring);
}
timeSinceFilled(i); // calc and publish daysHoursSince - fn cals and publishes

//##############################################
// for autoFill, threshold, etc ensure current values match EEPROM values



autoFill[i]=EEPROM.read(10+15*i+1);
itoa(autoFill[i], value,10);

// create topic"wickingBeds/<sysID>/<bedNumber>/init/auto"
strcpy_P(topic1, (PGM_P)string_table[0]);
strcpy_P(topic2, (PGM_P)string_table[7]);
strcpy_P(topic3, (PGM_P)string_table[12]);

    snprintf_P(topicstring, 
            countof(topicstring),
            PSTR("%s%1u/%1u/%s/%s"),    
      topic1,
      sysID,
	i+1,
      topic2,
      topic3);
      
//topicstring = "wickingBeds/<sysID>/<bedNumber>/init/auto"

client.publish(topicstring, value);

//***********************************************************

// reset ALERT message if auto turned back on after over-run or fill now started

			if ((autoFill[i] == 1 && overRun[i]== 1) || (fillNow[i]==1 && overRun[i]== 1) ){
				
				overRun[i] = 0;
								
				//construct ALERT "resolved" value
					
					strcpy_P(topic3, (PGM_P)string_table[25]);
					snprintf_P(value, 
					countof(value),
					PSTR("%s"),    
					topic3);
				
				
				//construct ALERT topic
					//strcpy_P(topic1, (PGM_P)string_table[0]);
					strcpy_P(topic1, (PGM_P)string_table[0]);
					strcpy_P(topic2, (PGM_P)string_table[14]);
					snprintf_P(topicstring, 
					countof(topicstring),
					PSTR("%s%1u/%1u/%s"),    
					topic1,
					sysID,
					i+1,
					topic2);
      
				client.publish(topicstring, value);
				
				// send a value of "99" then "0" to be graphed as "fillingFail"

					char val2[]="99";
				//construct "fillingFail" topic
					strcpy_P(topic2, (PGM_P)string_table[27]);
					snprintf_P(topicstring, 
					countof(topicstring),
					PSTR("%s%1u/%1u/%s"),    
					topic1,
					sysID,
					i+1,
					topic2);
      
				client.publish(topicstring, val2);
				client.publish(topicstring, "0");
			}
//**********************************************************
	
initialised =0;  // ensures main loop runs immediately and that new value is sent via mqtt to broker

//construct /init/threshold topic

threshold[i]=EEPROM.read(10+15*i+2);
itoa(threshold[i], value,10);

strcpy_P(topic1, (PGM_P)string_table[0]);
strcpy_P(topic2, (PGM_P)string_table[7]);
strcpy_P(topic3, (PGM_P)string_table[13]);

    snprintf_P(topicstring, 
            countof(topicstring),
            PSTR("%s%1u/%1u/%s/%s"),    
      topic1,
      sysID,
	i+1,
      topic2,
      topic3);
      
//topicstring = "wickingBeds/sysID/i+1/init/threshold"
client.publish(topicstring, value);

//*****************************************************************************
// initialise noRefillInterval from EEPROM

noRefillInterval[i]=EEPROM.read(10+15*i+11);
itoa(noRefillInterval[i], value,10);

// create topic
strcpy_P(topic1, (PGM_P)string_table[0]);
strcpy_P(topic2, (PGM_P)string_table[7]);
strcpy_P(topic3, (PGM_P)string_table[22]);

    snprintf_P(topicstring, 
            countof(topicstring),
            PSTR("%s%1u/%1u/%s/%s"),    
      topic1,
      sysID,
	i+1,
      topic2,
      topic3);
      
//topicstring = "wickingBeds/<sysID>/<bedNumber>/init/noRefillInterval"

client.publish(topicstring, value);
//}

// initialise maxTimeRefill from EEPROM

maxTimeRefill[i]=EEPROM.read(10+15*i+12);
itoa(maxTimeRefill[i], value,10);

// create topic
strcpy_P(topic1, (PGM_P)string_table[0]);
strcpy_P(topic2, (PGM_P)string_table[7]);
strcpy_P(topic3, (PGM_P)string_table[23]);

    snprintf_P(topicstring, 
            countof(topicstring),
            PSTR("%s%1u/%1u/%s/%s"),    
      topic1,
      sysID,
	i+1,
      topic2,
      topic3);
      
//topicstring = "wickingBeds/<sysID>/<bedNumber>/init/maxTimeRefill"

client.publish(topicstring, value);

//*****************************************************
}
Serial.println("Initialisation finished");
}
Serial.println();
}
//***********************************************************
void timeSinceFilled(int i) {  // called on every sensor loop

//calculates and publishes days and hours since bed was last filled
	
DateTime now=rtc.now();
filledHoursSinceDawn[i]=((EEPROM.read(10+15*i+9) << 8) & 0xFF00)+((EEPROM.read(10+15*i+8) << 0)); // when the bed was last filled
unsigned long hoursCalc=(now.unixtime()/60-dawn)/60-filledHoursSinceDawn[i] ;
unsigned long daysAgo=hoursCalc/24;
unsigned long hoursAgo = hoursCalc%24;
// note that this calc is rough - cuts off part-hours
	
// construct daysHoursSince topicstring


strcpy_P(topic1, (PGM_P)string_table[0]); // 
strcpy_P(topic2, (PGM_P)string_table[6]);

    snprintf_P(topicstring, 
            countof(topicstring),
            PSTR("%s%1u/%1u/%s"),    
      topic1,
      sysID,
	i+1,
      topic2);
      
//topicstring = "wickingBeds/<sysID>/<bedNumber>/daysHoursSince"

// construct daysHoursSince <value> as ( "daysAgo days, hoursAgo hours ago")
// unless year = 0 for fresh restart 


strcpy_P(topic1, (PGM_P)string_table[8]); // "days"
strcpy_P(topic2, (PGM_P)string_table[9]); // "hours"
char ago[]= "ago";
    snprintf_P(value, 
	countof(value),
	PSTR("%2u %s, %2u %s %s"),  
	daysAgo,
	topic1,
	hoursAgo,
	topic2,
	ago);
	
if (EEPROM.read(10+15*i+7)==0){
client.publish(topicstring,"");
}
else {
client.publish(topicstring, value);
}
}

//***************************************************************
void pubCurrentTime()  {// for Overview page, updates on each sensor reading loop

DateTime now=rtc.now();

// for <value> construct string as hh:mm dd/mm
	
	snprintf_P(value, 
            countof(value),
            PSTR("%02u/%02u @ %02u:%02u"),    
	now.day(),
      now.month(),
      now.hour(),
      now.minute());

Serial.println();
Serial.print("Current Time: ");
Serial.println(value);

// topicstring = wickingBeds/<sysID>/currentTime

strcpy_P(topic1, (PGM_P)string_table[0]); // 
strcpy_P(topic2, (PGM_P)string_table[10]); //

    snprintf_P(topicstring, 
            countof(topicstring),
            PSTR("%s%1u/%s"),    
      topic1,
      sysID,
      topic2);
      
client.publish(topicstring, value);

//Serial.println();
//Serial.print("topic: ");
//Serial.println(topicstring);
}
//**************************************************************
int sendAlert(int i){
	// called from checkFilling if filling time exceed timeOut set by user
	
// turn off filling for bed[i]	
	
	digitalWrite (fillSolenoidPin[i], LOW); // turn off solenoid
	fillingNow[i]=0;
	
Serial.println();
Serial.print("ALERT - Bed ");
Serial.print(i+1);
Serial.println(" has exceeded max fill time");
	
// turn off auto filling for bed[i]
	
char val1[] = "0";
 
//construct ...../init/auto topic, publish -m 0


strcpy_P(topic1, (PGM_P)string_table[0]); // 
strcpy_P(topic2, (PGM_P)string_table[7]);
strcpy_P(topic3, (PGM_P)string_table[12]);

    snprintf_P(topicstring, 
            countof(topicstring),
            PSTR("%s%1u/%1u/%s/%s"),    
      topic1,
      sysID,
	i+1,
      topic2,
      topic3);
	
	
client.publish(topicstring, val1);
EEPROM.write(10+15*i+1,0);
EEPROM.commit();
initialised=0; // 

//contruct alert message text


strcpy_P(topic1, (PGM_P)string_table[17]);
strcpy_P(topic2, (PGM_P)string_table[15]);
strcpy_P(topic3, (PGM_P)string_table[18]);
strcpy_P(topic4, (PGM_P)string_table[19]);

   snprintf_P(alertmsg, 
            countof(alertmsg),
            PSTR("%s %2u  %s - %s - %s"),    
      topic1,
      maxTimeRefill[i],
      topic2,
      topic3,
      topic4);

	
//construct ALERT topic
strcpy_P(topic1, (PGM_P)string_table[0]);
strcpy_P(topic2, (PGM_P)string_table[14]);

snprintf_P(topicstring, 
            countof(topicstring),
            PSTR("%s%1u/%1u/%s"),    
      topic1,
      sysID,
	i+1,
      topic2);
      
client.publish(topicstring, alertmsg);

// send a value of  "0" then "100" to be graphed as "fillingFail"

char val2[]="100";
//construct "fillingFail" topic
strcpy_P(topic1, (PGM_P)string_table[0]);
strcpy_P(topic2, (PGM_P)string_table[27]);
snprintf_P(topicstring, 
            countof(topicstring),
            PSTR("%s%1u/%1u/%s"),    
      topic1,
      sysID,
	i+1,
      topic2);
      
client.publish(topicstring, "0");      
client.publish(topicstring, val2);


return 1;

}
