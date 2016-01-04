/* 
    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/
    
/*
 Datalogger for Arduino
 
 Tested on:
 - MEGA 2560 with Ethernet Shield
 
 Default IP to connect (DHCP or 192.168.0.8)
 
 Supported protocols:
 - Modbus master RTU (only FC 3 and 4)
 - Modbus master TCP (only FC 3 and 4)
 
 Time measure capabilities:
 - Timekeeping ability (see http://www.pjrc.com/teensy/td_libs_Time.html)
 - DS1307 support (see http://www.pjrc.com/teensy/td_libs_DS1307RTC.html)
 - NTP support
 
 (Note: This libraries must be installed in './Arduino/libraries' path)
 
 Integration capabilities:
 - Web client to connet to web data acquisition service.
 - Support RC4 encryption (http://en.wikipedia.org/wiki/RC4).
 
 Deploy configuration:
 - Web server on port 80.
 - Needed port 8888 (local port to listen for UDP packets) for NTP. 
 
 Version 1.0.0 (27/02/2014)
 + Reading analog signals
 + Rollover data to SD card
 + Implements a web server to download the log files!
 + Encrypt the information to send over the Internet (RC4 algorithm)
 + Integrates with web services, to upload the collected information
 
 Version 1.0.1 (11/03/2014)
 + NTP support
 + DS1307 support
 + Records with timestamp!
 
 Version 1.0.2 (12/03/2014)
 + Configuration via web (experimental)
 + Modbus RTU implementation (experimental)
 
 Version 1.0.3 (19/03/2014)
 + Modbus TCP implementation (support for function codes 3 and 4)
 
 @author sergioibd / 
 */

// INCLUDES //////////////////////////////////////////////////////////

// time includes

#include <Time.h>
#include <Wire.h>
#include <DS1307RTC.h> // a basic DS1307 library that returns time as a time_t

// ethernet includes
#include <SPI.h>
#include <Ethernet.h>
#include <EthernetUdp.h>

// sd includes
#include <SD.h>

// GLOBAL VARIABLES //////////////////////////////////////////////////

const String _version = "1.0.3";
const String _serialNumber = "ES00-BL00-MEGA-0001";

const unsigned long _waitTime = 5000; // loop wait (milliseconds)
const boolean _debug = true;

// serial ------------------------------------------------------------

const int _bitsPerSecond = 9600;

// analog inputs -----------------------------------------------------

int _startAnalogPin = 6;
int _endAnalogPin = 15;

// ethernet ----------------------------------------------------------

// controller MAC address (printed on a sticker on the shield)
byte _mac[] = { 
  0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED };

// the static IP address to use if the DHCP fails to assign
const boolean _enableDhcp = true;
IPAddress _ip(192,168,0,8); // default ip

// initialize the ethernet server library
EthernetServer _localWebServer(80);
const boolean _enableLocalWebServer = true;

// ethernet client library (port 80 is default for HTTP)
EthernetClient _client;

// name address and port for WS web service
const boolean _enableSendInfoToWebService = true;

const String _host = "192.168.121.233:90";
const String _serverService = "/ActionSoapService";

IPAddress _serverIp(192,168,121,233);
const int _serverPort = 90;

// Authorization: Basic YWRtaW46YWRtaW4= (admin, admin)
// Authorization: Basic ZGF0YWxvZ2dlcjohZDR0NGwwZ2czcg== (datalogger, !d4t4l0gg3r)
const String _serverAuth = "Authorization: Basic YWRtaW46YWRtaW4=";

EthernetUDP _udp; // A UDP instance to let us send and receive packets over UDP
unsigned int _localPort = 8888; // local port to listen for UDP packets

// ntp server --------------------------------------------------------

// List of NTP servers:
//   - 192.43.244.18 (time.nist.gov)
//   - 130.149.17.21 (ntps1-0.cs.tu-berlin.de)
//   - 192.53.103.108 (ptbtime1.ptb.de)

IPAddress _sntpServerIp( 130, 149, 17, 21);
const long _timeZoneOffset = +1; // (GMT zone) set this to the offset to your local time

