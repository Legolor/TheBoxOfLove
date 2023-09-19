/*
████████╗██╗  ██╗███████╗    ██████╗  ██████╗ ██╗  ██╗ 
╚══██╔══╝██║  ██║██╔════╝    ██╔══██╗██╔═══██╗╚██╗██╔╝ 
   ██║   ███████║█████╗      ██████╔╝██║   ██║ ╚███╔╝  
   ██║   ██╔══██║██╔══╝      ██╔══██╗██║   ██║ ██╔██╗  
   ██║   ██║  ██║███████╗    ██████╔╝╚██████╔╝██╔╝ ██╗ 
   ╚═╝   ╚═╝  ╚═╝╚══════╝    ╚═════╝  ╚═════╝ ╚═╝  ╚═╝ 
                                                       
 ██████╗ ███████╗    ██╗      ██████╗ ██╗   ██╗███████╗
██╔═══██╗██╔════╝    ██║     ██╔═══██╗██║   ██║██╔════╝
██║   ██║█████╗      ██║     ██║   ██║██║   ██║█████╗  
██║   ██║██╔══╝      ██║     ██║   ██║╚██╗ ██╔╝██╔══╝  
╚██████╔╝██║         ███████╗╚██████╔╝ ╚████╔╝ ███████╗
 ╚═════╝ ╚═╝         ╚══════╝ ╚═════╝   ╚═══╝  ╚══════╝
*/
//My board Manager- ESP32 dev module
/***************************************************************************************************
P  I  N  O  U  T  S
***************************************************************************************************/
/*
 Devices   ESP32
  (tft)  
  VCC       3.3V
  GND       GND
  CS        5
  RESET     4
  DC        2
  MOSI      23*
  SCK       18**
  LED       25 (for no backlight control use 3.3V or VIN)
  MISO      19***

  (touch)  
  T_CLK     18**
  T_CS      21
  T_DIN     23*
  T_DO      19***
  T_IRQ     (not connected)

  (sd)
  SD_CS     26  (15 would be faster but it can causes boot fail after uploading)
  SD_MOSI   13
  SD_MISO   27  (12 would be faster but causes boot fail after uploading)
  SD_SCK    14

  (LED)
  VCC       VIN
  GND       GND
  DATA      32
*/
//The rest of the pins are defined in the User_Setup.h file
#define SD_CS 26//15              //Chip select for the SD card
#define SD_SCK  14                //Since I switched to the esp32 I changed to
#define SD_MISO  27//12           //the sd card on the hspi instead of vspi
#define SD_MOSI  13               //this is faster and eliminates potential clashing of espi and sd
                                  //note you have to do /filename for the esp32 sd library

#define TFT_LED 25//15//5//D1             //TFT LED backlight for brightness control
#define LED_PIN 32//3 (RGB LED)   //RGB LED data pin
#define ONBOARD_LED 2


/***************************************************************************************************
L  I  B  R  A  R  I  E  S
***************************************************************************************************/
#include <SPI.h>                  //SPI communications for TFT, touch controller, and SD
#include <TFT_eSPI.h>             //TFT controller library    MAKE SURE TO SETUP User_Setup.h FILE 
#include <SD.h>                   //For the external SD card, there are probably better libraries out there for this
#include <FastLED.h>              //For the WS2812 or similar RGB LEDS (1 data pin type)

#include "WiFi.h"                 //For basic WIFI control
#include <WiFiClient.h>           //More WIFI stuffs
#include <HTTPClient.h>           //For HTTP GET and POST requests
#include <WebServer.h>            //For the remote WIFI connection server setup
#include <EEPROM.h>               //For non-volatile variables (wifi ssid/psw, puzzle progress, screen brightness, etc)(esp has emulated eeprom)
#include "NTPClient.h"            //For checking the NTP time servers
#include "WiFiUdp.h"              //For checking the NTP time servers (manages the packets)

#include <ESPmDNS.h>              //For remote code upload
#include <Update.h>

#include <esp_heap_caps.h>        //Required for ESP32 memory functions in printMemoryStats()
#include <TimeLib.h>              //converting timestamps to epoch time and visa versa


/***************************************************************************************************
I  N  I  T  I  A  L  I  Z  A  T  I  O  N  S
***************************************************************************************************/
#define BOX_NUMBER_1            //Make switching uploading code between the two boxes easy
//#define BOX_NUMBER_2
//------------------------------------------------------------------------Screen
TFT_eSPI tft = TFT_eSPI();        //Initilizes the espi struct as "tft"
bool backlightOff = false;
int screenBrightness = 255;
void getSetBrightness(int);       //Function prototype (usually these are auto-generated but calling functions in setup sometimes throws an error)
                                  //This is preventable by having the setup and loop at the bottom below the creation of getSetBrightness() or defining this
//------------------------------------------------------------------------Touch
#ifdef BOX_NUMBER_1
uint16_t calData[5] = {278, 3618, 366, 3434, 1}; // Global variable to store the calibration data, you need to calibrate and check the serial monitor, update these values- and reflash
#elif defined(BOX_NUMBER_2)
uint16_t calData[5] = {201, 3475, 303, 3552, 1};
#endif
void getSetTouchCalibrate(uint16_t calData2[5], bool doIChange);      
                            
//------------------------------------------------------------------------SD Card
SPIClass spiSD(HSPI);             //Since the esp32 has 2 (3) spi connections I am using VSPI(default) for the tft+touch and HSPI for the sd card
                                  //I'm pretty sure they work on the same spi line but this will make things faster and avoid any collisions
//------------------------------------------------------------------------RGB LEDS
#define NUM_LEDS 1
#define COLOR_ORDER GRB           //Color order
#define LED_TYPE WS2811
CRGB leds[NUM_LEDS];
int toggleLEDs = 0;               //Three settings: on, off, partial

//------------------------------------------------------------------------INTERNET + ESP WIFI SERVER
WebServer server(80);
int i = 0;
int statusCode;
const char* ssid = "ssid";         //If you want to actually put anything here you will have to uncomment the line in setup
const char* passphrase = "psw";  //After the first connection to wifi the ssid and psw will be permanantly stored in the EEPROM
String st;
String content;
bool testWifi(void);              //Function prototypes for safety
void launchWeb(void);
void setupAP(void);
//------------------------------------------------------------------------Remote Code Upload
WebServer server1(80);
const char* host = "esp32";
const char* remoteCodeUploadUsername = "thebox";    //username and password for remote code upload
const char* remoteCodeUploadPassword = "oflove";
const char* loginIndexTemplate =                    //loginIndex template to allow editing of username and password
  "<form name='loginForm'>"
      "<table width='20%%' bgcolor='A09F9F' align='center'>"
          "<tr>"
              "<td colspan=2>"
                  "<center><font size=4><b>ESP32 Login Page</b></font></center>"
                  "<br>"
              "</td>"
              "<br>"
              "<br>"
          "</tr>"
          "<td>Username:</td>"
          "<td><input type='text' size=25 name='userid'><br></td>"
          "</tr>"
          "<br>"
          "<br>"
          "<tr>"
              "<td>Password:</td>"
              "<td><input type='Password' size=25 name='pwd'><br></td>"
              "<br>"
              "<br>"
          "</tr>"
          "<tr>"
              "<td><input type='submit' onclick='check(this.form)' value='Login'></td>"
          "</tr>"
      "</table>"
  "</form>"
  "<script>"
      "function check(form)"
      "{"
      "if(form.userid.value=='%s' && form.pwd.value=='%s')"    
      "{"
      "window.open('/serverIndex');"
      "}"
      "else"
      "{"
      " alert('Error Password or Username');"
      "}"
      "}"
"</script>";
const char* serverIndex = 
  "<script src='https://ajax.googleapis.com/ajax/libs/jquery/3.2.1/jquery.min.js'></script>"
  "<form method='POST' action='#' enctype='multipart/form-data' id='upload_form'>"
    "<input type='file' name='update'>"
          "<input type='submit' value='Update'>"
      "</form>"
  "<div id='prg'>progress: 0%</div>"
  "<script>"
    "$('form').submit(function(e){"
    "e.preventDefault();"
    "var form = $('#upload_form')[0];"
    "var data = new FormData(form);"
    " $.ajax({"
    "url: '/update',"
    "type: 'POST',"
    "data: data,"
    "contentType: false,"
    "processData:false,"
    "xhr: function() {"
    "var xhr = new window.XMLHttpRequest();"
    "xhr.upload.addEventListener('progress', function(evt) {"
    "if (evt.lengthComputable) {"
    "var per = evt.loaded / evt.total;"
    "$('#prg').html('progress: ' + Math.round(per*100) + '%');"
    "}"
    "}, false);"
    "return xhr;"
    "},"
    "success:function(d, s) {"
    "console.log('success!')" 
  "},"
  "error: function (a, b, c) {"
  "}"
  "});"
  "});"
"</script>";
//------------------------------------------------------------------------Remote SD Files Upload
WebServer server2(80);
bool loginCheckPassed = false;
String currentBasePath = "";
File uploadFile;
bool stayInSDServer = true;
String remoteSDConnectionUsername = "thebox";     //username and password for remote SD file editing
String remoteSDConnectionPassword = "oflove";

//------------------------------------------------------------------------Cloud Firebase Server
const char* projectID = "lovebox1-ff0c0";     //Your firebase project ID
#ifdef BOX_NUMBER_1
const char* mycollection = "box1";            //Your firebase targeted collection 
const char* theircollection = "box2";         //You will switch this between uploading to your two boards
#elif defined(BOX_NUMBER_2)
const char* mycollection = "box2";
const char* theircollection = "box1";
#endif
const char* emojiDocumentID = "emoji";        //Firebase documents and fields          
const char* emojiField = "number";
const char* fileDocumentID = "file";
const char* fileField = "string";
const char* messageDocumentID = "message";
const char* messageField = "string";

bool getCloudOnStartup = false;
String massiveSendString = "";                //The string to hold the file to be sent to firebase, http has to use strings :(
bool newEmoji = false;                        //Data for showing and saving newly gotten emojis and drawings
String newEmojiNumber = "";
int newEmojiTimestamp = 0;
bool newDrawing = false;
String newDrawingName = "";
int newDrawingTimestamp = 0;
bool newMessage = false;
int newMessageTimestamp = 0;
String messageTextString = "";

//------------------------------------------------------------------------NTP
const long utcOffsetInSeconds = -18000;       //Update for your timezone (seconds)
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", utcOffsetInSeconds);   //Starts the NTP client
long epochTime = 0;
int daysSinceStart = 0;

//------------------------------------------------------------------------Draw Screen
uint8_t imageLine[(9600*2)+(16)];                  //Holds all of the drawing pixels, 2bit color -> an array of 19200 bytes can store all the pixels
byte myByte = 0;                              //Temp stuff for when it comes to encoding and decoding the pixel colors
uint16_t tempPixels[4];
uint8_t fourPixels=0;
uint32_t drawColor1 = TFT_WHITE;
uint32_t drawColor2 = TFT_RED;
uint32_t drawColor3 = TFT_BLUE;
uint32_t drawColor4 = TFT_BLACK;
uint32_t currentColor = drawColor1;
int sizeOfBrush = 5; //3,5,9 are the options currently

//------------------------------------------------------------------------File Screen
int fileScrollNumber = 0;                     //:O How fancy, a scrolling file screen
String recieveddrawingsFilenames[200]={};     //All the filenames are stored in these lovely string arrays
String mydrawingsFilenames[200]={};
String artFilenames[200]={}; 

//------------------------------------------------------------------------Puzzle Screen
int puzzleCheckpoint = 0;                     //So you don't have to repeat puzzles
int todaysTry = 0;                            //You only get a few tries per day!

//------------------------------------------------------------------------Keyboard
int currenTKeyboard = 0;                      //Switch between numbers and letters and such
String keyboardString = "";

/***************************************************************************************************
S  E  T  U  P      &      L  O  O  P
***************************************************************************************************/
void setup() {
  Serial.begin(115200);                       //Serial monitor begin
  delay(3000);
  EEPROM.begin(512);                          //Start the (simulated) EEPROM that's 512 bytes long
  delay(10);

  tft.init();                                 //Initilize the display
  tft.setRotation(1);
  tft.invertDisplay(false);                   //I think I needed this line even though it's false

  //getSetBrightness(255);//Uncomment the first time
  getSetBrightness(-1);                                         //Initilize user settings to change screen brightness

  spiSD.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);                 //SCK,MISO,MOSI,SS (for the SD)
  
  //clearEEPROMRange(350, 366);touch_calibrate();//Uncomment the first time
  getSetTouchCalibrate(calData,false);

  LEDS.addLeds<LED_TYPE, LED_PIN, COLOR_ORDER>(leds, NUM_LEDS); //FastLED initilize
  FastLED.clear(true);                                          //Set to off, true means update

  //getSetLEDStatus(0,1);//Uncomment the first time
  getSetLEDStatus(0,-1);                                     //Initilize user settings to turn off the LEDS

  tft.fillScreen(TFT_WHITE);
  drawBmp24("/logos/splashscreen.bmp",0,0); //Draw a loding page while we wait for the wifi to connect
  
  //SetBreakWifi(ssid,passphrase);//Uncomment (the first time) if you want to set the wifi from the sketch
  remoteWifiSetup();                        //Calls the entire wifi connection and server script
  
  timeClient.begin();                       //Start NTP time
  ntpUpdate();                              //Update NTP time
  
  //puzzlegameScreenCheckpointSet(0);//Uncomment the first time
  puzzlegameScreenCheckpointRefresh();      //See what the puzzle checkpoint is
  puzzlegameScreenTodaysTryRefresh();       //See if the puzzle has been tried today (one try a day)
  
  randomSeed(epochTime);                    //Some functions want a random number, set the seed from the time

  //setLastEmojiDrawingTimestamp(1,1,1);//Uncomment the first time
  setLastEmojiDrawingTimestamp(-1,-1,-1);      //Initilizes the variables for checking if the emoji/drawing have already been recieved (by timestamp)
  massiveSendString.reserve(25800);//25610//Strings really shouldnt be this long... you have to reserve space if its larger than theoretically 4kB but it didnt give me errors untill ~20kB

  //getCloudOnStartupToggle(0, true);//Uncomment the first time
  getCloudOnStartupToggle(0, false);
  if (getCloudOnStartup){
    //drawBmp24("/logos/refreshlogoloading.bmp",290,210);
    String mrFile = "/art/" + String(daysSinceStart) + ".bmp";
    drawBmp24(mrFile.c_str(),0,0);
    getCloudEmoji();getCloudDrawing();getCloudMessage();
  }

  digitalWrite(ONBOARD_LED, LOW);
  //delay(1000);
}

void loop(void) {

  Serial.println("MAIN LOOP BEGINNING");
  homeScreen();

  if(backlightOff)
    passiveScreen();


  
}


/***************************************************************************************************
H  O  M  E      S  C  R  E  E  N  S
***************************************************************************************************/
void passiveScreen(){
  //The passive screen is an alternate to the home screen. If you are resting on the homescreen or click
  //the off button it will go to this screen. Low power mode was not an option really becasue we still
  //need to do a lot passivly: check the server, control leds, and wait for a touch to go back to the 
  //home screen. The passive screen is the only place that controls the RGB LED.
  tft.fillScreen(TFT_BLACK);
  analogWrite(TFT_LED, 0);
  Serial.println("backlight off");            //This offers a true black off!
  
  delay(800);
  int cloudCheckTimer = millis();
  int burnInCheckTimer = millis();
  bool saveButtonPressed = false;              //Flag to indicate if the screen was touched
  while (!saveButtonPressed) {    
  uint16_t x = 0, y = 0;
  bool pressed = tft.getTouch(&x, &y);
    if (pressed) {
      Serial.print(String(x) + "," + String(y) + "\n");
      saveButtonPressed = true;                //If the screen was pushed return
    }

    if ((toggleLEDs==0) && ((!newEmoji) || (!newDrawing) || (!newMessage))){
      fill_solid(leds, NUM_LEDS, CRGB::Red);//White
      double ledBrightness = (exp(sin((millis()-1000)/2000.0*PI)) - 0.3)*102;
      FastLED.setBrightness(ledBrightness);      //Breath effect function to control the brightness
      FastLED.show();
      //Serial.println(ledBrightness);      
    }
    
    if (((toggleLEDs==0) || (toggleLEDs==2)) && ((newEmoji) || (newDrawing) || (newMessage))){
      fill_solid(leds, NUM_LEDS, CRGB::White);//Red
      double ledBrightness = (exp(sin((millis()-1000)/2000.0*PI)) - 0.3)*102;
      FastLED.setBrightness(ledBrightness);      //Breath effect function to control the brightness
      FastLED.show();
      //Serial.println(ledBrightness);      
    }

    //Check the cloud every so often, but only while the leds are also in their lowest point (so it looks less weird when they pause)
    if( (millis()>(cloudCheckTimer+(60000*1))) && (millis()%4000 < 15) ){
      Serial.println("Cloud refresh due to timer");
      getCloudEmoji();
      getCloudDrawing();
      getCloudMessage();
      cloudCheckTimer = millis();
    }
    //I'm not really sure if there is burn in at all with tfts, or if it could burn in black. Either way can't hurt to open and close the filters every once in a while
    if( millis()>(burnInCheckTimer+(60000*3)) ){
      Serial.println("Burn in prevention??");
      tft.fillScreen(TFT_WHITE);
      delay(500);
      tft.fillScreen(TFT_BLACK);      
      burnInCheckTimer = millis();
    }
    

  } 

  FastLED.clear(true);
  backlightOff = false;
  getSetBrightness(-1);
}

void homeScreen(){
  //Home screen is really the landing place for most things. All the 'apps' and a lot of the pages are here
  tft.fillScreen(TFT_WHITE);
  drawBmp24("/logos/homelogo.bmp",0,170);
  drawBmp24("/logos/powerlogo.bmp",0,205);
  drawBmp24("/logos/settings.bmp",0,135);
  drawBmp24("/logos/refreshlogo.bmp",145,105);
  if((newEmoji) || (newDrawing) || (newMessage)){drawBmp24("/logos/mail.bmp",145,105);}
  drawBmp24("/logos/messages.bmp",130,20);
  drawBmp24("/logos/emojilogo.bmp",60,90);
  drawBmp24("/logos/drawlogo.bmp",200,90);
  drawBmp24("/logos/fileslogo.bmp",85,167);
  drawBmp24("/logos/pictureslogo.bmp",175,170);
  //drawBmp32("/logos/alphatest.bmp",180,80);//working alpha!

  int sleepTimer = millis();
  int refreshCloudTimer = millis();
  bool saveButtonPressed = false;                     //If this is true it will exit (and usually reopen) the home loop
  while (!saveButtonPressed) {

  if(millis()>(sleepTimer+(60000*2))){                //Every so often it will automatically turn the screen off and go to the passive screen
    Serial.println("Sleeping due to timer");
    backlightOff = true;
    saveButtonPressed = true;
    sleepTimer = millis();
  }
  if(millis()>(refreshCloudTimer+(60000*1))){         //Every so often it will automatically scan firebase
    Serial.println("Cloud refresh due to timer");
    drawBmp24("/logos/refreshlogoloading.bmp",145,105);
    getCloudEmoji();
    getCloudDrawing();
    getCloudMessage();
    refreshCloudTimer = millis();
    if((newEmoji) || (newDrawing) || (newMessage)){drawBmp24("/logos/mail.bmp",145,105);}
    else {drawBmp24("/logos/refreshlogo.bmp",145,105);}
  }
  
    
  uint16_t x = 0, y = 0;
  bool pressed = tft.getTouch(&x, &y);
  if (pressed) {                                      //IF the screen is touched
    sleepTimer = millis();
    Serial.print(String(x) + "," + String(y) + "\n");
           
    if((x>0)&&(x<30)&&(y>170)&&(y<200)){
      //homeScreen();
      saveButtonPressed = true;
    }

    if((x>0)&&(x<30)&&(y>200)&&(y<240)){
      backlightOff = true;
      saveButtonPressed = true;
    }

    if((x>0)&&(x<30)&&(y>135)&&(y<165)){
      settingsScreen();
      saveButtonPressed = true;
    }

    if((x>130)&&(x<190)&&(y>20)&&(y<80)){
      sendMessageString();
      saveButtonPressed = true;
    }
    if((x>175)&&(x<235)&&(y>170)&&(y<230)){
      artScreen();
      saveButtonPressed = true;
    }
    if((x>60)&&(x<120)&&(y>90)&&(y<150)){
      emojiScreen();
      saveButtonPressed = true;
    }
    if((x>85)&&(x<145)&&(y>167)&&(y<227)){
      while(filesScreen()){}    //doing it this way prevents a stack overflow becasue it goes back to homeScreen from filescreen each time which clears up stack memory (probably from the bubblesort)
      saveButtonPressed = true; //really it's just that recursive functions are bad, but we want to stay on the filescreen continually, so we breifly exit out of the filescreen only to go right back
    }                           //recursion would be an issue here if we didn't beacuse it would save the path of loop->homescreen->filescreen->filescreen->...n forever
    if((x>200)&&(x<260)&&(y>90)&&(y<150)){
      drawScreen2();//drawScreen();
      saveButtonPressed = true;
    }

    if((x>145)&&(x<175)&&(y>105)&&(y<135)){
      drawBmp24("/logos/refreshlogoloading.bmp",145,105);
      if (!(newEmoji) && (!newDrawing) && (!newMessage)){
        getCloudEmoji();
        getCloudDrawing();
        getCloudMessage();
      }
      if ((newEmoji) || (newDrawing) || (newMessage)){
        printIncomingData();
        saveButtonPressed = true;
      }
      drawBmp24("/logos/refreshlogo.bmp",145,105);
    }
    
    if((x>260)&&(x<320)&&(y>180)&&(y<240)){           //Takes you to the hidden utility screen
      bool leaveLoop = false;                         //(if you hold in the bottom right)
      int concurentPresses = 0;
      int pressStart = millis();    
      while (!leaveLoop){
        uint16_t x2 = 0, y2 = 0;
        bool pressed2 = tft.getTouch(&x2, &y2);
        Serial.println(String(x) + "," + String(y) + "\n");
        Serial.println("concurent presses: " + String(concurentPresses));
        if (millis()>(pressStart+1000))               //kicks you out if you pick up your finger/stylus
          leaveLoop = true;
        if (pressed2){
          if((x2>260)&&(x2<320)&&(y2>180)&&(y2<240)){
            concurentPresses++;
            pressStart = millis();         
          } 
           else    
            leaveLoop = true;                         //kicks you out if its not a concurent touch      
        }   
        if (concurentPresses>50){
          utilityScreen();
          leaveLoop = true;
          saveButtonPressed = true;          
        }
      }          
    }

    if((x>190)&&(x<320)&&(y>0)&&(y<30)){              //Takes you to the hidden keyboard message send screen
      bool leaveLoop = false;                         //(if you hold in the very top right for long enough)
      int concurentPresses = 0;
      int pressStart = millis();    
      while (!leaveLoop){
        uint16_t x2 = 0, y2 = 0;
        bool pressed2 = tft.getTouch(&x2, &y2);
        Serial.println(String(x2) + "," + String(y2) + "\n");
        Serial.println("concurent presses: " + String(concurentPresses));
        if (millis()>(pressStart+1000))
          leaveLoop = true;
        if (pressed2){
          Serial.println("1");
          if((x2>300)&&(x2<320)&&(y2>0)&&(y2<20)){
            concurentPresses++;
            pressStart = millis();         
          } 
           else    
            leaveLoop = true;                
        }   
        if (concurentPresses>100){
          //sendMessageString();
          //drawScreen2();          
          leaveLoop = true;
          saveButtonPressed = true;          
        }
      }          
    }
    
    puzzlegameScreenCheckpointRefresh();              //All of this bellow is for the enterence to the puzzle screen
    if(((x>0)&&(x<100)&&(y>0)&&(y<80)) && (puzzleCheckpoint==0)){  // 0
      delay(250);                                     //It's essentially a numpad (0-8 top-left to bottom-right)
      tft.fillCircle(30,30,10,TFT_GREEN);             //Draws a green circle to show when keypad unlocking is in progress
      bool leaveLoop = false;
      int passcodeCorrect = 0;                        //code is 04 15 2022
      int pressStart = millis();    
      while (!leaveLoop){
        uint16_t x2 = 0, y2 = 0;
        //delay(0);
        bool pressed2 = tft.getTouch(&x2, &y2);
        Serial.println(String(x2) + "," + String(y2) + "\n");
        Serial.println("passcodeCorrect: " + String(passcodeCorrect));
        if (millis()>(pressStart+2000)){
          leaveLoop = true;
          tft.fillCircle(30,30,10,TFT_WHITE);
        }
        if (pressed2) {
          //Serial.println("1");
          if (passcodeCorrect == 0){
            if ((x2 > 100) && (x2 < 200) && (y2 > 80) && (y2 < 160)){ // 4
              passcodeCorrect++;
              delay(350);
              pressStart = millis();
            }else{leaveLoop = true;}  
          } else if (passcodeCorrect == 1) {
            if ((x2 > 100) && (x2 < 200) && (y2 > 0) && (y2 < 80)) { // 1
              passcodeCorrect++;
              delay(350);
              pressStart = millis();
            }else{leaveLoop = true;}
          } else if (passcodeCorrect == 2) {
            if ((x2 > 200) && (x2 < 300) && (y2 > 80) && (y2 < 160)) { // 5
              passcodeCorrect++;
              delay(350);
              pressStart = millis();
            }else{leaveLoop = true;}
          } else if (passcodeCorrect == 3) {
            if ((x2 > 200) && (x2 < 300) && (y2 > 0) && (y2 < 80)) { // 2
              passcodeCorrect++;
              delay(350);
              pressStart = millis();
            }else{leaveLoop = true;}
          } else if (passcodeCorrect == 4) {
            if ((x2 > 0) && (x2 < 100) && (y2 > 0) && (y2 < 80)) { // 0
              passcodeCorrect++;
              delay(350);
              pressStart = millis();
            }else{leaveLoop = true;}
          } else if (passcodeCorrect == 5) {
            if ((x2 > 200) && (x2 < 300) && (y2 > 0) && (y2 < 80)) { // 2
              passcodeCorrect++;
              delay(350);
              pressStart = millis();
            }else{leaveLoop = true;}
          } else if (passcodeCorrect >= 6) {
            if ((x2 > 200) && (x2 < 300) && (y2 > 0) && (y2 < 80)) { // 2
              passcodeCorrect++;
              delay(350);
              pressStart = millis();
            }else{leaveLoop = true;}
          } //else {    
          //   leaveLoop = true;
          //   Serial.println("outside touch");
          // }    
        }
        
        if (passcodeCorrect>=7){
          Serial.println("passcode cracked");
          puzzlegameScreenCheckpointSet(1);
          puzzlegameScreen();
          leaveLoop = true;
          saveButtonPressed = true;          
        }
      }                
    }
    tft.fillCircle(30,30,10,TFT_WHITE);    
    if(((x>0)&&(x<100)&&(y>0)&&(y<80)) && (puzzleCheckpoint>0)){
      puzzlegameScreen();
      saveButtonPressed = true;    
    }

  }


    
  }
}


