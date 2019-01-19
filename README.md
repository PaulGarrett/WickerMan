# WickerMan
An Arduino/ESP8266/mqtt controller for garden wicking beds

WickerMan is an IOT controller for garden wicking beds that:

    • measures and reports soil moisture and temperature for a group of wicking beds
    
    • allows remote refilling of each bed's water reservoir
    
    • allows the user to set a soil moisture threshold for automatic refilling when the bed gets too dry
    
    • has an air temperature and humidity sensor for each array of beds ("a system")
    
    • uses the mqtt protocol to allow all functions to be controlled remotely over wifi
    
    • provides an online graphical interface for all functions via Node-red

WickerMan could also be adapted to be used as a watering controller for any garden application that requires responses to soil dryness, but is probably overkill for this purpose alone.

Note that this version (Beta 0.1) is INSECURE and designed to run on a secured home wifi network. It is possible to secure node-red and mqtt to expose the whole system to the wider internet but that process is not covered here.

Watch this space for further documentation:

    Notes on wicking bed setup tips for WickerMan
    Performance data
    
![alt text](https://github.com/PaulGarrett/WickerMan/blob/master/WickerManImages/Fig1.png?raw=true)

![alt text](https://github.com/PaulGarrett/WickerMan/blob/master/WickerManImages/Fig2.png?raw=true)

Hardware list

1 Adafruit Feather HUZZAH ESP8266 OR the Adafruit HUZZAH ESP8266 Breakout Board
1 Adafruit DS3231 RTC or equivalent or DS3232
1 DHT22 Humidity and Temperature Sensor
2 Adafruit STEMMA Soil Sensor - from Adafruit or your local agent
2 12v normally closed ½" Plastic Solenoid Valve - eg. From valves4projects
2 Float switches - ebay search for "Water Level Switch Liquid Level Sensor Plastic Vertical Float"
2 Rectifier Diode
2 Basic FET N-Channel
4 ¼ watt 10k Resistors (pull downs)
4 Sets of 2 core waterproof cables and connectors (for float switches and solenoids)
3 Sets of 4 core waterproof cable and connectors (for STEMMA sensors and the DHT sensor)
1 12v 1A power source

Circuit diagram

![alt text](https://github.com/PaulGarrett/WickerMan/blob/master/WickerManImages/New_Fig3.png?raw=true)

To power the unit, you will need a 12V 1A DC power supply AND a means to give the microcontroller 5VDC (which then reduces the supply to 3.3V. My solution (Fig 2) was to split the incoming 12v into a supply for the solenoids and a 5v supply for the micro-controller.
	
1 DC-DC step-down switching converter (12v to 5v), such as the TSR-1 2450, and a 22uf capacitor, to power the micro-controller and sensors from the same 12v 1A source as the solenoids). A ready-made solution is available from Adafruit.

![alt text](https://github.com/PaulGarrett/WickerMan/blob/master/WickerManImages/WickerManPower.png?raw=true)

Software Notes

WickerMan Version 0.1, as presented here, has only been tested running on home Linux (Ubuntu/Raspbian) machines. Installation and setup may vary for other operating systems.

The core of WickerMan is an "Arduino" Sketch for an ESP3266 module/board.  The outputs and variables of the sketch are trasmitted to a computer using the mqtt protocol and then used by an instance of IBM's Node-red system to populate a "Dashboard", or graphical interface.  To install and use WickerMan V0.1, the user should understand:

- the Arduino IDE needs to be set up for the ESP8266 board or module you are using.  See this guide if you are using one of the Adafruit ESP8266 boards or modules.
	- the libraries used in WickerMan have been designed for the ESP8266. Any that you don't already have need to be downloaded and installed.
- WickerMan is written to accommodate a variable number of separate wicking beds (and several WickerMan systems on the same mqtt and node-red setup)
- there is minimal "hardcoding"of variables - most can be changed via mqtt/node-red and are stored in EEPROM
	the pin assigments, i2c addresses of the moisture sensors may need to be changed in the sketch
	you do need to include your own wifi details.
- each instance of Wickerman requires initial setup of the ESP8266 EEPROM to establish a system ID number and the number of beds being controlled (WMan_setup.ino is provided to do this via the user input on the serial monitor.)
- the sketch makes extensive use of PROGMEM and strcpy "C" functions to construct the heirachical mqtt topics as they are needed (as well as some of the larger string payloads).
- WickerMan incorporates a number of "failsafe" features under user control, notably:
	- failure to fill a reservoir in a "reasonable time" (determined by the user as "Refill Timeout" on the dashboard) could be the result of several different types of failure, eg: the reservoir may be leaking, the float valve could be stuck, or the water supply might have failed (or been turned off). An over-run of the Timeout time results in wickerMan turning off the "autofill" function, displaying a notification on the Dashboard, and emailing an alert message to the user. When the user addresses the problem and either manually fills the reservoir (ie. presses "Fill Now" on the Dashboard or reinstates autofill) a message: "resolved" is displayed and sent as an email.
	- over-enthusiastic filling.  Capillary action in soil can take time. Once a reservoir is filled, the soil at the moisture sensor will not report increased moisture until sufficient "wicking" has drawn up water from the reservoir.  Wickerman allows the user to set a "Minimum Refill Interval", in hours, to allow this to occur before a subsequent automatic refill is triggered. Different growing media will wick at different speeds. 
- The frequency of sensor readings is also under user control (on the Overview tab of the Node-red Dashboard). Frequent (eg. once per minute) polling of the sensors may be required initially to help fine-tune settings but this can be extended to up to once per half-hour once the bed is operating well.
- The node_red interface displays a lot of data about the system and each bed. This is done to allow users to fine-tune the settings for each bed and observe how the bed responds. Changes can be made to accommodate the watering needs of different crops, growing media performance and seasons.
- It is reasonably straight-forward to create a more minimal interface for eg. viewing on a phone screen. Copy and past the required flows to a new Node_red tab and deploy.
- each function and section of the sketch code is commented
	
mqtt (Mosquitto)

- Wickerman relies on mqtt being installed on a local computer (see this guide).  

When wickerMan is running you can use the subscription function (mosquitto_sub) to view the flow of data on a terminal. 
	>$ mosquitto_sub -v -h <host address> - t "wickingBeds/#" 

Node-red

You will need a machine on your network with Node-red installed, inluding the standard Dashboard flows. You can add and observe Debug messages to any flow if you need to.

	See this guide to installing Node-red.

The WickerMan Node-red flows

Copy the provided JSON text documents into node red using the "Import > clipboard" options in the menu (create separate tabs for "overview" and "Single Bed" flows. 
	
- recopy the single Bed flows and install instances for as many beds as you are controlling on your first System.
	
Manually amend each additional copy of each mqtt node to insert the correct bed number . The format of all WickerMan mqtt topics is "wickingBeds/<sysID>/<bedNum>/<dataTopic>" (in the sample, sysID is 1 and bedNum is 1 - you should not have to edit the first sample, only the additional copies)

All of the bed management functions allowed by the Node-red dashboard can be replicated as mqtt_pub topic/value commands from the terminal. 

If you are working across different machines during installation and testing, it would be useful to be able to work with them simultaneously via secure remote access (eg. by installing ssh and running terminals on a single local machine).  This will allow you to eg. view the Arduino serial monitor alongside a terminal displaying the mqtt feed while also viewing the Node-red Dashboard).

Known Issues

Restarting the Node-red host machine wipes all data (variables, states and graphs).

	This is a known issue with Node-red. It can only be addressed by storing data in a persistent database (eg. sqllite) - a major change to the way WickerMan currently works. This will be considered in developing new versions.

	Work around: on rebooting the node-red host, the user can change a single setting (eg. a threshold or one of the timers)  and the sketch will reset all variables to their previous stored values. Graphs will still need to be repopulated over time.

The initial setup process (including setting up the Arduio IDE for ESP8266, installing the required libraries, flashing the ESP EEPROM, installing mqtt and Node-red and setting up the Node-red flows) is laborious, especially if the installation of each component does not go smoothly.
	
	WickerMan is a relatively complex set of separate applications that need to be integrated. Some of that integration, especially wrt. the Arduino sketches and the installation and set-up of the node-Red flows may be further automated in future versions.  For now, WickerMan is more a "proof of concept" than a fully mature, easily deployable application.


"I love the smell of feature creep in the morning." Anon
