CC=gcc
CFLAGS=-I. -Wall -Wextra

aslLCD: COSmon.o getIP.o ini.o
	$(CC) -Wall -Wextra -o COSmon COSmon.o getIP.o ini.o $(CFLAGS) -lwiringPi -lwiringPiDev -lpthread -lm -lcrypt -lrt -liniparser