/***************************************************************************************************
P  U  Z  Z  L  E      S  C  R  E  E  N  S
***************************************************************************************************/
void puzzlegameScreen(){
  //This is the landing page for all of the puzzle chalanges, they go in order and
  //have some flavor text inbetween them. 
  tft.fillScreen(TFT_SKYBLUE);
  puzzlegameScreenCheckpointRefresh();
  puzzlegameScreenTodaysTryRefresh();
  Serial.println("Checkpoint: " + String(puzzleCheckpoint));
  if (todaysTry==1){                                       //You only get a certin number of tries a day
    Serial.println("already tried today");
    tft.fillScreen(TFT_SKYBLUE);
    tft.drawString("already tried today",50,50);
    delay(2000);
    return;
  }
  
  if (puzzleCheckpoint==1){
    Serial.println("instructions prompt");
    tft.setTextSize(3);
    tft.setTextFont(1);
    tft.setTextColor(TFT_BLACK);
    tft.setTextDatum(TC_DATUM);
    //tft.setTextWrap(true);
    delay(1000);
    
    tft.drawString("Hello There",160,100);
    delay(3000);
    tft.fillScreen(TFT_SKYBLUE);
    tft.drawString("Good Job",160,50);
    tft.drawString("Cracking The Code",160,120);//
    delay(4000);
    tft.fillScreen(TFT_SKYBLUE);
    tft.drawString("Now it's time",160,50);
    tft.drawString("for some more",160,120);
    tft.drawString("challenges",160,190);
    delay(4000);
    tft.fillScreen(TFT_SKYBLUE);
    tft.drawString("They shouldn't be",160,50);//
    tft.drawString("too too hard...",160,120);//
    delay(3000);
    tft.fillScreen(TFT_SKYBLUE);
    tft.drawString("But... there's",160,50);//
    tft.drawString("a catch",160,120);
    delay(3000);
    tft.fillScreen(TFT_SKYBLUE);
    tft.drawString("You only get",160,50);
    tft.drawString("THREE tries",160,120);
    tft.drawString("per day haha",160,190);
    delay(5000);
    tft.fillScreen(TFT_SKYBLUE);
    tft.drawString("Remember you can",160,50);//
    tft.drawString("rst EVERYTHING on",160,120);//
    tft.drawString("the settings page",160,190);//
    delay(6000);
    tft.fillScreen(TFT_SKYBLUE);
    tft.drawString("Good Luck :)",160,100);
    delay(4000);
    
    puzzlegameScreenCheckpointSet(2);
    tft.setTextSize(1);
    tft.setTextFont(2);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setTextDatum(TL_DATUM);
    //tft.setTextWrap(false);
  }
  
  if (puzzleCheckpoint==2){
    puzzlegameScreenN1();
  }

  if (puzzleCheckpoint==3){
    puzzlegameScreenN2();
  }

  if (puzzleCheckpoint==4){
    puzzlegameScreenN3();
  }

  if (puzzleCheckpoint==5){
    puzzlegameScreenN4();    
  }  

  if (puzzleCheckpoint==6){                                 //This is a fake win screen. evilish...
    tft.setTextSize(2);
    tft.setTextFont(1);
    tft.setTextColor(TFT_BLACK);
    tft.setTextDatum(TC_DATUM);
    //tft.setTextWrap(true);
    delay(1000);
    
    tft.fillScreen(TFT_SKYBLUE);
    tft.drawString("Wow",160,100);
    delay(3000);
    tft.fillScreen(TFT_SKYBLUE);
    tft.drawString("You made it",160,50);
    delay(4000);
    tft.fillScreen(TFT_SKYBLUE);
    tft.drawString("Here's the reward: ",160,50);
    delay(2000);
    tft.fillScreen(TFT_RED);
    delay(3000);
    tft.fillScreen(TFT_WHITE);
    tft.drawString("Error code: 42",160,50);//
    analogWrite(TFT_LED, 50);
    delay(3000);
    tft.fillScreen(TFT_WHITE);
    drawBmp24("/logos/homelogo.bmp",0,170);
    drawBmp24("/logos/pictureslogo.bmp",130,20);
    drawBmp24("/logos/settings.bmp",0,135);
    drawBmp24("/logos/emojilogo.bmp",60,90);
    drawBmp24("/logos/drawlogo.bmp",200,90);
    drawBmp24("/logos/fileslogo.bmp",130,160);
    drawBmp24("/logos/powerlogo.bmp",0,205);
    drawBmp24("/logos/refreshlogo.bmp",145,105);
    delay(9000);
    tft.fillScreen(TFT_SKYBLUE);
    getSetBrightness(-1);
    tft.drawString("Just kidding",160,50);
    delay(1000);
    tft.fillScreen(TFT_SKYBLUE);
    tft.drawString("But there is",160,50);//
    tft.drawString("one more challenge",160,120);//
    delay(3000);
    
    puzzlegameScreenCheckpointSet(7);
    tft.setTextSize(1);
    tft.setTextFont(2);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setTextDatum(TL_DATUM);
  }

  if (puzzleCheckpoint==7){
    puzzlegameScreenN5();    
  }

  if (puzzleCheckpoint==8){                                 //Real win screen
    tft.setTextSize(2);
    tft.setTextFont(1);
    tft.setTextColor(TFT_BLACK);
    tft.setTextDatum(TC_DATUM);
    //tft.setTextWrap(true);
    delay(1000);
    
    tft.fillScreen(TFT_SKYBLUE);
    tft.drawString("Wow",160,100);
    delay(3000);
    tft.fillScreen(TFT_SKYBLUE);
    tft.drawString("You made it",160,50);
    delay(4000);
    tft.fillScreen(TFT_SKYBLUE);
    tft.drawString("Here's the reward: ",160,50);
    delay(2000);
    tft.fillScreen(TFT_RED);
    delay(2000);
   
    tft.setTextSize(1);
    tft.setTextFont(2);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setTextDatum(TL_DATUM);

    puzzlegameScreenCheckpointSet(9);
  }

  if (puzzleCheckpoint==9){                                 //This is where the prize is displayed
    //tft.fillScreen(TFT_RED);
    drawBmp24("/logos/qr2SecretPresent.bmp",0,0);
    delay(500);
    
    bool saveButtonPressed = false; // Flag to indicate if save button was pressed
    while (!saveButtonPressed) {
    uint16_t x = 0, y = 0;
    bool pressed = tft.getTouch(&x, &y);
      if (pressed) {
        Serial.print(String(x) + "," + String(y) + "\n");
        saveButtonPressed = true;
      }
    } 
        
  }
  
}

void puzzlegameScreenN1(){
  //Aim trainer, press the red dots that spawn on the screen within the time limit
  //TIP: drag the finger/stylus and scrub around the dot
  tft.fillScreen(TFT_SKYBLUE);
  tft.setTextDatum(TC_DATUM);
  tft.setTextSize(2);
  tft.drawString("Press the circles",160,50,1);
  tft.drawString("you're timed",160,120,1);
  tft.drawString("if you win, you'll move on",160,190,1);
  waitForTouch();delay(1000);
  tft.fillScreen(TFT_SKYBLUE);
  tft.drawString("Ready, set...",160,100,1);
  delay(2000);  
  tft.fillScreen(TFT_SKYBLUE);
  tft.drawString("Go!",160,100,1);
  delay(1000);
  tft.setTextDatum(TL_DATUM);
  tft.fillScreen(TFT_SKYBLUE);
  tft.setTextSize(1);
  
  int timerStart = millis();
  for (int lol=0; lol<30; lol++){
    tft.fillScreen(TFT_SKYBLUE);
    int randox = random(10,309);//random(319);                                //random coordintes for the circie
    int randoy = random(10,229);//random(239);                                //allows a buffer for the edge
    tft.fillCircle(randox,randoy,5,TFT_RED);
    
    bool saveButtonPressed = false;
    while (!saveButtonPressed) {
    uint16_t x = 0, y = 0;
    bool pressed = tft.getTouch(&x, &y);
    if (millis() > timerStart+35000)
      return;
    if (pressed) {

      Serial.print(String(x) + "," + String(y) + "\n");
            
      if((x>(randox-8))&&(x<(randox+8))&&(y>(randoy-8))&&(y<(randoy+8))){     //Define dot hitbox
        saveButtonPressed = true;
      }    
    }
    }
  }
  tft.setTextSize(2);
  tft.fillScreen(TFT_SKYBLUE);
  tft.drawString("Your time: ",50,120,1);
  tft.drawString(String((millis()-timerStart)),210,120,1);
  tft.drawString("Needed time: ",50,150,1);
  tft.drawString("22000",210,150,1);
  if (millis() < timerStart+22000){                                           //Define the needed time
    delay(4000);
    puzzlegameScreenCheckpointSet(3);
    Serial.println("puzzle done");
    Serial.println(String(millis())+"\t"+timerStart);
    puzzlegameWinScreen();
  } else{
    puzzlegameScreenTodayWasTriedSet();
    tft.drawString("Better luck next time",50,50,1);
    delay(3000);
  }
 //delay(3000);
 tft.setTextSize(1);
}

void puzzlegameScreenN2(){
  //Color within the lines, it's not timed, you have to get every single pixel inside and cant have a single
  //pixel around the imidiate edge of the white square, you can color on top of the white square
  //TIP: Go slow. Hold the stylus at an angle and start from the edge   
  tft.fillScreen(TFT_SKYBLUE);
  tft.setTextDatum(TC_DATUM);
  tft.setTextSize(2);
  tft.drawString("Color within the lines",160,50,1);
  tft.drawString("One outside or left black",160,120,1);
  tft.drawString("inside and you're donezo",160,190,1);
  waitForTouch();delay(1000);
  tft.fillScreen(TFT_SKYBLUE);
  tft.drawString("ON the white line is ok",160,50,1);
  tft.drawString("Stray marks are ok",160,120,1);
  tft.drawString("Take your time",160,190,1);
  waitForTouch();delay(1000);
  tft.setTextDatum(TL_DATUM);
  tft.fillScreen(TFT_BLACK);
  tft.setTextSize(1);
  

  tft.drawRect(130,90,60,60,TFT_WHITE);                     //Tripple thick lines
  tft.drawRect(129,89,62,62,TFT_WHITE);
  tft.drawRect(128,88,64,64,TFT_WHITE);
  tft.drawRect(0,210,30,30,TFT_WHITE);
  tft.drawString("CHECK",5,220,1);
  
  bool saveButtonPressed = false;
  while (!saveButtonPressed) {
  uint16_t x = 0, y = 0;
  bool pressed = tft.getTouch(&x, &y);
  if (pressed) {

    Serial.print(String(x) + "," + String(y) + "\n");

    tft.fillRect((x-3),(y-3),6,6,TFT_PINK);                 //Draw with a rectangle
    
    if((x>0)&&(x<30)&&(y>210)&&(y<240)){                    //Check button
      int blackPixels=0;
      int coloredPixels=0;
      bool speedyCheck = false;                             //The readPixel function is really slow, I turned the SPI_READ_FREQUENCY down to 1 MHz to be safe
                                                            //Becasue it's slow I just check the rectangle and around it and not the whole screen
      for(int xxxx=127; xxxx<193; xxxx++){                  //Also there is a faster check to weed out common errors to further reduce wait time
        for(int yyyy=87; yyyy<153; yyyy+=65){               //This first one is checking to make sure that the edge is completely black, so nothing
          uint16_t color = tft.readPixel(xxxx, yyyy);       //is colored outside which is usually the mistake
          delay(4);                                         //(extra delay for the readPixel function)
          if ((color == 0xFFFF) || (color == 0xFE19)){
            coloredPixels++; break;
          }
          else
            blackPixels++;      
        }
      }
      for(int yyyyy=87; yyyyy<153; yyyyy++){
        for(int xxxxx=127; xxxxx<193; xxxxx+=65){
          uint16_t color = tft.readPixel(xxxxx, yyyyy);
          delay(4);
          if ((color == 0xFFFF) || (color == 0xFE19)){
            coloredPixels++; break;
          }
          else
            blackPixels++;      
        }
      }

      if (coloredPixels==0)
        speedyCheck = true;

      if ( (tft.readPixel(160, 120)) != (0xFE19) )          //This second check is to see if there is a black pixel at the center of the square
        speedyCheck = false;                                //This happens if the drawing is not attempted   

      
      if (speedyCheck){                                     //Finally if both of the speedy checks are passed it checks every single pixel
        tft.drawString("checking",100,20,2);
        Serial.println("speedy check all black");        
        Serial.println("blackPixels: " + String(blackPixels));
        Serial.println("coloredPixels: " + String(coloredPixels));
        blackPixels=0;
        coloredPixels=0;
        for(int xxx=127; xxx<193; xxx++){
          for(int yyy=87; yyy<153; yyy++){
            uint16_t color = tft.readPixel(xxx, yyy);
            delay(4);
            //Serial.println(color);
            if ((color == 0xFFFF) || (color == 0xFE19))
              coloredPixels++;
            else
              blackPixels++;      
          }
        }
      }

      Serial.println("blackPixels: " + String(blackPixels));
      Serial.println("coloredPixels: " + String(coloredPixels));
      if ((blackPixels==260) && (coloredPixels==4096)){     //These are the exact numbers of black pixels around the edge and colored pixels inside/on the square
        puzzlegameScreenCheckpointSet(4);
        Serial.println("puzzle done");
        puzzlegameWinScreen();
      } else{
        tft.fillScreen(TFT_SKYBLUE);
        puzzlegameScreenTodayWasTriedSet();
        Serial.println("darn");
        tft.drawString("Better luck next time",50,50,1);
        delay(3000);        
      }
        
      saveButtonPressed = true;
    }    
  }
  }

  
}

void puzzlegameScreenN3(){

  tft.fillScreen(TFT_SKYBLUE);
  tft.setTextDatum(TC_DATUM);
  tft.setTextSize(2);
  tft.drawString("Match the colors",160,50,1);
  tft.drawString("(Top and Bottom)",160,120,1);
  tft.drawString("Get them exactly!",160,190,1);
  waitForTouch();delay(1000);
  tft.fillScreen(TFT_SKYBLUE);
  tft.drawString("Use the up/down arrows",160,50,1);
  tft.drawString("There are ~100 colors",160,120,1);
  tft.drawString("There is a top and a bottom",160,190,1);
  waitForTouch();delay(1000);
  tft.setTextDatum(TL_DATUM);
  tft.fillScreen(TFT_WHITE);
  tft.setTextSize(1);
  

  uint32_t color1 = 60600;//58000
  uint32_t color1Guess = 33000;
  uint32_t color2 = 3000;//600
  uint32_t color2Guess = 36000;
  uint32_t color3 = 28200;//24200
  uint32_t color3Guess = 39000;
  tft.fillCircle(60,50,30,color1);
  tft.fillCircle(160,50,30,color2);
  tft.fillCircle(260,50,30,color3);
  tft.fillCircle(60,190,30,color1Guess);
  tft.fillCircle(160,190,30,color2Guess);
  tft.fillCircle(260,190,30,color3Guess);
  drawBmp24("/logos/uparrow.bmp",45,85);//90
  drawBmp24("/logos/downarrow.bmp",45,125);//120
  drawBmp24("/logos/uparrow.bmp",145,85);
  drawBmp24("/logos/downarrow.bmp",145,125);  
  drawBmp24("/logos/uparrow.bmp",245,85);
  drawBmp24("/logos/downarrow.bmp",245,125);  
  tft.drawString("CHECK",2,220,1);
  tft.drawRect(0,210,30,30,TFT_BLACK);
  
  bool saveButtonPressed = false;
  while (!saveButtonPressed) {
  uint16_t x = 0, y = 0;
  bool pressed = tft.getTouch(&x, &y);
  if (pressed) {

    Serial.print(String(x) + "," + String(y) + "\n");
    Serial.println("color1Guess: " + String(color1Guess));
    Serial.println("color2Guess: " + String(color2Guess));
    Serial.println("color3Guess: " + String(color3Guess));
    
    if((x>45)&&(x<75)&&(y>90)&&(y<120)){
      if (color1Guess<64800)
        color1Guess+=600;
      delay(100);
      tft.fillCircle(60,190,30,color1Guess);
    }
    if((x>45)&&(x<75)&&(y>120)&&(y<150)){
      if (color1Guess>0)
        color1Guess-=600;
      delay(100);
      tft.fillCircle(60,190,30,color1Guess);
    }
    if((x>145)&&(x<175)&&(y>90)&&(y<120)){
      if (color2Guess<64800)
        color2Guess+=600;
      delay(100);
      tft.fillCircle(160,190,30,color2Guess);
    }
    if((x>145)&&(x<175)&&(y>120)&&(y<150)){
      if (color2Guess>0)
        color2Guess-=600;
      delay(100);
      tft.fillCircle(160,190,30,color2Guess);
    }
    if((x>245)&&(x<275)&&(y>90)&&(y<120)){
      if (color3Guess<64800)
        color3Guess+=600;
      tft.fillCircle(260,190,30,color3Guess);
    }
    if((x>245)&&(x<275)&&(y>120)&&(y<150)){
      if (color3Guess>0)
        color3Guess-=600;
      delay(100);
      tft.fillCircle(260,190,30,color3Guess);
    }
    
    if((x>0)&&(x<30)&&(y>210)&&(y<240)){
      Serial.println("Checking colors");
      Serial.println("color1Guess: " + String(color1Guess));
      Serial.println("color2Guess: " + String(color2Guess));
      Serial.println("color3Guess: " + String(color3Guess));
      if ((color1Guess==color1) && (color2Guess==color2) && (color3Guess==color3)){
        puzzlegameScreenCheckpointSet(5);
        Serial.println("puzzle done");
        puzzlegameWinScreen();
      } else{
        if (color1Guess!=color1)
          tft.drawString("WRONG",50,110,1);
        if (color2Guess!=color2)
          tft.drawString("WRONG",150,110,1);
        if (color3Guess!=color3)
          tft.drawString("WRONG",250,110,1);
        puzzlegameScreenTodayWasTriedSet();
        Serial.println("darn");
        tft.drawString("Better luck next time",50,50,1);
        delay(8000);        
      }
      saveButtonPressed = true;
    }   
     
  }
  }

    
}

void puzzlegameScreenN4(){
  
  tft.fillScreen(TFT_SKYBLUE);
  tft.setTextDatum(TC_DATUM);
  tft.setTextSize(2);
  tft.drawString("Simon Says!",160,50,1);
  tft.drawString("\"Get 12 in a row\"",160,120,1);
  waitForTouch();delay(1000);
  tft.setTextDatum(TL_DATUM);
  tft.fillScreen(TFT_WHITE);
  tft.setTextSize(1);
  

  uint32_t orderKeys = random();
  int roundCounter = 0;
  int actuallyCorrect = 0;
  Serial.println(orderKeys, BIN);
    
  tft.fillRoundRect(110,70,40,40,5,TFT_GREEN);
  tft.fillRoundRect(170,70,40,40,5,TFT_RED);
  tft.fillRoundRect(110,130,40,40,5,TFT_YELLOW);
  tft.fillRoundRect(170,130,40,40,5,TFT_BLUE);
  
  
  while (roundCounter<12) {
    if (actuallyCorrect>=100){
      break;
    }
    tft.setTextColor(TFT_BLACK);
    tft.setTextSize(3);
    tft.fillRect(5,205,40,40,TFT_WHITE);
    tft.drawString(String(roundCounter),10,210,1);
    Serial.println("roundCounter: " + String(roundCounter));
    tft.setTextSize(1);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    delay(1500);
    
    for (int hh=0; hh<=roundCounter; hh++){
      uint32_t shiftedValue = orderKeys >> (hh*2);
      int extractedBits = shiftedValue & 0b11;
      Serial.println("extractedBits: " + String(extractedBits));
      if (extractedBits==0){tft.fillCircle(130,90,10,TFT_WHITE);  delay(500); tft.fillRoundRect(110,70,40,40,5,TFT_GREEN);}
      if (extractedBits==1){tft.fillCircle(190,90,10,TFT_WHITE);  delay(500); tft.fillRoundRect(170,70,40,40,5,TFT_RED);}
      if (extractedBits==2){tft.fillCircle(130,150,10,TFT_WHITE);  delay(500); tft.fillRoundRect(110,130,40,40,5,TFT_YELLOW);}
      if (extractedBits==3){tft.fillCircle(190,150,10,TFT_WHITE);  delay(500); tft.fillRoundRect(170,130,40,40,5,TFT_BLUE);}
      delay(300);
    }
    
    actuallyCorrect=0;
    for (int kk=0; kk<=roundCounter; kk++){
      Serial.println("roundCounter2: " + String(roundCounter));
      Serial.println("kk: " + String(kk));      
      uint32_t shiftedValue = orderKeys >> (kk*2);
      int extractedBits = shiftedValue & 0b11;
      
      bool saveButtonPressed = false; // Flag to indicate if save button was pressed
      while (!saveButtonPressed) {
      uint16_t x = 0, y = 0;
      bool pressed = tft.getTouch(&x, &y);
      if (pressed) {
        Serial.print(String(x) + "," + String(y) + "\n");

        if((x>110)&&(x<150)&&(y>70)&&(y<110)){
          if(extractedBits==0){tft.fillCircle(130,90,10,TFT_WHITE);  delay(550); tft.fillRoundRect(110,70,40,40,5,TFT_GREEN);   actuallyCorrect++;}
          else{actuallyCorrect=100;}
          saveButtonPressed=true;
        }
        if((x>170)&&(x<210)&&(y>70)&&(y<110)){
          if(extractedBits==1){tft.fillCircle(190,90,10,TFT_WHITE);  delay(550); tft.fillRoundRect(170,70,40,40,5,TFT_RED);     actuallyCorrect++;}
          else{actuallyCorrect=100;}
          saveButtonPressed=true;
        }
        if((x>110)&&(x<150)&&(y>130)&&(y<170)){
          if(extractedBits==2){tft.fillCircle(130,150,10,TFT_WHITE);  delay(550); tft.fillRoundRect(110,130,40,40,5,TFT_YELLOW); actuallyCorrect++;}
          else{actuallyCorrect=100;}
          saveButtonPressed=true;
        }
        if((x>170)&&(x<210)&&(y>130)&&(y<170)){
          if(extractedBits==3){tft.fillCircle(190,150,10,TFT_WHITE);  delay(550); tft.fillRoundRect(170,130,40,40,5,TFT_BLUE);   actuallyCorrect++;}
          else{actuallyCorrect=100;}
          saveButtonPressed=true;
        }
        Serial.println("actuallyCorrect: " + String(actuallyCorrect));
        
      }
      }
    }
    roundCounter++;
  
  }

  Serial.println("actuallyCorrect: " + String(actuallyCorrect));
  if (actuallyCorrect >= 100){
    puzzlegameScreenTodayWasTriedSet();
    Serial.println("darn");
    tft.drawString("Better luck next time",50,50,1);
    delay(5000);
  }
  if (actuallyCorrect == 12){
    tft.fillScreen(TFT_SKYBLUE);
    puzzlegameScreenCheckpointSet(6);
    Serial.println("puzzle done");
    tft.drawString("Nice!",50,50,1);
    //puzzlegameWinScreen();
    delay(3000);
  }
                       
}

void puzzlegameScreenN5(){

  tft.fillScreen(TFT_SKYBLUE);
  tft.setTextDatum(TC_DATUM);
  tft.setTextSize(2);
  tft.drawString("Guess the random 3 digit",160,50,1);
  tft.drawString("number in 8 tries",160,120,1);
  tft.drawString(":)",160,190,1);
  waitForTouch();delay(1000);
  tft.setTextDatum(TL_DATUM);
  tft.fillScreen(TFT_WHITE);
  tft.setTextSize(1);

  tft.setTextDatum(MC_DATUM);
  tft.setTextSize(3);
  tft.setTextColor(TFT_WHITE);
  tft.fillRoundRect(90, 30, 140, 40, 2, TFT_BLUE);  tft.drawString("0",160,50,1);
  tft.fillRoundRect(90, 80, 40, 40, 2, TFT_BLUE);   tft.drawString("1",110,100,1);
  tft.fillRoundRect(140, 80, 40, 40, 2, TFT_BLUE);  tft.drawString("2",160,100,1);
  tft.fillRoundRect(190, 80, 40, 40, 2, TFT_BLUE);  tft.drawString("3",210,100,1);
  tft.fillRoundRect(90, 130, 40, 40, 2, TFT_BLUE);  tft.drawString("4",110,150,1);
  tft.fillRoundRect(140, 130, 40, 40, 2, TFT_BLUE); tft.drawString("5",160,150,1);
  tft.fillRoundRect(190, 130, 40, 40, 2, TFT_BLUE); tft.drawString("6",210,150,1);
  tft.fillRoundRect(90, 180, 40, 40, 2, TFT_BLUE);  tft.drawString("7",110,200,1);
  tft.fillRoundRect(140, 180, 40, 40, 2, TFT_BLUE); tft.drawString("8",160,200,1);
  tft.fillRoundRect(190, 180, 40, 40, 2, TFT_BLUE); tft.drawString("9",210,200,1);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextDatum(TL_DATUM);
  tft.setTextSize(1);
  tft.drawString("CHECK",2,220,1);
  tft.drawRect(0,210,30,30,TFT_BLACK);
  tft.drawString("CLEAR",2,10,1);
  tft.drawRect(0,0,30,30,TFT_BLACK);
  tft.drawString("LIVES",2,85,1);


  //ntpUpdate();
  //int randomDigit = (epochTime / 100) % 10;
  int randomDigit = random(1000);
  Serial.println("randomDigit: " + String(randomDigit));
  
  int lives = 8;//8 guesses gives a 1/8 chance if played perfectly (9 guesses would be 1/4 or 25%)
  tft.drawString(String(lives),10,100,1); 
  int guessedNumber = 0;
  int guessPlace = 2;
  //int oldtime = millis();
  bool saveButtonPressed = false; // Flag to indicate if save button was pressed
  while (!saveButtonPressed) {
  uint16_t x = 0, y = 0;
  bool pressed = tft.getTouch(&x, &y);
  // if (millis()>oldtime+3000){
  //   ntpUpdate();
  //   randomDigit = (epochTime / 100) % 10;
  //   Serial.println(randomDigit);
  //   oldtime = millis();
  //   Serial.println("randomDigit: " + String(randomDigit));
  // }
  
  tft.setTextColor(TFT_BLACK);
  tft.setTextSize(2);
  if (pressed) {

    Serial.print(String(x) + "," + String(y) + "\n");
    Serial.println("guessedNumber: " + String(guessedNumber));
    Serial.println("guessPlace: " + String(guessPlace));
    
    if((x>90)&&(x<230)&&(y>30)&&(y<70))   {guessedNumber+=(0*(pow(10, guessPlace)));  guessPlace--;}
    
    if((x>90)&&(x<130)&&(y>80)&&(y<120))  {guessedNumber+=(1*(pow(10, guessPlace)));  guessPlace--;}
    if((x>140)&&(x<180)&&(y>80)&&(y<120)) {guessedNumber+=(2*(pow(10, guessPlace)));  guessPlace--;}
    if((x>190)&&(x<230)&&(y>80)&&(y<120)) {guessedNumber+=(3*(pow(10, guessPlace)));  guessPlace--;}
    
    if((x>90)&&(x<130)&&(y>130)&&(y<170)) {guessedNumber+=(4*(pow(10, guessPlace)));  guessPlace--;}
    if((x>140)&&(x<180)&&(y>130)&&(y<170)){guessedNumber+=(5*(pow(10, guessPlace)));  guessPlace--;}
    if((x>190)&&(x<230)&&(y>130)&&(y<170)){guessedNumber+=(6*(pow(10, guessPlace)));  guessPlace--;}
    
    if((x>90)&&(x<130)&&(y>180)&&(y<220)) {guessedNumber+=(7*(pow(10, guessPlace)));  guessPlace--;}
    if((x>140)&&(x<180)&&(y>180)&&(y<220)){guessedNumber+=(8*(pow(10, guessPlace)));  guessPlace--;}
    if((x>190)&&(x<230)&&(y>180)&&(y<220)){guessedNumber+=(9*(pow(10, guessPlace)));  guessPlace--;}
    
    Serial.println("guessedNumber: " + String(guessedNumber));
    Serial.println("guessPlace: " + String(guessPlace));
    
    if(guessedNumber>999)
      guessedNumber=0;    
    
    if((x>90)&&(x<230)&&(y>30)&&(y<220)){

      tft.fillRect(250,30,70,50,TFT_WHITE);
      tft.drawString(String(guessedNumber),250,30,1);

      if (guessPlace<0){
        guessPlace=2;
      }
      
      delay(250);
    }

    

    if((x>0)&&(x<30)&&(y>0)&&(y<30)){
      guessedNumber=0;
      guessPlace=2;
      tft.fillRect(250,30,70,50,TFT_WHITE);
      tft.drawString(String(guessedNumber),250,30,1);
    }

    tft.setTextSize(1);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);

    if((x>0)&&(x<30)&&(y>210)&&(y<240)){
      Serial.println("guessedNumber: " + String(guessedNumber));
      Serial.println("randomDigit: " + String(randomDigit));
      if (guessedNumber==randomDigit){
        tft.fillScreen(TFT_SKYBLUE);
        puzzlegameScreenCheckpointSet(8);
        Serial.println("puzzle done");
        tft.drawString("Nice!",50,50,1);
        delay(3000);
        //puzzlegameWinScreen();
        saveButtonPressed = true;
      } else{
        tft.setTextSize(2);
        tft.fillRect(240,100,70,140,TFT_WHITE);
        tft.drawString(String(guessedNumber),250,100,1);
        tft.drawString("Wrong",250,150,1);
        if (guessedNumber>randomDigit)
          tft.drawString("too High",236,200,1);
        if (guessedNumber<randomDigit)
          tft.drawString("too Low",238,200,1);
        guessedNumber=0;
        lives--;
        tft.setTextColor(TFT_BLACK);
        tft.fillRect(250,30,70,50,TFT_WHITE);
        tft.drawString(String(guessedNumber),250,30,1);
        tft.setTextSize(1);
        tft.fillRect(5,95,40,40,TFT_WHITE);
        tft.drawString(String(lives),10,100,1);
        tft.setTextColor(TFT_WHITE,TFT_BLACK);
        delay(1000);      
        if(lives<=0){
          tft.fillScreen(TFT_SKYBLUE);
          puzzlegameScreenTodayWasTriedSet();
          Serial.println("darn");
          tft.drawString("Better luck next time",50,50,1);
          tft.drawString(String(randomDigit),50,100,1);
          delay(3000);
          saveButtonPressed = true;   
        } 
      }
      
    }  

  }
  }



}

