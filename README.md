# WickerMan
An Arduino/ESP8266/mqtt controller for garden wicking beds

WickerMan is a controller for garden wicking beds that:

    • measures and reports soil moisture and temperature for a group of wicking beds
    
    • allows remote refilling of each bed's water reservoir
    
    • allows the user to set a soil moisture threshold for automatic refilling when the bed gets too dry
    
    • has an air temperature and humidity sensor for each array of beds ("a system")
    
    • uses the mqtt protocol to allow all functions to be controlled remotely over wifi
    
    • provides an online graphical interface for all functions via Node-red

WickerMan could also be adapted to be used as a watering controller for any garden application that requires responses to soil dryness, but is probably overkill for this purpose alone.

Note that this version (Beta 0.1) is INSECURE and designed to run on a secured home wifi network. It is possible to secure node-red and mqtt to expose the whole system to the wider internet but that process is not covered here.

Watch this space for further documentation:

    Hardware list
    Fritzing circuit diagram
    Software notes
    mqtt and Node-Red setup links
    Node-red nodes for WickerMan
    Notes on wicking bed setup tips for WickerMan
    Performance data
    


![alt text](https://github.com/PaulGarrett/WickerMan/blob/master/WickerManImages/New_Fig3.png?raw=true)


