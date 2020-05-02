/* onewire.h - a part of avr-ds18b20 library
 *
 * Copyright (C) 2016 Jacek Wieczorek
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

/**
	\file
	\brief 1wire protocol functions
*/

#ifndef ONEWIRE_H
#define ONEWIRE_H

#include <inttypes.h>

#define ONEWIRE_ERROR_OK 	0 //! Communication success
#define ONEWIRE_ERROR_COMM 	1 //! Communication failure

extern volatile uint8_t * const onewire_port;
extern volatile uint8_t * const onewire_direction;
extern volatile uint8_t * const onewire_portin;
extern const uint8_t onewire_mask;

/**
	\brief Initializes 1wire bus (basically sends a reset pulse)
	\param port A pointer to the port output register
	\param direction A pointer to the port direction register
	\param portin A pointer to the port input register
	\param mask A bit mask, determining to which pin the device is connected
	\returns \ref ONEWIRE_ERROR_OK on success
*/
extern uint8_t onewireInit();

/**
	\brief Sends a single bit over 1wire bus
	\param port A pointer to the port output register
	\param direction A pointer to the port direction register
	\param portin A pointer to the port input register
	\param mask A bit mask, determining to which pin the device is connected
	\param bit The bit value
	\returns the bit value
*/
extern uint8_t onewireWriteBit(uint8_t bit);

/**
	\brief Sends a byte over 1wire bus
	\param port A pointer to the port output register
	\param direction A pointer to the port direction register
	\param portin A pointer to the port input register
	\param mask A bit mask, determining to which pin the device is connected
	\param data Data byte to be sent
*/
extern void onewireWrite(uint8_t data);

/**
	\brief Reads a single bit from 1wire bus
	\param port A pointer to the port output register
	\param direction A pointer to the port direction register
	\param portin A pointer to the port input register
	\param mask A bit mask, determining to which pin the device is connected
	\returns received bit value
*/
extern uint8_t onewireReadBit();

/**
	\brief Reads a byte from 1wire bus
	\param port A pointer to the port output register
	\param direction A pointer to the port direction register
	\param portin A pointer to the port input register
	\param mask A bit mask, determining to which pin the device is connected
	\returns received byte value
*/
extern uint8_t onewireRead();

#endif