void puzzlegameWinScreen(){
  tft.setTextDatum(TC_DATUM);
  tft.setTextSize(2);

  tft.fillScreen(TFT_ORANGE);
  //delay(300);
  tft.drawString("You Won!!!",160,50,1);
  delay(3000);
  tft.fillScreen(TFT_ORANGE);
  tft.drawString("One More :)",160,50,1);
  delay(2000);
  tft.fillScreen(TFT_ORANGE);
  tft.drawString("(tap to continue)",160,50,1);
  delay(500);

  tft.setTextDatum(TL_DATUM);
  tft.setTextSize(1);

  waitForTouch();  
  
}

void puzzlegameScreenTodaysTryRefresh(){
  ntpUpdate();
  todaysTry = 0;
  
  if ((daysSinceStart==EEPROM.read(201))&&(daysSinceStart==EEPROM.read(211))&&(daysSinceStart==EEPROM.read(221)))
    todaysTry = 1;//1     change to 0 for unlimited plays
}
void puzzlegameScreenTodayWasTriedSet(){
  ntpUpdate();//this way gives three tries
  if      (daysSinceStart!=EEPROM.read(201)) {EEPROM.write(201, static_cast<const uint8_t>(daysSinceStart));}
  else if (daysSinceStart!=EEPROM.read(211)) {EEPROM.write(211, static_cast<const uint8_t>(daysSinceStart));}
  else if (daysSinceStart!=EEPROM.read(221)) {EEPROM.write(221, static_cast<const uint8_t>(daysSinceStart));}
  EEPROM.commit();
}
void puzzlegameScreenCheckpointRefresh(){
  puzzleCheckpoint = EEPROM.read(200);
}
void puzzlegameScreenCheckpointSet(int num){
  EEPROM.write(200, static_cast<const uint8_t>(num));
  EEPROM.commit();
  puzzlegameScreenCheckpointRefresh();
}


/***************************************************************************************************
H  I  D  D  E  N      S  C  R  E  E  N  S
***************************************************************************************************/
void utilityScreen(){
  tft.fillScreen(TFT_SILVER);
  drawBmp24("/logos/homelogo.bmp",0,170);
  
  tft.fillRoundRect(30,20, 60, 60, 3, TFT_WHITE);
  tft.drawString("BREAK",35, 25, 2);
  tft.drawString("WIFI",35, 45, 2);
  tft.drawString("!!!",35, 65, 2); 
  tft.fillRoundRect(130,20, 60, 60, 3, TFT_WHITE);
  tft.drawString("DELETE",135, 25, 2);
  tft.drawString("FILES",135, 45, 2);
  tft.drawString("!!!!!!",135, 65, 2);
  tft.fillRoundRect(230,20, 60, 60, 3, TFT_WHITE);
  tft.drawString("DELETE",235, 25, 2);
  tft.drawString("ALL VARS",235, 45, 2);
  tft.drawString("!!!!!!",235, 65, 2);
  tft.fillRoundRect(30,100, 60, 60, 3, TFT_WHITE);
  tft.drawString("RECALIBRATE",35, 105, 2);
  tft.drawString("TOUCH",35, 125, 2);
  tft.drawString("!!!!",35, 145, 2); 
  tft.fillRoundRect(130,100, 60, 60, 3, TFT_WHITE);
  tft.drawString("EDIT",135, 105, 2);
  tft.drawString("FILES",135, 125, 2);
  tft.drawString("!!!!!!",135, 145, 2);
  tft.fillRoundRect(230,100, 60, 60, 3, TFT_WHITE);
  tft.drawString("REFLASH",235, 105, 2);
  tft.drawString("CODE",235, 125, 2);
  tft.drawString("!!!!!!!!!!!!!!!",235, 145, 2);
  
  tft.drawString("NTP",40, 170, 2);
  tft.drawString("WIFI",90, 170, 2);
  tft.drawString("SD",140, 170, 2);
  tft.drawString("CLOUD",190, 170, 2);
  tft.drawString("LOVE",240, 170, 2);
  
  ntpUpdate();
  if (epochTime>1687392472)
    tft.fillRoundRect(40,190, 40, 40, 3, TFT_GREEN);
  else
    tft.fillRoundRect(40,190, 40, 40, 3, TFT_RED); 
     
  if(WiFi.status() == WL_CONNECTED)
    tft.fillRoundRect(90,190, 40, 40, 3, TFT_GREEN);
  else
    tft.fillRoundRect(90,190, 40, 40, 3, TFT_RED);    
  
  if (SD.begin(SD_CS,spiSD))
    tft.fillRoundRect(140,190, 40, 40, 3, TFT_GREEN);
  else
    tft.fillRoundRect(140,190, 40, 40, 3, TFT_RED);
  
  tft.fillRoundRect(240,190, 40, 40, 3, TFT_GREEN);  
  
  if (isCloudWorking())
    tft.fillRoundRect(190,190, 40, 40, 3, TFT_GREEN);
  else
    tft.fillRoundRect(190,190, 40, 40, 3, TFT_RED);
  
  bool saveButtonPressed = false; // Flag to indicate if save button was pressed
  while (!saveButtonPressed) {
    
  uint16_t x = 0, y = 0;
  bool pressed = tft.getTouch(&x, &y);
  if (pressed) {
    Serial.print(String(x) + "," + String(y) + "\n");
    
    if((x>0)&&(x<30)&&(y>170)&&(y<200)){
      saveButtonPressed = true;
      //homeScreen();        
    }
    
    if((x>30)&&(x<90)&&(y>20)&&(y<80)){
      SetBreakWifi("","");
      saveButtonPressed = true;
    }

    if((x>130)&&(x<190)&&(y>20)&&(y<80)){
      deleteFilesInFolder("/mydrawings","/recieveddrawings");
      saveButtonPressed = true;      
    }

    if((x>230)&&(x<290)&&(y>20)&&(y<80)){
      clearEepromVars();
      saveButtonPressed = true;
    } 

    if((x>30)&&(x<90)&&(y>100)&&(y<160)){
      touch_calibrate();
      saveButtonPressed = true;
    }

    if((x>130)&&(x<190)&&(y>100)&&(y<160)){
      remoteSDFilesHandler();
      saveButtonPressed = true;      
    }

    if((x>230)&&(x<290)&&(y>100)&&(y<160)){
      remoteCodeUploadInstructions();
      remoteCodeUpload();
      saveButtonPressed = true;
    } 


  }
  } 
  
}


/***************************************************************************************************
S  E  T  T  I  N  G  S      S  C  R  E  E  N
***************************************************************************************************/
void settingsScreen(){
  tft.fillScreen(TFT_WHITE);
  drawBmp24("/logos/homelogo.bmp",0,170);
  puzzlegameScreenCheckpointRefresh();
  getSetBrightness(-1);

  tft.fillRoundRect(30,50, 60, 60, 3, TFT_SILVER);
  tft.drawString("RESTART",35, 55, 2);
  //tft.drawString("BREAK",35, 55, 2);
  //tft.drawString("WIFI",35, 75, 2);
  //tft.drawString("!!!!!",35, 95, 2); 
  tft.fillRoundRect(130,50, 60, 60, 3, TFT_SILVER);
  if (puzzleCheckpoint==0){
    tft.drawString("SOLVE A",135, 55, 2);
    tft.drawString("PUZZLE?",135, 75, 2);    
  }
  if (puzzleCheckpoint>0){
    tft.drawString("RESET",135, 55, 2);
    tft.drawString("PUZZLE",135, 75, 2);
    tft.drawString("PROGRESS",135, 95, 2);   
  }
  tft.fillRoundRect(230,50, 60, 60, 3, TFT_SILVER);
  tft.drawString("TOGGLE",235, 55, 2);
  tft.drawString("LEDS TO",235, 75, 2);
  if (toggleLEDs==0)
    tft.drawString("OFF",235, 95, 2);
  if (toggleLEDs==1)
    tft.drawString("PARTIAL",235, 95, 2);
  if (toggleLEDs==2)
    tft.drawString("ON",235, 95, 2);
  drawBmp24("/logos/downarrow.bmp",50,200);//50,210
  drawBmp24("/logos/uparrow.bmp",50,170);//80,210
  tft.drawString("BRIGHTNESS",90, 190, 2);
  tft.drawString(String(screenBrightness),90, 210, 2);

  tft.fillRoundRect(230,120, 60, 60, 3, TFT_SILVER);
  tft.drawString("TOGGLE CLOUD",235, 125, 2);
  tft.drawString("ON STARTUP TO",235, 145, 2);
  if (getCloudOnStartup)
    tft.drawString("OFF",235, 165, 2);
  if (!getCloudOnStartup)
    tft.drawString("ON",235, 165, 2);
  
  bool saveButtonPressed = false; // Flag to indicate if save button was pressed
  while (!saveButtonPressed) { 
  uint16_t x = 0, y = 0;
  bool pressed = tft.getTouch(&x, &y);
  if (pressed) {
    Serial.print(String(x) + "," + String(y) + "\n");
    
    if((x>0)&&(x<30)&&(y>170)&&(y<200)){
      saveButtonPressed = true;
      //homeScreen();        
    }
    
    if((x>30)&&(x<90)&&(y>50)&&(y<110)){
      tft.fillScreen(TFT_WHITE);
      ESP.restart();
    }

    if((x>50)&&(x<80)&&(y>200)&&(y<230)){
      if (screenBrightness>=15){
        getSetBrightness((screenBrightness-5));
        tft.fillRect(90,210,50,20,TFT_WHITE);
        tft.drawString(String(screenBrightness),90, 210, 2);
        Serial.println(String(screenBrightness));
        delay(150);     
      }
    }
    
    if((x>50)&&(x<80)&&(y>170)&&(y<200)){
      if (screenBrightness<=250){
        getSetBrightness((screenBrightness+5));
        tft.fillRect(90,210,50,20,TFT_WHITE);
        tft.drawString(String(screenBrightness),90, 210, 2);
        Serial.println(String(screenBrightness));
        delay(150);       
      }
    }

    if(((x>130)&&(x<190)&&(y>50)&&(y<110)) && (puzzleCheckpoint==0)){//135 55
      tft.fillRoundRect(130,50, 60, 60, 3, TFT_SILVER);
      tft.drawRect(135,55,50,37,TFT_BLACK);
      tft.drawLine(135,67,185,67,TFT_BLACK);
      tft.drawLine(135,79,185,79,TFT_BLACK);
      tft.drawLine(152,55,152,92,TFT_BLACK);
      tft.drawLine(169,55,169,92,TFT_BLACK);
      tft.setTextColor(TFT_BLACK); 
      tft.drawString("0  1 2",138, 55, 2);
      tft.drawString("3  4 5",138, 67, 2);
      tft.drawString("6  7 8",138, 79, 2);
      tft.drawString("_/_/__",135, 93, 2);
      // delay(5000);
      tft.setTextColor(TFT_WHITE, TFT_BLACK);
      // tft.fillRoundRect(130,50, 60, 60, 3, TFT_WHITE);
      // tft.drawString("SOLVE A",135, 55, 2);
      // tft.drawString("PUZZLE?",135, 75, 2);
    }
    if(((x>130)&&(x<190)&&(y>50)&&(y<110)) && (puzzleCheckpoint>0)){//135 55
      puzzlegameScreenCheckpointSet(0);
      EEPROM.write(201, 0);EEPROM.commit(); //give yourself another try
    }
    
    if((x>230)&&(x<290)&&(y>50)&&(y<110)){
      if (toggleLEDs<2)
        toggleLEDs++;
      else
        toggleLEDs=0;  
      getSetLEDStatus(toggleLEDs,1);
      tft.fillRoundRect(230,50, 60, 60, 3, TFT_SILVER);
      tft.drawString("TOGGLE",235, 55, 2);
      tft.drawString("LEDS TO",235, 75, 2);
      if (toggleLEDs==0)
        tft.drawString("OFF",235, 95, 2);
      if (toggleLEDs==1)
        tft.drawString("PARTIAL",235, 95, 2);
      if (toggleLEDs==2)
        tft.drawString("ON",235, 95, 2);
      delay(500);
    }
    
    if((x>230)&&(x<290)&&(y>120)&&(y<180)){
      getCloudOnStartup = !getCloudOnStartup;
      getCloudOnStartupToggle(static_cast<int>(getCloudOnStartup), true);
      tft.fillRoundRect(230,120, 60, 60, 3, TFT_SILVER);
      tft.drawString("TOGGLE CLOUD",235, 125, 2);
      tft.drawString("ON STARTUP TO",235, 145, 2);
      if (getCloudOnStartup)
        tft.drawString("OFF",235, 165, 2);
      if (!getCloudOnStartup)
        tft.drawString("ON",235, 165, 2);
      delay(500);
    }

      
  }
  } 

  
}

/***************************************************************************************************
A  R  T      S  C  R  E  E  N
***************************************************************************************************/
void artScreen(){

  ntpUpdate();
  
  tft.fillRectHGradient(0, 0, 320, 240, 0xD5BF, 0x2572);

  String mrFile = "/art/" + String(daysSinceStart) + ".bmp";
  drawBmp24(mrFile.c_str(),0,0);
  delay(250);
    
  bool saveButtonPressed = false; // Flag to indicate if save button was pressed
  while (!saveButtonPressed) {

  
    
  uint16_t x = 0, y = 0;
  bool pressed = tft.getTouch(&x, &y);
  if (pressed) {
    Serial.print(String(x) + "," + String(y) + "\n");
    saveButtonPressed = true;
  }

  } 
   
}


/***************************************************************************************************
S  E  N  D      E  M  O  J  I      S  C  R  E  E  N
***************************************************************************************************/
void sendMessageString(){
  tft.fillScreen(TFT_WHITE);
  drawBmp24("/logos/homelogo.bmp",0,170);
  tft.drawRect(0,210,30,30,TFT_BLACK);
  tft.drawString("SEND",0, 217, 2);
  tft.drawString("Line 1:",40, 30, 2);
  tft.drawString("Line 2:",40, 100, 2);
  tft.drawString("Line 3:",40, 170, 2);
  tft.fillRoundRect(40,60,240,30,4,TFT_YELLOW);
  tft.fillRoundRect(40,130,240,30,4,TFT_YELLOW);
  tft.fillRoundRect(40,200,240,30,4,TFT_YELLOW);
  tft.drawString("(touch to add)",45, 65, 2);
  tft.drawString("(touch to add)",45, 135, 2);
  tft.drawString("(touch to add)",45, 205, 2);
  delay(400);

  String messageLine1 = "";
  String messageLine2 = "";
  String messageLine3 = "";
  bool saveButtonPressed = false; // Flag to indicate if save button was pressed
  while (!saveButtonPressed) {

  uint16_t x = 0, y = 0;
  bool pressed = tft.getTouch(&x, &y);
  if (pressed) {
    //Serial.print(String(x) + "," + String(y) + "\n");
    
    if((x>0)&&(x<30)&&(y>170)&&(y<200)){
      saveButtonPressed = true;
      //homeScreen();        
    }

    if((x>0)&&(x<30)&&(y>210)&&(y<240)){
      Serial.println("save button pushed");
      tft.fillRect(0,210,33,30,TFT_WHITE);
      drawBmp24("/logos/refreshlogoloading.bmp",0,210);
      messageTextString = messageLine1 + "'" + messageLine2 + "'" + messageLine3;
      postCloudMessage(messageTextString);
      saveButtonPressed = true;
    }

    if((x>40)&&(x<280)&&(y>60)&&(y<90)){
      if(!handleKeyboard())
        return;
      messageLine1 = keyboardString;
      tft.fillScreen(TFT_WHITE);
      drawBmp24("/logos/homelogo.bmp",0,170);
      tft.drawRect(0,210,30,30,TFT_BLACK);
      tft.drawString("SEND",0, 217, 2);
      tft.drawString("Line 1:",40, 30, 2);
      tft.drawString("Line 2:",40, 100, 2);
      tft.drawString("Line 3:",40, 170, 2);
      tft.fillRoundRect(40,60,240,30,4,TFT_YELLOW);
      tft.fillRoundRect(40,130,240,30,4,TFT_YELLOW);
      tft.fillRoundRect(40,200,240,30,4,TFT_YELLOW);
      tft.drawString(messageLine1,45, 65, 2);
      tft.drawString(messageLine2,45, 135, 2);
      tft.drawString(messageLine3,45, 205, 2);
      delay(200);
    }

    if((x>40)&&(x<280)&&(y>130)&&(y<160)){
      if(!handleKeyboard())
        return;
      messageLine2 = keyboardString;
      tft.fillScreen(TFT_WHITE);
      drawBmp24("/logos/homelogo.bmp",0,170);
      tft.drawRect(0,210,30,30,TFT_BLACK);
      tft.drawString("SEND",0, 217, 2);
      tft.drawString("Line 1:",40, 30, 2);
      tft.drawString("Line 2:",40, 100, 2);
      tft.drawString("Line 3:",40, 170, 2);
      tft.fillRoundRect(40,60,240,30,4,TFT_YELLOW);
      tft.fillRoundRect(40,130,240,30,4,TFT_YELLOW);
      tft.fillRoundRect(40,200,240,30,4,TFT_YELLOW);
      tft.drawString(messageLine1,45, 65, 2);
      tft.drawString(messageLine2,45, 135, 2);
      tft.drawString(messageLine3,45, 205, 2);
      delay(200);
    }

    if((x>40)&&(x<280)&&(y>200)&&(y<230)){
      if(!handleKeyboard())
        return;
      messageLine3 = keyboardString;
      tft.fillScreen(TFT_WHITE);
      drawBmp24("/logos/homelogo.bmp",0,170);
      tft.drawRect(0,210,30,30,TFT_BLACK);
      tft.drawString("SEND",0, 217, 2);
      tft.drawString("Line 1:",40, 30, 2);
      tft.drawString("Line 2:",40, 100, 2);
      tft.drawString("Line 3:",40, 170, 2);
      tft.fillRoundRect(40,60,240,30,4,TFT_YELLOW);
      tft.fillRoundRect(40,130,240,30,4,TFT_YELLOW);
      tft.fillRoundRect(40,200,240,30,4,TFT_YELLOW);
      tft.drawString(messageLine1,45, 65, 2);
      tft.drawString(messageLine2,45, 135, 2);
      tft.drawString(messageLine3,45, 205, 2);
      delay(200);
    }




  }

  }   


}


/***************************************************************************************************
S  E  N  D      E  M  O  J  I      S  C  R  E  E  N
***************************************************************************************************/
void emojiScreen(){
  tft.fillScreen(TFT_WHITE);

  drawBmp24("/logos/homelogo.bmp",0,170);
  tft.drawRect(0,210,30,30,TFT_BLACK);
  tft.drawString("SEND",0, 217, 2);
  tft.drawRect(290,210,30,30,TFT_BLACK);
  tft.drawString("CLEAR",286, 217, 2);

  drawBmp24("/logos/uparrow.bmp",290,90);
  drawBmp24("/logos/downarrow.bmp",290,120);

  // drawBmp24("/logos/emoji1.bmp",60,55);
  // drawBmp24("/logos/emoji2.bmp",130,55);
  // drawBmp24("/logos/emoji3.bmp",200,55);
  // drawBmp24("/logos/emoji4.bmp",60,125);
  // drawBmp24("/logos/emoji5.bmp",130,125);
  // drawBmp24("/logos/emoji6.bmp",200,125);


  int currentEmoji = 0;
  String currentEmojiString = "";
  int currentPage = 0;
  refreshEmojis(currentPage,currentEmojiString);
  
  bool saveButtonPressed = false; // Flag to indicate if save button was pressed
  while (!saveButtonPressed) {
  
  uint16_t x = 0, y = 0;
  bool pressed = tft.getTouch(&x, &y);
  
  if (pressed) {
    Serial.print(String(x) + "," + String(y) + "\n");
    
    if((x>60)&&(x<120)&&(y>55)&&(y<115)){
      // emojiScreenRefresh();
      // tft.drawCircle(90, 85, 34, TFT_BLACK);
      // tft.drawCircle(90, 85, 32, TFT_BLACK);
      // tft.drawCircle(90, 85, 33, TFT_BLACK);
      currentEmoji = 0 + (6*currentPage);
      if (currentEmoji<10)
        currentEmojiString = currentEmojiString + "0" + String(currentEmoji);
      else
        currentEmojiString = currentEmojiString + String(currentEmoji);
      refreshEmojis(currentPage,currentEmojiString);
    }
    if((x>130)&&(x<190)&&(y>55)&&(y<115)){
      // emojiScreenRefresh();
      // tft.drawCircle(160, 85, 34, TFT_BLACK);
      // tft.drawCircle(160, 85, 32, TFT_BLACK);
      // tft.drawCircle(160, 85, 33, TFT_BLACK);
      currentEmoji = 1 + (6*currentPage);
      if (currentEmoji<10)
        currentEmojiString = currentEmojiString + "0" + String(currentEmoji);
      else
        currentEmojiString = currentEmojiString + String(currentEmoji);
      refreshEmojis(currentPage,currentEmojiString);
    }
    if((x>200)&&(x<260)&&(y>55)&&(y<115)){
      // emojiScreenRefresh();
      // tft.drawCircle(230, 85, 34, TFT_BLACK);
      // tft.drawCircle(230, 85, 32, TFT_BLACK);
      // tft.drawCircle(230, 85, 33, TFT_BLACK);
      currentEmoji = 2 + (6*currentPage);
      if (currentEmoji<10)
        currentEmojiString = currentEmojiString + "0" + String(currentEmoji);
      else
        currentEmojiString = currentEmojiString + String(currentEmoji);
      refreshEmojis(currentPage,currentEmojiString);
    }
    if((x>60)&&(x<120)&&(y>125)&&(y<185)){
      // emojiScreenRefresh();
      // tft.drawCircle(90, 155, 34, TFT_BLACK);
      // tft.drawCircle(90, 155, 32, TFT_BLACK);
      // tft.drawCircle(90, 155, 33, TFT_BLACK);
      currentEmoji = 3 + (6*currentPage);
      if (currentEmoji<10)
        currentEmojiString = currentEmojiString + "0" + String(currentEmoji);
      else
        currentEmojiString = currentEmojiString + String(currentEmoji);
      refreshEmojis(currentPage,currentEmojiString);
    }
    if((x>130)&&(x<190)&&(y>125)&&(y<185)){
      // emojiScreenRefresh();
      // tft.drawCircle(160, 155, 34, TFT_BLACK);
      // tft.drawCircle(160, 155, 32, TFT_BLACK);
      // tft.drawCircle(160, 155, 33, TFT_BLACK);
      currentEmoji = 4 + (6*currentPage);
      if (currentEmoji<10)
        currentEmojiString = currentEmojiString + "0" + String(currentEmoji);
      else
        currentEmojiString = currentEmojiString + String(currentEmoji);
      refreshEmojis(currentPage,currentEmojiString);
    }
    if((x>200)&&(x<260)&&(y>125)&&(y<185)){
      // emojiScreenRefresh();
      // tft.drawCircle(230, 155, 34, TFT_BLACK);
      // tft.drawCircle(230, 155, 32, TFT_BLACK);
      // tft.drawCircle(230, 155, 33, TFT_BLACK);
      currentEmoji = 5 + (6*currentPage);
      if (currentEmoji<10)
        currentEmojiString = currentEmojiString + "0" + String(currentEmoji);
      else
        currentEmojiString = currentEmojiString + String(currentEmoji);
      refreshEmojis(currentPage,currentEmojiString);
    }

    if((x>290)&&(x<320)&&(y>90)&&(y<120)){
      Serial.println(currentPage);
      if (currentPage>0){
        currentPage--;
        refreshEmojis(currentPage,currentEmojiString);
        delay(150); 
      }
    }
    if((x>290)&&(x<320)&&(y>120)&&(y<150)){
      Serial.println(currentPage);
      if (currentPage<3){
        currentPage++;
        refreshEmojis(currentPage,currentEmojiString);
        delay(150); 
      }
    }
    if((x>290)&&(x<320)&&(y>210)&&(y<240)){
      // if (currentEmojiString.length()<5){
      //   currentEmojiString +=
      // }
      currentEmojiString = "";
      refreshEmojis(currentPage,currentEmojiString);
    }

    if((x>0)&&(x<30)&&(y>170)&&(y<200)){
      saveButtonPressed = true;
      //homeScreen();        
    }
    if((x>0)&&(x<30)&&(y>210)&&(y<240))
    {
      if(currentEmojiString.length() != 0){
        Serial.println("save button pushed");
        tft.fillRect(0,210,33,30,TFT_WHITE);
        drawBmp24("/logos/refreshlogoloading.bmp",0,210);
        //currentEmojiString = "z" + currentEmojiString;
        currentEmojiString = currentEmojiString;
        postCloudEmoji(currentEmojiString);
        Serial.println(currentEmojiString);
        saveButtonPressed = true;
      }
    }

  }

  }   

}

