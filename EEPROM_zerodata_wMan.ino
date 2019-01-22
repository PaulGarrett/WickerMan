//Write wickerMan zero data to EEPROM

#include <EEPROM.h>

uint8_t sysID=1;

void setup () 
{

#ifndef ESP8266
  while (!Serial); // for Leonardo/Micro/Zero
#endif

Serial.begin(115200);

EEPROM.begin(512);


Serial.println();
Serial.println("Write zeroed data to  to EEPROM:");
EEPROM.write (0, sysID); 



for (int i=1; i<61; i++){

EEPROM.write (i,0);

}
EEPROM.commit();
Serial.println("Zero Written to EEPROM Positions 1 to 60");
Serial.println("Reading test data from EEPROM");
	
	
Serial.print("sysID: ");
Serial.println (EEPROM.read(0));
	
for (int i=1; i<61; i++){
Serial.print("Pos ");
	Serial.print(i);
	Serial.print(": "); 
Serial.println (EEPROM.read(i));

}
}
void loop () {

}
