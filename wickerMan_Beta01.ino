/*

WickerMan is a controller for garden wicking beds that:
	measures and reports soil moisture and temperature for an array of wicking beds
	allows remote refilling of each bed's water reservoir
	allows setting a soil moisture threshold for automatic refilling when the bed gets too dry
	has an air temperature and humidity sensor for each array of beds ("system")
	uses mqtt protocol to allow all functions to be controlled remotely over wifi
	
Hardware (for an array of two wicking beds):
	1 x Adafruit Feather HUZZAH ESP8266
	1 x Adafruit DS3231 RTC or equivalent or DS3232
	1 x DHT22 Humidity and Temperature Sensor
	2 x Adafruit STEMMA Soil Sensor - from Adafruit or your local agent
	2 x 12v normally closed ½" Plastic Solenoid Valve - eg. From valves4projects
	2 x Float switches - ebay search for "Water Level Switch Liquid Level Sensor Plastic Vertical Float"
	2 x Rectifier Diode (across terminals of each solenoid)
	2 x Basic FET N-Channel (for activation of solenoids)
	4 x ¼ watt 10k Resistors (pull downs for solenoid FETs and float swiches)
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
	Full dashboard implementation via Node-red on any computer on your home network
	
Hardcoded settings
	This version of Wickerman uses the following hardcoded values which should be changed to suit user
		sysID - set to 1 here but available for other values in a multi-system setup
		numBeds - set here to 2 but a system could theoretically have 1,2 or 3 beds
		readingsInterval = 60; // seconds between sensor readings
		fillingCheckInterval  = 2e3; //(2 seconds) - loop for when a bed is filling
		noRefillInterval [] = {600, 600}; // stops beds refilling until adequate time has passed for wicking
		dawn=25771680; // an arbitrary date in unix "minutes" for the "beginning of time" (00:00 on 01/01/2019)
		ssid = "<insert your wifi ssid>";
		password = "<insert your wifi password>";
		mqtt_server = "<10.10.10.225>"; replace <> with the address of you mqtt server
		

This version (.01) - BUGS and Refiinements in progress

1. fix initial display of "NEVER" and blank daysHoursAgo when first initialised
2. further refinements to node-Red Dashboard

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

#define DHTTYPE DHT22   // DHT 22  (AM2302), AM2321
DHT dht(DHTPIN, DHTTYPE);

RTC_DS3231 rtc; //(i2c address is 0x68 but this is handled in the library. Note that the DS3232 also works fine with this library)

Adafruit_seesaw ss;


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
static const char string_14[] PROGMEM = "ALERT";  // _pub not yet implemented - for time over-runs / failures

const char* const string_table[] PROGMEM = {string_0, string_1, string_2, string_3, string_4, string_5, string_6, string_7, string_8, string_9, string_10, string_11,string_12,string_13,string_14};

char daysOfTheWeek[7][5] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};

String topic;

int ssAddr[]= {0x36,0x37}; // i2c addresses for 2 Adafruit soil moisture sensors
//GPIO pin assignments
int waterLevelPin[]={12,16};  // - change if required
int fillSolenoidPin[]={13,15};// - change if required


char topic1[15];
char topic2[25];
char topic3[25];

char value[25];
char topicstring[45];

// TIME VARS
unsigned long previousReadingsSeconds= 0;
unsigned long previousFillingSeconds[] = {0,0};
const long readingsInterval = 60; // seconds change to 600=10 minutes - or other value if required
const long fillingCheckInterval  = 2e3; //2 seconds - change if required
const long noRefillInterval [] = {600, 600}; // reset to eg. 21600 seconds = 6 hours  
unsigned long lastautofill[] = {0,0};// only used to check against noRefillInterval[]

char datestring[25];
char hoursMinutesString[20];
long dawn=25771680; // prev 25761600 .unixtime in minutes at 00:00 25/12/2018 reset to 25771680 (00:00 1/1/2019)
uint16_t hoursSinceDawnNow;
uint16_t filledHoursSinceDawn[2];

uint8_t sysID=1;
uint8_t numBeds=2;

float airTemp;
float soilTemp;
float humidity;
uint8_t soilMoisture;

int autoFill[]={0,0};
int threshold[]={20,20};
int fillNow[]={0,0};

int initialised=0; // flag to reset settings or upload new init data from EEPROM

int fillingNow[] ={0,0};
int fillFlag[]={0,0};

// network.

const char* ssid = "ishtar";
const char* password = "1Stanbul";
const char* mqtt_server = "10.1.1.226";

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

Serial.println("CallBack started");
	
	if ((char)topic[16]=='a'){ // topic includes "auto", 
			//Serial.println("New topic/value pair received");
			//Serial.println("char of topic[16] = a");
		int bed = (int)topic[14]-48; // bedNum is topic[14] , convert ascii to int
			//Serial.print("int of topic[14] = ");
			//Serial.println (topic[14]);
			//Serial.print ("Bed Number: ");
			//Serial.println(bed);
		if ((int)payload[0] - 48 != autoFill[bed-1]){
			
			//Serial.print("current value of autoFill[");
			//Serial.print(bed-1);
			//Serial.print("] = ");
			//Serial.println(autoFill[bed-1]);
						
		autoFill[bed-1] = (int)payload[0] - 48;
			
			Serial.print("new value of autoFill[");
			Serial.print(bed-1);
			Serial.print("] = ");
			Serial.println(autoFill[bed-1] );
			//Serial.print("now written to EEPROM position ");
			//Serial.println(10*(bed-1)+1);
		EEPROM.write (10*(bed-1)+1,autoFill[bed-1] );
		EEPROM.commit();
	
			//Serial.print ("autoFill[");
			//Serial.print (bed-1);
			//Serial.print ("]  in EEPROM is now= ");
			//Serial.println (autoFill[bed-1]);
			//Serial.print ("EEPROM position is = ");
			//Serial.println (10*(bed-1)+1);
			//Serial.print ("EEPROM value is = ");	
			//Serial.println (EEPROM.read(10*(bed-1)+1));
			//Serial.println ("DONE");
	initialised =0;  // ensures main loop runs immediately and that new value is sent via mqtt to broker

}
		//Serial.println("New topic/value pair received");
		//Serial.print("value of autofill[] was already ");
		//Serial.println((int) payload[0] - 48);
}

//Diagnostics
	if ((char)topic[16]=='t'){ // looking for "threshold"
			Serial.println("Threshold message received");
		int bed = (int)topic[14]-48; // bedNum is topic[14] , convert ascii to int
			//Serial.print ("Bed Number = ");
			//Serial.println(bed);
			//Serial.print ("Current Value for threshold[");
			//Serial.print (bed-1);
			//Serial.print ("] = ");
			//Serial.println (threshold[bed-1]);
			//Serial.print ("Received value - as string - is - ");
			//for (int i=0; i<length;i++){
			//Serial.print (payload[i],DEC);
		//}
			Serial.println ();
		
			Serial.print ("Received value for threshold is - ");
		int threshVal[numBeds];
		threshVal[bed-1] = 10*((int)payload[0]-48) + (int)payload[1]-48; // two char ascii converted to int
			Serial.println (threshVal[bed-1] );
		if (threshold[bed-1] != threshVal[bed-1] ){
			//Serial.println("SAME AS STORED VALUE");
			Serial.println("DIFFERENT FROM STORED VALUE");
		//	Serial.println("do an EEPROM.write here");
		EEPROM.write(10*(bed-1)+2,threshVal[bed-1] );
		EEPROM.commit();
		//}
			Serial.print("value in EEPROM is now = ");
			Serial.println(EEPROM.read(10*(bed-1)+2));
		threshold[bed-1]=threshVal[bed-1] ;
		initialised=0; // ensures main loop runs immediately and that new value is sent via mqtt to broker
	}
}
	
	if ((char)topic[16]=='f'){// looking for "fillNow"
			Serial.println();
						
		int bed = (int)topic[14]-48; // bedNum is topic[14] , convert ascii to int
			Serial.print("Fill Now received for Bed ");
			Serial.println(bed);
		if (fillingNow[bed-1]!=1){
			// check time since last fill NOT WORKING????
			/*
			filledHoursSinceDawn[bed-1] = ((EEPROM.read(10*(bed-1)+9) << 8) & 0xFF00)+((EEPROM.read(10*(bed-1)+8) << 0)); 
			
			DateTime now = rtc.now();	
			// find hours now since dawn
			if (((now.unixtime()/60 - dawn)/60-filledHoursSinceDawn[bed-1])>= noRefillInterval){
			*/
			
				fillFlag[bed-1]=1; //  flag for main loop. A dirst call to startFilling() fn does not work
						
			}
		if (fillingNow[bed-1]){
			Serial.println("Filling Already Happening");
		}

  }

