/*
 * Modified by Alex Norman 6/3/2009 to work with the Encoder Board
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
 *  Main source file for the MIDI input demo. This file contains the main tasks of the demo and
 *  is responsible for the initial application hardware configuration.
 */

#include "MIDI.h"
#include "RingBuff.h"
#include <util/delay.h>
#include <avr/eeprom.h>

/* Scheduler Task List */
TASK_LIST
{
	{ .Task = USB_USBTask          , .TaskStatus = TASK_STOP },
		{ .Task = USB_MIDI_Task        , .TaskStatus = TASK_STOP },
		{ .Task = BUTTONS_Task        , .TaskStatus = TASK_STOP },
		{ .Task = LEDS_Task        , .TaskStatus = TASK_STOP },
};

//hold the data to send
RingBuff_t midiout_buf;

#define HISTORY 4

volatile uint16_t leds[4];
volatile uint8_t led_col;
volatile uint8_t row;
volatile uint8_t history;
volatile uint16_t button_history[HISTORY];
volatile uint16_t button_last;

int main(void)
{
	uint8_t i;

	row = 0;
	history = 0;

	_delay_ms(100);
	/* Disable watchdog if enabled by bootloader/fuses */
	MCUSR &= ~(1 << WDRF);
	wdt_disable();

	/* Disable clock division */
	clock_prescale_set(clock_div_1);

	//init ringbuffers
	Buffer_Initialize(&midiout_buf);

	//LED ground outputs and switch inputs
	DDRC = 0x55;
	DDRA = 0xFF;
	DDRE = 0xc3;

	//turn on button pullups
	PORTC |= 0xAA;

	//turn leds off
	PORTC |= 0x55;
	PORTA = 0x00;
	PORTE &= ~(0xc3);

	//button grounds
	DDRB |= 0xF0;
	PORTB = 0xF0;

	PORTB = (PORTB & 0x0F) | ~(0x10 << row);

	for(i = 0; i < 4; i++)
		leds[0] = 0;
	for(i = 0; i < HISTORY; i++)
		button_history[i] = 0;
	button_last = 0;


	/* Indicate USB not ready */
	UpdateStatus(Status_USBNotReady);

	/* Initialize Scheduler so that it can be used */
	Scheduler_Init();

	/* Initialize USB Subsystem */
	USB_Init();

	Scheduler_SetTaskMode(LEDS_Task, TASK_RUN);

	/* Scheduling - routine never returns, so put this last in the main function */
	Scheduler_Start();
}

/** Event handler for the USB_Connect event. This indicates that the device is enumerating via the status LEDs. */
EVENT_HANDLER(USB_Connect)
{
	/* Start USB management task */
	Scheduler_SetTaskMode(USB_USBTask, TASK_RUN);

	/* Indicate USB enumerating */
	UpdateStatus(Status_USBEnumerating);
}

/** Event handler for the USB_Disconnect event. This indicates that the device is no longer connected to a host via
 *  the status LEDs, disables the sample update and PWM output timers and stops the USB and MIDI management tasks.
 */
EVENT_HANDLER(USB_Disconnect)
{
	/* Stop running audio and USB management tasks */
	Scheduler_SetTaskMode(USB_MIDI_Task, TASK_STOP);
	Scheduler_SetTaskMode(USB_USBTask, TASK_STOP);
	Scheduler_SetTaskMode(BUTTONS_Task, TASK_STOP);

	/* Indicate USB not ready */
	UpdateStatus(Status_USBNotReady);
}

/** Event handler for the USB_ConfigurationChanged event. This is fired when the host set the current configuration
 *  of the USB device after enumeration - the device endpoints are configured and the MIDI management task started.
 */
EVENT_HANDLER(USB_ConfigurationChanged)
{
	/* Setup MIDI stream endpoints */
	Endpoint_ConfigureEndpoint(MIDI_STREAM_OUT_EPNUM, EP_TYPE_BULK,
			ENDPOINT_DIR_OUT, MIDI_STREAM_EPSIZE,
			ENDPOINT_BANK_SINGLE);

	Endpoint_ConfigureEndpoint(MIDI_STREAM_IN_EPNUM, EP_TYPE_BULK,
			ENDPOINT_DIR_IN, MIDI_STREAM_EPSIZE,
			ENDPOINT_BANK_SINGLE);

	/* Indicate USB connected and ready */
	UpdateStatus(Status_USBReady);

	/* Start MIDI task */
	Scheduler_SetTaskMode(USB_MIDI_Task, TASK_RUN);
	Scheduler_SetTaskMode(BUTTONS_Task, TASK_RUN);
}

/** Task to handle the generation of MIDI note change events in response to presses of the board joystick, and send them
 *  to the host.
 */