void refreshEmojis(int pageNumber, String emojiString){

  tft.fillRect(40,30,240,170,TFT_WHITE);

  int emojiPositionArray[6][2] = {
  {60, 55},
  {130, 55},
  {200, 55},
  {60, 125},
  {130, 125},
  {200, 125}
  };

  for (int ut = 0; ut < 6; ut++){
    int displayEmojiNumber = ut + (6*pageNumber);
    String displayEmojiString = "";
    if (displayEmojiNumber<10)
      displayEmojiString = "0" + String(displayEmojiNumber);
    else
      displayEmojiString = String(displayEmojiNumber);      
    String displayEmojiName = "/logos/emoji"+displayEmojiString+".bmp";
    drawBmp24(displayEmojiName.c_str(),emojiPositionArray[ut][0],emojiPositionArray[ut][1]);


    if ((displayEmojiNumber==(emojiString.substring(0,2)).toInt()) && (emojiString.length() > 1)){
      tft.drawString("1",(emojiPositionArray[ut][0])-5+30, (emojiPositionArray[ut][1])-15, 2);
      Serial.println("emoji #1: " + String(displayEmojiNumber));
    }
    if ((displayEmojiNumber==(emojiString.substring(2,4)).toInt()) && (emojiString.length() > 3)){
      tft.drawString("2",(emojiPositionArray[ut][0])+30, (emojiPositionArray[ut][1])-15, 2);
      Serial.println("emoji #2: " + String(displayEmojiNumber));
    }
    if ((displayEmojiNumber==(emojiString.substring(4,6)).toInt()) && (emojiString.length() > 5)){
      tft.drawString("3",(emojiPositionArray[ut][0])+5+30, (emojiPositionArray[ut][1])-15, 2);
      Serial.println("emoji #3: " + String(displayEmojiNumber));
    }
  }
  
  // drawBmp24("/logos/emoji1.bmp",60,55);
  // drawBmp24("/logos/emoji2.bmp",130,55);
  // drawBmp24("/logos/emoji3.bmp",200,55);
  // drawBmp24("/logos/emoji4.bmp",60,125);
  // drawBmp24("/logos/emoji5.bmp",130,125);
  // drawBmp24("/logos/emoji6.bmp",200,125);
  
}

void emojiScreenRefresh(){
  
  tft.drawCircle(90, 85, 34, TFT_WHITE);
  tft.drawCircle(90, 85, 32, TFT_WHITE);
  tft.drawCircle(90, 85, 33, TFT_WHITE);
  tft.drawCircle(160, 85, 34, TFT_WHITE);
  tft.drawCircle(160, 85, 32, TFT_WHITE);
  tft.drawCircle(160, 85, 33, TFT_WHITE);
  tft.drawCircle(230, 85, 34, TFT_WHITE);
  tft.drawCircle(230, 85, 32, TFT_WHITE);
  tft.drawCircle(230, 85, 33, TFT_WHITE);
  tft.drawCircle(90, 155, 34, TFT_WHITE);
  tft.drawCircle(90, 155, 32, TFT_WHITE);
  tft.drawCircle(90, 155, 33, TFT_WHITE);
  tft.drawCircle(160, 155, 34, TFT_WHITE);
  tft.drawCircle(160, 155, 32, TFT_WHITE);
  tft.drawCircle(160, 155, 33, TFT_WHITE);
  tft.drawCircle(230, 155, 34, TFT_WHITE);
  tft.drawCircle(230, 155, 32, TFT_WHITE);
  tft.drawCircle(230, 155, 33, TFT_WHITE);
  delay(100);
  
}


/***************************************************************************************************
F  I  L  E      M  A  N  A  G  E  R     S  C  R  E  E  N
***************************************************************************************************/
bool filesScreen(){
  
  tft.fillScreen(TFT_WHITE);
  drawBmp24("/logos/homelogo.bmp",0,170);
  drawBmp24("/logos/uparrow.bmp",0,10);
  drawBmp24("/logos/downarrow.bmp",0,45);
  tft.drawRect(0,210,30,30,TFT_BLACK);
  tft.drawString("OPEN",0, 217, 2);
  tft.drawString("Recieved",50, 5, 2);
  tft.drawString("Sent",150, 5, 2);
  tft.drawString("Art",250, 5, 2);
  tft.fillRoundRect(0,100,30,30,4,TFT_ORANGE);
  tft.drawString("LOG",2, 107, 2);

  fileScreenGetFileNames();
  //printMemoryStats();
  fileScreenRenderFilenames(fileScrollNumber);

  String currentFile = "";
  int currentFileX = 0;
  int currentFileY = 0;
  bool saveButtonPressed = false; // Flag to indicate if save button was pressed
  while (!saveButtonPressed) {
  
  uint16_t x = 0, y = 0;
  bool pressed = tft.getTouch(&x, &y);
  
  if (pressed) {
    Serial.print(String(x) + "," + String(y) + "\n");
    
    if((x>0)&&(x<30)&&(y>0)&&(y<40)){
      if (fileScrollNumber>0){
        fileScrollNumber-=2;
        fileScreenRenderFilenames(fileScrollNumber);
      }
    }
    if((x>0)&&(x<30)&&(y>50)&&(y<90)){
      if (fileScrollNumber<190){
        fileScrollNumber+=2;
        fileScreenRenderFilenames(fileScrollNumber);        
      }
    }
    
    if((x>0)&&(x<30)&&(y>210)&&(y<240)){
      if (currentFileX<2){
        zeroImageArray();
        Serial.println(currentFile);
        readSD2bitImage(currentFile.c_str());
        display2bitImage();
      }
      else if (currentFileX==2){
        Serial.println(currentFile);
        drawBmp24(currentFile.c_str(),0,0);
      }
      delay(200);
      bool stayOnScreen = false;
      while (!stayOnScreen){
        uint16_t x2 = 0, y2 = 0;
        bool pressed2 = tft.getTouch(&x2, &y2);
        if (pressed2){
          stayOnScreen = true;
          saveButtonPressed = true;
          fileScreenClearFilenames();
          return true;
          //filesScreen();//this would be nice, but if we stay in the loop too long (recursively) it causes a stack overflow
        }
      }     
    }

    if((x>0)&&(x<30)&&(y>170)&&(y<200)){
      saveButtonPressed = true;
      return false;
      //homeScreen();        
    }

    if((x>0)&&(x<30)&&(y>100)&&(y<130)){
      fileScreenLogs();
      saveButtonPressed = true;
      return false;
      //homeScreen();        
    }

    if((x>50)&&(x<140)&&(y>30)&&(y<60)){
      currentFileX=0;
      currentFileY=0+fileScrollNumber;
      fileScreenRenderBoxes(currentFileX,currentFileY);
      currentFile="/recieveddrawings/"+recieveddrawingsFilenames[currentFileY]+".bmp";
    }
    if((x>50)&&(x<140)&&(y>60)&&(y<90)){
      currentFileX=0;
      currentFileY=1+fileScrollNumber;
      fileScreenRenderBoxes(currentFileX,currentFileY);
      currentFile="/recieveddrawings/"+recieveddrawingsFilenames[currentFileY]+".bmp";
    }
    if((x>50)&&(x<140)&&(y>90)&&(y<120)){
      currentFileX=0;
      currentFileY=2+fileScrollNumber;
      fileScreenRenderBoxes(currentFileX,currentFileY);
      currentFile="/recieveddrawings/"+recieveddrawingsFilenames[currentFileY]+".bmp";
    }
    if((x>50)&&(x<140)&&(y>120)&&(y<150)){
      currentFileX=0;
      currentFileY=3+fileScrollNumber;
      fileScreenRenderBoxes(currentFileX,currentFileY);
      currentFile="/recieveddrawings/"+recieveddrawingsFilenames[currentFileY]+".bmp";
    }
    if((x>50)&&(x<140)&&(y>150)&&(y<180)){
      currentFileX=0;
      currentFileY=4+fileScrollNumber;
      fileScreenRenderBoxes(currentFileX,currentFileY);
      currentFile="/recieveddrawings/"+recieveddrawingsFilenames[currentFileY]+".bmp";
    }
    if((x>50)&&(x<140)&&(y>180)&&(y<210)){
      currentFileX=0;
      currentFileY=5+fileScrollNumber;
      fileScreenRenderBoxes(currentFileX,currentFileY);
      currentFile="/recieveddrawings/"+recieveddrawingsFilenames[currentFileY]+".bmp";
    }
    if((x>50)&&(x<140)&&(y>210)&&(y<240)){
      currentFileX=0;
      currentFileY=6+fileScrollNumber;
      fileScreenRenderBoxes(currentFileX,currentFileY);
      currentFile="/recieveddrawings/"+recieveddrawingsFilenames[currentFileY]+".bmp";
    }

    if((x>150)&&(x<240)&&(y>30)&&(y<60)){
      currentFileX=1;
      currentFileY=0+fileScrollNumber;
      fileScreenRenderBoxes(currentFileX,currentFileY);
      currentFile="/mydrawings/"+mydrawingsFilenames[currentFileY]+".bmp";
    }
    if((x>150)&&(x<240)&&(y>60)&&(y<90)){
      currentFileX=1;
      currentFileY=1+fileScrollNumber;
      fileScreenRenderBoxes(currentFileX,currentFileY);
      currentFile="/mydrawings/"+mydrawingsFilenames[currentFileY]+".bmp";
    }
    if((x>150)&&(x<240)&&(y>90)&&(y<120)){
      currentFileX=1;
      currentFileY=2+fileScrollNumber;
      fileScreenRenderBoxes(currentFileX,currentFileY);
      currentFile="/mydrawings/"+mydrawingsFilenames[currentFileY]+".bmp";
    }
    if((x>150)&&(x<240)&&(y>120)&&(y<150)){
      currentFileX=1;
      currentFileY=3+fileScrollNumber;
      fileScreenRenderBoxes(currentFileX,currentFileY);
      currentFile="/mydrawings/"+mydrawingsFilenames[currentFileY]+".bmp";
    }
    if((x>150)&&(x<240)&&(y>150)&&(y<180)){
      currentFileX=1;
      currentFileY=4+fileScrollNumber;
      fileScreenRenderBoxes(currentFileX,currentFileY);
      currentFile="/mydrawings/"+mydrawingsFilenames[currentFileY]+".bmp";
    }
    if((x>150)&&(x<240)&&(y>180)&&(y<210)){
      currentFileX=1;
      currentFileY=5+fileScrollNumber;
      fileScreenRenderBoxes(currentFileX,currentFileY);
      currentFile="/mydrawings/"+mydrawingsFilenames[currentFileY]+".bmp";
    }
    if((x>150)&&(x<240)&&(y>210)&&(y<240)){
      currentFileX=1;
      currentFileY=6+fileScrollNumber;
      fileScreenRenderBoxes(currentFileX,currentFileY);
      currentFile="/mydrawings/"+mydrawingsFilenames[currentFileY]+".bmp";
    }

    if((x>250)&&(x<310)&&(y>30)&&(y<60)){
      currentFileX=2;
      currentFileY=0+fileScrollNumber;
      fileScreenRenderBoxes(currentFileX,currentFileY);
      currentFile="/art/"+artFilenames[currentFileY]+".bmp";
    }
    if((x>250)&&(x<310)&&(y>60)&&(y<90)){
      currentFileX=2;
      currentFileY=1+fileScrollNumber;
      fileScreenRenderBoxes(currentFileX,currentFileY);
      currentFile="/art/"+artFilenames[currentFileY]+".bmp";
    }
    if((x>250)&&(x<310)&&(y>90)&&(y<120)){
      currentFileX=2;
      currentFileY=2+fileScrollNumber;
      fileScreenRenderBoxes(currentFileX,currentFileY);
      currentFile="/art/"+artFilenames[currentFileY]+".bmp";
    }
    if((x>250)&&(x<310)&&(y>120)&&(y<150)){
      currentFileX=2;
      currentFileY=3+fileScrollNumber;
      fileScreenRenderBoxes(currentFileX,currentFileY);
      currentFile="/art/"+artFilenames[currentFileY]+".bmp";
    }
    if((x>250)&&(x<310)&&(y>150)&&(y<180)){
      currentFileX=2;
      currentFileY=4+fileScrollNumber;
      fileScreenRenderBoxes(currentFileX,currentFileY);
      currentFile="/art/"+artFilenames[currentFileY]+".bmp";
    }
    if((x>250)&&(x<310)&&(y>180)&&(y<210)){
      currentFileX=2;
      currentFileY=5+fileScrollNumber;
      fileScreenRenderBoxes(currentFileX,currentFileY);
      currentFile="/art/"+artFilenames[currentFileY]+".bmp";
    }
    if((x>250)&&(x<310)&&(y>210)&&(y<240)){
      currentFileX=2;
      currentFileY=6+fileScrollNumber;
      fileScreenRenderBoxes(currentFileX,currentFileY);
      currentFile="/art/"+artFilenames[currentFileY]+".bmp";
    }



  }

  }   
  

  
}

void fileScreenGetFileNames(){
  
  ntpUpdate();
  

  int place = 0;
  for (int rr=daysSinceStart; rr>=0; rr--){
    if ((rr<=daysSinceStart)&&(place<199)){
      artFilenames[place]=String(rr);
      place++;
    }   
  }
  
  if (!SD.begin(SD_CS, spiSD)) {
    Serial.println("initialization failed!");
    tft.drawString("no SD",0, 0, 2);
    return;
  }
  Serial.println("initialization done.");

  File root = SD.open("/recieveddrawings");
  if (!root.isDirectory()) {
    Serial.println("Invalid directory");
    tft.drawString("no dir",0, 0, 2);
    return;
  }
  
  int fileCounter = 0;
  while (true) {
  File entry = root.openNextFile();
    if (!entry) {
      break;
    }
    if (!entry.isDirectory()) {
      String filename = entry.name();
      //Serial.println(filename);

      int extensionIndex = filename.lastIndexOf('.');
      if (extensionIndex != -1) {
        filename = filename.substring(0, extensionIndex);
      }
      
      recieveddrawingsFilenames[fileCounter] = filename;
      fileCounter++;
    }
    entry.close();
  }
  
  root.close();


  File rooty = SD.open("/mydrawings");
  if (!rooty.isDirectory()) {
    Serial.println("Invalid directory");
    tft.drawString("no dir",0, 0, 2);
    return;
  }
  
  int fileCounter2 = 0;
  while (true) {
  File entry2 = rooty.openNextFile();
    if (!entry2) {
      break;
    }
    if (!entry2.isDirectory()) {
      String filename2 = entry2.name();
      //Serial.println(filename);
      
      int extensionIndex2 = filename2.lastIndexOf('.');
      if (extensionIndex2 != -1) {
        filename2 = filename2.substring(0, extensionIndex2);
      }

      mydrawingsFilenames[fileCounter2] = filename2;
      fileCounter2++;
    }
    entry2.close();
  }
  
  rooty.close();
  

  fileScreenFilenamesbubbleSortDescending(recieveddrawingsFilenames, 200);
  fileScreenFilenamesbubbleSortDescending(mydrawingsFilenames, 200);
  //fileScreenFilenamesbubbleSortDescending(artFilenames, 200);

  Serial.println("Got Filenames");
}

void fileScreenRenderBoxes(int xnum, int ynum){
  
  for (int rr=0; rr<7; rr++){
    for ( int tt=0; tt<3; tt++){
      tft.drawRect((50+(tt*100)),(45+(rr*30)),85,2,TFT_WHITE);      
    }
  }
  
  tft.drawRect((50+(xnum*100)),(45+((ynum-fileScrollNumber)*30)),85,2,TFT_RED);
    
}

void fileScreenRenderFilenames(int scrollNumber){
  Serial.println("Rendering file names");
  //tft.setTextSize(2);
  tft.setTextFont(2);
  tft.setTextColor(TFT_BLACK);
  //tft.setTextBackground(TFT_TRANSPARENT);
  
  tft.fillRect(40,30,280,210,TFT_WHITE);
  for (int qq=0; qq<8; qq++){
    tft.drawString(recieveddrawingsFilenames[qq+scrollNumber],50,(30+(qq*30)));
  }
  //tft.fillRect(120,30,100,210,TFT_WHITE);
  for (int ww=0; ww<8; ww++){
    tft.drawString(mydrawingsFilenames[ww+scrollNumber],150,(30+(ww*30)));
  }
  //tft.fillRect(210,30,110,210,TFT_WHITE);
  for (int ee=0; ee<8; ee++){
    tft.drawString(artFilenames[ee+scrollNumber],250,(30+(ee*30)));
  }
  //tft.fillRect(300,30,20,210,TFT_WHITE);

  //tft.setTextSize(1);
  //tft.setTextFont(2);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  //tft.setTextBackground(TFT_BLACK);
}

void fileScreenFilenamesbubbleSortDescending(String arr[], int size) {
  // for (int pp=0; pp<200; pp++) {
  //   Serial.println(String(pp)+"\t\t"+arr[pp]);
  // }
  
  int iteration = 0;
  //while (iteration<size) {
  bool sorted = false;
  while (!sorted) {
    sorted = true; // Assume the array is sorted
    
    for (int i = 0; i < size - 1; i++) {
      String str1 = arr[i];
      String str2 = arr[i + 1];

      // Convert strings to integers for numerical comparison
      double num1 = str1.toDouble();
      double num2 = str2.toDouble();

      if (num1 < num2) {
        String temp = arr[i];
        arr[i] = arr[i + 1];
        arr[i + 1] = temp;
        sorted = false;
      }
    }
    //Serial.println("iteration done: " + String(iteration));
    iteration++;
    
    //Serial.println(arr[0]+"\t\t"+arr[1]+"\t\t"+arr[2]+"\t\t"+arr[3]+"\t\t"+arr[4]+"\t\t"+arr[5]+"\t\t"+arr[6]);

  }

  Serial.println("sorted in: " + String(iteration) + " full iterations"); 
  // for (int pp=0; pp<200; pp++) {
  //   Serial.println(String(pp)+"\t\t"+arr[pp]);
  // }
}

void fileScreenClearFilenames(){
  for (int zoom=0; zoom<200; zoom++){
    recieveddrawingsFilenames[zoom]="";
    mydrawingsFilenames[zoom]="";
    artFilenames[zoom]="";
  } 
}

void fileScreenLogs(){
  tft.fillScreen(TFT_WHITE);
  drawBmp24("/logos/homelogo.bmp",0,170);
  drawBmp24("/logos/uparrow.bmp",0,10);
  drawBmp24("/logos/uparrow.bmp",0,90);
  drawBmp24("/logos/uparrow.bmp",0,80);
  drawBmp24("/logos/downarrow.bmp",0,45);
  drawBmp24("/logos/downarrow.bmp",0,125);
  drawBmp24("/logos/downarrow.bmp",0,135);
  int logPageNumber = 0;
  fileScreenLogsRefresh(logPageNumber);

  bool saveButtonPressed = false; // Flag to indicate if save button was pressed
  while (!saveButtonPressed) {
  
  uint16_t x = 0, y = 0;
  bool pressed = tft.getTouch(&x, &y);
  
  if (pressed) {
    
    if((x>0)&&(x<30)&&(y>0)&&(y<40)){
      if (logPageNumber>0)
        logPageNumber-=1;
      fileScreenLogsRefresh(logPageNumber);
      delay(100);
    }
    if((x>0)&&(x<30)&&(y>50)&&(y<90)){
      if (logPageNumber<=499)
        logPageNumber+=1;
      fileScreenLogsRefresh(logPageNumber);
      delay(100);
    }
    if((x>0)&&(x<30)&&(y>80)&&(y<120)){
      if (logPageNumber>=10)
        logPageNumber-=10;
      fileScreenLogsRefresh(logPageNumber);
      delay(100);
    }
    if((x>0)&&(x<30)&&(y>125)&&(y<165)){
      if (logPageNumber<=490)
        logPageNumber+=10;
      fileScreenLogsRefresh(logPageNumber);
      delay(100);
    }

    if((x>0)&&(x<30)&&(y>170)&&(y<200)){
      saveButtonPressed = true;
      //homeScreen();        
    }
  }
  }

}

void fileScreenLogsRefresh(int page){
  tft.fillRect(40,0,280,240,TFT_WHITE);
  tft.drawString(("Log Page:  "+String(page)),130, 5, 2);

  File myFile = SD.open("/cloud_log.txt", FILE_READ);

  myFile.seek(0);
  uint16_t lineCount = 0;//finds the starting place based on the page number
  bool prematureEnd = false;
  while ((lineCount < (page*10)) && (!prematureEnd)) {//10 lines on a page
    char c = myFile.read();//read reads the next byte (if there is one)
    //Serial.print(c);
    //Serial.println(int(c));
    if (c == '\n') {
      lineCount++;
    }
    if (c == 255) {
      prematureEnd = true;
    }
    //Serial.print("1");
  }

  String logDisplay = "";
  lineCount = 0;
  prematureEnd = false;
  while ((lineCount < (10+(page*10))) && (!prematureEnd)) {//reads the file to assign to a string
    char c = myFile.read();
    logDisplay += c;
    //Serial.print(c);
    if (c == '\n') {
      lineCount++;
    }
    if (c == 255) {
      prematureEnd = true;
    }
    //Serial.print("2");
  }

  myFile.close();

  
  Serial.println("logDisplay: "+String(logDisplay));
  int yOffset = 30; // Starting Y coordinate
  int lineHeight = 20; // Line height
  int oldPos = 0;
  int newPos = 0;
  for (int i = 0; i < 10; i++) {
    newPos = logDisplay.indexOf('\n',oldPos);
    if (newPos ==-1)
      return;
    Serial.println("old pos: "+String(oldPos)+"\tnew pos: "+String(newPos));
    String line = logDisplay.substring(oldPos, newPos);
    oldPos = newPos+1;
    tft.drawString(line, 40, yOffset + (lineHeight*i), 2);
    Serial.println("line: "+line);
  }
  
}


/***************************************************************************************************
P  I  C  T  U  R  E      D  R  A  W      S  C  R  E  E  N
***************************************************************************************************/
void drawScreen2(){
  tft.fillScreen(drawColor4);
  drawScreenRefresh();
  zeroImageArray();
  
  drawBmp24("/logos/homelogoinverted.bmp",0,170);
  drawBmp24("/logos/settingsinverted.bmp",0,135);

  tft.drawRect(0,210,30,30,TFT_WHITE);
  tft.drawString("SEND",0, 217, 2);  
  drawScreenRefresh();
  currentColor = drawColor1;
  tft.fillCircle(15,15,5,TFT_SILVER);


  delay(100);
  int currentLineTimer = 0;
  int lastx = -1;
  int lasty = -1;
  bool saveButtonPressed = false; // Flag to indicate if save button was pressed
  while (!saveButtonPressed) {



  uint16_t x = 0, y = 0; // To store the touch coordinates

  // Pressed will be set true is there is a valid touch on the screen
  bool pressed = tft.getTouch(&x, &y);

  // Draw a white spot at the detected coordinates
  if (pressed) {
    if (millis() > (currentLineTimer+300)){//150
      lastx = -1;
      lasty = -1;
      Serial.println("new line\n");
    }

    tft.fillCircle(x, y, sizeOfBrush, currentColor);   //5
    saveCircleColors(x,y); 
    Serial.println("touched point: "+String(x) + "," + String(y) + "\t" + String(lastx) + "," + String(lasty));

    Serial.println("Time inbetween dots: "+String(millis()-currentLineTimer));
    currentLineTimer = millis();

    linearInterpolateDrawingPoints(x,y,lastx,lasty);
    lastx = x;
    lasty = y;      

    if((x>0)&&(x<30)&&(y>0)&&(y<30)){
      currentColor = drawColor1;
      drawScreenRefresh();
      tft.fillCircle(15,15,5,TFT_SILVER);
    }
    if((x>0)&&(x<30)&&(y>32)&&(y<62)){
      currentColor = drawColor2;
      drawScreenRefresh();
      tft.fillCircle(15,47,5,TFT_SILVER);
    }
    if((x>0)&&(x<30)&&(y>64)&&(y<94)){
      currentColor = drawColor3;
      drawScreenRefresh();
      tft.fillCircle(15,79,5,TFT_SILVER);
    }
    if((x>0)&&(x<30)&&(y>96)&&(y<126)){
      // currentColor = TFT_BLACK;
      // drawScreenRefresh();
      // tft.fillCircle(15,111,5,TFT_SILVER);
      zeroImageArray();
      tft.fillScreen(drawColor4);
      currentColor = drawColor1;
      drawScreenRefresh();
      tft.fillCircle(15,15,5,TFT_SILVER);
      drawBmp24("/logos/settingsinverted.bmp",0,135);
      drawBmp24("/logos/homelogoinverted.bmp",0,170);
      tft.drawRect(0,210,30,30,TFT_WHITE);
      tft.drawString("SEND",0, 217, 2); 
      
    }
    if((x>0)&&(x<30)&&(y>140)&&(y<170)){
      drawScreenSettings();
      
      zeroImageArray();
      tft.fillScreen(drawColor4);
      currentColor = drawColor1;
      drawScreenRefresh();
      tft.fillCircle(15,15,5,TFT_SILVER);
      drawBmp24("/logos/settingsinverted.bmp",0,135);
      drawBmp24("/logos/homelogoinverted.bmp",0,170);
      tft.drawRect(0,210,30,30,TFT_WHITE);
      tft.drawString("SEND",0, 217, 2); 
      delay(200);
    }
    if((x>0)&&(x<30)&&(y>170)&&(y<200)){
      saveButtonPressed = true;
      //homeScreen();        
    }
    
    if((x>0)&&(x<30)&&(y>210)&&(y<240))
    {
      //add all values to the array via a function
      Serial.println("save button pushed");
      tft.fillRect(0,210,33,30,TFT_BLACK);
      drawBmp24("/logos/refreshlogoloadingblack.bmp",0,210);
      //int timeNumber = epochTime;
      ntpUpdate();
      String bmpformat = "/mydrawings/" + String(epochTime) + ".bmp";
      captureScreenToBMP(bmpformat.c_str());
      saveButtonPressed = true;
      //readSD2bitImage("/8868.bmp");
      //display2bitImage();
      postCloudDrawing(epochTime);
    }
  }


  }
}