const int _NTP_PACKET_SIZE= 48; // NTP time stamp is in the first 48 bytes of the message
byte _packetBuffer[ _NTP_PACKET_SIZE]; //buffer to hold incoming and outgoing packets 

// sd card -----------------------------------------------------------

// On the Ethernet Shield, CS is pin 4. Note that even if it's not
// used as the CS pin, the hardware CS pin (10 on most Arduino boards,
// 53 on the Mega) must be left as an output or the SD library
// functions will not work.

const int _chipSelect = 4;
const int _hardwareCSPin = 53;

Sd2Card _card;
SdVolume _volume;

char _datalogFile[] = "datalog.log";
char _statusFile[] = "status.cfg";

const boolean _cleanSdOnInit = true;

unsigned long _filePosition = 0;
const unsigned long _maxFileSize = 10485760; // 10MB, Max value: 4294967295

unsigned long _fileNumber = 0;
boolean _updateStatusFile = false;

// modbus rtu --------------------------------------------------------
boolean _enableModbusRTU = true;

#define SSerialTxControl 3 // RS485 Direction control

#define RS485Transmit   HIGH
#define RS485Receive    LOW

#define POLY_MODBUS     0xA001

// modbus tcp --------------------------------------------------------

const boolean _enableModbusTCP = false;

#define MB_FC_READ_REGISTERS       0x03
#define MB_FC_READ_INPUT_REGISTER  0x04

#define MB_TCP_DEVICES             0x01
#define MB_TCP_BATCH               0x01

byte _modbusServer[MB_TCP_DEVICES][4] = { 
  { 
    192, 168, 121, 233             } 
};

int _modbusPort[MB_TCP_DEVICES]= { 
  502 
};

byte _modbusTCPReading[MB_TCP_BATCH][6] = { 
  { 
    0, 1, 0, 255, MB_FC_READ_REGISTERS, 0                } 
}; // device, slave, start, end, function code, [current = 0]

// encryption (RC4)---------------------------------------------------

unsigned char S[256];
unsigned int i, j;

// md5("examplekey") = a13d8c5e3d9440fc5203078a9a174fba
unsigned char *enckey = (unsigned char*) "a13d8c5e3d9440fc5203078a9a174fba"; 

// METHODS ///////////////////////////////////////////////////////////

// initialization methods --------------------------------------------

void initializeModbusRTU()
{
  pinMode(SSerialTxControl, OUTPUT);    

  Serial.println("RS485 RECEIVE MODE");
  digitalWrite(SSerialTxControl, RS485Receive);  // Init Transceiver   

  // start the software serial port, to another device
  Serial1.begin(9600);
}

// sets the sd card
void initializeSdCard()
{
  Serial.print("Initializing SD card... ");

  // make sure that the default chip select pin is set to
  // output, even if you don't use it
  pinMode(_hardwareCSPin, OUTPUT);

  if (!_card.init(SPI_HALF_SPEED, _chipSelect)) {
    Serial.println("initialization failed. Things to check:");
    Serial.println(" * is a card inserted?");
    Serial.println(" * Is your wiring correct?");
    Serial.println(" * did you change the chipSelect pin to match your shield or module?");
    return;
  } 

  // Now we will try to open the 'volume'/'partition' - it should be FAT16 or FAT32
  if (!_volume.init(_card)) {
    Serial.println("Could not find FAT16/FAT32 partition.\nMake sure you've formatted the card!");
    return;
  }

  // see if the card is present and can be initialized
  if (!SD.begin(_chipSelect)) {
    Serial.println("card failed, or not present");
    // don't do anything more
    return;
  }

  if(_cleanSdOnInit) {
    Serial.print("removing files... ");
    cleanSdCard();
  }

  Serial.println("card initialized.");
}

