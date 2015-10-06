/*
 * pathplotter.h
 *
 *  Created on: 2 Oct 2015
 *      Author: michel
 */

#ifndef _PATHPLOTTER_H_
#define _PATHPLOTTER_H_

#include "stepper.h"
#include "c_array.h"

struct cairo_surface_t;
struct avr_t;

typedef struct pp_dot_t {
	uint32_t 	axis : 1,
				direction: 1,
				delta_cycle;
} pp_dot_t, *pp_dot_p;

DECLARE_C_ARRAY(pp_dot_t, pp_dots, 128);

typedef struct pathplot_t {
	struct avr_t * 	avr;
	stepper_p		steppers;

	avr_cycle_count_t last_cycle;
	pp_dots_t 		dots;
} pathplot_t, *pathplot_p;

int
pathplot_init(
		struct avr_t * avr,
		pathplot_p plot,
		stepper_p steppers);

/* start (or restart) data gathering */
int
pathplot_start(
		pathplot_p plot );

/* filename is the output path for the .svg file, pass NULL to cancel */
int
pathplot_stop(
		pathplot_p plot,
		const char *filename );

#endif /* _PATHPLOTTER_H_ */