void linearInterpolateDrawingPoints(int x, int y, int lastx, int lasty){
  if (lastx < 0){//and lasty < 0
    return;
  }
  
  double xdif = (x-lastx);
  double ydif = (y-lasty);
  double biggerXYdif = max(abs(xdif), abs(ydif));
  double maxApplicableStep = sizeOfBrush+5;
  if (biggerXYdif < maxApplicableStep){
    return;
  }
  double interpolationStepsNumber = biggerXYdif/maxApplicableStep;
  double interpolationStepSizeX = xdif / (interpolationStepsNumber+1);
  double interpolationStepSizeY = ydif / (interpolationStepsNumber+1);

  Serial.println("xydif: "+String(xdif)+", "+String(ydif)+"\tbiggerXYdif: "+String(biggerXYdif)+"\tinterpolationStepsNumber: "+String(interpolationStepsNumber)+"\tinterpolationStepSizeXY: "+String(interpolationStepSizeX)+", "+String(interpolationStepSizeY));
  Serial.println("Initial: "+String(x)+", "+String(y)+"\tEndpoint: "+String(lastx)+", "+String(lasty));
  for (int zaza = 1; zaza <= interpolationStepsNumber; zaza++){
    tft.fillCircle(x+(interpolationStepSizeX*zaza*-1), y+(interpolationStepSizeY*zaza*-1), sizeOfBrush, currentColor);
    saveCircleColors(x+(interpolationStepSizeX*zaza*-1), y+(interpolationStepSizeY*zaza*-1));
    Serial.println("New Coords: "+String(x+(interpolationStepSizeX*zaza*-1))+", "+String(y+(interpolationStepSizeY*zaza*-1)));
  }

} 

/*void drawScreen(){
  tft.fillScreen(drawColor4);
  drawScreenRefresh();
  zeroImageArray();

  //tft.drawRect(0,170,30,30,TFT_WHITE);
  //tft.drawString("HOME",0, 177, 2);
  drawBmp24("/logos/homelogoinverted.bmp",0,170);
  drawBmp24("/logos/settingsinverted.bmp",0,135);

  tft.drawRect(0,210,30,30,TFT_WHITE);
  tft.drawString("SEND",0, 217, 2);  
  drawScreenRefresh();
  currentColor = drawColor1;
  tft.fillCircle(15,15,5,TFT_SILVER);


  delay(100);
  bool saveButtonPressed = false; // Flag to indicate if save button was pressed
  while (!saveButtonPressed) {



  uint16_t x = 0, y = 0; // To store the touch coordinates

  // Pressed will be set true is there is a valid touch on the screen
  bool pressed = tft.getTouch(&x, &y);

  // Draw a white spot at the detected coordinates
  if (pressed) {
    tft.fillCircle(x, y, sizeOfBrush, currentColor);   //5

    //Serial.print(String(x) + "," + String(y) + "\n");
    
    saveCircleColors(x,y);        

    if((x>0)&&(x<30)&&(y>0)&&(y<30)){
      currentColor = drawColor1;
      drawScreenRefresh();
      tft.fillCircle(15,15,5,TFT_SILVER);
    }
    if((x>0)&&(x<30)&&(y>32)&&(y<62)){
      currentColor = drawColor2;
      drawScreenRefresh();
      tft.fillCircle(15,47,5,TFT_SILVER);
    }
    if((x>0)&&(x<30)&&(y>64)&&(y<94)){
      currentColor = drawColor3;
      drawScreenRefresh();
      tft.fillCircle(15,79,5,TFT_SILVER);
    }
    if((x>0)&&(x<30)&&(y>96)&&(y<126)){
      // currentColor = TFT_BLACK;
      // drawScreenRefresh();
      // tft.fillCircle(15,111,5,TFT_SILVER);
      zeroImageArray();
      tft.fillScreen(drawColor4);
      currentColor = drawColor1;
      drawScreenRefresh();
      tft.fillCircle(15,15,5,TFT_SILVER);
      drawBmp24("/logos/settingsinverted.bmp",0,135);
      drawBmp24("/logos/homelogoinverted.bmp",0,170);
      tft.drawRect(0,210,30,30,TFT_WHITE);
      tft.drawString("SEND",0, 217, 2); 
      
    }
    if((x>0)&&(x<30)&&(y>140)&&(y<170)){
      drawScreenSettings();
      
      zeroImageArray();
      tft.fillScreen(drawColor4);
      currentColor = drawColor1;
      drawScreenRefresh();
      tft.fillCircle(15,15,5,TFT_SILVER);
      drawBmp24("/logos/settingsinverted.bmp",0,135);
      drawBmp24("/logos/homelogoinverted.bmp",0,170);
      tft.drawRect(0,210,30,30,TFT_WHITE);
      tft.drawString("SEND",0, 217, 2); 
      delay(200);
    }
    if((x>0)&&(x<30)&&(y>170)&&(y<200)){
      saveButtonPressed = true;
      //homeScreen();        
    }
    
    if((x>0)&&(x<30)&&(y>210)&&(y<240))
    {
      Serial.println("save button pushed");
      tft.fillRect(0,210,33,30,TFT_BLACK);
      drawBmp24("/logos/refreshlogoloadingblack.bmp",0,210);
      //int timeNumber = epochTime;
      ntpUpdate();
      String bmpformat = "/mydrawings/" + String(epochTime) + ".bmp";
      captureScreenToBMP(bmpformat.c_str());
      saveButtonPressed = true;
      //readSD2bitImage("/8868.bmp");
      //display2bitImage();
      postCloudDrawing(epochTime);
    }
  }



  }
}*/

void drawScreenRefresh(){
  //This just reprints the color boxes and icons, it leaves the drawing alone
  //It makes it so that you don't need a sprite, you can't really draw on top of the color boxes,
  //and that you the dot on the current color is always correct
  tft.drawRect(0,0,30,30,TFT_WHITE);
  tft.fillRect(2,2,26,26,drawColor1);

  tft.drawRect(0,32,30,30,TFT_WHITE);
  tft.fillRect(2,34,26,26,drawColor2);

  tft.drawRect(0,64,30,30,TFT_WHITE);
  tft.fillRect(2,66,26,26,drawColor3);

  tft.drawRect(0,96,30,30,TFT_WHITE);
  tft.fillRect(2,98,26,26,drawColor4);
  tft.drawLine(2,98,28,124,TFT_RED);
  tft.drawLine(28,98,2,124,TFT_RED);

}

void drawScreenSettings(){
  tft.fillScreen(TFT_BLACK);
  tft.drawRect(0,210,30,30,TFT_WHITE);
  tft.drawString("SAVE",0, 217, 2);  
  //drawBmp24("/logos/homelogoinverted.bmp",0,170);
  tft.drawString("You can only have 4 colors in one drawing",18,10,2);
  tft.drawString("Brush Size",18,60,2);
  tft.drawRect(10,50,80,110,TFT_WHITE);

  //sizeOfBrush = 5;
  tft.fillCircle(45,80,3,TFT_GREEN);
  tft.fillCircle(45,105,5,TFT_GREEN);
  tft.fillCircle(45,140,9,TFT_GREEN);
  //tft.fillCircle(25,105,3,TFT_RED);

  drawSolidColorWheel(160,100,50,10);//10
  tft.fillRectVGradient(260, 50, 40, 100, 0xffff, 0x0000);
  int activeColor = 0;
  bool colorChange = false;
  int drawColor1OG=drawColor1, drawColor2OG=drawColor2, drawColor3OG=drawColor3, drawColor4OG=drawColor4;
  tft.fillCircle(100,210,20,drawColor1);
  tft.fillCircle(160,210,20,drawColor2);
  tft.fillCircle(220,210,20,drawColor3);
  tft.fillCircle(280,210,20,drawColor4);
  tft.drawCircle(100,210,21,TFT_WHITE);
  tft.drawCircle(160,210,21,TFT_WHITE);
  tft.drawCircle(220,210,21,TFT_WHITE);
  tft.drawCircle(280,210,21,TFT_WHITE);
  tft.drawCircle(100,210,22,TFT_WHITE);
  tft.drawCircle(160,210,22,TFT_WHITE);
  tft.drawCircle(220,210,22,TFT_WHITE);
  tft.drawCircle(280,210,22,TFT_WHITE);
  tft.fillCircle(100,236,3,TFT_RED);
  tft.fillCircle(230,60,12,TFT_WHITE);
  tft.fillCircle(230,140,12,0x39c7);


  delay(100);
  bool saveButtonPressed = false; // Flag to indicate if save button was pressed
  while (!saveButtonPressed) {
  uint16_t x = 0, y = 0; // To store the touch coordinates
  bool pressed = tft.getTouch(&x, &y);
  if (pressed) {
    if (pow(x - 160, 2) + pow(y - 100, 2) <= pow(75, 2)){//increased hit box radius from 50 to 75
      int selectedColor = getColorAtCoordinate(160,100,50,x,y);
      Serial.println("Selected Color: " + String(selectedColor));
      switch (activeColor){
        case 0:
          drawColor1 = selectedColor;drawColor1OG=drawColor1;break;
        case 1:
          drawColor2 = selectedColor;drawColor2OG=drawColor2;break;
        case 2:
          drawColor3 = selectedColor;drawColor3OG=drawColor3;break;
        case 3:
          drawColor4 = selectedColor;drawColor4OG=drawColor4;break;
      }
      colorChange = true;   
    }

    if((x>260)&&(x<300)&&(y>50)&&(y<150)){
      int selectedColor;
      switch (activeColor){
        case 0:
          selectedColor = drawColor1OG;break;
        case 1:
          selectedColor = drawColor2OG;break;
        case 2:
          selectedColor = drawColor3OG;break;
        case 3:
          selectedColor = drawColor4OG;break;
      }
      Serial.println("Before darkening: " + String(selectedColor));
      selectedColor = changeColorBrightness(50,150,y,selectedColor);
      Serial.println("After darkening: " + String(selectedColor));
      switch (activeColor){
        case 0:
          drawColor1 = selectedColor;break;
        case 1:
          drawColor2 = selectedColor;break;
        case 2:
          drawColor3 = selectedColor;break;
        case 3:
          drawColor4 = selectedColor;break;
      }
      colorChange = true;  
      delay(100);
    }

    if(pow(x - 230, 2) + pow(y - 60, 2) <= pow(12, 2)){
      switch (activeColor){
        case 0:
          drawColor1 = TFT_WHITE ;drawColor1OG=drawColor1;break;
        case 1:
          drawColor2 = TFT_WHITE;drawColor2OG=drawColor2;break;
        case 2:
          drawColor3 = TFT_WHITE;drawColor3OG=drawColor3;break;
        case 3:
          drawColor4 = TFT_WHITE;drawColor4OG=drawColor4;break;
      }
      colorChange = true;
    }
    if(pow(x - 230, 2) + pow(y - 140, 2) <= pow(12, 2)){
      switch (activeColor){
        case 0:
          drawColor1 = TFT_BLACK ;drawColor1OG=drawColor1;break;
        case 1:
          drawColor2 = TFT_BLACK;drawColor2OG=drawColor2;break;
        case 2:
          drawColor3 = TFT_BLACK;drawColor3OG=drawColor3;break;
        case 3:
          drawColor4 = TFT_BLACK;drawColor4OG=drawColor4;break;
      }
      colorChange = true;
    }
    // tft.drawRect(10,50,80,110,TFT_WHITE);
    if(pow(x - 45, 2) + pow(y - 80, 2) <= pow(6, 2)){
      tft.fillRect(11,75,24,70,TFT_BLACK);
      tft.fillCircle(25,80,3,TFT_RED);
      sizeOfBrush = 3;
    }
    if(pow(x - 45, 2) + pow(y - 105, 2) <= pow(9, 2)){
      tft.fillRect(11,75,24,70,TFT_BLACK);
      tft.fillCircle(25,105,3,TFT_RED);
      sizeOfBrush = 5;
    }
    if(pow(x - 45, 2) + pow(y - 140, 2) <= pow(11, 2)){
      tft.fillRect(11,75,24,70,TFT_BLACK);
      tft.fillCircle(25,140,3,TFT_RED);
      sizeOfBrush = 9;
    }

    if((x>100)&&(x<140)&&(y>210)&&(y<250)){
      tft.fillRect(40,233,260,10,TFT_BLACK);
      tft.fillCircle(100,236,3,TFT_RED);
      activeColor = 0;
    }
    if((x>160)&&(x<200)&&(y>210)&&(y<250)){
      tft.fillRect(40,233,260,10,TFT_BLACK);
      tft.fillCircle(160,236,3,TFT_RED);
      activeColor = 1;
    }
    if((x>220)&&(x<260)&&(y>210)&&(y<250)){
      tft.fillRect(40,233,260,10,TFT_BLACK);
      tft.fillCircle(220,236,3,TFT_RED);
      activeColor = 2;
    }
    if((x>280)&&(x<320)&&(y>210)&&(y<250)){
      tft.fillRect(40,233,260,10,TFT_BLACK);
      tft.fillCircle(280,236,3,TFT_RED);
      activeColor = 3;
    }

    if (colorChange){
      tft.fillCircle(100,210,20,drawColor1);
      tft.fillCircle(160,210,20,drawColor2);
      tft.fillCircle(220,210,20,drawColor3);
      tft.fillCircle(280,210,20,drawColor4);
      colorChange = false;
    }
    
    if((x>0)&&(x<30)&&(y>210)&&(y<240)){
      // zeroImageArray();
      // memcpy(&imageLine[19200], &drawColor1, sizeof(drawColor1));
      // memcpy(&imageLine[19204], &drawColor2, sizeof(drawColor2));
      // memcpy(&imageLine[19208], &drawColor3, sizeof(drawColor3));
      // memcpy(&imageLine[19212], &drawColor4, sizeof(drawColor4));
      // Serial.println("color1: " + String(drawColor1) + "\t color2: " + String(drawColor2) + "\t color3: " + String(drawColor3) + "\t color4: " + String(drawColor4));
      // for (int sdf = 19000; sdf<sizeof(imageLine); sdf += 4)  {
      //   uint32_t tempp = 0;
      //   memcpy(&tempp, &imageLine[sdf], sizeof(tempp));
      //   Serial.println(String(sdf) + "\t" + String(tempp));
      // }
      saveButtonPressed = true;
    }
    
  }
  }    
}

void saveCircleColors(int xx,int yy){
  
  if (sizeOfBrush==5){
    for (int uu = 0; uu < 89; uu++){        //89 pixels for a "5" radius circle... which is actually 6

      int x=0;
      int y=0;
      r5CircleArray(xx, yy, uu, x, y); 
      if ((x<0)||(x>319)){x=xx;}   //if x or y are out of bounds set them back to the center
      if ((y<0)||(y>239)){y=yy;}
              
      int bytePosition = ((320*(239-y)) + x)/4;
      int bitPosition = (x%4)*2;

      if      (bitPosition==0){imageLine[bytePosition] &= 0xFC;}
      else if (bitPosition==2){imageLine[bytePosition] &= 0xF3;}
      else if (bitPosition==4){imageLine[bytePosition] &= 0xCF;}
      else if (bitPosition==6){imageLine[bytePosition] &= 0x3F;} 
      
      if      (currentColor == drawColor1){imageLine[bytePosition] |= (3 << bitPosition);} // 11
      else if (currentColor == drawColor2)  {imageLine[bytePosition] |= (2 << bitPosition);} // 10
      else if (currentColor == drawColor3) {imageLine[bytePosition] |= (1 << bitPosition);} // 01
      //(else) if it's black do nothing                                                   // 00
      
      //Serial.print("Byte " + String(bytePosition) + "  Bit " + String(bitPosition, HEX) + "  Value " + String(imageLine[bytePosition]) + "  x " + x + "  y " + y +"\n\n");
    }
  }

  else if (sizeOfBrush==3){
    for (int uu = 0; uu < 37; uu++){        //37 pixels for a "3" radius circle... which is actually 4

      int x=0;
      int y=0;
      r3CircleArray(xx, yy, uu, x, y); 
      if ((x<0)||(x>319)){x=xx;}   //if x or y are out of bounds set them back to the center
      if ((y<0)||(y>239)){y=yy;}
              
      int bytePosition = ((320*(239-y)) + x)/4;
      int bitPosition = (x%4)*2;

      if      (bitPosition==0){imageLine[bytePosition] &= 0xFC;}
      else if (bitPosition==2){imageLine[bytePosition] &= 0xF3;}
      else if (bitPosition==4){imageLine[bytePosition] &= 0xCF;}
      else if (bitPosition==6){imageLine[bytePosition] &= 0x3F;} 
      
      if      (currentColor == drawColor1){imageLine[bytePosition] |= (3 << bitPosition);} // 11
      else if (currentColor == drawColor2)  {imageLine[bytePosition] |= (2 << bitPosition);} // 10
      else if (currentColor == drawColor3) {imageLine[bytePosition] |= (1 << bitPosition);} // 01
      //(else) if it's black do nothing                                                   // 00
    }
  }

  else if (sizeOfBrush==9){
    for (int uu = 0; uu < 269; uu++){        //269 pixels for a "9" radius circle... which is actually 10

      int x=0;
      int y=0;
      r9CircleArray(xx, yy, uu, x, y); 
      if ((x<0)||(x>319)){x=xx;}   //if x or y are out of bounds set them back to the center
      if ((y<0)||(y>239)){y=yy;}
              
      int bytePosition = ((320*(239-y)) + x)/4;
      int bitPosition = (x%4)*2;

      if      (bitPosition==0){imageLine[bytePosition] &= 0xFC;}
      else if (bitPosition==2){imageLine[bytePosition] &= 0xF3;}
      else if (bitPosition==4){imageLine[bytePosition] &= 0xCF;}
      else if (bitPosition==6){imageLine[bytePosition] &= 0x3F;} 
      
      if      (currentColor == drawColor1){imageLine[bytePosition] |= (3 << bitPosition);} // 11
      else if (currentColor == drawColor2)  {imageLine[bytePosition] |= (2 << bitPosition);} // 10
      else if (currentColor == drawColor3) {imageLine[bytePosition] |= (1 << bitPosition);} // 01
      //(else) if it's black do nothing                                                   // 00
    }
  }

}

void r3CircleArray(int xx, int yy, int uu, int& x, int& y) {
    int modifications[][2] = {
                            {-1, 3},  {0, 3},  {1, 3},
                  {-2, 2},  {-1, 2},  {0, 2},  {1, 2},  {2, 2},
        {-3, 1},  {-2, 1},  {-1, 1},  {0, 1},  {1, 1},  {2, 1},  {3, 1},
        {-3, 0},  {-2, 0},  {-1, 0},  {0, 0},  {1, 0},  {2, 0},  {3, 0},
        {-3, -1}, {-2, -1}, {-1, -1}, {0, -1}, {1, -1}, {2, -1}, {3, -1},
                  {-2, -2}, {-1, -2}, {0, -2}, {1, -2}, {2, -2},
                            {-1, -3}, {0, -3}, {1, -3}
    };

    x = xx + modifications[uu][0];
    y = yy + modifications[uu][1];
}

void r5CircleArray(int xx, int yy, int uu, int& x, int& y) {
  //This if for the realtime saving of where you tapped to draw (a circle), these are all of the pixels that one tap will color in
  //If you wanted to for example change the drawing cursor size, you would have to redefine a new function with bigger or smaller radius of effected circles
  //I could have done this more effeciently by having the xx and yy in the last 2 lines instead of repeating them like 100 times
  int modifications[][2] = {

                                                          {xx-1, yy+5}, {xx, yy+5}, {xx+1, yy+5},
                              {xx-3, yy+4}, {xx-2, yy+4}, {xx-1, yy+4}, {xx, yy+4}, {xx+1, yy+4}, {xx+2, yy+4}, {xx+3, yy+4},
                {xx-4, yy+3}, {xx-3, yy+3}, {xx-2, yy+3}, {xx-1, yy+3}, {xx, yy+3}, {xx+1, yy+3}, {xx+2, yy+3}, {xx+3, yy+3}, {xx+4, yy+3},
                {xx-4, yy+2}, {xx-3, yy+2}, {xx-2, yy+2}, {xx-1, yy+2}, {xx, yy+2}, {xx+1, yy+2}, {xx+2, yy+2}, {xx+3, yy+2}, {xx+4, yy+2},
  {xx-5, yy+1}, {xx-4, yy+1}, {xx-3, yy+1}, {xx-2, yy+1}, {xx-1, yy+1}, {xx, yy+1}, {xx+1, yy+1}, {xx+2, yy+1}, {xx+3, yy+1}, {xx+4, yy+1}, {xx+5, yy+1},
  {xx-5, yy+0}, {xx-4, yy+0}, {xx-3, yy+0}, {xx-2, yy+0}, {xx-1, yy+0}, {xx, yy+0}, {xx+1, yy+0}, {xx+2, yy+0}, {xx+3, yy+0}, {xx+4, yy+0}, {xx+5, yy+0},
  {xx-5, yy-1}, {xx-4, yy-1}, {xx-3, yy-1}, {xx-2, yy-1}, {xx-1, yy-1}, {xx, yy-1}, {xx+1, yy-1}, {xx+2, yy-1}, {xx+3, yy-1}, {xx+4, yy-1}, {xx+5, yy-1},
                {xx-4, yy-2}, {xx-3, yy-2}, {xx-2, yy-2}, {xx-1, yy-2}, {xx, yy-2}, {xx+1, yy-2}, {xx+2, yy-2}, {xx+3, yy-2}, {xx+4, yy-2}, 
                {xx-4, yy-3}, {xx-3, yy-3}, {xx-2, yy-3}, {xx-1, yy-3}, {xx, yy-3}, {xx+1, yy-3}, {xx+2, yy-3}, {xx+3, yy-3}, {xx+4, yy-3}, 
                              {xx-3, yy-4}, {xx-2, yy-4}, {xx-1, yy-4}, {xx, yy-4}, {xx+1, yy-4}, {xx+2, yy-4}, {xx+3, yy-4},
                                                          {xx-1, yy-5}, {xx, yy-5}, {xx+1, yy-5}

  };
  
  x = modifications[uu][0];
  y = modifications[uu][1];
}

void r9CircleArray(int xx, int yy, int uu, int& x, int& y) {
    int modifications[][2] = {
        {-2, 9}, {-1, 9}, {0, 9}, {1, 9}, {2, 9},
        {-4, 8}, {-3, 8}, {-2, 8}, {-1, 8}, {0, 8}, {1, 8}, {2, 8}, {3, 8}, {4, 8},
        {-5, 7}, {-4, 7}, {-3, 7}, {-2, 7}, {-1, 7}, {0, 7}, {1, 7}, {2, 7}, {3, 7}, {4, 7}, {5, 7},
        {-6, 6}, {-5, 6}, {-4, 6}, {-3, 6}, {-2, 6}, {-1, 6}, {0, 6}, {1, 6}, {2, 6}, {3, 6}, {4, 6}, {5, 6}, {6, 6},
        {-7, 5}, {-6, 5}, {-5, 5}, {-4, 5}, {-3, 5}, {-2, 5}, {-1, 5}, {0, 5}, {1, 5}, {2, 5}, {3, 5}, {4, 5}, {5, 5}, {6, 5}, {7, 5},
        {-8, 4}, {-7, 4}, {-6, 4}, {-5, 4}, {-4, 4}, {-3, 4}, {-2, 4}, {-1, 4}, {0, 4}, {1, 4}, {2, 4}, {3, 4}, {4, 4}, {5, 4}, {6, 4}, {7, 4}, {8, 4},
        {-8, 3}, {-7, 3}, {-6, 3}, {-5, 3}, {-4, 3}, {-3, 3}, {-2, 3}, {-1, 3}, {0, 3}, {1, 3}, {2, 3}, {3, 3}, {4, 3}, {5, 3}, {6, 3}, {7, 3}, {8, 3},
        {-9, 2}, {-8, 2}, {-7, 2}, {-6, 2}, {-5, 2}, {-4, 2}, {-3, 2}, {-2, 2}, {-1, 2}, {0, 2}, {1, 2}, {2, 2}, {3, 2}, {4, 2}, {5, 2}, {6, 2}, {7, 2}, {8, 2}, {9, 2},
        {-9, 1}, {-8, 1}, {-7, 1}, {-6, 1}, {-5, 1}, {-4, 1}, {-3, 1}, {-2, 1}, {-1, 1}, {0, 1}, {1, 1}, {2, 1}, {3, 1}, {4, 1}, {5, 1}, {6, 1}, {7, 1}, {8, 1}, {9, 1},
        {-9, 0}, {-8, 0}, {-7, 0}, {-6, 0}, {-5, 0}, {-4, 0}, {-3, 0}, {-2, 0}, {-1, 0}, {0, 0}, {1, 0}, {2, 0}, {3, 0}, {4, 0}, {5, 0}, {6, 0}, {7, 0}, {8, 0}, {9, 0},
        {-9, -1}, {-8, -1}, {-7, -1}, {-6, -1}, {-5, -1}, {-4, -1}, {-3, -1}, {-2, -1}, {-1, -1}, {0, -1}, {1, -1}, {2, -1}, {3, -1}, {4, -1}, {5, -1}, {6, -1}, {7, -1}, {8, -1}, {9, -1},
        {-9, -2}, {-8, -2}, {-7, -2}, {-6, -2}, {-5, -2}, {-4, -2}, {-3, -2}, {-2, -2}, {-1, -2}, {0, -2}, {1, -2}, {2, -2}, {3, -2}, {4, -2}, {5, -2}, {6, -2}, {7, -2}, {8, -2}, {9, -2},
        {-8, -3}, {-7, -3}, {-6, -3}, {-5, -3}, {-4, -3}, {-3, -3}, {-2, -3}, {-1, -3}, {0, -3}, {1, -3}, {2, -3}, {3, -3}, {4, -3}, {5, -3}, {6, -3}, {7, -3}, {8, -3},
        {-8, -4}, {-7, -4}, {-6, -4}, {-5, -4}, {-4, -4}, {-3, -4}, {-2, -4}, {-1, -4}, {0, -4}, {1, -4}, {2, -4}, {3, -4}, {4, -4}, {5, -4}, {6, -4}, {7, -4}, {8, -4},
        {-7, -5}, {-6, -5}, {-5, -5}, {-4, -5}, {-3, -5}, {-2, -5}, {-1, -5}, {0, -5}, {1, -5}, {2, -5}, {3, -5}, {4, -5}, {5, -5}, {6, -5}, {7, -5},
        {-6, -6}, {-5, -6}, {-4, -6}, {-3, -6}, {-2, -6}, {-1, -6}, {0, -6}, {1, -6}, {2, -6}, {3, -6}, {4, -6}, {5, -6}, {6, -6},
        {-5, -7}, {-4, -7}, {-3, -7}, {-2, -7}, {-1, -7}, {0, -7}, {1, -7}, {2, -7}, {3, -7}, {4, -7}, {5, -7},
        {-4, -8}, {-3, -8}, {-2, -8}, {-1, -8}, {0, -8}, {1, -8}, {2, -8}, {3, -8}, {4, -8},
        {-2, -9}, {-1, -9}, {0, -9}, {1, -9}, {2, -9}
    };

    x = xx + modifications[uu][0];
    y = yy + modifications[uu][1];
}

