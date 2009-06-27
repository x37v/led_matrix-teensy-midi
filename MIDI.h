/*
             LUFA Library
     Copyright (C) Dean Camera, 2009.
              
  dean [at] fourwalledcubicle [dot] com
      www.fourwalledcubicle.com
*/

/*
  Copyright 2009  Dean Camera (dean [at] fourwalledcubicle [dot] com)

  Permission to use, copy, modify, and distribute this software
  and its documentation for any purpose and without fee is hereby
  granted, provided that the above copyright notice appear in all
  copies and that both that the copyright notice and this
  permission notice and warranty disclaimer appear in supporting
  documentation, and that the name of the author not be used in
  advertising or publicity pertaining to distribution of the
  software without specific, written prior permission.

  The author disclaim all warranties with regard to this
  software, including all implied warranties of merchantability
  and fitness.  In no event shall the author be liable for any
  special, indirect or consequential damages or any damages
  whatsoever resulting from loss of use, data or profits, whether
  in an action of contract, negligence or other tortious action,
  arising out of or in connection with the use or performance of
  this software.
*/

/** \file
 *
 *  Header file for AudioOutput.c.
 */

#ifndef _AUDIO_OUTPUT_H_
#define _AUDIO_OUTPUT_H_

#define VERSION 1
#define HISTORY 4
#define NUMBOARDS 2
#define ENC_BTN_CC_OFFSET 16
#define BTN_CC_OFFSET 32

typedef enum {
	GET_VERSION = 0,
	GET_NUM_BOARDS = 1,
	GET_ENCODER_DATA = 2,
	GET_BUTTON_DATA = 3,
	SET_ENCODER_DATA = 4,
	SET_BUTTON_DATA = 5,
	RET_VERSION = 6,
	RET_NUM_BOARDS = 7,
	RET_ENCODER_DATA = 8,
	RET_BUTTON_DATA = 9,
	SYSEX_INVALID = 10
} sysex_t;

/* Includes: */
#include <avr/io.h>
#include <avr/wdt.h>
#include <avr/power.h>
#include <stdbool.h>

#include "Descriptors.h"

#include <LUFA/Version.h>                            // Library Version Information
#include <LUFA/Drivers/USB/USB.h>                    // USB Functionality
#include <LUFA/Scheduler/Scheduler.h>                // Simple scheduler for task management

typedef struct {
	//which midi channel and which cc number (or multiplier)
	uint8_t chan;
	uint8_t num;
} midi_cc_t;

//these are settings for the encoders
//if set then we send absolute not relative values [and keep a count]
#define ENC_ABSOLUTE 0x1
//if set then we only send data when we are at a detent
#define ENC_DETENT_ONLY 0x2
//if set then the button acts as a multiplier not its own cc
#define ENC_BUTTON_MUL 0x10

//falid flags for encoders
#define ENC_FLAGS (ENC_ABSOLUTE | ENC_DETENT_ONLY | ENC_BUTTON_MUL)

typedef struct {
	//settings
	uint8_t flags;

	//which midi channel and which cc number
	uint8_t chan;
	uint8_t num;

	//button
	midi_cc_t btn;
} encoder_t;

/* Macros: */
/** MIDI command for a note on (activation) event */
#define MIDI_COMMAND_NOTE_ON         0x90

/** MIDI command for a note off (deactivation) event */
#define MIDI_COMMAND_NOTE_OFF        0x80

#define MIDI_COMMAND_CC         0xB0

/** Standard key press velocity value used for all note events, as no pressure sensor is mounted */
#define MIDI_STANDARD_VELOCITY       64

#define SYSEX_BEGIN 0xF0
#define SYSEX_END 0xF7

//This ID is for educational or development use only
#define SYSEX_EDUMANUFID 0x7D

//spells 'buzzr' in ascii
//0, our first product
const uint8_t sysex_header[] = {SYSEX_EDUMANUFID, 98, 117, 122, 122, 114, 0};
#define SYSEX_HEADER_SIZE 7

//just the header back
const uint8_t sysex_ack[] = {SYSEX_EDUMANUFID, 98, 117, 122, 122, 114, 0};
#define SYSEX_ACK_SIZE 7

//the header, code, boardcount
const uint8_t sysex_boards[] = {SYSEX_EDUMANUFID, 98, 117, 122, 122, 114, 0, RET_NUM_BOARDS, NUMBOARDS};
#define SYSEX_BOARD_CNT_SIZE 9

//the header, code, version
const uint8_t sysex_version[] = {SYSEX_EDUMANUFID, 98, 117, 122, 122, 114, 0, RET_VERSION, VERSION};
#define SYSEX_VERSION_SIZE 9

/** Convenience macro. MIDI channels are numbered from 1-10 (natural numbers) however the logical channel
 *  addresses are zero-indexed. This converts a natural MIDI channel number into the logical channel address.
 *
 *  \param channel  MIDI channel number to address
 */
#define MIDI_CHANNEL(channel)        (channel - 1)

/* Enums: */
/** Enum for the possible status codes for passing to the UpdateStatus() function. */
enum MIDI_StatusCodes_t
{
	Status_USBNotReady    = 0, /**< USB is not ready (disconnected from a USB host) */
	Status_USBEnumerating = 1, /**< USB interface is enumerating */
	Status_USBReady       = 2, /**< USB interface is connected and ready */
};

/* Task Definitions: */
TASK(USB_MIDI_Task);
TASK(BUTTONS_Task);
TASK(LEDS_Task);

/* Event Handlers: */
/** Indicates that this module will catch the USB_Connect event when thrown by the library. */
HANDLES_EVENT(USB_Connect);

/** Indicates that this module will catch the USB_Disconnect event when thrown by the library. */
HANDLES_EVENT(USB_Disconnect);

/** Indicates that this module will catch the USB_ConfigurationChanged event when thrown by the library. */
HANDLES_EVENT(USB_ConfigurationChanged);

/* Function Prototypes: */
void SendMIDINoteChange(const uint8_t Pitch, const bool OnOff,
		const uint8_t CableID, const uint8_t Channel);		
void SendMIDICC(const uint8_t num, const uint8_t val, 
		const uint8_t CableID, const uint8_t Channel);

//send a sysex message contained in buf
//automatically adds the beg and end messages to it
void SendSysex(const uint8_t * buf, const uint8_t len, 
		const uint8_t CableID);

void UpdateStatus(uint8_t CurrentStatus);


#endif