// sets the ethernet port
void initializeEthernet()
{
  Serial.print("Initializing Ethernet... ");

  // start the Ethernet connection:
  if (_enableDhcp && Ethernet.begin(_mac) == 0) {
    if(_enableDhcp) {
      Serial.print("failed to configure Ethernet using DHCP.");
      Serial.print("Try to configure using default IP instead of DHCP... ");
    }

    // try to congifure using _ip address instead of DHCP
    Ethernet.begin(_mac, _ip);
  }

  Serial.print("(");
  for (byte thisByte = 0; thisByte < 4; thisByte++) {
    // print the value of each byte of the IP address:
    Serial.print(Ethernet.localIP()[thisByte], DEC);
    Serial.print(".");
  }

  Serial.print(") ");

  // give the Ethernet shield a second to initialize:
  delay(1000);

  _localWebServer.begin();
  Serial.println("initialized.");
}

void initializeClock()
{
  Serial.print("Initializing clock... ");
  _udp.begin(_localPort);

  int count = 0;
  setSyncProvider(getNtpTime);
  while(timeStatus() == timeNotSet) {
    if(count % 1024 == 0)
      break;

    count++; // wait until the time is set by the sync provider
  }

  if(timeStatus() == timeNotSet) {
    Serial.print("unable to sync with the NTP server... ");
    setSyncProvider(RTC.get);   // the function to get the time from the RTC
  }

  if(timeStatus() == timeNotSet)
    Serial.println("failed.");
  else
    Serial.println("initialized.");  
}

// ethernet methods --------------------------------------------------