void captureScreenToBMP(const char* filename){
  //This function is for saving the 2bit image (which is stored in the imageLine array) to the SD card
  
  //****************** tft espi collides with the sd library... you must reinitilize sd every time (if you use tft inbetween (probably))
  // thus it is reinitilized, opened, and then closed for every line

  //***************** The bmp is saved in multiple lines (1 line of 320 pixels (960 bytes) at a time). This is becasue the memory (BSS memory(probably)) isn't big enough for all at once
  // it is important to save the big char array (imageLine) as a global variable. This way it is not competing for RAM (?) memory through the heap

  //***************** Colors get saved incorrectly normally, slow down the frequency 10 mhz or slower of read tft in user setup and/or add delay() 3 after readPixel
  // for my purposes for 2 bit color I dont really care about correct so the incorrect colors are hard remapped

  int32_t width = tft.width();            //Get the screen dimensions
  int32_t height = tft.height();          //This project is pretty 320x240 specific though
  //Serial.print(width);
  //Serial.print(height);
  
  //Serial.print("Initializing SD card...");  //I have better luck reinitilizing every time rather than once in setup
  if (!SD.begin(SD_CS, spiSD)) {                     //It seems that sometimes the sd and espi library mess each other up
    Serial.println("initialization failed!");
    return;
  }
  //Serial.println("initialization done.");
  
  if (SD.exists(filename)) {                  //if the file somehow already exists delete the old one
    SD.remove(filename);
    Serial.println("Existing file deleted.");
  }    
  
  // Open a file on the SD card
  File myFile = SD.open(filename, FILE_WRITE);//"Opens" the file with the passed filename to create/write into
  if (!myFile) {
    Serial.println("Error opening file");
    return;
  }


  // BMP file header                            //Currently the header information is tottaly turned off, this is becasue nothing really supports 2bit bmp reading
  uint8_t header[54] = {                        //and becasue it will save us a couple bytes of memory, i did leave it in though, you just have to make the imageLine[]
    0x42, 0x4D,             // Signature: BM    //bigger and uncomment myFile.write(header, sizeof(header));, but then you will have to change the reader too to parse the header 
    0x36, 0x84, 0x03, 0,             // File size in bytes (to be filled later)
    0, 0,                   // Reserved
    0, 0,                   // Reserved
    54, 0, 0, 0,            // Offset to image data
    40, 0, 0, 0,            // Header size
    0x40, 1, 0, 0,             // Image width (to be filled later)
    240, 0, 0, 0,             // Image height (to be filled later)
     1, 0,                  // Number of color planes
    2, 0,                  // Bits per pixel (24 for 24-bit BMP)
    0, 0, 0, 0,             // Compression method
    0, 0, 0, 0,             // Image size (can be 0 for uncompressed images)
    0, 0, 0, 0,             // X pixels per meter
    0, 0, 0, 0,             // Y pixels per meter
    0, 0, 0, 0,             // Number of colors (default: 0 for 2^n)
    0, 0, 0, 0              // Number of important colors (0 means all)
  };

  // Calculate some values for the BMP header
  uint32_t imageSize = sizeof(imageLine);  // 2 bits per pixel
  uint32_t fileSize = imageSize + sizeof(header);

  // Fill in the BMP header with the appropriate values  //We kind of already hardcoded it above but this will edit it again
  memcpy(&header[2], &fileSize, 4);        // File size
  memcpy(&header[18], &width, 4);          // Image width
  memcpy(&header[22], &height, 4);         // Image height
  memcpy(&header[34], &imageSize, 4);      // Image size

  // Write the BMP header to the file
  Serial.println("header start");
  //myFile.write(header, sizeof(header));                //uncomment this line to add back in the header
  Serial.println("header done");
  


  myFile.write(imageLine, sizeof(imageLine));             //actually writes the array to the file
  myFile.close();                                         //closes the file

  Serial.println("Screen captured and saved as BMP");
  delay(300);
}

void drawSolidColorWheel(int x, int y, int radius, int layers) {
  for (int layer = 0; layer < layers; layer++) {
    int currentRadius = radius - layer; // Adjust radius for the current layer
    for (int angle = 0; angle < 360; angle++) {
      int hue = map(angle, 0, 359, 0, 255); // Full hue range
      int saturation = 255; // Max saturation
      int value = map(layer, 0, layers - 1, 255, 0); // Gradually decrease brightness per layer

      // Ensure value stays within valid range
      value = constrain(value, 0, 255);

      int color = hsvToRgb565(hue, saturation, value);

      float radians = angle * PI / 180.0;

      // Calculate px and py based on the currentRadius for the inward effect
      int px = x + int(currentRadius * cos(radians));
      int py = y + int(currentRadius * sin(radians));

      tft.drawPixel(px, py, color);
    }
  }
}

int getColorAtCoordinate(int centerX, int centerY, int radius, int x, int y) {
  int dx = x - centerX;
  int dy = y - centerY;
  float angle = atan2(dy, dx) * 180.0 / PI;
  if (angle < 0) {
    angle += 360;
  }
  
  // Calculate the angle offset to match the orientation of the color wheel
  // The offset depends on how your color wheel is drawn
  float angleOffset = -60.0; // Adjust this based on your color wheel's orientation
  
  int hue = int(angle + angleOffset) % 360;
  int saturation = 255; // Max saturation
  int value = 255; // Max value (brightness)
  
  return hsvToRgb565(hue, saturation, value);
}

int changeColorBrightness(int minY, int maxY, int y, int color) {
  // Calculate the brightness percentage based on y position//50,150
  int brightnessPercentage;

  if (y <= (minY + maxY) / 2) {
    // Calculate brightness for the top half (lightening)
    brightnessPercentage = map(y, minY, (minY + maxY) / 2, 100, 200);//
  } else {
    // Calculate brightness for the bottom half (darkening)
    brightnessPercentage = map(y, (minY + maxY) / 2, maxY, 100, 0);//
  }

  // Adjust brightness while keeping the color within valid range
  int adjustedColor = adjustBrightness(color, brightnessPercentage);
  
  return adjustedColor;
}

uint16_t adjustBrightness(uint16_t color, int brightnessPercentage) {
  // Extract red, green, and blue components from RGB565 color
  int red = (color >> 11) & 0x1F;
  int green = (color >> 5) & 0x3F;
  int blue = color & 0x1F;

  // Calculate adjusted values for each color channel
  red = int(red * brightnessPercentage / 100);
  green = int(green * brightnessPercentage / 100);
  blue = int(blue * brightnessPercentage / 100);

  // Ensure values stay within the valid range (0-31 for each channel)
  red = constrain(red, 0, 31);
  green = constrain(green, 0, 63);
  blue = constrain(blue, 0, 31);

  // Combine color channels back into RGB565 format
  return (red << 11) | (green << 5) | blue;
}

