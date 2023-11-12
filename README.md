# ESP32_First_Project
A small test project to get familiar with the ESP32

Part of the "Programmierpraktikum Verteilte Systeme" at TU Berlin

---
## Project Description
The ESP32 can be used to connect to wifi and send a HTTP POST request to a gcp cloud function and can be customized via Bluetooth. 

The ESP32 offers four Bluetooth services:
1. 0x0AA - Set the WiFi ssid you want to connect to (stored locally)
2. 0x0BB - Set the WiFi password for the ssid (stored locally)
3. 0x0CC - Set the message you want to send and send it
4. 0x0DD - Connect to WiFi (If ssid and/or password were not defined before, it uses the data it got the last time)

---
## ToDo:
- [ ] Handle incorrect WiFi information
- [ ] Handle Bluetooth incoming connection correctly
- [ ] Option to disconnect from WiFi
- [ ] Process POST request on gcp
