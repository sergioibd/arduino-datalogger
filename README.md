# datalogger for arduino
In order to store measurements of temperature, humidity and power consumption of my house I started the development of this application. And today has other capabilities, such as the integration with other devices via Modbus, and an outline for sending information stored to external systems through web services.

Tested on:
 - Ethernet Shield With MEGA 2560

Supported protocols:
 - Modbus RTU master (only FC 3 and 4, experimental feature)
 - Modbus TCP master (only FC 3 and 4)

Integration capabilities:
 - Web client to connet to web data acquisition service.
 - Support RC4 encryption (http://en.wikipedia.org/wiki/RC4).

Time measure capabilities:
 - Timekeeping Ability (see http://www.pjrc.com/teensy/td_libs_Time.html)
 - DS1307 support (see http://www.pjrc.com/teensy/td_libs_DS1307RTC.html)
 - NTP support

Other features:
 + Implements a web server to download the log files!
 + Encrypt the information to send over the Internet (RC4 algorithm)
 + Rollover data to SD card