uint16_t hsvToRgb565(int h, int s, int v) {
  int r, g, b;
  int i = (h * 6) / 256;
  int f = (h * 6) % 256;
  int p = (v * (256 - s)) / 256;
  int q = (v * (256 - f * s / 256)) / 256;
  int t = (v * (256 - (256 - f) * s / 256)) / 256;

  switch (i) {
    case 0: r = v; g = t; b = p; break;
    case 1: r = q; g = v; b = p; break;
    case 2: r = p; g = v; b = t; break;
    case 3: r = p; g = q; b = v; break;
    case 4: r = t; g = p; b = v; break;
    case 5: r = v; g = p; b = q; break;
  }

  // Convert RGB888 to RGB565
  return (((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}


/***************************************************************************************************
D  I  S  P  L  A  Y  I  N  G      2 BIT      I  M  A  G  E  S
***************************************************************************************************/
void zeroImageArray(){
  //We need to zero the image array before we do anything to it, this is because we need it to be all zeros for when we begin to add data
  //and bitshift and & for the decoding/encoding of the 2Bit bmp~esque image file
  for (int oo = 0; oo< sizeof(imageLine); oo++){
    imageLine[oo]=0;
  }
  Serial.println("Image array cleared");

  memcpy(&imageLine[19200], &drawColor1, sizeof(drawColor1));
  memcpy(&imageLine[19204], &drawColor2, sizeof(drawColor2));
  memcpy(&imageLine[19208], &drawColor3, sizeof(drawColor3));
  memcpy(&imageLine[19212], &drawColor4, sizeof(drawColor4));
  Serial.println("color1: " + String(drawColor1) + "\t color2: " + String(drawColor2) + "\t color3: " + String(drawColor3) + "\t color4: " + String(drawColor4));
  // for (int sdf = 19200; sdf<sizeof(imageLine); sdf += 4)  {
  //   uint32_t tempp = 0;
  //   memcpy(&tempp, &imageLine[sdf], sizeof(tempp));
  //   Serial.println(String(sdf) + "\t" + String(tempp));
  // }
}

void readSD2bitImage(const char *filename){

  if (!SD.begin(SD_CS, spiSD)) {
    Serial.println("initialization failed!");
    tft.drawString("no SD",0, 0, 2);
    return;
  }
  Serial.println("initialization done.");

  File myFile = SD.open(filename, FILE_READ);
  if (!myFile) {
    Serial.println("Error opening file");
    tft.drawString("no File",0, 0, 2);
    return;
  }


  for (uint16_t cc = 0; cc<sizeof(imageLine); cc++){
    byte buffer;
    if (myFile.read(&buffer, sizeof(buffer)) == sizeof(buffer)) {
      imageLine[cc] = buffer;
      //Serial.println(String(buffer, HEX));
    }
    else {
    Serial.println("Error reading from file or old file type");
    //tft.drawString("error",0, 0, 2);    
    break;
  }
  }

  if (sizeof(imageLine) > 19200){
    memcpy(&drawColor1, &imageLine[19200], sizeof(drawColor1));
    memcpy(&drawColor2, &imageLine[19204], sizeof(drawColor2));
    memcpy(&drawColor3, &imageLine[19208], sizeof(drawColor3));
    memcpy(&drawColor4, &imageLine[19212], sizeof(drawColor4));
  }
  else if (sizeof(imageLine) <= 19200){//legacy support for simple three color
    drawColor1 = TFT_WHITE;
    drawColor2 = TFT_RED;
    drawColor3 = TFT_BLUE;
    drawColor4 = TFT_BLACK;
  }
  
  Serial.println("color1: " + String(drawColor1) + "\t color2: " + String(drawColor2) + "\t color3: " + String(drawColor3) + "\t color4: " + String(drawColor4));
  // for (int sdf = 19000; sdf<sizeof(imageLine); sdf += 4)  {
  //   uint32_t tempp = 0;
  //   memcpy(&tempp, &imageLine[sdf], sizeof(tempp));
  //   Serial.println(String(sdf) + "\t" + String(tempp));
  // }

  
  Serial.println("file read");
}

void display2bitImage(){
  //tft.pushImage();
  
  for (int y=239; y>=0; y--){
    int fourCounter=0;
    for (int x = 0; x<320; x++){
      if ((x%4)==0){
        fourPixels = imageLine[((320*(239-y)) + x)/4];
        fourCounter=0;
      }
      else
        fourCounter++;
        
      
      byte tempPixel = (fourPixels >> (fourCounter*2)) & 3;
      if (tempPixel == 3)
        tempPixels[fourCounter]= drawColor1;
      else if (tempPixel == 2)
        tempPixels[fourCounter]= drawColor2;
      else if (tempPixel == 1)
        tempPixels[fourCounter]= drawColor3;
      else
        tempPixels[fourCounter]= drawColor4;  
              
      tft.drawPixel(x, y, tempPixels[fourCounter]);
      
      //Serial.print("x "+String(x)+"  y "+String(y) + "  z " + String(fourCounter)+ "  fourPixels " + String(fourPixels) + "  tempPixel " + String(tempPixel)+"actual color" + String(tempPixels[fourCounter])+ "\n\n");          
    }
  }
  
  Serial.println("displayed image from memory");
}


/***************************************************************************************************
D  I  S  P  L  A  Y  I  N  G      24 BIT      I  M  A  G  E  S
***************************************************************************************************/
void drawBmp24(const char *filename, int16_t x, int16_t y) {

  if ((x >= tft.width()) || (y >= tft.height())) return;

  // fs::File bmpFS;

  // // Open requested file on SD card
  // bmpFS = SPIFFS.open(filename, "r");

  Serial.print("Initializing SD card...");

  if (!SD.begin(SD_CS, spiSD)) {
    Serial.println("initialization failed!");
    tft.drawString("no SD",x, y, 2);
    return;
  }
  Serial.println("initialization done.");

  File myFile = SD.open(filename, FILE_READ);
  if (!myFile)
  {
    Serial.print("File not found");
    tft.drawString("no File",x, y, 2);
    return;
  }


  uint32_t seekOffset;
  uint16_t w, h, row, col;
  uint8_t  r, g, b;

  uint32_t startTime = millis();

  if (read16(myFile) == 0x4D42)
  {
    read32(myFile);
    read32(myFile);
    seekOffset = read32(myFile);
    read32(myFile);
    w = read32(myFile);
    h = read32(myFile);

    if ((read16(myFile) == 1) && (read16(myFile) == 24) && (read32(myFile) == 0))
    {
      y += h - 1;

      bool oldSwapBytes = tft.getSwapBytes();
      tft.setSwapBytes(true);
      myFile.seek(seekOffset);

      uint16_t padding = (4 - ((w * 3) & 3)) & 3;
      uint8_t lineBuffer[w * 3 + padding];

      for (row = 0; row < h; row++) {
        
        myFile.read(lineBuffer, sizeof(lineBuffer));
        uint8_t*  bptr = lineBuffer;
        uint16_t* tptr = (uint16_t*)lineBuffer;
        // Convert 24 to 16 bit colours
        for (uint16_t col = 0; col < w; col++)
        {
          b = *bptr++;
          g = *bptr++;
          r = *bptr++;
          *tptr++ = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
        }

        // Push the pixel row to screen, pushImage will crop the line if needed
        // y is decremented as the BMP image is drawn bottom up
        tft.pushImage(x, y--, w, 1, (uint16_t*)lineBuffer);
      }
      tft.setSwapBytes(oldSwapBytes);
     // Serial.print("Loaded in "); Serial.print(millis() - startTime);
     // Serial.println(" ms");
    }
    else{
      Serial.println("BMP format not recognized.");
      tft.drawString("??",x, y, 2);
    } 
  }
  myFile.close();
}

uint16_t read16(fs::File &f) {
  uint16_t result;
  ((uint8_t *)&result)[0] = f.read(); // LSB
  ((uint8_t *)&result)[1] = f.read(); // MSB
  return result;
}

uint32_t read32(fs::File &f) {
  uint32_t result;
  ((uint8_t *)&result)[0] = f.read(); // LSB
  ((uint8_t *)&result)[1] = f.read();
  ((uint8_t *)&result)[2] = f.read();
  ((uint8_t *)&result)[3] = f.read(); // MSB
  return result;
}

void drawBmp32(const char *filename, int16_t x, int16_t y) {
  File bmpFile = SD.open(filename, FILE_READ);

  if (!bmpFile) {
    Serial.println("File not found");
    return;
  }
  if (bmpFile.size() < 54) {
    Serial.println("Invalid BMP file");
    bmpFile.close();
    return;
  }

  // Read and verify the BMP header
  uint8_t header[54];
  bmpFile.read(header, sizeof(header));

  // Check if the header indicates a valid BMP format
  if (header[0] == 'B' && header[1] == 'M') {
    // Extract the image data offset from the header (bytes 10-13)
    uint32_t imageDataOffset;
    memcpy(&imageDataOffset, header + 10, sizeof(imageDataOffset));

    // Seek to the beginning of the image data
    bmpFile.seek(imageDataOffset);

    // Read the image width and height from the header (bytes 18-21 and 22-25)
    uint32_t width;
    uint32_t height;
    memcpy(&width, header + 18, sizeof(width));
    memcpy(&height, header + 22, sizeof(height));

    // Calculate padding for each row (if needed) (for even multiples of 4 bytes of data)
    int rowPadding = (4 - (width * 4) % 4) % 4;

    // Loop through each row and column of the image, extracting pixel data
    // and drawing pixels based on alpha values.
    for (int row = height - 1; row >= 0; row--) {
      for (int col = 0; col < width; col++) {
        uint32_t color;
        bmpFile.read((uint8_t *)&color, sizeof(color));

        // Extract the alpha channel (bits 24-31)
        uint8_t alpha = (color >> 24) & 0xFF;

        // Check if the pixel has any alpha (skip if alpha is 0)
        if (alpha != 0) {
          // Extract the RGB components (bits 16-23, 8-15, 0-7)
          uint8_t red = (color >> 16) & 0xFF;
          uint8_t green = (color >> 8) & 0xFF;
          uint8_t blue = color & 0xFF;

          // Draw the pixel at the specified position
          tft.drawPixel(x + col, y + row, tft.color565(red, green, blue));
        }
      }

      // Skip any row padding
      if (rowPadding > 0) {
        bmpFile.seek(bmpFile.position() + rowPadding);
      }
    }
  } else {
    Serial.println("BMP format not recognized.");
  }

  // Close the BMP file
  bmpFile.close();
}



// Helper function to read a 32-bit integer from a file
// uint32_t read322(File &file) {
//   uint32_t result;
//   file.read((uint8_t *)&result, sizeof(result));
//   return result;
// }


/***************************************************************************************************
N  T  P      T  I  M  E
***************************************************************************************************/
void ntpUpdate(){
  timeClient.update();
  epochTime = timeClient.getEpochTime();
  daysSinceStart = (epochTime - 1692080000)/86400;//1687392472
  Serial.println(epochTime);
  Serial.println(daysSinceStart);
}


/***************************************************************************************************
C  L  O  U  D      S  T  O  R  A  G  E      S  E  R  V  E  R
***************************************************************************************************/
bool isCloudWorking(){
  Serial.println("Seeing if cloud is Working");
  bool isWorking = true;
  
  WiFiClient client;
  HTTPClient http;
  
  String url = "https://firestore.googleapis.com/v1/projects/" + String(projectID) + "/databases/(default)/documents/" + String(mycollection) + "/" + String(emojiDocumentID);
  
  if (http.begin(url)) {
    int httpResponseCode = http.GET();
    if (httpResponseCode == 200) {
    isWorking = true;
    } else {
      Serial.print("GET Error: ");
      Serial.println(httpResponseCode);
      isWorking = false;
    }
  
    http.end();
    client.stop();
  } else {
    isWorking = false;
    Serial.println("Failed to connect to server");
  }
  return isWorking;
}

void postCloudDrawing(int filename){
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  
  ////for (int itterationChunk=0; itterationChunk<(sizeof(imageLine)/chunkSize); itterationChunk++){
    

  
    ////fileField = "string" + String(itterationChunk);
    String url = "https://firestore.googleapis.com/v1/projects/" + String(projectID) + "/databases/(default)/documents/" + String(mycollection) + "/" + String(fileDocumentID) + "?updateMask.fieldPaths=" + String(fileField);
    
    massiveSendString = "";
    base64Encode(imageLine,sizeof(imageLine),0);//sizeof(imageLine)
    Serial.println("massiveSendString length: " + String(massiveSendString.length()));
    massiveSendString = "{\"fields\": {\"" + String(fileField) + "\": {\"stringValue\": \"" + String(filename) + massiveSendString + "\"}}}";

    
    if (http.begin(client, url)) {
      http.addHeader("Content-Type", "application/json");

      int httpResponseCode = http.PATCH(massiveSendString);
    
      // Check the response status
      if (httpResponseCode == 200) {
        String response = http.getString();
        Serial.println("POST response:");
        //Serial.println(response);
      } else {
        Serial.print("POST Error: ");
        Serial.println(httpResponseCode);
      }
    
      
    } else {
      Serial.println("Failed to connect to server");
    }
  ////}   
  massiveSendString = "";
  http.end();
  client.stop(); 

  // Serial.println("post cloud drawing image array");
  // for (int sdf = 19000; sdf<sizeof(imageLine); sdf += 4)  {
  //   uint32_t tempp = 0;
  //   memcpy(&tempp, &imageLine[sdf], sizeof(tempp));
  //   Serial.println(String(sdf) + "\t" + String(tempp));
  // }
}

void postCloudEmoji(String emojiToSend){
  
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  
  String url = "https://firestore.googleapis.com/v1/projects/" + String(projectID) + "/databases/(default)/documents/" + String(mycollection) + "/" + String(emojiDocumentID) + "?updateMask.fieldPaths=" + String(emojiField);

  ntpUpdate();      //I'm putting in the epoch time before the emoji just because the emoji won't show as updated if you send the same one twice in a row
  String data = "{\"fields\": {\"" + String(emojiField) + "\": {\"stringValue\": \"" + String(epochTime) + emojiToSend + "\"}}}";


  // Perform the POST request
  
  if (http.begin(client, url)) {
    http.addHeader("Content-Type", "application/json");

    int httpResponseCode = http.PATCH(data);
  
    // Check the response status
    if (httpResponseCode == 200) {
      String response = http.getString();
      Serial.println("POST response:");
      Serial.println(response);
    } else {
      Serial.print("POST Error: ");
      Serial.println(httpResponseCode);
    }
  
    // Cleanup
    http.end();
    client.stop();
  } else {
    Serial.println("Failed to connect to server");
  }
  
}

void getCloudEmoji(){
  Serial.println("Seeing if cloud has EMOJI data");
  
  WiFiClient client;
  HTTPClient http;

  String url = "https://firestore.googleapis.com/v1/projects/" + String(projectID) + "/databases/(default)/documents/" + String(theircollection) + "/" + String(emojiDocumentID);
  
  if (http.begin(url)) {
    int httpResponseCode = http.GET();
    if (httpResponseCode == 200) {
      String response = http.getString();
      Serial.println("GET response:");
      //Serial.println(response);//
      
      String tempEmojiTimestamp = "";
      int stringValueIndex = response.indexOf("\"updateTime\":");
      if (stringValueIndex != -1) {
        int startIndex = stringValueIndex + 15;
        int endIndex = response.indexOf("\"", startIndex);
        if (endIndex != -1) {
          tempEmojiTimestamp = response.substring(startIndex, endIndex);
          //Serial.println("Emoji Timestamp: " + tempEmojiTimestamp);//
        }        
      }

      int gotEmojiTimestamp = convertToNtpEpochTime(tempEmojiTimestamp);
      Serial.println("gotEmojiTimestamp: " + String(gotEmojiTimestamp));
      if (gotEmojiTimestamp > newEmojiTimestamp){
        Serial.println("New Emoji Alert");
        
        int stringValueIndex2 = response.indexOf("\"stringValue\":");
        if (stringValueIndex2 != -1) {
          int startIndex2 = stringValueIndex2 + 16 + (String(epochTime)).length();
          int endIndex2 = response.indexOf("\"", startIndex2);
          if (endIndex2 != -1) {
            newEmojiNumber = response.substring(startIndex2, endIndex2);
            Serial.println("Emoji String Value: " + newEmojiNumber);
          }        
        }
        
        newEmoji = true;
        setLastEmojiDrawingTimestamp(gotEmojiTimestamp,-1,-1);
        saveDataLog(String(gotEmojiTimestamp),1);
      }
        
         
    } else {
      Serial.print("GET Error: ");
      Serial.println(httpResponseCode);
    }
  
    http.end();
    client.stop();
  } else {
    Serial.println("Failed to connect to server");
  }
  
}

void getCloudDrawing(){
  Serial.println("Seeing if cloud has DRAWING data");
  
  WiFiClient client;
  HTTPClient http;
  
  String url = "https://firestore.googleapis.com/v1/projects/" + String(projectID) + "/databases/(default)/documents/" + String(theircollection) + "/" + String(fileDocumentID) + "?mask.fieldPaths=" + String(fileField);


  if (http.begin(url)) {
    int httpResponseCode = http.GET();
  
    // Check the response status
    if (httpResponseCode == 200) {
      //String response = http.getString();
      massiveSendString = http.getString();
      Serial.println("GET response:");
      ////Serial.println(response);//massiveSendString

      String tempDrawingTimestamp = "";
      int stringValueIndex = massiveSendString.indexOf("\"updateTime\":");
      if (stringValueIndex != -1) {
        int startIndex = stringValueIndex + 15;
        int endIndex = massiveSendString.indexOf("\"", startIndex);
        if (endIndex != -1) {
          tempDrawingTimestamp = massiveSendString.substring(startIndex, endIndex);
          //Serial.println("Drawing Timestamp: " + tempDrawingTimestamp);
        }        
      }
      
      int gotDrawingTimestamp = convertToNtpEpochTime(tempDrawingTimestamp);
      Serial.println("gotDrawingTimestamp: " + String(gotDrawingTimestamp));
      String parsedFilename = String(gotDrawingTimestamp);
      if (gotDrawingTimestamp > newDrawingTimestamp){
        Serial.println("New Drawing Alert");
        
        // Parse the response and extract the string value
        int stringIndex2 = massiveSendString.indexOf("\"string\":");
        if (stringIndex2 != -1) {
          int stringValueIndex2 = massiveSendString.indexOf("\"stringValue\":", stringIndex2);
          if (stringValueIndex2 != -1) {
            int startIndex2 = stringValueIndex2 + 16;
            int endIndex2 = massiveSendString.indexOf("\"", startIndex2);
            if (endIndex2 != -1) {
              //Serial.println("1");
              massiveSendString = massiveSendString.substring(startIndex2, endIndex2);//this string is big! make sure to reserve the space for it in setup
              parsedFilename = massiveSendString.substring(0,parsedFilename.length());
              Serial.println("parsedFilename: " + parsedFilename);
              massiveSendString = massiveSendString.substring(parsedFilename.length());
              // Serial.println("response length: " + String(response.length()));
              // int quarterOfTheWay = ((endIndex2 - startIndex2) / 4);              
              // Serial.println("startIndex2: " + String(startIndex2));
              // Serial.println("endIndex2: " + String(endIndex2));
              // Serial.println("quarterOfTheWay: " + String(quarterOfTheWay));
              // massiveSendString = response.substring(startIndex2,(quarterOfTheWay+startIndex2));
              // Serial.println("massiveSendString length: " + String(massiveSendString.length()));
              // Serial.println("response length: " + String(response.length()));
              // delay(20);
              // massiveSendString = massiveSendString + response.substring((quarterOfTheWay+startIndex2),((quarterOfTheWay*2)+startIndex2));
              // Serial.println("massiveSendString length: " + String(massiveSendString.length()));
              // delay(20);
              // massiveSendString = massiveSendString + response.substring(((quarterOfTheWay*2)+startIndex2),((quarterOfTheWay*3)+startIndex2));
              // Serial.println("massiveSendString length: " + String(massiveSendString.length()));
              // delay(20);
              // massiveSendString = massiveSendString + response.substring(((quarterOfTheWay*3)+startIndex2),endIndex2);
              // Serial.println("massiveSendString length: " + String(massiveSendString.length()));
              // delay(20);        
              //Serial.println("2");
              ////Serial.println("massiveSendString: " + massiveSendString);
              Serial.println("massiveSendString length: " + String(massiveSendString.length()));
            }
          }
        }
        
        //massiveSendString = "";
        newDrawingName = "/recieveddrawings/" + parsedFilename + ".bmp";//String(gotDrawingTimestamp)
        zeroImageArray();
        //Serial.println(massiveSendString);
        base64Decode(massiveSendString, imageLine, 0, sizeof(imageLine));
        //for (int oppa=0; oppa<500; oppa++){Serial.print(imageLine[oppa]);}
        captureScreenToBMP(newDrawingName.c_str());
        Serial.println("massiveSendString length (after decoding): " + String(massiveSendString.length()));
        massiveSendString = "";
        
        newDrawing = true;
        setLastEmojiDrawingTimestamp(-1,gotDrawingTimestamp,-1);
        saveDataLog(parsedFilename,0);//
      }  
      
    } else {
      Serial.print("GET Error: ");
      Serial.println(httpResponseCode);
    }
  
    // Cleanup
    http.end();
    client.stop();
  } else {
    Serial.println("Failed to connect to server");
  }
  
}

void postCloudMessage(String messageToSend){
  
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  
  String url = "https://firestore.googleapis.com/v1/projects/" + String(projectID) + "/databases/(default)/documents/" + String(mycollection) + "/" + String(messageDocumentID) + "?updateMask.fieldPaths=" + String(messageField);

  ntpUpdate();      //I'm putting in the epoch time before the emoji just because the emoji won't show as updated if you send the same one twice in a row
  String data = "{\"fields\": {\"" + String(messageField) + "\": {\"stringValue\": \"" + String(epochTime) + String(messageToSend) + "\"}}}";


  // Perform the POST request
  
  if (http.begin(client, url)) {
    http.addHeader("Content-Type", "application/json");

    int httpResponseCode = http.PATCH(data);
  
    // Check the response status
    if (httpResponseCode == 200) {
      String response = http.getString();
      Serial.println("POST response:");
      Serial.println(response);
    } else {
      Serial.print("POST Error: ");
      Serial.println(httpResponseCode);
    }
  
    // Cleanup
    http.end();
    client.stop();
  } else {
    Serial.println("Failed to connect to server");
  }
  
}

void getCloudMessage(){
  Serial.println("Seeing if cloud has MESSAGE data");
  
  WiFiClient client;
  HTTPClient http;

  String url = "https://firestore.googleapis.com/v1/projects/" + String(projectID) + "/databases/(default)/documents/" + String(theircollection) + "/" + String(messageDocumentID);
  
  if (http.begin(url)) {
    int httpResponseCode = http.GET();
    if (httpResponseCode == 200) {
      String response = http.getString();
      //Serial.println("GET response:");
      //Serial.println(response);//
      
      String tempMessageTimestamp = "";
      int stringValueIndex = response.indexOf("\"updateTime\":");
      if (stringValueIndex != -1) {
        int startIndex = stringValueIndex + 15;
        int endIndex = response.indexOf("\"", startIndex);
        if (endIndex != -1) {
          tempMessageTimestamp = response.substring(startIndex, endIndex);
          //Serial.println("Message Timestamp: " + tempMessageTimestamp);//
        }        
      }

      int gotMessageTimestamp = convertToNtpEpochTime(tempMessageTimestamp);
      Serial.println("gotMessageTimestamp: " + String(gotMessageTimestamp));
      if (gotMessageTimestamp > newMessageTimestamp){
        Serial.println("New Message Alert");
        
        int stringValueIndex2 = response.indexOf("\"stringValue\":");
        if (stringValueIndex2 != -1) {
          int startIndex2 = stringValueIndex2 + 16 + (String(epochTime)).length();
          int endIndex2 = response.indexOf("\"", startIndex2);
          if (endIndex2 != -1) {
            messageTextString = response.substring(startIndex2, endIndex2);
            Serial.println("Message String Value: " + messageTextString);
          }        
        }
        
        newMessage  = true;
        setLastEmojiDrawingTimestamp(-1,-1,gotMessageTimestamp);
        saveDataLog(String(gotMessageTimestamp),2);
      }
        
         
    } else {
      Serial.print("GET Error: ");
      Serial.println(httpResponseCode);
    }
  
    http.end();
    client.stop();
  } else {
    Serial.println("Failed to connect to server");
  }
  
}

void printIncomingData(){
  
  if (newEmoji){
    newEmoji = false;
    tft.fillScreen(TFT_WHITE);

    
    if (newEmojiNumber.length() < 3){//if there's just one emoji
      String currentFile = "";
      currentFile="/logos/emoji"+newEmojiNumber.substring(0,2)+".bmp";
      drawBmp24(currentFile.c_str(),130,90);
      delay(500);
    }
    else if (newEmojiNumber.length() < 5){
      String currentFile = "/logos/emoji"+newEmojiNumber.substring(0,2)+".bmp";
      drawBmp24(currentFile.c_str(),60,90);//130 200
      currentFile = "/logos/emoji"+newEmojiNumber.substring(2,4)+".bmp";
      drawBmp24(currentFile.c_str(),130,90);
      delay(500);
    }
    else {
      String currentFile = "/logos/emoji"+newEmojiNumber.substring(0,2)+".bmp";
      drawBmp24(currentFile.c_str(),60,90);//130 200
      currentFile = "/logos/emoji"+newEmojiNumber.substring(2,4)+".bmp";
      drawBmp24(currentFile.c_str(),130,90);
      currentFile = "/logos/emoji"+newEmojiNumber.substring(4,6)+".bmp";
      drawBmp24(currentFile.c_str(),200,90);
      delay(500);
    }
    
    bool stayOnScreen = false;
    while (!stayOnScreen){
      uint16_t x = 0, y = 0;
      bool pressed = tft.getTouch(&x, &y);
      if (pressed)
        stayOnScreen = true;
    }   
  }

  if (newDrawing){
    newDrawing = false;
    tft.fillScreen(TFT_WHITE);
    zeroImageArray();
    readSD2bitImage(newDrawingName.c_str());
    display2bitImage();
    delay(500);
    
    bool stayOnScreen2 = false;
    while (!stayOnScreen2){
      uint16_t x2 = 0, y2 = 0;
      bool pressed2 = tft.getTouch(&x2, &y2);
      if (pressed2)
        stayOnScreen2 = true;
    }   
  }

  if (newMessage){
    newMessage = false;
    tft.fillScreen(TFT_WHITE);

    //Serial.println(messageTextString);
    decodeUnicodeEscapeInPlace();
    Serial.println(messageTextString);
    String line1, line2, line3;
    int delimiterIdx1 = messageTextString.indexOf("'");
    if (delimiterIdx1 != -1) {
        line1 = messageTextString.substring(0, delimiterIdx1);
        int delimiterIdx2 = messageTextString.indexOf("'", delimiterIdx1 + 1);
        if (delimiterIdx2 != -1) {
            line2 = messageTextString.substring(delimiterIdx1 + 1, delimiterIdx2);
            line3 = messageTextString.substring(delimiterIdx2 + 1);
        } else {
            line2 = messageTextString.substring(delimiterIdx1 + 1);
        }
    } else {
        line1 = messageTextString;
    }
    Serial.println(line1 + "\n" + line2 + "\n" + line3);

    tft.setTextDatum(TC_DATUM);
    tft.setTextSize(2);
    tft.fillScreen(TFT_BLACK);
    tft.drawString(line1,160,50,1);
    tft.drawString(line2,160,120,1);
    tft.drawString(line3,160,190,1);  
    tft.setTextDatum(TL_DATUM);
    tft.setTextSize(1);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);

    delay(500);
    bool stayOnScreen = false;
    while (!stayOnScreen){
      uint16_t x = 0, y = 0;
      bool pressed = tft.getTouch(&x, &y);
      if (pressed)
        stayOnScreen = true;
    }   
  }

}

void saveDataLog(String timestamp, int type){
  //0 is drawing, 1 is emoji, 2 is message
  if (!SD.exists("/cloud_log.txt")){
    File myFile = SD.open("/cloud_log.txt", FILE_WRITE);
    myFile.close(); //if there is no log go ahead an create one
  } 
  File myFile = SD.open("/cloud_log.txt", FILE_APPEND);

  // if the file opened okay, write to it:
  if (myFile) {
    Serial.println("Saveing log");
    myFile.println();
    myFile.println(convertNtpToHumanTime(timestamp));
    //myFile.println(timestamp);
    if (type==0){
      myFile.println("File");
      myFile.println(timestamp);
    }
    if (type==1){
      myFile.println("Emoji");
      myFile.println(newEmojiNumber);
    }
    if (type==2){
      myFile.println("Message");
      myFile.println(messageTextString);
    }
    
    
    myFile.println();    
  } else {
    // if the file didn't open, print an error:
    Serial.println("error saveing the log");
  }

  myFile.close(); 
}

void decodeUnicodeEscapeInPlace(){
    String decodedString;
    int length = messageTextString.length();

    for (int readIndex = 0; readIndex < length; ) {
        if (messageTextString[readIndex] == '\\' && messageTextString[readIndex + 1] == 'u') {
            // Found a Unicode escape sequence
            String hex = messageTextString.substring(readIndex + 2, readIndex + 6);
            int codepoint = strtol(hex.c_str(), nullptr, 16);

            // Replace the escape sequence with the actual character
            decodedString += static_cast<char>(codepoint);

            // Move read index past the escape sequence
            readIndex += 6;
        } else {
            // Copy the character as is
            decodedString += messageTextString[readIndex++];
        }
    }

    // Assign the modified string back to myString
    messageTextString = decodedString;
}

void base64Encode(const uint8_t* data, size_t length, size_t startIndex) {
  //base64 is lossless and has a 4:3 ratio from raw data of space (bulkier than raw y ~33%)
  Serial.print("Encoding Drawing");
  const char base64Chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

  size_t i = startIndex;
  while (i < length) {
    uint32_t octetA = i < length ? data[i++] : 0;
    uint32_t octetB = i < length ? data[i++] : 0;
    uint32_t octetC = i < length ? data[i++] : 0;

    uint32_t triple = (octetA << 16) | (octetB << 8) | octetC;

    massiveSendString += base64Chars[(triple >> 18) & 63];
    massiveSendString += base64Chars[(triple >> 12) & 63];
    massiveSendString += base64Chars[(triple >> 6) & 63];
    massiveSendString += base64Chars[triple & 63];
  }

  // Handle padding characters
  size_t paddingCount = (3 - length % 3) % 3;

  // Append the padding characters
  for (size_t i = 0; i < paddingCount; i++) {
    massiveSendString += '=';
  }
  
  // if (length % 3 == 1) {
  //   massiveSendString[massiveSendString.length() - 1] = '=';
  //   massiveSendString[massiveSendString.length() - 2] = '=';
  // } else if (length % 3 == 2) {
  //   massiveSendString[massiveSendString.length() - 1] = '=';
  // }

}

void base64Decode(const String& encodedString, uint8_t* outputData, size_t outputStartIndex, size_t outputLength) {
  Serial.print("Decoding Drawing");
  const char base64Chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

  size_t encodedLength = encodedString.length();
  size_t dataIndex = 0;
  size_t outputIndex = outputStartIndex;

  while (dataIndex < encodedLength && outputIndex < outputLength) {
    uint32_t quadruple = 0;
    size_t paddingCount = 0;

    for (int i = 0; i < 4; i++) {
      char c = encodedString[dataIndex++];
      if (c == '=') {
        paddingCount++;
        break;
      }

      const char* position = strchr(base64Chars, c);
      if (position == nullptr) {
        // Invalid base64 character, handle error or skip
        Serial.println("Invalid base64 character" );
        continue;
      }

      uint32_t value = position - base64Chars;
      quadruple = (quadruple << 6) | value;
    }

    if (paddingCount < 2) {
      outputData[outputIndex++] = static_cast<uint8_t>((quadruple >> 16) & 0xFF);
      if (paddingCount < 1) {
        outputData[outputIndex++] = static_cast<uint8_t>((quadruple >> 8) & 0xFF);
        outputData[outputIndex++] = static_cast<uint8_t>(quadruple & 0xFF);
      }
    }   
  }
}

void setLastEmojiDrawingTimestamp(int emojiTimestamp, int drawingTimestamp, int messageTimestamp){
  if (emojiTimestamp > 0){
    EEPROM.put(400, emojiTimestamp);      //use put for larger pieces of data
    EEPROM.commit();
    Serial.println("Actually changing emojiTimestamp");
  }
  if (drawingTimestamp > 0){
    EEPROM.put(410, drawingTimestamp);
    EEPROM.commit();
    Serial.println("Actually changing drawingTimestamp");
  }
  if (messageTimestamp > 0){
    EEPROM.put(420, messageTimestamp);
    EEPROM.commit();
    Serial.println("Actually changing messageTimestamp");
  }
  EEPROM.get(400,newEmojiTimestamp);   //use get for larger pieces of data
  Serial.println("Last Emoji: " + String(newEmojiTimestamp));
  EEPROM.get(410,newDrawingTimestamp);
  Serial.println("Last Drawing: " + String(newDrawingTimestamp));
  EEPROM.get(420,newMessageTimestamp );
  Serial.println("Last Drawing: " + String(newMessageTimestamp ));
}

int convertToNtpEpochTime(const String& timestamp) {
  struct tm tm;
  memset(&tm, 0, sizeof(struct tm));

  // Parse the timestamp string
  sscanf(timestamp.c_str(), "%d-%d-%dT%d:%d:%d",
         &tm.tm_year, &tm.tm_mon, &tm.tm_mday,
         &tm.tm_hour, &tm.tm_min, &tm.tm_sec);

  // Adjust the year, month, and day values
  tm.tm_year -= 1900;
  tm.tm_mon -= 1;

  // Print the parsed values for troubleshooting
    // Serial.print("Parsed timestamp: ");
    // Serial.print(tm.tm_year + 1900);
    // Serial.print("-");
    // Serial.print(tm.tm_mon + 1);
    // Serial.print("-");
    // Serial.print(tm.tm_mday);
    // Serial.print(" ");
    // Serial.print(tm.tm_hour);
    // Serial.print(":");
    // Serial.print(tm.tm_min);
    // Serial.print(":");
    // Serial.println(tm.tm_sec);

  // Calculate the NTP epoch time
  // time_t epochTime = mktime(&tm);
  // unsigned long ntpEpochTime = static_cast<unsigned long>(epochTime) + 2208988800UL;
  int ntpEpochTime = static_cast<int>(mktime(&tm));

  return ntpEpochTime;
}

String convertNtpToHumanTime(const String& ntpTimeString) {
    // Convert the input string to an unsigned long
    unsigned long ntpTime = strtoul(ntpTimeString.c_str(), NULL, 10);

    // Create a human-readable time string
    String humanTime = String(year(ntpTime)) + "-" + String(month(ntpTime)) + "-" + String(day(ntpTime)) + " " +
                       String(hour(ntpTime)) + ":" + String(minute(ntpTime)) + ":" + String(second(ntpTime));

    Serial.println("NTP Epoch time: " + ntpTimeString + "\tConverted to: " + humanTime);
    return humanTime;
}


/***************************************************************************************************
T  O  O  L  S
***************************************************************************************************/
void touch_calibrate(){
  //nessacery for the first time calibration of the touch screen
  //uncomment the call in the setup, do the calibration, and then copy the numbers
  //from the serial monitor into the new setup line that's currently there
  delay(1500);
  uint16_t calData3[5];
  uint8_t calDataOK = 0;

  // Calibrate
  tft.fillScreen(TFT_BLACK);
  tft.setCursor(20, 0);
  tft.setTextFont(2);
  tft.setTextSize(1);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);

  tft.println("Touch corners as indicated");

  tft.setTextFont(1);
  tft.println();

  tft.calibrateTouch(calData3, TFT_MAGENTA, TFT_BLACK, 15);

  Serial.println(); Serial.println();
  Serial.println("// Use this calibration code for the global variable:");
  Serial.print("  uint16_t calData[5] = ");
  Serial.print("{ ");

  for (uint8_t i = 0; i < 5; i++)
  {
    Serial.print(calData3[i]);
    if (i < 4) Serial.print(", ");
  }

  Serial.println(" };");
  Serial.print("  tft.setTouch(calData3);");
  Serial.println(); Serial.println();

  tft.fillScreen(TFT_BLACK);
  
  tft.setTextColor(TFT_GREEN, TFT_BLACK);
  tft.println("Calibration complete!");
  tft.println("Calibration code sent to Serial port.");
  tft.setTextColor(TFT_WHITE, TFT_BLACK);

  tft.println("Calibration code saved to EEPROM.");
  getSetTouchCalibrate(calData3,true);
  delay(3000);
}

void getSetBrightness(int brightness){
  if (brightness > 0){
    EEPROM.write(300, brightness);
    EEPROM.commit();
    Serial.println("Actuall changing brightness");
  }
  screenBrightness = EEPROM.read(300);
  analogWrite(TFT_LED, screenBrightness);
  Serial.println("Screen Brightness: " + String(screenBrightness));
}

void getSetLEDStatus(int ledStatus, int shouldIWrite){
  if (shouldIWrite > 0){
    EEPROM.write(350, ledStatus);
    EEPROM.commit();
    Serial.println("Actuall changing led status");
  }
  ledStatus = EEPROM.read(350);
  toggleLEDs = ledStatus;
  Serial.println("LED Status (toggleLEDs): " + String(toggleLEDs));
}

void getSetTouchCalibrate(uint16_t calData2[5], bool doIChange) {
  for (int haha=0; haha<4; haha++){//this is really for on startup for if the memory is ever way off
    uint16_t tempVal = 0;
    EEPROM.get(((haha*3)+350),tempVal);
    if ((tempVal > (calData[haha]+220)) || (tempVal < (calData[haha]-220))){
      Serial.println("stored value too far from calData");
      Serial.println("index number: " + String(haha));
      Serial.println("stored " + String(tempVal) + " changed to " + String(calData[haha]));      
      EEPROM.put(((haha*3)+350),calData[haha]);   
    }
    EEPROM.commit();
  }

  for (int xaxa=0; xaxa<4; xaxa++){//this is for if you pass far off values while recalibrating
    //you may have to comment this out for the first time if your 
    //values are way different (change caldata[] at the top though)
    if ((calData2[xaxa] > (calData[xaxa]+220)) || (calData2[xaxa] < (calData[xaxa]-220))){
      Serial.println("passed value too far from calData");
      Serial.println("index number: " + String(xaxa));
      Serial.println(String(calData2[xaxa]) + " changed to " + String(calData[xaxa]));
      calData2[xaxa] = calData[xaxa]; 
    }
  }
  
  if (doIChange) {
    for (int jaja=350; jaja<362; jaja+=3){
      EEPROM.put(jaja, calData2[(jaja-350)/3]);      
    }
    EEPROM.commit();
    Serial.println("Calibration data actually changed");
  }
  
  Serial.println("Calibration stored");
  for (int hehe=350; hehe<362; hehe+=3){
    EEPROM.get(hehe,calData[(hehe-350)/3]);
    Serial.println(calData[(hehe-350)/3]);      
  }
  tft.setTouch(calData);
  //for (uint8_t i = 0; i < 5; i++){Serial.println(calData[i]);}
  
}

void SetBreakWifi(String ssid, String psw){
  
  Serial.println("clearing eeprom");
   for (int i = 0; i < 96; ++i) {
   EEPROM.write(i, 0);
   //Serial.print(EEPROM.read(i));
  } 

  Serial.println("writing eeprom ssid:");
  for (int i = 0; i < ssid.length(); ++i)
  {
    EEPROM.write(i, ssid.charAt(i));
    Serial.print(ssid.charAt(i));
  }
  Serial.println("writing eeprom pass:");
  for (int i = 0; i < psw.length(); ++i)
  {
    EEPROM.write(32 + i, psw.charAt(i));
    Serial.print(psw.charAt(i));
  }

  EEPROM.commit();
}

void deleteFilesInFolder(String folder, String secondFolder){
  
  Serial.print("Initializing SD card...");
  if (!SD.begin(SD_CS, spiSD)) {
    Serial.println("initialization failed!");
    tft.drawString("no SD",0, 0, 2);
    return;
  }
  Serial.println("initialization done.");

  
  File dir = SD.open(folder);
  if (!dir || !dir.isDirectory()) {
    Serial.println("Error opening directory");
    return;
  }

  int ifNoFilesAtAllDoSecondDir = 0;
  while (true) {
    File file = dir.openNextFile();
    if (!file) {
      // No more files in the directory
      if (ifNoFilesAtAllDoSecondDir==0){
        if (secondFolder.equals("return")){
          return;
        }
        deleteFilesInFolder(secondFolder, "return");  //dont want to go on forever if they're both empty
      }
      break;
    }
    ifNoFilesAtAllDoSecondDir++;
    
    if (file.isDirectory()) {
      // Recursively delete files in subdirectories
      deleteFilesInFolder(file.name(), "return");
      file.close();
      SD.rmdir(file.name());
    } else {
      // Delete the file
      Serial.print("Deleting: ");
      Serial.println(file.name());
      String fullFilename = folder + "/" + String(file.name());
      file.close();
      if (SD.remove(fullFilename)) {
        //Serial.println("Deleted successfully.");
      } else {
        Serial.println("Error deleting the file.");
        //Serial.println(SD.error());
      }
    }
  }
  
  dir.close();
}

void getCloudOnStartupToggle(int trueFalse, bool doIChange){
  if (doIChange){
    EEPROM.write(150, trueFalse);
    EEPROM.commit();
  }

  if (EEPROM.read(150)==0)
    getCloudOnStartup = false;
  else if (EEPROM.read(150)==1)
    getCloudOnStartup = true;
}

void clearEepromVars(){
  Serial.println("clearing ALL the eeprom");
   for (int i = 0; i < 512; ++i) {
   EEPROM.write(i, 0);           //zero out the memory
  }
  EEPROM.commit();
  
  getSetBrightness(255);//300          //restore some defaults
  getSetLEDStatus(0,1);//350
  SetBreakWifi(ssid,passphrase);//0-96
  puzzlegameScreenCheckpointSet(0);//200
  setLastEmojiDrawingTimestamp(1,1,1);//400-430
  uint16_t calData4[5]={278,3618,366,3434,1};getSetTouchCalibrate(calData4,true);//350-370
  getCloudOnStartupToggle(0, true);//150
}

void printMemoryStats(){
  //This can be called to serial print a number of memory stats for the esp8266
  //This can be useful especially for events such as stack overflows due to recursion of large variables
  //If you get a watchdog (WDT) you can call this function to see if you're leaking memory somewhere
  Serial.println("Memory Stats:");

  Serial.print("Free heap memory: ");
  Serial.print(ESP.getFreeHeap());
  Serial.println(" bytes");

  Serial.print("Free stack memory: ");
  Serial.print(uxTaskGetStackHighWaterMark(NULL) * 4); // Stack size is in words (32 bits)
  Serial.println(" bytes");

  Serial.print("Free sketch space: ");
  Serial.print(ESP.getFreeSketchSpace());
  Serial.println(" bytes");

  Serial.print("Free flash memory: ");
  Serial.print(ESP.getFlashChipSize() - ESP.getSketchSize());
  Serial.println(" bytes");

  Serial.print("Free RAM memory: ");
  Serial.print(heap_caps_get_free_size(MALLOC_CAP_8BIT));
  Serial.println(" bytes");

  Serial.print("Free EEPROM memory: ");
  Serial.print(ESP.getFlashChipSize() - ESP.getSketchSize() - heap_caps_get_free_size(MALLOC_CAP_8BIT));
  Serial.println(" bytes");

  Serial.print("Text (code) memory: ");
  Serial.print(ESP.getSketchSize());
  Serial.println(" bytes");

  Serial.print("Data memory: ");
  Serial.print(ESP.getFreeSketchSpace() - ESP.getSketchSize());
  Serial.println(" bytes");

  Serial.print("BSS memory: ");
  Serial.print(heap_caps_get_free_size(MALLOC_CAP_8BIT) - ESP.getFreeSketchSpace());
  Serial.println(" bytes");

  Serial.print("IRAM (Instruction RAM) memory: ");
  Serial.print(ESP.getFlashChipSize());
  Serial.println(" bytes");

  Serial.println();
}

void clearEEPROMRange(int startAddress, int endAddress){
  for (int address = startAddress; address < endAddress; address++) {
    EEPROM.write(address, 0); // Write zero (or any other specific value) to the address
  }
  EEPROM.commit();
}

void waitForTouch(){
  bool saveButtonPressed = false; // Flag to indicate if save button was pressed
  while (!saveButtonPressed) {
  uint16_t x = 0, y = 0;
  bool pressed = tft.getTouch(&x, &y);
    if (pressed) {
      Serial.print(String(x) + "," + String(y) + "\n");
      saveButtonPressed = true;
    }
  }   
}


/***************************************************************************************************
R  E  M  O  T  E      C  O  D  E      U  P  L  O  A  D
***************************************************************************************************/
void remoteCodeUpload(){
  // char loginIndex[sizeof(loginIndexTemplate) + sizeof(remoteCodeUploadUsername) + sizeof(remoteCodeUploadPassword)];
  // sprintf(loginIndex, loginIndexTemplate, remoteCodeUploadUsername, remoteCodeUploadPassword);
  /*use mdns for host name resolution*/
  if (!MDNS.begin(host)) { //http://esp32.local
    Serial.println("Error setting up MDNS responder!");
    while (1) {
      delay(1000);
    }
  }
  Serial.println("mDNS responder started");

  size_t loginIndexSize = strlen(loginIndexTemplate) + strlen(remoteCodeUploadUsername) + strlen(remoteCodeUploadPassword);
  char* loginIndex = new char[loginIndexSize];
  sprintf(loginIndex, loginIndexTemplate, remoteCodeUploadUsername, remoteCodeUploadPassword);
  //server1.on("/", HTTP_GET, []() {
  server1.on("/", HTTP_GET, [loginIndex]() {
  // server1.on("/", HTTP_GET, [loginIndexTemplate, remoteCodeUploadUsername, remoteCodeUploadPassword]() {
  //   char loginIndex[sizeof(loginIndexTemplate) + strlen(remoteCodeUploadUsername) + strlen(remoteCodeUploadPassword)];
  //   sprintf(loginIndex, loginIndexTemplate, remoteCodeUploadUsername, remoteCodeUploadPassword);
    server1.sendHeader("Connection", "close");
    server1.send(200, "text/html", loginIndex);
  });
  server1.on("/serverIndex", HTTP_GET, []() {
    server1.sendHeader("Connection", "close");
    server1.send(200, "text/html", serverIndex);
  });
  /*handling uploading firmware file */
  server1.on("/update", HTTP_POST, []() {
    server1.sendHeader("Connection", "close");
    server1.send(200, "text/plain", (Update.hasError()) ? "FAIL" : "OK");
    ESP.restart();
  }, []() {
    HTTPUpload& upload = server1.upload();
    if (upload.status == UPLOAD_FILE_START) {
      Serial.printf("Update: %s\n", upload.filename.c_str());
      if (!Update.begin(UPDATE_SIZE_UNKNOWN)) { //start with max available size
        Update.printError(Serial);
      }
    } else if (upload.status == UPLOAD_FILE_WRITE) {
      /* flashing firmware to ESP*/
      if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
        Update.printError(Serial);
      }
    } else if (upload.status == UPLOAD_FILE_END) {
      if (Update.end(true)) { //true to set the size to the current progress
        Serial.printf("Update Success: %u\nRebooting...\n", upload.totalSize);
      } else {
        Update.printError(Serial);
      }
    }
  });
  server1.begin();

  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
  tft.fillScreen(TFT_WHITE);
  String ipAddy = "local IP:  " + WiFi.localIP().toString();
  tft.drawString(ipAddy,160,100,1);
  
  while(WiFi.status() == WL_CONNECTED){
    server1.handleClient();
    delay(1);
  }
  
  delete[] loginIndex;
}