TASK(USB_MIDI_Task)
{
	/* Select the MIDI IN stream */
	Endpoint_SelectEndpoint(MIDI_STREAM_IN_EPNUM);

	/* Check if endpoint is ready to be written to */
	if (Endpoint_IsINReady())
	{
		if (midiout_buf.Elements > 2){
			/* Wait until Serial Tx Endpoint Ready for Read/Write */
			while (!(Endpoint_IsINReady()));
			if(Endpoint_BytesInEndpoint() < MIDI_STREAM_EPSIZE){
				uint8_t chan = Buffer_GetElement(&midiout_buf);
				//the channel always has the top bit set.. 
				//this way we make sure to line up data if data is dropped
				//so if we don't have the top bit set, don't grab more data..
				//we can catch up next time
				if(chan & 0x80){
					uint8_t addr = Buffer_GetElement(&midiout_buf);
					uint8_t val = Buffer_GetElement(&midiout_buf);
					SendMIDICC(addr & 0x7F, val & 0x7F, 0, chan & 0x0F);
				}
			}
		}
	}

	/* Select the MIDI OUT stream */
	Endpoint_SelectEndpoint(MIDI_STREAM_OUT_EPNUM);

	// Clear the endpoint buffer
	Endpoint_ClearOUT();
}

TASK(LEDS_Task)
{
	//turn all them off
	PORTC |= 0x55;
	PORTA = (uint8_t)leds[led_col];
	PORTE = 0;
	PORTE = (leds[led_col] >> 8) & 0x03;
	PORTE |= (leds[led_col] >> 4) & 0xc0;
	//set the col
	PORTC &= ~(0x1 << (led_col << 1));
	led_col = (led_col + 1) % 4;
}

TASK(BUTTONS_Task)
{
	uint8_t row_offset = 4 * row;
	uint8_t i, j;

	for(i = 0; i < 4; i++){
		//zero out the bit we're working with
		button_history[history] &= ~((uint16_t)(1 << (i + row_offset)));
		//set the bit
		button_history[history] |= (uint16_t)((~PINC >> (1 + (i * 2))) & 0x1) << (i + row_offset);
	}

	//debounce
	for(i = 0; i < 4; i++){
		bool down = (bool)(0x1 & (button_history[0] >> (i + row_offset)));
		bool consistent = true;
		for(j = 1; j < HISTORY; j++){
			if(down != (bool)(0x1 & (button_history[j] >> (i + row_offset)))){
				consistent = false;
				break;
			}
		}
		if(consistent){
			if(down){
				if(!((button_last >> (i + row_offset)) & 0x1)){
					leds[i] |= (0x1 << (2 + 3 * row));
					button_last |= 1 << (i + row_offset);
					Buffer_StoreElement(&midiout_buf, 0x80);
					Buffer_StoreElement(&midiout_buf, (i + row_offset));
					Buffer_StoreElement(&midiout_buf, 127);
				}
			} else {
				if((button_last >> (i + row_offset)) & 0x1){
					leds[i] &= ~(0x1 << (2 + 3 * row));
					button_last &= ~(1 << (i + row_offset));
					Buffer_StoreElement(&midiout_buf, 0x80);
					Buffer_StoreElement(&midiout_buf, (i + row_offset));
					Buffer_StoreElement(&midiout_buf, 0);
				}
			}
		}
	}

	/*
	if(~PINC & 0x2)
		leds[0] |= (0x1 << (1 + 3 * row));
	else
		leds[0] &= ~(0x1 << (1 + 3 * row));
	if(~PINC & 0x8)
		leds[1] |= (0x1 << (1 + 3 * row));
	else
		leds[1] &= ~(0x1 << (1 + 3 * row));
	if(~PINC & 0x20)
		leds[2] |= (0x1 << (1 + 3 * row));
	else
		leds[2] &= ~(0x1 << (1 + 3 * row));
	if(~PINC & 0x80)
		leds[3] |= (0x1 << (1 + 3 * row));
	else
		leds[3] &= ~(0x1 << (1 + 3 * row));
		*/

	//increment the history index
	if(row == 3)
		history = (history + 1) % HISTORY;

	row = (row + 1) % 4;
	PORTB = (PORTB & 0x0F) | ~(0x10 << row);

}

/** Function to manage status updates to the user. This is done via LEDs on the given board, if available, but may be changed to
 *  log to a serial port, or anything else that is suitable for status updates.
 *
 *  \param CurrentStatus  Current status of the system, from the MIDI_StatusCodes_t enum
 */
void UpdateStatus(uint8_t CurrentStatus)
{
	//by default turn off the LED
	PORTD |= _BV(PORTD6);

	switch (CurrentStatus)
	{
		case Status_USBNotReady:
			break;
		case Status_USBEnumerating:
			break;
		case Status_USBReady:
			//Turn on the LED when we are ready
			PORTD &= ~_BV(PORTD6);
			break;
	}
}

