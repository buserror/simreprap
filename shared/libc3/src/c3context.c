/*
	c3context.c

	Copyright 2008-2012 Michel Pollet <buserror@gmail.com>

 	This file is part of libc3.

	libc3 is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	libc3 is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with libc3.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <math.h>
#include "c3context.h"
#include "c3object.h"
#include "c3light.h"
#include "c3driver_context.h"

c3context_p
c3context_new(
		int w,
		int h)
{
	c3context_p res = malloc(sizeof(*res));
	return c3context_init(res, w, h);
}

c3context_p
c3context_init(
		c3context_p c,
		int w,
		int h)
{
	memset(c, 0, sizeof(*c));

	c3context_view_t v = {
			.type = C3_CONTEXT_VIEW_EYE,
			.size = c3vec2f(w, h),
			.dirty = 1,
			.index = c->views.count,
	};
	c3cam_init(&v.cam);
	c3context_view_array_add(&c->views, v);
	c->root = c3object_new(NULL);
	c->root->context = c;

	return c;
}

void
c3context_dispose(
		c3context_p c)
{
	c3object_dispose(c->root);
	for (int i = 0; i < c->views.count; i++)
		c3geometry_array_free(&c->views.e[i].projected);
	free(c);
}

static c3context_view_p qsort_view;

void
c3_bbox_vertices(
		c3bbox_t * b,
		c3vec3 out[8])
{
	out[0] = c3vec3f(b->min.x, b->min.y, b->min.z);
	out[1] = c3vec3f(b->min.x, b->min.y, b->max.z);
	out[2] = c3vec3f(b->max.x, b->min.y, b->min.z);
	out[3] = c3vec3f(b->max.x, b->min.y, b->max.z);
	out[4] = c3vec3f(b->max.x, b->max.y, b->min.z);
	out[5] = c3vec3f(b->max.x, b->max.y, b->max.z);
	out[6] = c3vec3f(b->min.x, b->max.y, b->min.z);
	out[7] = c3vec3f(b->min.x, b->max.y, b->max.z);
}

static void
_c3_minmax_distance2(
		c3vec3 * in,
		int count,
		c3vec3 pt,
		c3f * min, c3f * max)
{
	for (int i = 0; i < count; i++) {
		c3f d = c3vec3_length2(c3vec3_sub(in[i], pt));
		if (i == 0 || d < *min)
			*min = d;
		if (i == 0 || d > *max)
			*max = d;
	}
}

/*
 * Computes the distance from the 'eye' of the camera, sort by this value
 */
static int
_c3_z_sorter(
		const void *_p1,
		const void *_p2)
{
	c3geometry_p g1 = *(c3geometry_p*)_p1;
	c3geometry_p g2 = *(c3geometry_p*)_p2;
	c3cam_p cam = &qsort_view->cam;

	c3vec3	v1[8], v2[8];
	c3_bbox_vertices(&g1->wbbox, v1);
	c3_bbox_vertices(&g2->wbbox, v2);

	c3f 	d1min, d1max;
	c3f 	d2min, d2max;

	_c3_minmax_distance2(v1, 8, cam->eye, &d1min, &d1max);
	_c3_minmax_distance2(v2, 8, cam->eye, &d2min, &d2max);

	if (d1max > qsort_view->z.max) qsort_view->z.max = d1max;
	if (d1min < qsort_view->z.min) qsort_view->z.min = d1min;
	if (d2max > qsort_view->z.max) qsort_view->z.max = d2max;
	if (d2min < qsort_view->z.min) qsort_view->z.min = d2min;
	/*
	 * make sure transparent items are drawn after everyone else
	 */
	if (g1->mat.color.n[3] < 1)
		d1min -= 100000.0;
	if (g2->mat.color.n[3] < 1)
		d2min -= 100000.0;
	if (g1->type.type == C3_LIGHT_TYPE)
		d1min = -200000 + (int)((intptr_t)((c3light_p)g1)->light_id);
	if (g2->type.type == C3_LIGHT_TYPE)
		d2min = -200000 + (int)((intptr_t)((c3light_p)g2)->light_id);

	return d1min < d2min ? -1 : d1min > d2min ? 1 : 0;
}

int
c3context_project(
		c3context_p c)
{
	if (!c->root)
		return 0;
	int res = 0;
	/*
	 * if the root object is dirty, all the views are also
	 * dirty since the geometry has changed
	 */
	if (c->root->dirty) {
		for (int ci = 0; ci < c->views.count; ci++)
			c->views.e[ci].dirty = 1;
		c3mat4 m = identity3D();
		c3object_project(c->root, &m);
		res++;
	}

	/*
	 * if the current view is dirty, gather all the geometry
	 * and Z sort it in a basic way
	 */
	c3context_view_p v = qsort_view = c3context_view_get(c);
	if (v->dirty) {
		res++;

		c3geometry_array_p  array = &c3context_view_get(c)->projected;
		c3geometry_array_clear(array);
		c3object_get_geometry(c->root, array);

		v->z.min = 1000000000;
		v->z.max = -1000000000;

		qsort(v->projected.e,
				v->projected.count, sizeof(v->projected.e[0]),
		        _c3_z_sorter);
		v->z.min = sqrt(v->z.min) * 0.8f;
		v->z.max = sqrt(v->z.max);

		/*
		 * Recalculate the perspective view using the new Z values
		 */
		if (v->cam.fov > 0) {
			c3cam_update_matrix(&v->cam);
			v->projection = perspective3D(
				v->cam.fov,
				v->size.x / v->size.y,
				v->z.min, v->z.max);
		}
		v->dirty = 0;
	}
	return res;
}

void
c3context_draw(
		c3context_p c)
{
	c3context_project(c);

	c3context_view_p v = c3context_view_get(c);

	C3_DRIVER(c, context_view_draw, v);

	c3geometry_array_p  array = &v->projected;
	for (int gi = 0; gi < array->count; gi++) {
		c3geometry_p g = array->e[gi];
		c3geometry_draw(g);
	}
}

