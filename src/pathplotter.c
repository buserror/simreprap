/*
 * pathplotter.c
 *
 *  Created on: 2 Oct 2015
 *      Author: michel
 */


#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "sim_avr.h"
#include "sim_time.h"
#include "pathplotter.h"

#include "reprap.h" /* just for axis declaration */

#include <cairo/cairo.h>
#include <cairo/cairo-svg.h>

IMPLEMENT_C_ARRAY(pp_dots);


static void
pathplot_step_hook(
		struct avr_irq_t * irq,
		uint32_t value,
		void * param )
{
	pathplot_p plot = param;

	if (!plot->last_cycle)
		return;
	int axis = !!(irq->flags & IRQ_FLAG_USER);
	pp_dot_t dot = {
		.axis = axis,
		.direction = !!plot->steppers[axis].irq[IRQ_STEPPER_DIR_IN].value,
		.delta_cycle = plot->avr->cycle - plot->last_cycle,
	};
	plot->last_cycle = plot->avr->cycle;
	pp_dots_add(&plot->dots, dot);
//	printf("%s axis:%d dir:%d\n", __func__, dot.axis, dot.direction);
}


int
pathplot_init(
		struct avr_t * avr,
		pathplot_p plot,
		stepper_p steppers)
{
	plot->avr = avr;
	plot->steppers = steppers;
	plot->last_cycle = 0;

	avr_irq_register_notify(steppers[AXIS_X].irq + IRQ_STEPPER_STEP_IN,
			pathplot_step_hook, plot);
	avr_irq_register_notify(steppers[AXIS_Y].irq + IRQ_STEPPER_STEP_IN,
			pathplot_step_hook, plot);
	/* differenciate steppers via the user flag */
	steppers[AXIS_Y].irq[IRQ_STEPPER_STEP_IN].flags |= IRQ_FLAG_USER;

	return 0;
}


int
pathplot_start(
		pathplot_p plot )
{
	pp_dots_clear(&plot->dots);
	pp_dots_trim(&plot->dots);
	plot->last_cycle = 1 + plot->avr->cycle;

	return 0;
}

/* filename is the output path for the .svg file, pass NULL to cancel */
int
pathplot_stop(
		pathplot_p plot,
		const char *filename )
{
	if (!plot->dots.count)
		return 0;
	plot->last_cycle = 0;

	/* calculate the bounding box of all the movements */
	int32_t min[2] = {0,0};
	int32_t max[2] = {0,0};
	{
		int32_t pos[2] = {0,0};
		pp_dot_p dot = plot->dots.e;
		for (int i = 0; i < plot->dots.count; i++, dot++) {
			pos[dot->axis] += dot->direction ? 1 : -1;
			if (pos[dot->axis] < min[dot->axis])
				min[dot->axis] = pos[dot->axis];
			if (pos[dot->axis] > max[dot->axis])
				max[dot->axis] = pos[dot->axis];
		}
	}

	const double stepsize = 10;
	double maxwidth = stepsize + (2 + (min[0] + max[0]) * stepsize);
	double maxheight = stepsize + (2 + (min[1] + max[1]) * stepsize);

	printf("%s min:%d,%d max:%d,%d steps - w:%.1g h:%.1g\n", __func__,
			min[0], min[1], max[0], max[1],
			maxwidth, maxheight);

	cairo_surface_t * svg = cairo_svg_surface_create(filename,
			maxwidth, maxheight);

	cairo_t *cr = cairo_create(svg);
	/* Draw a frame around the path */
	cairo_set_source_rgba (cr, 0.2, 0.2, 0.8, 0.8);
	cairo_set_line_width (cr, 2.0);
	cairo_move_to (cr, stepsize/2, stepsize/2);
	cairo_rel_line_to (cr, maxwidth - stepsize, 0);
	cairo_rel_line_to (cr, 0, maxheight - stepsize);
	cairo_rel_line_to (cr, -(maxwidth - stepsize), 0);
	cairo_rel_line_to (cr, 0, -(maxheight - stepsize));
	cairo_stroke (cr);

	/* Draw the path itself, the /exact/ way it is driven */
	cairo_set_source_rgba (cr, 0.2, 0.2, 0.2, 0.6);
	cairo_set_line_width (cr, 2.0);
	cairo_move_to (cr, stepsize + (-min[0] * stepsize), stepsize + (-min[1] * stepsize));

	{
		pp_dot_p dot = plot->dots.e;
		for (int i = 0; i < plot->dots.count; i++, dot++) {
			double m[2] = {0};
			m[dot->axis] = (dot->direction ? 1 : -1) * stepsize;
			cairo_rel_line_to (cr, m[0], m[1]);
		}
	}
	cairo_stroke (cr);

	/* Draw dots at each step edge */
	cairo_set_source_rgba (cr, 1, 0.2, 0.2, 0.8);
	cairo_set_line_width (cr, 3.0);
	cairo_move_to (cr, stepsize + (-min[0] * stepsize), stepsize + (-min[1] * stepsize));
	cairo_set_line_cap (cr, CAIRO_LINE_CAP_ROUND);
	{
		pp_dot_p dot = plot->dots.e;
		for (int i = 0; i < plot->dots.count; i++, dot++) {
			double m[2] = {0};
			m[dot->axis] = (dot->direction ? 1 : -1) * stepsize;
			cairo_rel_move_to (cr, m[0], m[1]);
			cairo_rel_line_to (cr, 0, 0);
		}
	}
	cairo_stroke (cr);

	cairo_destroy(cr);
	cairo_surface_destroy(svg);

	return 0;
}
