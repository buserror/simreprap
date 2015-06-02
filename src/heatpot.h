/*
	heatpot.h

	Copyright 2008-2012 Michel Pollet <buserror@gmail.com>

 	This file is part of simavr.

	simavr is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	simavr is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with simavr.  If not, see <http://www.gnu.org/licenses/>.
 */


#ifndef __HEATPOT_H___
#define __HEATPOT_H___

#include "sim_irq.h"

/* A 'heatpot' is a small system that keeps track of a running temperature
 * and applies various 'bias' at regular interval. It always tries to
 * move toward an 'ambiant' temperature and will settle there if no other
 * bias is applied.
 * If a bias is applied (for example, a positive bias for when a heater
 * is on) the temperature will rise steadily, while still being dragged
 * down by any other negative bias (drift toward ambiant, a fan etc)
 *
 * This is used to simulate the heated hotend and hotbeds
 */

enum {
	IRQ_HEATPOT_TALLY = 0,		// heatpot_data_t
	IRQ_HEATPOT_TEMP_OUT,		// Celcius * 256
	IRQ_HEATPOT_COUNT
};

#define HEATPOT_RESAMPLE_US 100000 /* 100ms */

typedef union {
	int32_t sid : 8, cost;
	uint32_t v;
} heatpot_data_t;

typedef struct heatpot_t {
	avr_irq_t *	irq;		// irq list
	struct avr_t * avr;
	char name[32];

	struct { float cost; } tally[4];

	float ambiant;
	float current;

	avr_cycle_count_t	cycle;
} heatpot_t, *heatpot_p;

void
heatpot_init(
		struct avr_t * avr,
		heatpot_p p,
		const char * name,
		float ambiant );

void
heatpot_tally(
		heatpot_p p,
		uint8_t sid,
		float cost );

#endif /* __HEATPOT_H___ */
