/*
 * Modified by Alex Norman 6/29/2009 to work with the LED matrix
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

//spells 'buzzr' in ascii
//1, our 2nd product
const uint8_t sysex_header[] = {SYSEX_EDUMANUFID, 98, 117, 122, 122, 114, 1};
#define SYSEX_HEADER_SIZE 7

//just the header back
const uint8_t sysex_ack[] = {SYSEX_EDUMANUFID, 98, 117, 122, 122, 114, 1};
#define SYSEX_ACK_SIZE 7

//the header, code, version
const uint8_t sysex_version[] = {SYSEX_EDUMANUFID, 98, 117, 122, 122, 114, 1, RET_VERSION, VERSION};
#define SYSEX_VERSION_SIZE 9

//index, chan, num, flags, color
uint8_t sysex_button_data[] = {SYSEX_EDUMANUFID, 98, 117, 122, 122, 114, 1, RET_BUTTON_DATA, 0, 1, 2, 3, 4};
#define SYSEX_BUTTON_DATA_SIZE 13

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
//to hold commands
RingBuff_t cmd_buf;

volatile uint16_t leds[4];
volatile uint8_t led_col;
volatile uint8_t row;
volatile uint8_t history;
volatile uint16_t button_history[HISTORY];
volatile uint16_t button_last;
volatile uint16_t button_toggle; //the toggle state.. 1 means down, 0 means up

volatile bool send_ack;
volatile bool send_version;
volatile bool sysex_in;
volatile uint8_t sysex_in_cnt;
volatile sysex_t sysex_in_type;
volatile uint8_t sysex_setting_index;

volatile midi_cc_t button_settings[NUM_BUTTONS];

//remap row, column to an index
uint8_t index_mapping(uint8_t row, uint8_t col){
	return row * 4 + (3 - col);
}

int main(void)
{
	uint8_t i, j;

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
	Buffer_Initialize(&cmd_buf);

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

	//init history and settings
	for(i = 0; i < 4; i++)
		leds[0] = 0;
	for(i = 0; i < HISTORY; i++)
		button_history[i] = 0;
	button_toggle = button_last = 0;

	for(i = 0; i < NUM_BUTTONS; i++){
		button_settings[i].chan = 0;
		button_settings[i].num = i;
		button_settings[i].flags = BTN_LED_MIDI_DRIVEN;
		button_settings[i].color = 0x1;
	}

	//XXX tmp
	for(i = 4; i < 8; i++){
		button_settings[i].flags = BTN_TOGGLE | BTN_LED_MIDI_DRIVEN;
		button_settings[i].color = 0x2 | (0x4 << 3);
	}

	//init led state [all buttons are up]
	for(i = 0; i < 4; i++){
		for(j = 0; j < 4; j++){
			//if the leds are being driven by MIDI then don't set them
			if(!(button_settings[i + j * 4].flags & BTN_LED_MIDI_DRIVEN))
				leds[i] |= ((button_settings[i + j * 4].color >> 3) & 0x7) << (3 * j);
		}
	}

	send_version = send_ack = false;
	sysex_in = false;
	sysex_in_cnt = 0;
	sysex_in_type = SYSEX_INVALID;

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
	uint8_t i, j;
	/* Select the MIDI IN stream */
	Endpoint_SelectEndpoint(MIDI_STREAM_IN_EPNUM);

	/* Check if endpoint is ready to be written to */
	if (Endpoint_IsINReady())
	{
		if(send_ack){
			send_ack = false;
			while (!(Endpoint_IsINReady()));
			SendSysex(sysex_ack, SYSEX_ACK_SIZE, 0);
		}
		if(send_version){
			send_version = false;
			while (!(Endpoint_IsINReady()));
			SendSysex(sysex_version, SYSEX_VERSION_SIZE, 0);
		}

		while(cmd_buf.Elements){
			i = Buffer_GetElement(&cmd_buf);
			//fill the buffer
			//index, chan, num, flags, color
			button_settings[i].num;
			sysex_button_data[SYSEX_BUTTON_DATA_SIZE - 5] = i;
			sysex_button_data[SYSEX_BUTTON_DATA_SIZE - 4] = button_settings[i].chan;
			sysex_button_data[SYSEX_BUTTON_DATA_SIZE - 3] = button_settings[i].num;
			sysex_button_data[SYSEX_BUTTON_DATA_SIZE - 2] = button_settings[i].flags;
			sysex_button_data[SYSEX_BUTTON_DATA_SIZE - 1] = button_settings[i].color;
			while (!(Endpoint_IsINReady()));
			SendSysex(sysex_button_data, SYSEX_BUTTON_DATA_SIZE, 0);
		}

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

	if (Endpoint_IsOUTReceived()){
		while (Endpoint_BytesInEndpoint()){
			uint8_t byte[3];
			//always comes in packets of 4 bytes
			byte[0] = Endpoint_Read_Byte();
			//ditch the first byte and grab the next 3
			byte[0] = Endpoint_Read_Byte();
			byte[1] = Endpoint_Read_Byte();
			byte[2] = Endpoint_Read_Byte();

			//if this is a CC input deal with that
			if((byte[0] & 0xF0) == MIDI_COMMAND_CC){
				sysex_in = false;
				for(j = 0; j < NUM_BUTTONS; j++){
					if((button_settings[j].flags & BTN_LED_MIDI_DRIVEN) &&
							((byte[0] & 0x0F) == button_settings[j].chan)){
						//does the num match
						if(button_settings[j].num == byte[1]){
							uint8_t index = 3 - (j % 4);
							uint8_t shift = 3 * (j / 4);
							//clear
							leds[index] &= ~(0x7 << shift);
							//set
							leds[index] |=  (byte[2] & 0x7) << shift;
							break;
						}
					} 
				}
			} else {
				for(i = 0; i < 3; i++){
					//otherwise, maybe it is sysex data?
					if(byte[i] == SYSEX_BEGIN){
						sysex_in = true;
						sysex_in_cnt = 0;
					} else if(byte[i] == SYSEX_END){
						//if we were in sysex mode and we just got a header [just a ping]
						//send an ack
						if(sysex_in && sysex_in_cnt == SYSEX_HEADER_SIZE)
							send_ack = true;
						sysex_in = false;
						break;
					} else if(byte[i] & 0x80){
						sysex_in = false;
						break;
					} else if(sysex_in){
						//match the header
						if(sysex_in_cnt < SYSEX_HEADER_SIZE){
							if(!sysex_header[sysex_in_cnt] == byte[i]){
								sysex_in = false;
								break;
							}
						} else {
							//here we have matched the header and we're parsing input data
							uint8_t index = sysex_in_cnt - SYSEX_HEADER_SIZE;
							//if the index is 0 then we're matching the type
							if(index == 0){
								if(byte[i] == GET_VERSION){
									send_version = true;
									sysex_in = false;
									break;
								} else if (byte[i] < SYSEX_INVALID){
									sysex_in_type = byte[i];
								} else {
									sysex_in_type = SYSEX_INVALID;
									sysex_in = false;
									break;
								}
							} else if(index == 1){
								if (sysex_in_type == SET_BUTTON_DATA)
									sysex_setting_index = byte[i];
								else if(sysex_in_type == GET_BUTTON_DATA){
									if(byte[i] < NUM_BUTTONS)
										Buffer_StoreElement(&cmd_buf, byte[i]);
									sysex_in = false;
									sysex_in_type = SYSEX_INVALID;
								}
							} else if(index > 1) { 
								if(sysex_in_type == SET_BUTTON_DATA){
									//make sure we're in range
									if(sysex_setting_index < NUM_BUTTONS){
										switch(index){
											case 2:
												button_settings[sysex_setting_index].chan = byte[i] & 0x0F;
												break;
											case 3:
												button_settings[sysex_setting_index].num = byte[i] & 0x7F;
												break;
											case 4:
												button_settings[sysex_setting_index].flags = byte[i];
												break;
											case 5:
												button_settings[sysex_setting_index].color = byte[i] & 0x3F;
												//clear
												leds[3 - (sysex_setting_index % 4)] &= ~(0x7 << (3 * (sysex_setting_index / 4)));
												//set
												leds[3 - (sysex_setting_index % 4)] |= 
													((button_settings[sysex_setting_index].color >> 3) & 0x7) << (3 * (sysex_setting_index / 4));
												send_ack = true;
												//just fall through
											default:
												sysex_in = false;
												sysex_in_type = SYSEX_INVALID;
												break;
										}
									} else {
										sysex_in = false;
										sysex_in_type = SYSEX_INVALID;
										break;
									}
								} else {
									sysex_in = false;
									sysex_in_type = SYSEX_INVALID;
									break;
								}
							}
						}
						sysex_in_cnt++;
					}
				}
			}
		}
		// Clear the endpoint buffer
		Endpoint_ClearOUT();
	}
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
	uint8_t i, j;

	for(i = 0; i < 4; i++){
		uint8_t index = index_mapping(row, i);
		//zero out the bit we're working with
		button_history[history] &= ~((uint16_t)(1 << index));
		//set the bit
		button_history[history] |= (uint16_t)((~PINC >> (1 + (i * 2))) & 0x1) << index;
	}

	//debounce
	for(i = 0; i < 4; i++){
		uint8_t index = index_mapping(row, i);
		bool down = (bool)(0x1 & (button_history[0] >> index));
		bool consistent = true;
		for(j = 1; j < HISTORY; j++){
			if(down != (bool)(0x1 & (button_history[j] >> index))){
				consistent = false;
				break;
			}
		}
		if(consistent){
			if(down){
				if(!((button_last >> index) & 0x1)){
					button_last |= 1 << index;
					//if we're not in toggle mode just send out data
					if(!(button_settings[index].flags & BTN_TOGGLE)){
						Buffer_StoreElement(&midiout_buf, 0x80 | button_settings[index].chan);
						Buffer_StoreElement(&midiout_buf, button_settings[index].num);
						Buffer_StoreElement(&midiout_buf, 127);
						//if the LEDS are not midi driven, set them
						if(!(button_settings[index].flags & BTN_LED_MIDI_DRIVEN)){
							//clear
							leds[i] &= ~(0x7 << (3 * row));
							//set
							leds[i] |= (button_settings[index].color & 0x7) << (3 * row);
						}
						//if we're in toggle mode we have to know the toggle state
					} else {
						//swap states
						button_toggle ^= (uint16_t)(0x1 << index);
						//down
						if(button_toggle & (uint16_t)(0x1 << index)){
							Buffer_StoreElement(&midiout_buf, 0x80 | button_settings[index].chan);
							Buffer_StoreElement(&midiout_buf, button_settings[index].num);
							Buffer_StoreElement(&midiout_buf, 127);
							//if the LEDS are not midi driven, set them
							if(!(button_settings[index].flags & BTN_LED_MIDI_DRIVEN)){
								//clear
								leds[i] &= ~(0x7 << (3 * row));
								//set
								leds[i] |= (button_settings[index].color & 0x7) << (3 * row);
							}
						} else {
							//up
							Buffer_StoreElement(&midiout_buf, 0x80 | button_settings[index].chan);
							Buffer_StoreElement(&midiout_buf, button_settings[index].num);
							Buffer_StoreElement(&midiout_buf, 0);
							//if the LEDS are not midi driven, set them
							if(!(button_settings[index].flags & BTN_LED_MIDI_DRIVEN)){
								//clear
								leds[i] &= ~(0x7 << (3 * row));
								//set
								leds[i] |= ((button_settings[index].color >> 3) & 0x7) << (3 * row);
							}
						}
					}
				}
			} else {
				if((button_last >> index) & 0x1){
					button_last &= ~(1 << index);
					//in toggle mode we don't do anything on 'up'
					if(!(button_settings[index].flags & BTN_TOGGLE)){
						Buffer_StoreElement(&midiout_buf, 0x80 | button_settings[index].chan);
						Buffer_StoreElement(&midiout_buf, button_settings[index].num);
						Buffer_StoreElement(&midiout_buf, 0);
						//if the LEDS are not midi driven, set them
						if(!(button_settings[index].flags & BTN_LED_MIDI_DRIVEN)){
							//clear
							leds[i] &= ~(0x7 << (3 * row));
							//set
							leds[i] |= ((button_settings[index].color >> 3) & 0x7) << (3 * row);
						}
					}
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
