/****************************************************************************
*  Copyright (c)2023 John Gedde (Amateur radio callsign AD2DK)
*  
*	aslLCD is free software: you can redistribute it and/or modify
*	it under the terms of the GNU Lesser General Public License as
*	published by the Free Software Foundation, either version 3 of the
*	License, or (at your option) any later version.
*
*	aslLCD is distributed in the hope that it will be useful,
*	but WITHOUT ANY WARRANTY; without even the implied warranty of
*	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*	GNU Lesser General Public License for more details.
*
*	You should have received a copy of the GNU Lesser General Public
*	License along with wiringPi.
*	If not, see <http://www.gnu.org/licenses/>.        
*
*	This file is part of cosMON:
*	https://github.com/ IT AIN'T THERE YET          
*
*  COSmon.c
*                                                                          
*  Synopsis:	Purpose: COS monitor from Raspberry Pi using Wiring Pi
*				We detect COS from an external radio on the rasp
*				Pi GPIO pins and use it to Key and unkey Allstar COS by
*				calling asterisk commands from asterisk's command line calls.
*				If external COS is stuck high we will automatically unkey COS
*				after COS_TIMEOUT_MS milliseconds.
*
*  Project:	Allstar Link LCD
*                                                                         
*  File Version History:                                                       
*    Date    |      Eng     |               Description
*  ----------+--------------+------------------------------------------------
*  See Below
*  
****************************************************************************/

/* 
	Version History:
	John Gedde Rev 0 02/24/23 Original Version
	John Gedde Rev 1 02/25/23 Added COS timeout feature
	John Gedde Rev 2 02/26/23 Added support of command line options for COS Timeout, 
							  poll timing, GPIO pin select, and help.
	John Gedde Rev 3 02/28/23 Fixed issue with COS timeout on start-up.
	John Gedde Rev 4 03/23/23 Added support for network status LED and shutdown switch
	John Gedde Rev 5 03/24/23 Got rid of command line setuip in favor of conf file.	
*/

#include <stdio.h>
#include <stdlib.h>
#include <wiringPi.h>
#include <string.h>
#include <iniparser.h>
#include <stdint.h>
#include <stdbool.h>

#include "getIP.h"
#include "ini.h"

const char strVersion[]="v1.1";

// This is where COS will come into the Rasppi
#define DEFAULT_EXTCOS_GPIO			29 		// GPIO.29 (pin 40)
#define DEFAULT_LOOP_DELAY			100		// milliseconds	
#define DEFAULT_COS_TIMEOUT_MS		150000	// milliseconds
#define DEFAULT_COS_TIMEOUT_COUNT	(DEFAULT_COS_TIMEOUT_MS / DEFAULT_LOOP_DELAY)
#define DEFAULT_NETWORK_GPIO		3		// GPIO.3 (pin 15)
#define DEFAULT_SHUTDOWN_GPIO		7		// GPIO.7 (pin 7)
#define DEFAULT_NET_CHECK_DIVISOR	20		// every 20 times through the main COS loop
#define DEFAULT_SD_ACTIVATE_COUNT	30      // Must be pressed for 30 times through the main loop

enum
{
	RETVAL_OK=0,
	RETVAL_ILLEGALARG=-1,
	RETVAL_UNEXPECTEDARG=-2,
	RETVAL_BADPARM=-3,
	RETVAL_HELPME=-4
};

static const uint16_t GPIOAllowed[]={0, 1, 2, 3, 4, 5, 6, 7, 21, 22, 23, 24, 25, 26, 27, 28, 29};
static uint16_t networkStatusPin=DEFAULT_NETWORK_GPIO;

/*-----------------------------------------------------------------------------
Function:
	main   
Synopsis:
	Called to check to see if we have an IP address.
Author:
	John Gedde
Inputs:
	None	
Outputs:
	Control gpio Pin
-----------------------------------------------------------------------------*/
static void wifiLightHandler()
{
	char IPaddr[17]={ 0 };
	static uint8_t lastWrite=LOW;
	int IPlen;
		
	getIPaddress(IPaddr);
	IPlen=strlen(IPaddr);
	
	if (IPlen==0 && lastWrite==HIGH)
	{
		digitalWrite(networkStatusPin, LOW);
		lastWrite=HIGH;
	}
	else if ((IPlen!=0) & (lastWrite==LOW))
	{
		digitalWrite(networkStatusPin, HIGH);
		lastWrite = LOW;
	}		
}