// web service and homepage 
void webServerImpl()
{
  // listen for incoming clients
  EthernetClient client = _localWebServer.available();
  if (client) {
    if(_debug) {
      Serial.println("Local web server: New client!");
    }

    boolean addToCommand = false;
    String command = "";

    // an http request ends with a blank line
    boolean currentLineIsBlank = true;
    while (client.connected()) {
      if (client.available()) {
        char c = client.read();

        if(addToCommand && c != ' ')
          command += c;

        if(_debug)
          Serial.write(c);

        // if you've gotten to the end of the line (received a newline
        // character) and the line is blank, the http request has ended,
        // so you can send a reply
        if (c == '\n' && currentLineIsBlank) {
          // send a standard http response header
          client.println("HTTP/1.1 200 OK");
          client.println("Content-Type: text/html");
          client.println("Connection: close");  // the connection will be closed after completion of the response
          client.println();

          if(_debug)
            Serial.println("Received command: " + command);

          if(command.startsWith(_datalogFile) || command.startsWith("backup")) {
            unsigned long readPosition = 0;
            while(true) {    
              char charBuf[50];
              command.toCharArray(charBuf, 50);

              String data = readFromFile(charBuf, readPosition, 10240, &readPosition);
              if(data == "")
                break;

              client.println(data);
            }
          } 
          else if (command.startsWith("config?")) {
            char charBuf[50];
            command.toCharArray(charBuf, 50);

            int beginIdx = 0;
            int idx = command.indexOf(",");

            int argIdx = 0;
            char charBuffer[16];

            String arg;
            while (idx != -1)
            {
              arg = command.substring(beginIdx, idx);
              Serial.println(arg);

              switch(argIdx)
              {
              case 1:
                arg.toCharArray(charBuffer, 16);
                _startAnalogPin = atoi(charBuffer);
                break;
              case 2:
                arg.toCharArray(charBuffer, 16);
                _endAnalogPin = atoi(charBuffer);
                break;
              }

              //arg.toCharArray(charBuffer, 16);
              //data[numArgs++] = atoi(charBuffer);

              beginIdx = idx + 1;
              idx = command.indexOf(",", beginIdx);
              argIdx++;
            }
          }
          else {
            client.println("<!DOCTYPE HTML>");
            client.println("<html>");

            client.println("<h1>Datalogger for Arduino</h1>");
            client.println("<h3>ardlogger - " + _serialNumber + ", Version " + _version + "</h3>");
            client.println("<h4>Not allow access to this application from an unsecured network!</h4>");

            client.println("<b>Configuration</b>");
            client.println("<br/><br/>");
            client.println("Serial port speed: " + String(_bitsPerSecond) + "<br/>");
            client.println("Start analog pin is <input id='startPin' type='textbox' style='width:18px;' value='" + String(_startAnalogPin) + "'/>, end analog pin is <input id='endPin' type='textbox' style='width:18px;' value='" + String(_endAnalogPin) + "'/><br/>");
            client.println("Data acquisition host: <input id='dataAcquisitionHost' type='textbox' value='" + _host + "'/><br/>");
            client.println("Data acquisition service: " + _serverService + "<br/>");
            client.println("Clean SD on init: <input id='cleanSdCard' type='textbox' style='width:10px;' value='" + String(_cleanSdOnInit) + "'/><br/>");
            client.println("Max file (log) size: " + String(_maxFileSize) + "B<br/>");
            client.println("Timestamp: " + getTimestamp() + "<br/>");

            client.println("NTP server: ");
            for (byte thisByte = 0; thisByte < 4; thisByte++) {
              // print the value of each byte of the IP address:
              client.print(_sntpServerIp[thisByte], DEC);
              client.print(".");
            }

            client.println("<br/><br/>");
            client.println("<input type='button' value='Submit' onclick='action()'>");
            client.println("<script>");
            client.println("function action() {");
            client.println("startPin=document.getElementById('startPin').value;");
            client.println("endPin=document.getElementById('endPin').value;");
            client.println("dataAcquisitionHost=document.getElementById('dataAcquisitionHost').value;");
            client.println("cleanSdCard=document.getElementById('cleanSdCard').value;");
            client.println("window.location='/config?0,' + startPin + ',' + endPin + ',' + dataAcquisitionHost + ',' +cleanSdCard ;");
            client.println("}");
            client.println("</script>");
            client.println("<br/><br/>");  

            client.println("<b>Integration capabilities</b>");
            client.println("<br/><br/>");
            client.println("&nbsp;- Web client to connet to web acquisition service.");
            client.println("<br/>");
            client.println("&nbsp;- Support RC4 encryption (http://en.wikipedia.org/wiki/RC4).");

            client.println("<br/><br/>");          
            client.println("<b>SD Card</b>");
            client.println("<br/><br/>");     

            // print the type of card
            client.print("Card type: ");
            switch(_card.type()) {
            case SD_CARD_TYPE_SD1:
              client.print("SD1");
              break;
            case SD_CARD_TYPE_SD2:
              client.print("SD2");
              break;
            case SD_CARD_TYPE_SDHC:
              client.print("SDHC");
              break;
            default:
              client.print("Unknown");
            }

            client.println("<br/>");

            // print the type and size of the first FAT-type volume
            uint32_t volumesize;
            client.println("Volume type is FAT");
            client.println(_volume.fatType(), DEC);
            client.println("<br/>");

            volumesize = _volume.blocksPerCluster(); // clusters are collections of blocks
            volumesize *= _volume.clusterCount(); // we'll have a lot of clusters
            volumesize *= 512; // SD card blocks are always 512 bytes

            client.print("Volume size: ");
            client.print(volumesize);
            client.println("B");
            client.println("<br/><br/>");

            client.println("Files found on the card: ");
            client.println("<br/>");
            client.println("<a href='/" + String(_datalogFile) + "'>" + String(_datalogFile) + "</a>");

            for(int i = 0; i < _fileNumber; i++) {
              String backupFileName = String("backup.") + i;

              client.println("<br/>");
              client.println("<a href='" + backupFileName + "'>" + backupFileName + "</a>");              
            }

            client.println("<br/><br/>");
            client.println("</html>");
          }

          break;
        }

        if (c == '\n') {          
          addToCommand = false;

          // you're starting a new line
          currentLineIsBlank = true;
        }
        else if (c != '\r') {
          if(addToCommand && c == ' ')
            addToCommand = false;              
          else if(command == "" && c == '/')
            addToCommand = true;

          // you've gotten a character on the current line
          currentLineIsBlank = false;
        }
      }
    }

    // give the web browser time to receive the data
    delay(1);

    // close the connection:
    client.stop();

    if(_debug) {
      Serial.println("Local web server: Client disconnected.");
    }
  }
}

