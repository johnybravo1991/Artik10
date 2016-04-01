/*
 ###############################################################################
 #
 # Industrial Water Management
 #
 # https://www.temboo.com/iot-applications/water-management/
 #
 # The code below was generated using Temboo.
 #
 # This application monitors water levels in water tanks, 
 # and remotely refills the tanks when necessary. 
 # If the water volume in the tank falls below a specified level, 
 # the application will check the weather forecast to determine if 
 # rain is expected in the area within the following 48 hours; 
 # if no rain is expected, a call is placed to the tank superintendent
 # to allow him or her to remotely refill the tank from a reserve water source.
 # An MCU attached to the tank uses an eTape water level sensor to track the amount
 # of water in the tank and a 12v brushless pump to refill it. 
 # This application uses the following services:
 #
 # Yahoo! Weather https://temboo.com/library/Library/Yahoo/Weather/
 #
 # Nexmo https://temboo.com/library/Library/Nexmo/
 #
 ###############################################################################
*/

/*
 ###############################################################################
 #
 # Copyright 2015, Temboo Inc.
 # 
 # Licensed under the Apache License, Version 2.0 (the "License");
 # you may not use this file except in compliance with the License.
 # You may obtain a copy of the License at
 # 
 # http://www.apache.org/licenses/LICENSE-2.0
 # 
 # Unless required by applicable law or agreed to in writing,
 # software distributed under the License is distributed on an
 # "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
 # either express or implied. See the License for the specific
 # language governing permissions and limitations under the License.
 #
 ###############################################################################
*/

#define _BSD_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <unistd.h>
#include <Temboo.h>
#include <TembooSession.h>
#include <TembooNetworkClient.h>
#include "TembooAccount.h"

#define HIGH 1
#define LOW 0
#define INPUT 1
#define OUTPUT 0

// SocketConnection is a user defined struct containing whatever
// data is needed for communicating with the network interface.
// In this demo, it is defind in demo.h and contains a socket handle.
SocketConnection theSocket;

// There should only be one TembooSession per device.  It represents
// the connection to the Temboo system.
TembooSession theSession;

// Defines a time in seconds for how long the Choreo
// has to complete before timing
const int CHOREO_TIMEOUT = 300;

// Initialize a boolean to track the state of the pump
bool pumpStatus = false;

// Initialize a minimum water level threshold (NOTE: our sensor values go up when the water goes down.
// This should be adjusted based on your sensor calibration)
const int triggerAlertThreshold = 2100;

// Initialize a water level threshold for the water pump to stop filling
const int pumpOffThreshold = 2030;

// Initialize Pins
const int waterLevelSensor = 2;
const int motorPin = 22;

// Initialize a variable to hold the sensorValue
int waterLevel = 0;

bool digitalPinMode(int pin, int dir) {
    FILE * fd;
    char fName[32];
    // Exporting the pin to be used
    if(( fd = fopen("/sys/class/gpio/export", "w")) == NULL) {
        printf("Error: unable to export pin\n");
        return false;
    }
    fprintf(fd, "%d\n", pin);
    fclose(fd);
    
    // Setting direction of the pin
    sprintf(fName, "/sys/class/gpio/gpio%d/direction", pin);
    if((fd = fopen(fName, "w")) == NULL) {
        printf("Error: can't open pin direction\n");
        return false;
    }
    if(dir == OUTPUT) {
        fprintf(fd, "out\n");
    } else {
        fprintf(fd, "in\n");
    }
    fclose(fd);
    return true;
}

int digitalRead(int pin) {
    FILE * fd;
    char fName[32];
    char val[2];
    
    // Open pin value file
    sprintf(fName, "/sys/class/gpio/gpio%d/value", pin);
    if((fd = fopen(fName, "r")) == NULL) {
        printf("Error: can't open pin value\n");
        return false;
    }
    fgets(val, 2, fd);
    fclose(fd);
    return atoi(val);
}