//////////////////////////////////////////////////////////////////////////////////
/*-----------------------------------------------------------------------------
Function:
	main   
Synopsis:
	c main function for COSmon
Author:
	John Gedde
Inputs:
	None	
Outputs:
	return val to caller
-----------------------------------------------------------------------------*/
int main(void)
{
	bool 			LastCOSState;
	bool 			CurrCOSState;
	uint16_t 		netCheckDivisor;
	uint16_t 		ExtCOSPin;
	uint16_t 		TimeoutCountCOS;
	uint16_t 		TimeoutCount;	
	uint16_t	 	LoopDelayMs;
	uint32_t		TimeoutMs;
	float 			tempval;
	bool 			networkStatusOn;
	bool 			shutdownSwitchEnable;
	bool			COStimeoutEnable;
	uint16_t 		shutdownSwitchPin;
	uint16_t		SDswitchActivateCount;
	uint16_t		SDswitchPressedCount;
		
	initIni("/etc/COSmon.conf");
	
	ExtCOSPin=				iniparser_getint(ini, "gpio:gpio_COS", DEFAULT_EXTCOS_GPIO);
	networkStatusPin=		iniparser_getint(ini, "gpio:gpio_network", DEFAULT_NETWORK_GPIO);
	shutdownSwitchPin=		iniparser_getint(ini, "gpio:gpio_shutdown", DEFAULT_SHUTDOWN_GPIO);
	networkStatusOn=		iniparser_getboolean(ini, "functions:enable_network_status_LED", 0);
	shutdownSwitchEnable=	iniparser_getboolean(ini, "functions:enable_shutdown_switch", 0);
	LoopDelayMs=			iniparser_getint(ini, "COS settings:COS_poll_loop_interval_ms", DEFAULT_LOOP_DELAY);
	TimeoutMs=				iniparser_getint(ini, "COS settings:COS_timeout_ms", DEFAULT_COS_TIMEOUT_MS);
	COStimeoutEnable=		iniparser_getboolean(ini, "COS settings:COS_timeout_enable", 1);
	netCheckDivisor=		iniparser_getint(ini, "COS settings:network_check_divisor", DEFAULT_NET_CHECK_DIVISOR);
	SDswitchActivateCount=	iniparser_getint(ini, "COS settings:shutdown_switch_activate_count", DEFAULT_SD_ACTIVATE_COUNT);

	const char KeyCmd[]="asterisk -rx \"susb tune menu-support K\"";
	const char UnkeyCmd[]="asterisk -rx \"susb tune menu-support k\"";
	
	unsigned int loopCount=0;
	
	if (access("/var/run/asterisk.ctl", F_OK) != 0)
	{
		fprintf(stderr, "\nAsterisk needs to be running first!  Exiting\n\n");
		exit(-1);
	}
		
	// Printf Config
	printf("\nCOSmon version %s\n", strVersion);
	printf("Config:\n");
	printf("\tCOS GPIO number: %u\n", ExtCOSPin);
	if (COStimeoutEnable==0)
		printf("\tCOS timeout disabled\n");
	else	
		printf("\tCOS timeout (ms): %u\n", TimeoutMs);
	printf("\tCOS check loop delay (ms): %u\n", LoopDelayMs);	
	printf("\tShutdown switch: %s\n", (shutdownSwitchEnable ? "ENABLED" : "DISABLED"));
	printf("\tNetwork connected Indicator: %s\n", (networkStatusOn ? "ENABLED" : "DISABLED"));
	printf("\tNetwork status GPIO number: %u\n", networkStatusPin);
	printf("\tShutdwon switch GPIO number: %d\n", shutdownSwitchPin);
	printf("\tNetwork check divisor: %u\n", netCheckDivisor);
	printf("\n");

	// Compute timeout count
	tempval=(float)TimeoutMs/(float)LoopDelayMs;
	tempval += 0.5;
	TimeoutCountCOS=(int)tempval;

	// Initialize so we don't get an immediate timeout on startup.
	TimeoutCount=-1;

	// initialize wiringPi and setup pins
	wiringPiSetup();
	pinMode(ExtCOSPin, INPUT);
	pinMode(networkStatusPin, OUTPUT);
	digitalWrite(networkStatusPin, LOW);
	pinMode(shutdownSwitchPin, INPUT);
	pullUpDnControl(shutdownSwitchPin, PUD_UP) ;

	printf("COSmon running\n");

	// Initialize change detection vars
	LastCOSState=digitalRead(ExtCOSPin);
	
	// Unkey asterisk
	system(UnkeyCmd);

	for(;;)  // forever
	{
		CurrCOSState=digitalRead(ExtCOSPin);
		
		// check for change is COS pin state
		if (LastCOSState!=CurrCOSState)
		{
			// Something has changed...
			// (we only do something when COS changes so we don't continually
			// call asterisk for no reason every time throgh the loop.
			// this seems to cause loading issues.)

			// Read COS pin state
			if (CurrCOSState==HIGH)
			{
				// Key asterisk
				system(KeyCmd);
				TimeoutCount = TimeoutCountCOS;
			}
			else
			{
				// Unkey asterisk
				system(UnkeyCmd);
			}
			LastCOSState=CurrCOSState;
		}
		else if (COStimeoutEnable)
		{
			// Nothing has changed.  Check for COS stuck high.
			if (TimeoutCount>0 && CurrCOSState==HIGH)
				TimeoutCount--;
			else if (TimeoutCount==0)
			{
				// Timeout has been reached, unkey the node.  Only once...  When count=0.  
				printf("COS Timeout\n");
				system(UnkeyCmd);
				TimeoutCount=-1;
			}
		}
		
		// Handle shutdown switch.  Needs to be pressed for SDswitchActivateCount times through the loop
		if (digitalRead(shutdownSwitchPin)==LOW)	// Active low
		{
			SDswitchPressedCount++;
			if (SDswitchPressedCount>SDswitchActivateCount)
			{
				// Turn of network light as acknokwledge
				digitalWrite(networkStatusPin, HIGH);
				printf("Shutting down!\n");
				system("/usr/local/sbin/astdn.sh");
				delay(5000);
				system("/usr/bin/poweroff");
				break;
			}
			
		}
		else
			SDswitchPressedCount=0;
		
		
		// if enabled check network status, but only netCheckDivisor times through the main loop
		if ((loopCount++ % netCheckDivisor)==0 && networkStatusOn)  
			wifiLightHandler();
		delay(LoopDelayMs);
	}
	
	// ......Can't actually get here....
	// Close out ini
	iniparser_freedict(ini);

	return RETVAL_OK;
}