// get a web service connection
boolean post(String dataString)
{
  String service = "POST " + _serverService + " HTTP/1.1";

  if(_debug)
    Serial.println("Try to connect to the web service...");

  if (_client.connect(_serverIp, _serverPort)) {
    if(_debug)
      Serial.println(" Send data to web service '" + service + "'");

    dataString = "TODO";

    // make a HTTP (POST) request
    _client.println(service);
    _client.println("Host: " + _host);

    _client.println("Accept-Encoding: gzip, deflate");
    _client.println("Content-Type: text/xml;charset=UTF-8");
    _client.print("Content-Length: "); 
    _client.println(dataString.length());
    _client.println("Connection: keep-alive");
    _client.println("User-Agent: Arduino/1.0");
    _client.println(_serverAuth); 

    _client.println("");
    _client.print(dataString);

    _client.stop();
    return true;
  } 
  else {
    // no connection
    Serial.println(" Connection to web server failed!");

    return false;
  }
}

// sd methods --------------------------------------------------------

// reads the status file
void readStatusFile()
{
  String buffer;
  Serial.println("Read status file...");

  if(SD.exists(_statusFile)) {
    // open the file, note that only one file can be open at a time,
    // so you have to close this one before opening another
    File dataFile = SD.open(_statusFile, FILE_READ);

    if (dataFile) {
      int parameter = 0;
      while (dataFile.available()) {
        char character = dataFile.read();

        if(character != '\n')
          buffer.concat(character);
        else {
          int lenght = buffer.length();

          char info[lenght];
          buffer.toCharArray(info, lenght);

          if(parameter == 0) {
            _filePosition = strtoul(info, 0, 10);

            Serial.print(" Last file position: ");
            Serial.println(_filePosition);
          }
          else if(parameter == 1) {
            _fileNumber = strtoul(info, 0, 10);

            Serial.print(" Last file position: ");
            Serial.println(_filePosition);
          }

          parameter++;
        }
      }

      dataFile.close();
    }
  }
}

// updates the status file
boolean updateStatusFile()
{ 
  if(SD.exists(_statusFile)) {
    SD.remove(_statusFile);
  }

  // open the file, note that only one file can be open at a time,
  // so you have to close this one before opening another
  File dataFile = SD.open(_statusFile, FILE_WRITE);

  if (dataFile) {
    if(_debug)
    {
      Serial.print("Write file position/number on status file: ");
      Serial.print(_filePosition);
      Serial.print(", ");
      Serial.println(_fileNumber);
    }

    dataFile.println(_filePosition);
    dataFile.println(_fileNumber);

    dataFile.close();

    return true;
  }
  else {
    Serial.println("Error opening status file!");
    return false;
  } 
}

// makes a copy of the datalog file
boolean backupDlFile()
{
  _filePosition = 0;

  char charBuf[50];
  String dest = String("backup.") + _fileNumber;
  dest.toCharArray(charBuf, 50);

  if(SD.exists(charBuf)) {
    SD.remove(charBuf);
  }

  unsigned long readPosition = 0;
  while(true) {
    String output = readFromFile(_datalogFile, readPosition, 10240, &readPosition);
    if(output == "")
      break;

    // open the file, note that only one file can be open at a time,
    // so you have to close this one before opening another
    File dataFile = SD.open(charBuf, FILE_WRITE);

    // if the file is available, write to it:
    if (dataFile) {
      dataFile.print(output);
      dataFile.close();
    }
    else {
      Serial.println("Error opening datalog backup file!");
      return false;
    }
  }

  Serial.println("New backup file: " + String(dest));

  _fileNumber++;
  _updateStatusFile = true;

  return true;
}

// write the given string on datalogger file
boolean writeOnDlFile(String dataString)
{    
  if(_filePosition + dataString.length() >= _maxFileSize) {
    if(backupDlFile()) {
      SD.remove(_datalogFile);
    }    
  }

  // open the file, note that only one file can be open at a time,
  // so you have to close this one before opening another
  File dataFile = SD.open(_datalogFile, FILE_WRITE);

  // if the file is available, write to it:
  if (dataFile) {
    dataFile.println(dataString);
    dataFile.close();

    return true;
  }
  else {
    Serial.println("Error opening datalog file!");
    return false;
  } 
}