/** Sends a MIDI note change event (note on or off) to the MIDI output jack, on the given virtual cable ID and channel.
 *
 *  \param Pitch    Pitch of the note to turn on or off
 *  \param OnOff    Set to true if the note is on (being held down), or false otherwise
 *  \param CableID  ID of the virtual cable to send the note change to
 *  \param Channel  MIDI channel number to send the note change event to
 */
void SendMIDINoteChange(const uint8_t Pitch, const bool OnOff, const uint8_t CableID, const uint8_t Channel)
{
	/* Wait until endpoint ready for more data */
	while (!(Endpoint_IsReadWriteAllowed()));

	/* Check if the message should be a Note On or Note Off command */
	uint8_t Command = ((OnOff)? MIDI_COMMAND_NOTE_ON : MIDI_COMMAND_NOTE_OFF);

	/* Write the Packet Header to the endpoint */
	Endpoint_Write_Byte((CableID << 4) | (Command >> 4));

	/* Write the Note On/Off command with the specified channel, pitch and velocity */
	Endpoint_Write_Byte(Command | Channel);
	Endpoint_Write_Byte(Pitch);
	Endpoint_Write_Byte(MIDI_STANDARD_VELOCITY);

	/* Send the data in the endpoint to the host */
	Endpoint_ClearIN();
}

void SendMIDICC(const uint8_t num, const uint8_t val, const uint8_t CableID, const uint8_t Channel)
{
	/* Wait until endpoint ready for more data */
	while (!(Endpoint_IsReadWriteAllowed()));

	/* Check if the message should be a Note On or Note Off command */
	uint8_t Command = MIDI_COMMAND_CC;

	/* Write the Packet Header to the endpoint */
	Endpoint_Write_Byte((CableID << 4) | (Command >> 4));

	Endpoint_Write_Byte(Command | Channel);
	Endpoint_Write_Byte(num);
	Endpoint_Write_Byte(val);

	/* Send the data in the endpoint to the host */
	Endpoint_ClearIN();
}

void SendSysex(const uint8_t * buf, const uint8_t len, const uint8_t CableID)
{
	if(len == 0)
		return;
	else if(len == 1){
		/* Wait until endpoint ready for more data */
		while (!(Endpoint_IsReadWriteAllowed()));
		/* Write the Packet Header to the endpoint */
		Endpoint_Write_Byte((CableID << 4) | 0x7);
		Endpoint_Write_Byte(SYSEX_BEGIN);
		Endpoint_Write_Byte(buf[0]);
		Endpoint_Write_Byte(SYSEX_END);
		/* Send the data in the endpoint to the host */
		Endpoint_ClearIN();
	} else {
		uint8_t i;
		//write the first packet
		
		// Wait until endpoint ready for more data
		while (!(Endpoint_IsReadWriteAllowed()));
		// Write the Packet Header to the endpoint
		Endpoint_Write_Byte((CableID << 4) | 0x4);
		Endpoint_Write_Byte(SYSEX_BEGIN);
		Endpoint_Write_Byte(buf[0]);
		Endpoint_Write_Byte(buf[1]);
		// Send the data in the endpoint to the host
		Endpoint_ClearIN();

		//write intermediate bytes
		for(i = 2; (i + 2) < len; i += 3){
			// Wait until endpoint ready for more data
			while (!(Endpoint_IsReadWriteAllowed()));
			// Write the Packet Header to the endpoint
			Endpoint_Write_Byte((CableID << 4) | 0x4);
			Endpoint_Write_Byte(buf[i]);
			Endpoint_Write_Byte(buf[i + 1]);
			Endpoint_Write_Byte(buf[i + 2]);
			// Send the data in the endpoint to the host
			Endpoint_ClearIN();
		}

		// Wait until endpoint ready for more data
		while (!(Endpoint_IsReadWriteAllowed()));
		// Write the Packet Header to the endpoint
		switch((len - 2) % 3){
			case 0:
				Endpoint_Write_Byte((CableID << 4) | 0x5);
				Endpoint_Write_Byte(SYSEX_END);
				Endpoint_Write_Byte(0);
				Endpoint_Write_Byte(0);
				break;
			case 1:
				Endpoint_Write_Byte((CableID << 4) | 0x6);
				Endpoint_Write_Byte(buf[len - 1]);
				Endpoint_Write_Byte(SYSEX_END);
				Endpoint_Write_Byte(0);
				break;
			case 2:
				Endpoint_Write_Byte((CableID << 4) | 0x7);
				Endpoint_Write_Byte(buf[len - 2]);
				Endpoint_Write_Byte(buf[len - 1]);
				Endpoint_Write_Byte(SYSEX_END);
				break;
		}
		// Send the data in the endpoint to the host
		Endpoint_ClearIN();
	}
}
