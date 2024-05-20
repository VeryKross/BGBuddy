The bin file here is a compiled version of the program in the src folder.

NOTE: The IdeaSpark versions are only for the IdeaSpark ESP8266 with built-in OLED screen. Don't use these if building the project from the documentation with the separate ESP8266 and SSD1306 OLED components. Note also that there are two versions of the IdeaSpark binary, SDA12 and SDA14. This corresponds to whether your particular IdeaSpark package is using the GPIO12 pin or the GPIO14 pin for the screen's data connection (OLEDSDA). Check the documentation/diagram for your IdeaSpark to know which binary is right for you. If you're not sure, reach out to me.

To install the bin file into your ESP8266 processor, use a ROM "flasher" like the one from ESPHome Flasher: https://github.com/esphome/esphome-flasher/releases