// reads data from datalogger file
String readFromFile(const char* filename, unsigned long filePosition, unsigned long endPosition, unsigned long *readPosition)
{
  const int maxBufferLength = 1024; // characters
  String buffer;

  // open the file, note that only one file can be open at a time,
  // so you have to close this one before opening another
  File dataFile = SD.open(filename, FILE_READ);

  // if the file is available, read it
  if (dataFile) {
    dataFile.seek(filePosition);
    int charIndex = 0;

    while (dataFile.available() || charIndex == maxBufferLength) {
      char character = dataFile.read();
      buffer.concat(character);

      *readPosition = dataFile.position();
      if(endPosition!= 0 && *readPosition == filePosition + endPosition)
        break;

      charIndex++;
    }

    dataFile.close();
  }

  return buffer;
}

// clean all known files
void cleanSdCard()
{
  SD.remove(_datalogFile);
  SD.remove(_statusFile);
}

// modbus rtu methods ------------------------------------------------

unsigned int modbusRTU_CRC16(unsigned char* ptucBuffer, unsigned int uiLen)
{
  unsigned char ucCounter;
  unsigned int uiCRCResult;

  for(uiCRCResult=0xFFFF; uiLen!=0; uiLen --)
  {
    uiCRCResult ^=*ptucBuffer ++;
    for(ucCounter =0; ucCounter <8; ucCounter ++)
    {
      if(uiCRCResult && 0x0001)
        uiCRCResult =( uiCRCResult >>1)^POLY_MODBUS;
      else
        uiCRCResult >>=1;
    }
  }

  return uiCRCResult;
}

String readModbusRTUMap()
{
  const int count = 0x01;
  const int maxReadingInCycle = 10;

  String data = "";

  byte modbusMessage[] = { 
    0x01, // The Slave Address (1)
    0x03, // The Function Code (1)
    0x00, // The Data Address of the first register requested (2)
    0x00, 
    0x00, // The total number of registers requested (2)
    0x02,
    0x00,
    0x00
  };

  Serial.print(", set RS485 transmit mode");
  digitalWrite(SSerialTxControl, RS485Transmit); // enable RS485 Transmit   
  Serial.println("-------------");
  
  for(int i = 0; i < sizeof(modbusMessage); i++){
  Serial.println(modbusMessage[i]);
  }
  Serial1.write(modbusMessage, sizeof(modbusMessage)); // send byte to Remote Arduino

  Serial.println(", set RS485 receive mode");
  digitalWrite(SSerialTxControl, RS485Receive); // disable RS485 Transmit
  
  return data;
}

// modbus tcp methods ------------------------------------------------

// makes a modbus request using the given parameters
void modbusRequest(byte *result, byte modbusServer[], int modbusPort, int fc, int slave, int address, int count)
{
  byte modbusMessage[] = { 
    0x00, // Transaction Identifier (2)
    0x01, 
    0x00, // Protocol Identifier (2)
    0x00, 
    0x00, // Message Length, 6 bytes to follow (2)
    0x06, 
    0x01, // The Slave Address (1)
    0x03, // The Function Code (1)
    0x00, // The Data Address of the first register requested (2)
    0x01, 
    0x00, // The total number of registers requested (2)
    0x01                      };

  if(_debug)
    Serial.print("Modbus request... ");

  modbusMessage[6] = slave;
  modbusMessage[7] = fc;
  modbusMessage[8] = highByte(address);
  modbusMessage[9] = lowByte(address);
  modbusMessage[10] = highByte(count);
  modbusMessage[11] = lowByte(count);

  if (_client.connect(modbusServer, modbusPort)) {
    _client.write(modbusMessage, sizeof(modbusMessage));

    delay(8);

    int index = 0;
    while (_client.available()) {
      byte response = _client.read();

      if(_debug){
        Serial.print(response, HEX);
        Serial.print(",");
      }

      if(index > 8) {
        result[index - 9] = response;
      }

      index++;
    }

    if(_debug) {
      Serial.println(" ok!");
    }
    _client.stop();
  }
  else
    Serial.println("connection with modbus master failed!");
}