void remoteCodeUploadInstructions(){

  tft.setTextDatum(TC_DATUM);
  tft.setTextSize(2);

  tft.fillScreen(TFT_RED);
  tft.drawString("To stop this process,",160,50,1);
  tft.drawString("unplug NOW",120,120,1);
  tft.drawString("Do not cut power later",160,190,1);
  delay(500);waitForTouch();
  tft.fillScreen(TFT_RED);
  tft.drawString("download the .bin file",160,0,1);
  tft.drawString("onto your computer",160,70,1);
  String myBoxNumber = "you are:  " + String(mycollection);
  tft.drawString(myBoxNumber,160,140,1);
  tft.drawString("(link to a recent build:)",160,210,1);
  delay(500);waitForTouch();
  drawBmp24("/logos/qr1BinCode.bmp",0,0);
  delay(500);waitForTouch();
  tft.fillScreen(TFT_RED);
  tft.drawString("On your computer connect",160,50,1);
  tft.drawString("to the given IP adress",160,120,1);
  delay(500);waitForTouch();
  tft.fillScreen(TFT_RED);
  tft.drawString("The Username & Password",160,50,1);
  tft.drawString("are \""+String(remoteCodeUploadUsername)+"\" & \""+String(remoteCodeUploadPassword)+"\"",160,120,1);
  delay(500);waitForTouch();
  tft.fillScreen(TFT_RED);
  tft.drawString("Then just upload the file",160,50,1);
  tft.drawString("After it's done, restart",160,120,1);
  delay(500);waitForTouch();
  tft.fillScreen(TFT_RED);
  tft.drawString("Good Luck",160,50,1);
  tft.drawString("(touch to begin)",160,120,1);
  
  tft.setTextDatum(TL_DATUM);
  tft.setTextSize(1);
  delay(500);waitForTouch();
  
}


/***************************************************************************************************
R  E  M  O  T  E      S  D      C  A  R  D      C  O  N  N  E  C  T  I  O  N
***************************************************************************************************/
void remoteSDFilesHandler(){
  if (WiFi.status() != WL_CONNECTED){
    Serial.println("Not connected to wifi");
    return;   
  }
  if (!SD.begin(SD_CS, spiSD)) {
    Serial.println("Card Mount Failed");
    return;
  } 
    
  server2.on("/", HTTP_GET, handleRoot);
  server2.onNotFound(handleNotFound);
  server2.on("/login", HTTP_POST, handleLogin);
  server2.on("/login", HTTP_GET, handleLogin);
  server2.on("/leaveserver", HTTP_GET, handleLeaveServer);
  server2.on("/upload", HTTP_GET, [](){
  String html = "<html><body>";
  html += "<h1>Upload File</h1>";
  html += "<form method='POST' action='/upload' enctype='multipart/form-data'>";
  html += "<input type='file' name='file'>";
  html += "<input type='submit' value='Upload'>";
  html += "</form>";
  html += "<br><a href='/'>HOME</a>";
  html += "<p>Once you click upload, wait until the page finishes loading</p>";
  html += "</body></html>";
  server2.send(200, "text/html", html);
  });
  server2.on("/upload", HTTP_POST, [](){ server2.send(200); }, handleFileUpload);
  server2.on("/delete", HTTP_GET, [](){
  String html = "<html><body>";
  html += "<h1>Delete File</h1>";
  html += "<form method='POST' action='/delete'>";
  html += "<label for='filename'>Enter filename to delete:</label><br>";
  html += "<input type='text' name='filename'><br>";
  html += "<input type='submit' value='Delete'>";
  html += "</form>";
  html += "<br><a href='/'>HOME</a>";
  html += "</body></html>";
  server2.send(200, "text/html", html);
  });
  server2.on("/delete", HTTP_POST, [](){
  String filenameToDelete = "";
  if (currentBasePath=="/"){filenameToDelete = currentBasePath + server2.arg("filename");}
  else {filenameToDelete = currentBasePath + "/" + server2.arg("filename");}
  if (filenameToDelete != "") {
    if (SD.remove(filenameToDelete)) {
      server2.sendHeader("Location", "/delete", true);///
      server2.send(302);
    } else {
      server2.sendHeader("Location", "/", true);///
      server2.send(302);
    }
  } else {
    server2.sendHeader("Location", "/", true);///
    server2.send(302);
  }
  });

  server2.begin();
  Serial.println("HTTP server started");
  Serial.println("\nConnected to "+WiFi.SSID()+" Use IP address: "+WiFi.localIP().toString());

  tft.setTextDatum(TC_DATUM);
  tft.setTextSize(2);


  tft.fillScreen(TFT_RED);
  tft.drawString("On your computer connect",160,50,1);
  tft.drawString("to the given IP adress",160,120,1);
  tft.drawString(WiFi.localIP().toString(),160,180,1);
  delay(3000);waitForTouch();
  tft.fillScreen(TFT_RED);
  tft.drawString("The Username & Password",160,50,1);
  tft.drawString("are \""+String(remoteSDConnectionUsername)+"\" & \""+String(remoteSDConnectionPassword)+"\"",160,120,1);
  delay(500);waitForTouch();
  tft.fillScreen(TFT_RED);
  tft.drawString("Good Luck",160,50,1);
  tft.drawString("(touch to begin)",160,120,1);
  tft.drawString(WiFi.localIP().toString(),160,180,1);
  
  tft.setTextDatum(TL_DATUM);
  tft.setTextSize(1);
  delay(500);waitForTouch();
  tft.fillScreen(TFT_RED);

  while (stayInSDServer){
    server2.handleClient();
    delay(1);  
  }
  
  delay(300);
  ESP.restart();
}

void handleRoot() {
  Serial.println("R handle root"); 
  String path = server2.arg("path");
  if (path == "") {
    path = "/";
  }
  // if (path == "/")
  //   currentBasePath = "";

  if (!loginCheckPassed){
    Serial.println("1");
    server2.sendHeader("Location", "/login");
    server2.send(302, "text/plain", "");    
  }

  Serial.println("R path");
  Serial.println(path);  
  currentBasePath = "/" + determineFilePath(path.substring(1));
  Serial.println("R combined total path");
  Serial.println(currentBasePath);
  String html = "<html><body><h1>File Explorer</h1><ul>";

  html += "<a href='/?path=/'>HOME</a> ";
  html += "<a href='/upload'><button>Upload File</button></a>";
  html += "<a href='/delete'><button>Delete File</button></a>";
  // html += "<a href='/downloadfolder'><button>Download Folder</button></a>";

  listDirectory(currentBasePath, html);//path
  
  html += "<a href='/leaveserver'><button>EXIT SERVER</button></a>";
  html += "</ul></body></html>";
  server2.send(200, "text/html", html);
}

void handleLogin() {
  Serial.println("please login");
  if (server2.method() == HTTP_GET) {
    String loginPage = "<html><body><h1>Login Page</h1>";
    loginPage += "<form method='post' action='/login'>";
    loginPage += "Username: <input type='text' name='username'><br>";
    loginPage += "Password: <input type='password' name='password'><br>";
    loginPage += "<input type='submit' value='Login'></form></body></html>";
    loginPage += "<a href='/leaveserver'><button>EXIT SERVER</button></a>";
    server2.send(200, "text/html", loginPage);
  } else if (server2.method() == HTTP_POST) {
    String username = server2.arg("username");
    String password = server2.arg("password");
    //Serial.println("usernames: " + username + remoteSDConnectionUsername);
    //Serial.println("passwords: " + password + remoteSDConnectionPassword);
    if (username == remoteSDConnectionUsername && password == remoteSDConnectionPassword) {
      loginCheckPassed = true;
      server2.sendHeader("Location", "/");
      server2.send(302, "text/plain", ""); // Redirect to root
    } else {
      // Authentication failed, show an error message
      String errorMessage = "<html><body><h1>Login Failed</h1>";
      errorMessage += "<p>Invalid username or password.</p>";
      errorMessage += "<a href='/login'>Try Again</a></body></html>";
      server2.send(401, "text/html", errorMessage); // 401 Unauthorized
    }
  }
}

void listDirectory(const String &dirName, String &output) {
  Serial.println("list dir");
  Serial.println(dirName);

  File dir = SD.open(dirName);
  if (!dir) {
    return;
    Serial.println("sd not found");
  }
  
  while (true) {
    File entry = dir.openNextFile();
    if (!entry) {
      break;
    }
    output += "<li>";
    if (entry.isDirectory()) {
      //Serial.println(String(entry.name())); 
      output += "Dir: <a href='/?path=/" + String(entry.name()) + "'>" + entry.name() + "</a>";
    }else {
      //Serial.println(String(entry.name()));
      if (dirName=="/"){output += "File: <a href='/" + String(entry.name()) + "'>" + entry.name() + "</a>";}
      else {output += "File: <a href='" + dirName + "/" + String(entry.name()) + "'>" + entry.name() + "</a>";} 
    }
    output += "</li>";
    entry.close();
  }
  
  dir.close();
}

void handleFileUpload() {
  HTTPUpload& upload = server2.upload();
  String downloadFilePath = currentBasePath;
  String downloadFileName = "";

  if (upload.status == UPLOAD_FILE_START) {
    if (downloadFilePath=="/"){downloadFileName = downloadFilePath + String(upload.filename);}
    else {downloadFileName = downloadFilePath + "/" + String(upload.filename);}
    Serial.println("downloadFilePath: " + String(downloadFilePath));
    Serial.println("upload.filename: " + String(upload.filename));
    Serial.println("Uploading file: " + String(downloadFileName));

    uploadFile = SD.open(downloadFileName, FILE_WRITE); // Open the file for writing
    if (!uploadFile) {
      Serial.println("Failed to create file");
      return;
    }
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (uploadFile) {
      uploadFile.write(upload.buf, upload.currentSize); // Write the received bytes to the file
    }
  } else if (upload.status == UPLOAD_FILE_END) {
    if (uploadFile) {
      uploadFile.close(); // Close the file when upload is complete
      Serial.print("Upload Size: ");
      Serial.println(upload.totalSize);
      // Add your response code here, like sending a success message
      server2.sendHeader("Location", "/upload", true);///
      server2.send(302);
    } else {
      Serial.println("File upload failed");
    }
  }
}

void handleLeaveServer(){
  stayInSDServer = false;
  server2.sendHeader("Location", "/");
  server2.send(302, "text/plain", "");
}

void handleNotFound() {
  if (!loginCheckPassed){
    server2.sendHeader("Location", "/login");
    server2.send(302, "text/plain", "");    
  }
  
  String uri = urlDecode(server2.uri());
  
  if (uri.endsWith("/")) {
    uri.remove(uri.length() - 1);
  }

  Serial.println("uri: " + uri);
  if (SD.exists(uri)) {
    File file = SD.open(uri);
    server2.streamFile(file, "text/plain");
    file.close();
  } else {
    server2.send(404, "text/plain", "Page or File not found");
  }
}

String urlDecode(const String &input) {
  String decoded = "";
  char a, b;
  for (size_t i = 0; i < input.length(); i++) {
    if (input[i] == '%') {
      if (isHexadecimalDigit(input[i + 1]) && isHexadecimalDigit(input[i + 2])) {
        a = input[i + 1];
        b = input[i + 2];
        int decimalValue = (charToHex(a) * 16) + charToHex(b);
        decoded += static_cast<char>(decimalValue);
        i += 2;
      } else {
        decoded += '%';  // Handle malformed escape sequence, retain '%'
      }
    } else if (input[i] == '+') {
      decoded += ' ';
    } else {
      decoded += input[i];
    }
  }
  return decoded;
}

bool isHexadecimalDigit(char c) {
  return ((c >= '0' && c <= '9') || (c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f'));
}

unsigned char charToHex(unsigned char x) {
  return (x >= '0' && x <= '9') ? (x - '0') : ((x >= 'A' && x <= 'F') ? (x - 'A' + 10) : (x - 'a' + 10));
}

String determineFilePath(const String& folderName) {
  Serial.println("finding file path: " + folderName);
  if (folderName == "/")
    return "";
  File root = SD.open("/");
  if (!root) {
    return ""; // Return an empty string if unable to open root directory
  }

  File file = root.openNextFile();
  while (file) {
    if (file.isDirectory()) {
      if (String(file.name()) == ( folderName)) {
        Serial.println("current foldername: " + String(file.name()));
        Serial.println("desired foldername: " + ( folderName));
        file.close();
        return (folderName  );
      } else {
        String subPath = determineFilePathInSubdirectory(file, folderName);
        if (!subPath.isEmpty()) {
          return String(file.name()) + "/"+ subPath ;
        }
      }
    }
    file = root.openNextFile();
  }
  return ""; // Return an empty string if the folder is not found
}

String determineFilePathInSubdirectory(File directory, const String& folderName) {
  Serial.println("222 finding file path: " + folderName);
  File file = directory.openNextFile();
  while (file) {
    if (file.isDirectory()) {
      if (String(file.name()) == ( folderName)) {
        Serial.println("222 current foldername: " + String(file.name()));
        Serial.println("222 desired foldername: " + ( folderName));
        file.close();
        return (folderName  );
      } else {
        String subPath = determineFilePathInSubdirectory(file, folderName);
        if (!subPath.isEmpty()) {
            return String(file.name()) + "/"+ subPath;
        }
      }
    }
    file = directory.openNextFile();
  }
  return ""; // Return an empty string if the folder is not found
}


/***************************************************************************************************
K  E  Y  B  O  A  R  D
***************************************************************************************************/
const char alphabeticKeys[4][10] = {
  {'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p'},
  {'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', '\\'},
  {'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/'},
  {'abAB', '12^#', 'Space', 'Bksp'}
};
const char numericKeys[4][10] = {
  {'1', '2', '3', '4', '5', '6', '7', '8', '9', '0'},
  {'-', '/', ':', ';', '(', ')', '$', '&', '@', '"'},
  {'+', '*', '=', '?', '!', '_', '{', '}', '[', ']'},
  {'abAB', '12^#', 'Space', 'Bksp'}
};
const char specialKeys[4][10] = {
  {'~', '`', '|', '\\', '^', '<', '>', '{', '}', '%'},
  {'_', '&', '[', ']', '#', '\\', '!', '?', '*', '('},
  {')', '-', '+', '=', ':', ';', '"', '@', '.', '/'},
  {'abAB', '12^#', 'Space', 'Bksp'}
};
const char capsKeys[4][10] = {
  {'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P'},
  {'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', '\\'},
  {'Z', 'X', 'C', 'V', 'B', 'N', 'M', ',', '.', '/'},
  {'abAB', '12^#', 'Space', 'Bksp'}
};

bool handleKeyboard() {
  currenTKeyboard = 0;
  keyboardString = "";
  drawKeyboard();
  drawBmp24("/logos/homelogoinverted.bmp",0,0);

  bool saveButtonPressed = false; // Flag to indicate if save button was pressed
  while (!saveButtonPressed) {
  uint16_t x = 0, y = 0;
  bool pressed = tft.getTouch(&x, &y);
    if (pressed) {
      Serial.print(String(x) + "," + String(y) + "\n");
      if (handleKeyPress(x,y))
        saveButtonPressed = true;
      if((x>0)&&(x<30)&&(y>0)&&(y<30))
        return false;
      delay(150);
    }
  }
  Serial.println(keyboardString); 
  return true;
}

void drawKeyboard() {
  tft.fillScreen(TFT_BLACK); // Clear the screen
  
  // Set text color
  tft.setTextColor(TFT_WHITE);
  tft.setTextFont(1);  
  
  // Draw keys
  int keyWidth = tft.width() / 10;
  int keyHeight = tft.height() / 6;//12
  
  for (int row = 0; row < 3; row++) {
    for (int col = 0; col < 10; col++) {
      int x = col * keyWidth;
      int y = tft.height() / 3 + (row * keyHeight);
      
      // Draw the key as a rectangle
      tft.drawRect(x, y, keyWidth, keyHeight, TFT_BLUE);
      
      // Draw the character on the key
      tft.setTextSize(2); // Adjust text size as needed
      tft.setCursor(x + keyWidth / 2 - 12, y + keyHeight / 2 - 12);
      if      (currenTKeyboard==0){tft.print(alphabeticKeys[row][col]);}
      else if (currenTKeyboard==1){tft.print(numericKeys[row][col]);}
      else if (currenTKeyboard==2){tft.print(specialKeys[row][col]);}
      else if (currenTKeyboard==3){tft.print(capsKeys[row][col]);}
    }
  }
  
  tft.setTextDatum(MC_DATUM);
  // Draw special keys
  
  tft.drawRect(0, tft.height() - keyHeight, keyWidth*1.5, keyHeight, TFT_SILVER);
  tft.setTextSize(2);
  tft.setCursor(((keyWidth*1.5)/2) -21, ((tft.height() - keyHeight) + keyHeight/2)-5);
  tft.print("aA");

  tft.drawRect(keyWidth*1.5, tft.height() - keyHeight, keyWidth*1.5, keyHeight, TFT_SILVER);
  tft.setTextSize(2);
  tft.setCursor(((keyWidth*1.5) + (keyWidth*3)/2) -42, ((tft.height() - keyHeight) + keyHeight/2)-5);
  tft.print("1#");
  
  // Backspace key
  tft.drawRect(keyWidth*7.5, tft.height() - keyHeight, keyWidth*2.5, keyHeight, TFT_SILVER);
  tft.setTextSize(2);
  tft.setCursor(((keyWidth*7.5)+(keyWidth*2.5)/2) -25, ((tft.height() - keyHeight) + keyHeight/2)-5);
  tft.print("Bksp");
  
  // Space key
  tft.drawRect(keyWidth * 3, tft.height() - keyHeight, keyWidth * 4, keyHeight, TFT_SILVER);
  tft.setTextSize(2);
  tft.setCursor(((keyWidth * 3)+(keyWidth * 4)/2) -24, ((tft.height() - keyHeight) + keyHeight/2)-5);
  tft.print("Space");

  tft.drawRect(230, 10, 90, 70, TFT_GREEN);//70
  tft.setTextSize(2);
  tft.setCursor(240, 25);
  tft.print("FINISH");

  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextDatum(TL_DATUM);
  tft.setTextSize(1);

  displayKeyboardString();
}

bool handleKeyPress(int touchX, int touchY) {
  int keyWidth = tft.width() / 10;
  int keyHeight = tft.height() / 6;
  
  int row = (touchY - (tft.height() / 3)) / keyHeight;
  int col = touchX / keyWidth;

  String keyPressed = "";
  
  if (row >= 0 && row < 3 && col >= 0 && col < 10) {
    
    if (currenTKeyboard == 0) {
      keyPressed = alphabeticKeys[row][col];
    } else if (currenTKeyboard == 1) {
      keyPressed = numericKeys[row][col];
    } else if (currenTKeyboard == 2) {
      keyPressed = specialKeys[row][col];
    } else if (currenTKeyboard == 3) {
      keyPressed = capsKeys[row][col];
    }
    Serial.println(keyPressed);
    
  } else if (row == 3) {
    
    if (touchX >= 0 && touchX < keyWidth * 1.5) {
      if (currenTKeyboard==0)
        currenTKeyboard = 3;
      else
        currenTKeyboard = 0;
      drawKeyboard();
    }else if (touchX >= keyWidth * 1.5 && touchX < keyWidth * 3) {
      if (currenTKeyboard==1)
        currenTKeyboard = 2;
      else
        currenTKeyboard = 1;
      drawKeyboard();
    }else if (touchX >= keyWidth * 7.5 && touchX < keyWidth * 10) {
      keyboardString.remove(keyboardString.length() - 1);
    }else if (touchX >= keyWidth * 3 && touchX < keyWidth * 7) {
      keyPressed = " ";
    }
  }

  if((touchX>230)&&(touchX<320)&&(touchY>0)&&(touchY<70))
    return true; 
  
  keyboardString += keyPressed;
  displayKeyboardString();
  return false;
}

void displayKeyboardString(){
  tft.fillRect(0,0,229,79,TFT_BLACK);
  drawBmp24("/logos/homelogoinverted.bmp",0,0);
  tft.setTextSize(2);
  tft.drawString(keyboardString,20,20);
  tft.setTextSize(1);
}


/***************************************************************************************************
W  I  F  I      &      R  E  M  O  T  E      W  I  F  I      C  O  N  N  E  C  T  I  O  N
***************************************************************************************************/
void typeInWIFIManually(){
  String typedSSID = "";
  String typedPSW = "";

  tft.setTextDatum(TC_DATUM);tft.setTextSize(3);
  tft.fillScreen(TFT_VIOLET);
  tft.drawString("Type in the",160,50,1);
  tft.drawString("WIFI SSID",160,170,1);
  tft.setTextDatum(TL_DATUM);tft.setTextSize(1);
  waitForTouch();delay(600);
  if(!handleKeyboard()){}
  typedSSID = keyboardString;
  tft.setTextDatum(TC_DATUM);tft.setTextSize(3);
  tft.fillScreen(TFT_VIOLET);
  tft.drawString("Type in the",160,50,1);
  tft.drawString("WIFI PASSWORD",160,170,1);
  tft.setTextDatum(TL_DATUM);tft.setTextSize(1);
  waitForTouch();delay(600);
  if(!handleKeyboard()){}
  typedPSW = keyboardString;
  tft.fillScreen(TFT_VIOLET);

  SetBreakWifi(typedSSID,typedPSW);
  delay(300);
  ESP.restart();
}

void remoteWifiSetup(){

  WiFi.disconnect();
  // EEPROM.begin(512);
  // delay(10);
  //pinMode(LED_BUILTIN, OUTPUT);


  String esid;
  for (int i = 0; i < 32; ++i)
  {
    esid += char(EEPROM.read(i));
  }
  Serial.println();
  Serial.print("SSID: ");
  Serial.println(esid);
  Serial.println("Reading EEPROM pass");
 
  String epass = "";
  for (int i = 32; i < 96; ++i)
  {
    epass += char(EEPROM.read(i));
  }
  Serial.print("PASS: ");
  Serial.println(epass);
 
 
  WiFi.begin(esid.c_str(), epass.c_str());
  if (testWifi())
  {
    Serial.println("Succesfully Connected!!!");
    Serial.println(WiFi.localIP());
    return;
  }
  else
  {
    Serial.println("Turning the HotSpot On");
    launchWeb();
    setupAP();// Setup HotSpot
  }
 
  Serial.println();
  Serial.println("Waiting.");
  
  while ((WiFi.status() != WL_CONNECTED))
  {
    Serial.print(".");
    delay(100);
    server.handleClient();


    uint16_t x = 0, y = 0;
    bool pressed = tft.getTouch(&x, &y);
    if (pressed) {
      Serial.print(String(x) + "," + String(y) + "\n");
      if((x>0)&&(x<30)&&(y>170)&&(y<200)){
        return;
        //homeScreen();     
      }
      if((x>120)&&(x<300)&&(y>200)&&(y<230))
        typeInWIFIManually();  
    }

  
  }
  
}

bool testWifi(void){
  int c = 0;
  Serial.println("Waiting for Wifi to connect");
  while ( c < 30 ) {
    if (WiFi.status() == WL_CONNECTED)
    {
      return true;
    }
    delay(500);
    Serial.print("*");
    c++;
  }
  Serial.println("");
  Serial.println("Connect timed out, opening AP");
  return false;
}

void launchWeb(){
  Serial.println("");
  if (WiFi.status() == WL_CONNECTED)
    Serial.println("WiFi connected");
  Serial.print("Local IP: ");
  Serial.println(WiFi.localIP());
  Serial.print("SoftAP IP: ");
  String ipString = WiFi.softAPIP().toString();
  Serial.println(ipString);
  createWebServer();
  // Start the server
  server.begin();
  Serial.println("Server started");

  // delay(100);
  // String ipString = ipString + WiFi.softAPIP().toString() +"  ";
  // delay(20);
  // ipString = ipString + WiFi.softAPIP().toString() +"  ";
  // delay(20);
  // ipString = ipString + WiFi.softAPIP().toString() +"  ";
  tft.fillScreen(TFT_WHITE);
  tft.setTextSize(2);
  tft.setTextFont(1);
  tft.setTextColor(TFT_BLACK);
  tft.drawString("Setup WIFI :)",20, 10, 2);
  tft.setTextSize(1);
  tft.setTextFont(2);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString("1) Connect to new network on phone/computer",20, 50, 2);
  tft.drawString("2) Type in IP to chrome",20, 70, 2);
  tft.drawString("3) Send wifi credentials",20, 90, 2);
  tft.drawString(ipString, 20, 120, 2);
  drawBmp24("/logos/homelogo.bmp",0,170);
  tft.drawString("OR TYPE IT IN",120, 200, 2);
  tft.setTextFont(1);

}

void setupAP(void){
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(100);
  int n = WiFi.scanNetworks();
  Serial.println("scan done");
  if (n == 0)
 
   Serial.println("no networks found");  else
  {
    Serial.print(n);
    Serial.println(" networks found");
    for (int i = 0; i < n; ++i)
    {
      // Print SSID and RSSI for each network found
      Serial.print(i + 1);
      Serial.print(": ");
      Serial.print(WiFi.SSID(i));
      Serial.print(" (");
      Serial.print(WiFi.RSSI(i));
      Serial.print(")");
      Serial.println((WiFi.encryptionType(i) == WIFI_AUTH_OPEN) ? " " : "*");
      delay(10);
    }
  }
  Serial.println("");
  st = "<ol>";
  for (int i = 0; i < n; ++i)
  {
    // Print SSID and RSSI for each network found
    st += "<li>";
    st += WiFi.SSID(i);
    st += " (";
    st += WiFi.RSSI(i);
 
    st += ")";
    st += (WiFi.encryptionType(i) == WIFI_AUTH_OPEN) ? " " : "*";
    st += "</li>";
  }
  st += "</ol>";
  delay(100);
  WiFi.softAP("The Box of Love", "");
  Serial.println("softap");
  launchWeb();
  Serial.println("over");
}
 
void createWebServer(){
 {
    server.on("/", []() {
 
      IPAddress ip = WiFi.softAPIP();
      String ipStr = String(ip[0]) + '.' + String(ip[1]) + '.' + String(ip[2]) + '.' + String(ip[3]);
      content = "<!DOCTYPE HTML>\r\n<html>Hello From The Box of Love! ";
      content += "<form action=\"/scan\" method=\"POST\"><input type=\"submit\" value=\"scan\"></form>";
      content += ipStr;
      content += "<p>";
      content += st;
      content += "</p><form method='get' action='setting'><label>SSID: </label><input name='ssid' length=32><input name='pass' length=64><input type='submit'></form>";
      content += "</html>";
      server.send(200, "text/html", content);
    });
    server.on("/scan", []() {
      //setupAP();
      IPAddress ip = WiFi.softAPIP();
      String ipStr = String(ip[0]) + '.' + String(ip[1]) + '.' + String(ip[2]) + '.' + String(ip[3]);
 
      content = "<!DOCTYPE HTML>\r\n<html>go back";
      server.send(200, "text/html", content);
    });
 
    server.on("/setting", []() {
      String qsid = server.arg("ssid");
      String qpass = server.arg("pass");
      if (qsid.length() > 0 && qpass.length() >= 0) {
        Serial.println("clearing eeprom");
        for (int i = 0; i < 96; ++i) {
          EEPROM.write(i, 0);
        }
        Serial.println(qsid);
        Serial.println("");
        Serial.println(qpass);
        Serial.println("");
 
        Serial.println("writing eeprom ssid:");
        for (int i = 0; i < qsid.length(); ++i)
        {
          EEPROM.write(i, qsid[i]);
          Serial.print("Wrote: ");
          Serial.println(qsid[i]);
        }
        Serial.println("writing eeprom pass:");
        for (int i = 0; i < qpass.length(); ++i)
        {
          EEPROM.write(32 + i, qpass[i]);
          Serial.print("Wrote: ");
          Serial.println(qpass[i]);
        }
        EEPROM.commit();
 
        content = "{\"Success\":\"saved to eeprom... reset to boot into new wifi\"}";
        statusCode = 200;
        ESP.restart();
      } else {
        content = "{\"Error\":\"404 not found\"}";
        statusCode = 404;
        Serial.println("Sending 404");
      }
      server.sendHeader("Access-Control-Allow-Origin", "*");
      server.send(statusCode, "application/json", content);
 
    });
  } 
}