Serial.println("Message arrived : ");
Serial.print("topic[14] = ");
Serial.println(topic[14]);
Serial.print("topic[16] = ");
Serial.println(topic[16]);
Serial.print("payload = ");

  for (int i = 0; i < length; i++) {
Serial.print((char)payload[i]);

  }
Serial.println();
Serial.println("CallBack completed");
Serial.println();
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
      client.publish("reconnected", "hello world");	    
	    
// ... and re/subscribe to all topics published by node-Red (auto, threshold and fillNow
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
	}

Serial.println ("subscriptions done"); 

      
      // ... and resubscribe to a single "messages" topic This might be useful for messages and alerts?
      client.subscribe("inTopic");
    } else {
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


#ifndef ESP8266
  while (!Serial); // for Leonardo/Micro/Zero
#endif

  Serial.begin(115200);
  setup_wifi();
  client.setServer(mqtt_server, 1883);
  client.setCallback(callback);
  
// pin modes added
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
rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
DateTime now = rtc.now();
EEPROM.begin(128);

}
//****************************************************************************************************************************
void loop() {



  if (!client.connected()) {
    reconnect();
  }
  client.loop();

  
// every few seconds, check any beds that are filling now   
for (int i = 0; i<numBeds; i++){
	if (fillingNow[i]==1 ){
		if (millis()-previousFillingSeconds[i]>= fillingCheckInterval) {
		checkFillingNow(i);
			previousFillingSeconds[i]=millis();
		}
	}
	// and check whether "FillNow[] has been received
	if (fillFlag[i]==1) {
		startFilling(i);
	}
}  
  
  

// main loop for getting and publishing sensor data


DateTime now = rtc.now();


if((now.unixtime()>=previousReadingsSeconds+readingsInterval) || (initialised==0)){// start of sensor reading loop
 
	if (initialised==0){
		Serial.println("Initialised Was Set to Zero");
	rLoad();		// if new boot or change to subscribed settings run rLoad() fn
	initialised=1; // sets initialised to 1 so it doesn't run again
}
	
pubCurrentTime(); // updates timestamp on Overview page


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


strcpy_P(topic1, (PGM_P)string_table[0]); // 
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
strcpy_P(topic1, (PGM_P)string_table[0]); // 
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

strcpy_P(topic1, (PGM_P)string_table[0]); // 
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
//****************************************************************
			
DateTime now = rtc.now();

if (now.unixtime() < noRefillInterval [j] + lastautofill[j]){
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
	
	
digitalWrite (fillSolenoidPin[i], HIGH); //open solenoid
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
strcpy_P(topic1, (PGM_P)string_table[0]); // 
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
	
if (digitalRead(waterLevelPin[i]) == 0) { // pin read "0" when water-level sensor closes - ie reservoir is full
	
	
	digitalWrite (fillSolenoidPin[i], LOW); // turn off solenoid
	
	DateTime now = rtc.now();
	EEPROM.begin(128);
		EEPROM.write (10*i+3,now.minute());
		EEPROM.write (10*i+4,now.hour());
		EEPROM.write (10*i+5,now.day());
		EEPROM.write (10*i+6,now.month());
		EEPROM.write (10*i+7,now.year()-2000);
		
		hoursSinceDawnNow= (now.unixtime()/60-dawn)/60; //hours since 25/12/2018 for reference
	
		EEPROM.write (10*i+8,lowByte(hoursSinceDawnNow));
		EEPROM.write (10*i+9,highByte(hoursSinceDawnNow));
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


// create the topicstring[]
	strcpy_P(topic1, (PGM_P)string_table[0]); // 
	strcpy_P(topic2, (PGM_P)string_table[5]);

	snprintf_P(topicstring, 
		countof(topicstring),
		PSTR("%s%1u/%1u/%s"),    
	topic1,
	sysID,
	i+1,
	topic2);

	
//	topicstring= wickingBeds/<sysID>/<bedNumber>/lastFilledDateTime

client.publish(topicstring, datestring);

		Serial.print("publshed as ");
		Serial.println(topicstring);
		Serial.println(datestring);
		
// construct daysHoursSince[] topic and publish with an initial value of "Just Now"

char val1[] = "Just Now";

 
// create the topicstring[]
strcpy_P(topic1, (PGM_P)string_table[0]); // 
strcpy_P(topic2, (PGM_P)string_table[6]);

    snprintf_P(topicstring, 
            countof(topicstring),
            PSTR("%s%1u/%1u/%s"),    
      topic1,
      sysID,
	i+1,
      topic2);
//topicstring = "wickingBeds/<sysID>/<bedNumber>/lastFilled"

client.publish(topicstring, val1);
	
// reset fillingNow[] flag
	fillingNow[i]=0;


	} // end of "stop filling" loop
} // eof

//****************************************************************
void rLoad() { // called from top of loop() if initialised flag=0
	
	Serial.println ("initialisation started.....");
	
int year[numBeds];
//initialises interface - gets data from EEPROM and publish[i+1] for threshold, 
//auto, lastFilled etc
DateTime now=rtc.now();

	
for (int i=0; i<numBeds;i++){
Serial.println();
Serial.print("YEAR = ");
Serial.println(EEPROM.read(i*10+7));
	
if (EEPROM.read(i*10+7)==0){ // following runs only if EEPROM value for year has been cleared (useful for total reset)
	
	Serial.println("YEAR = 0");
	
char  val1[]= "NEVER";
   	
// construct lastFilled topicstring	
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
// daysHoursSince = blank

char val2[] = " ";
	
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

client.publish(topicstring, val2);


// initialise "auto" to OFF
char val3[] = "0";
 
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
      
//topicstring = "wickingBeds/<sysID>/<bedNumber>/init/auto"

client.publish(topicstring, val3);
if (EEPROM.read (10*1+1) != 0){
EEPROM.write (10*i+1,0);
EEPROM.commit();
}
//construct /init/threshold topic and publish -m 25 -  a low value

char val4[] = "25";

// create the topicstring[]
strcpy_P(topic1, (PGM_P)string_table[0]); // 
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
if (EEPROM.read (10*1+2) != 25){
EEPROM.write (10*i+2,25);
	EEPROM.write(10*i+7, now.year()-2000); // ensure year !=0 
EEPROM.commit();
}
}
else { // if init set to zero because a setting has changed or initialised set to 0 on boot
	
	Serial.println("YEAR NOT 0");

//construct datestring for lastFilledDateTime[] from EEPROM as "28/12 @ 16:45"

	snprintf_P(datestring, 
		countof(datestring),
		PSTR("%02u/%02u @ %02u:%02u"),    
	//daysOfTheWeek[now.dayOfTheWeek()],
	EEPROM.read (10*i+5),
	EEPROM.read (10*i+6),
	EEPROM.read (10*i+4),
	EEPROM.read (10*i+3));
	
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
	
//topicstring= wickingBeds/<sysID>/<bedNumber>/lastFilledDateTime

client.publish(topicstring, datestring);

timeSinceFilled(i); // calc and publish daysHoursSince - fn cals and publishes

autoFill[i]=EEPROM.read(10*i+1);
itoa(autoFill[i], value,10);

// create topic

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
      
//topicstring = "wickingBeds/<sysID>/<bedNumber>/init/auto"

client.publish(topicstring, value);

//construct /init/threshold topic

threshold[i]=EEPROM.read(10*i+2);
itoa(threshold[i], value,10);

strcpy_P(topic1, (PGM_P)string_table[0]); // 
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

}
Serial.println("Initialisation finished");
}

}
//***********************************************************
void timeSinceFilled(int i) {  // called on every sensor loop

//calculates and publishes days and hours since bed was last filled

	
DateTime now=rtc.now();
filledHoursSinceDawn[i]=((EEPROM.read(10*i+9) << 8) & 0xFF00)+((EEPROM.read(10*i+8) << 0)); // when the bed was last filled
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
// unless year = 0 for fresh restart ?


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

client.publish(topicstring, value);

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