// performs the reading tasks (see variable '_modbusTCPReading')
String readModbusTCPMap()
{
  const int count = 0x01;
  const int maxReadingInCycle = 10;

  String data = "";

  for(int batch = 0; batch < MB_TCP_BATCH; batch++) {
    int device = _modbusTCPReading[batch][0];
    int slave = _modbusTCPReading[batch][1];
    int startAddress = _modbusTCPReading[batch][2];
    int endAddress = _modbusTCPReading[batch][3];
    int fc = _modbusTCPReading[batch][4];
    int currentAddress = _modbusTCPReading[batch][5];

    for(int request = 0; request < maxReadingInCycle; request++) {
      if(currentAddress > endAddress)
        currentAddress = startAddress;

      byte modbusResponse[count * 2];
      modbusRequest(&modbusResponse[0], _modbusServer[device], _modbusPort[device], fc, slave, currentAddress, count);

      data += "MTR" + String(slave) + ".";
      for(int i = 0; i < count * 2; i++) {
        data += String(i + currentAddress) + ":" + String(modbusResponse[i], HEX);
      }

      currentAddress += count;
      _modbusTCPReading[batch][5] = currentAddress;
    }
  }

  return data;
}

// rc4 encryption methods --------------------------------------------

void swap(unsigned char *s, unsigned int i, unsigned int j) {
  unsigned char temp = s[i];
  s[i] = s[j];
  s[j] = temp;
}

// key scheduling algorithm (ksa)
void rc4_init(unsigned char *key, unsigned int key_length) {
  for (i = 0; i < 256; i++)
    S[i] = i;

  for (i = j = 0; i < 256; i++) {
    j = (j + key[i % key_length] + S[i]) & 255;
    swap(S, i, j);
  }

  i = j = 0;
}

// pseudo-random generation algorithm (prga)
unsigned char rc4_output() {
  i = (i + 1) & 255;
  j = (j + S[i]) & 255;

  swap(S, i, j);

  return S[(S[i] + S[j]) & 255];
}

// encrypt the given string
String encrypt(String dataString)
{
  String output = "";
  rc4_init(enckey, strlen((char *)enckey));
  for (int i = 0; i < dataString.length(); i++){
    if(output != "")
      output += ",";
    output += dataString[i] ^ rc4_output();
  }

  return output;
}

// time methods ------------------------------------------------------

// gets the current datetime
String getTimestamp()
{
  String timestamp = "";

  timestamp += day();
  timestamp += "/";
  timestamp += month();
  timestamp += "/";
  timestamp += year(); 
  timestamp += "T";
  timestamp += hour();
  timestamp += ":";
  timestamp += minute();
  timestamp += ":";
  timestamp += second();

  return timestamp;
}

// ntp methods -------------------------------------------------------

// send an NTP packet vía “sendNTPpacket” routine and parse the response
unsigned long getNtpTime()
{
  Serial.print("get NTP Time... ");
  sendNTPpacket(_sntpServerIp); // send an NTP packet to a time server

  delay(1000);
  if(_udp.parsePacket()) {    
    // we've received a packet, read the data from it
    _udp.read(_packetBuffer, _NTP_PACKET_SIZE);  // read the packet into the buffer

    // the timestamp starts at byte 40 of the received packet and is four bytes, or two words, long
    // first, esxtract the two words...

    unsigned long highWord = word(_packetBuffer[40], _packetBuffer[41]);
    unsigned long lowWord = word(_packetBuffer[42], _packetBuffer[43]);

    // combine the four bytes (two words) into a long integer
    // this is NTP time (seconds since Jan 1 1900)...

    unsigned long secsSince1900 = highWord << 16 | lowWord; 
    const unsigned long seventyYears = 2208988800UL;

    // subtract seventy years and add the time zone

    unsigned long epoch = secsSince1900 - seventyYears + (_timeZoneOffset * 3600L);

    Serial.print(String(epoch) + ", ");
    return epoch;
  }

  Serial.print("error! ");
  return 0;
}