bool digitalWrite(int pin, int val) {
    FILE * fd;
    char fName[32];
    
    // Open value file
    sprintf(fName, "/sys/class/gpio/gpio%d/value", pin);
    if((fd = fopen(fName, "w")) == NULL) {
        printf("Error: can't open pin value\n");
        return false;
    }
    if(val == HIGH) {
        fprintf(fd, "1\n");
    } else {
        fprintf(fd, "0\n");
    }
    fclose(fd);
    return true;
}

int analogRead(int pin) {
    FILE * fd;
    char fName[64];
    char val[8];
    
    // Open value file
    sprintf(fName, "/sys/devices/12d10000.adc/iio:device0/in_voltage%d_raw", pin);
    if((fd = fopen(fName, "r")) == NULL) {
        printf("Error: can't open analog voltage value\n");
        return 0;
    }
    fgets(val, 8, fd);
    fclose(fd);
    return atoi(val);
}

TembooError setup() {
    
    // We have to initialize the TembooSession struct exactly once
    // The TEMBOO_ACCOUNT, TEMBOO_APP_KEY_NAME, and TEMBOO_APP_KEY
    // values are your Temboo account credentials.
    
    // We have to initialize the TembooSession struct exactly once.
    TembooError returnCode = TEMBOO_SUCCESS;
    
#ifndef USE_SSL
    returnCode = initTembooSession(
                                   &theSession,
                                   TEMBOO_ACCOUNT,
                                   TEMBOO_APP_KEY_NAME,
                                   TEMBOO_APP_KEY,
                                   &theSocket);
#else
    printf("Enabling TLS...\n");
    returnCode = initTembooSessionSSL(
                                      &theSession,
                                      TEMBOO_ACCOUNT,
                                      TEMBOO_APP_KEY_NAME,
                                      TEMBOO_APP_KEY,
                                      &theSocket,
                                      "/opt/iothub/artik/temboo/temboo_artik_library/lib/temboo.pem",
                                      NULL);
#endif
    
    digitalPinMode(motorPin, OUTPUT);
    
    return returnCode;
}

void runGetWeatherByAddress(TembooSession* session, char* msg) {
    // Initialize Choreo data structure
    TembooChoreo choreo;
    const char choreoName[] = "/Library/Yahoo/Weather/GetWeatherByAddress";
    initChoreo(&choreo, choreoName);
    
    // Set profile to use for execution
    // This profile contains input data for Address
    const char profileName[] = "ser";
    setChoreoProfile(&choreo, profileName);

    ChoreoInput TmbAppSrcIn;
    TmbAppSrcIn.name = "TmbAppSrc";
    TmbAppSrcIn.value = "WaterManagementApp";
    addChoreoInput(&choreo, &TmbAppSrcIn);
    
    // Output filter to get the forecast for tomorrow
    ChoreoOutput filterTomorrow;
    filterTomorrow.name = "tomorrow";
    filterTomorrow.xpath = "/rss/channel/item/yweather:forecast[2]/@text";
    filterTomorrow.variable = "Response";
    
    // Output filter to get the forecast for today
    ChoreoOutput filterToday;
    filterToday.name = "today";
    filterToday.xpath = "/rss/channel/item/yweather:forecast[1]/@text";
    filterToday.variable = "Response";
    
    addChoreoOutput(&choreo, &filterTomorrow);
    addChoreoOutput(&choreo, &filterToday);
    
    int returnCode = runChoreo(&choreo, session, CHOREO_TIMEOUT);
    if (returnCode == 0) {
        while (tembooClientAvailable(session->connectionData)) {
            char name[64];
            char value[64];
            memset(name, 0, sizeof(name));
            memset(value, 0, sizeof(value));
            
            choreoResultReadStringUntil(session->connectionData, name, sizeof(name), '\x1F');
            // Parsing the result and adding the forecast to the message being sent to the phone call
            if (0 == strcmp(name, "tomorrow")) {
                if (choreoResultReadStringUntil(session->connectionData, value, sizeof(value), '\x1E') == -1) {
                    printf("Error: char array is not large enough to store the string\n");
                } else {
                    strcat(msg, "Tomorrow's forecast is ");
                    strcat(msg, value);
                    strcat(msg,". ");
                }
            } else if (0 == strcmp(name, "today")) {
                if (choreoResultReadStringUntil(session->connectionData, value, sizeof(value), '\x1E') == -1) {
                    printf("Error: char array is not large enough to store the string\n");
                } else {
                    strcat(msg, "Today's forecast is ");
                    strcat(msg, value);
                    strcat(msg, ". ");
                }
            }
            else {
                choreoResultFind(session->connectionData, "\x1E");
            }
        }
    }
    
    // When we're done, close the connection
    tembooClientStop(session->connectionData);
}