// send an NTP request to the time server at the given address 
unsigned long sendNTPpacket(IPAddress& address)
{
  // set all bytes in the buffer to 0
  memset(_packetBuffer, 0, _NTP_PACKET_SIZE); 

  // initialize values needed to form NTP request

  _packetBuffer[0] = 0b11100011;   // LI, Version, Mode
  _packetBuffer[1] = 0;     // stratum, or type of clock
  _packetBuffer[2] = 6;     // polling Interval
  _packetBuffer[3] = 0xEC;  // peer Clock Precision

  // 8 bytes of zero for Root Delay & Root Dispersion
  _packetBuffer[12]  = 49; 
  _packetBuffer[13]  = 0x4E;
  _packetBuffer[14]  = 49;
  _packetBuffer[15]  = 52;

  // all NTP fields have been given values, now
  // you can send a packet requesting a timestamp...	

  _udp.beginPacket(address, 123); // NTP requests are to port 123
  _udp.write(_packetBuffer, _NTP_PACKET_SIZE);

  _udp.endPacket(); 
}

// arduino methods ---------------------------------------------------

// the setup routine runs once when you press reset
void setup() {
  // initialize serial communication at '_bitsPerSecond' bits per second
  Serial.begin(_bitsPerSecond);
  while (!Serial) {
    ; // wait for serial port to connect. Needed for Leonardo only
  }

  // setup
  initializeSdCard();
  initializeEthernet();
  initializeClock();

  initializeModbusRTU();

  // read settings from status file
  readStatusFile();

  Serial.println("Datalogger ready!");
}

// the loop routine runs over and over again forever
void loop() {
  unsigned long startTime = millis();
  String timestamp = getTimestamp();

  if(_debug)
    Serial.println("Timestamp: " + timestamp);

  if(_enableLocalWebServer) {
    webServerImpl();
  }

  // make a string for assembling the data to log:
  String dataString = "R" + timestamp + "@";

  // read the data (sensors) and append to the string
  for (int analogPin = _startAnalogPin; analogPin < _endAnalogPin + 1; analogPin++) {
    dataString += "AMR" + String(analogPin) + ":" + String(analogRead(analogPin));
    if (analogPin < _endAnalogPin) {
      dataString += ","; 
    }
  }

  if(_debug) {
    Serial.print("Analog readings: ");
    Serial.println(dataString);
  }

  // read the modbus-rtu data
  if(_enableModbusRTU) {
    dataString += "@";

    String modbusRTUData = readModbusRTUMap();
    dataString += modbusRTUData;

    if(_debug) {
      Serial.print("Modbus-RTU readings: ");
      Serial.println(modbusRTUData);
    }
  }

  // read the modbus-tcp data
  if(_enableModbusTCP) {
    dataString += "@";

    String modbusTCPData = readModbusTCPMap();
    dataString += modbusTCPData;

    if(_debug) {
      Serial.print("Modbus-TCP readings: ");
      Serial.println(modbusTCPData);
    }
  }

  // try to write the data on SD card
  if(!writeOnDlFile(dataString)) {
    Serial.println("Error when write data on SD!");
  }

  // send (encrypt) data to the web service
  if(_enableSendInfoToWebService) {
    unsigned long readPosition;
    String buffer = readFromFile(_datalogFile, _filePosition, 0, &readPosition);

    if(post(encrypt(buffer))) {
      _filePosition = readPosition;
      _updateStatusFile = true;
    }
  }

  // update status file and wait
  if(_updateStatusFile) {
    updateStatusFile();
    _updateStatusFile = false;
  }

  unsigned long elapsedMilliseconds = millis() - startTime;
  if(elapsedMilliseconds < _waitTime) {
    delay(_waitTime - elapsedMilliseconds );
  }
}