void runCaptureTextToSpeechPromptChoreo(TembooSession* session) {
    // Create our message buffer. Make sure it is big enough to fit the full message
    char msg[256] = "Alert, your tank is running low.";
    
    runGetWeatherByAddress(session, msg);
    // Append the text for which button to press to turn on the pump
    strcat(msg, "Press 1 to turn on the pump.");
    
    // Initialize Choreo data structure
    TembooChoreo choreo;
    const char choreoName[] = "/Library/Nexmo/Voice/CaptureTextToSpeechPrompt";
    initChoreo(&choreo, choreoName);
    
    ChoreoInput TextIn;
    TextIn.name = "Text";
    TextIn.value = msg;
    addChoreoInput(&choreo, &TextIn);

    ChoreoInput TmbAppSrcIn;
    TmbAppSrcIn.name = "TmbAppSrc";
    TmbAppSrcIn.value = "WaterManagementApp";
    addChoreoInput(&choreo, &TmbAppSrcIn);
    
    // Set profile to use for execution
    // This profile contains input data for:
    // APIKey, APISecret, ByeText, MaxDigits, and To
    const char profileName[] = "abhishek22";
    setChoreoProfile(&choreo, profileName);

    int returnCode = runChoreo(&choreo, session, CHOREO_TIMEOUT);
    if (returnCode == 0) {
        while(tembooClientAvailable(session->connectionData)) {
            char nameString[64];
            char dataString[32];
            
            memset(nameString,0,sizeof(nameString));
            memset(dataString,0,sizeof(dataString));
            
            choreoResultReadStringUntil(session->connectionData, nameString, sizeof(nameString), '\x1F');
            
            if(!strcmp(nameString, "Digits")) {
                if(choreoResultReadStringUntil(session->connectionData, dataString, sizeof(dataString), '\x1E') == -1) {
                    printf("Error: char array is not large enough to store the string\n");
                } else {
                    // If the number 1 is pressed on the phone keypad, turn on the pump
                    if(!strcmp(dataString, "1")){
                        digitalWrite(motorPin, HIGH);
                        // Set the motor state boolean to ON
                        pumpStatus = true;
                    }
                }
            } else {
                choreoResultFind(session->connectionData, "\x1E");
            }
        }
    }
    
    // When we're done, close the connection
    tembooClientStop(session->connectionData);
}

int main(void) {
    if (setup() != TEMBOO_SUCCESS) {
        return EXIT_FAILURE;
    }
    
    while(1){
        int i = 0;
        // Averaging the last 10 values to smooth the water level
        waterLevel = 0;
        for(i = 0; i <10; i++) {
             waterLevel = waterLevel + analogRead(waterLevelSensor);
        }
        waterLevel *= .1;
        
        printf("The water level is %i\n", waterLevel);
        
        // If water is above this value, make a call to see if the pump should be turned on
        if (waterLevel > triggerAlertThreshold && !pumpStatus){
            runCaptureTextToSpeechPromptChoreo(&theSession);
        }
        
        // If water level is below this value, turn the pump off
        if(waterLevel < pumpOffThreshold && pumpStatus) {
            digitalWrite(motorPin, LOW);
            pumpStatus = false;
        }
        
        usleep(5000);
    }
    
#ifdef USE_SSL
    // Free the SSL context and and set Temboo connections to no TLS
    endTembooSessionSSL(&theSession);
#endif
    
    return EXIT_SUCCESS;
}