/*
	c3cube.c

	Copyright 2008-2012 Michel Pollet <buserror@gmail.com>

 	This file is part of libc3.

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


#include <stdio.h>
#include "c3geometry.h"
#include "c3cube.h"


c3geometry_p
c3cube_add(
		c3geometry_p g,
		c3vec3 position,
		c3vec3 size,
		uint16_t	flags,
		struct c3object_t * parent)
{
	c3vec3 c = c3vec3_add(position,
			flags & C3CUBE_CENTER ?
					c3vec3f(-size.x/2,-size.y/2, size.z/2) :
					c3vec3f(0,0,0));
	c3vec3 v[8] = {
			c, c3vec3_add(c, c3vec3f(size.x, 0, 0)),
			c3vec3_add(c, c3vec3f(size.x, size.y, 0)), c3vec3_add(c, c3vec3f(0, size.y, 0)),

			c3vec3_add(c, c3vec3f(0, 0, -size.z)), c3vec3_add(c, c3vec3f(size.x, 0, -size.z)),
			c3vec3_add(c, c3vec3f(size.x, size.y, -size.z)), c3vec3_add(c, c3vec3f(0, size.y, -size.z)),
	};

	static const struct {
		c3f normal[3];
		uint8_t i[6];
	} face[6] = {
			{ { 0, 0, 1 }, { 0, 1, 2, 2, 3, 0 }},
			{ { 0, -1, 0 }, { 0, 4, 5, 5, 1, 0 }},
			{ { 1, 0, 0 }, { 1, 5, 6, 6, 2, 1 }},
			{ { 0, 1, 0 }, { 2, 6, 7, 7, 3, 2 }},
			{ { -1, 0, 0 }, { 3, 7, 4, 4, 0, 3 }},
			{ { 0, 0, -1 }, { 4, 7, 6, 6, 5, 4 }},
	};
	// texture is Y flipped by default
	static const c3f tex[12] = {
		0,1, 1,1, 1,0,  1,0, 0,0, 0,1
	};

	for (int f = 0; f < 6; f++) if (flags & (1 << f)) {
		for (int i = 0; i < 6; i++) {
			c3vertex_array_add(&g->vertice, v[(int)face[f].i[i]]);
			c3vertex_array_add(&g->normals, *((c3vec3*)face[f].normal));
		}
		c3tex_array_insert(&g->textures, g->textures.count, (c3vec2*)tex, 6);
	}
	g->dirty = 1;

	return g;
}

c3geometry_p
c3cube_new(
		c3vec3 position,
		c3vec3 size,
		uint16_t	flags,
		struct c3object_t * parent)
{
	c3geometry_p g = c3geometry_new(c3geometry_type(C3_TRIANGLE_TYPE, 0), parent);

	c3cube_add(g, position, size, flags, parent);

	return g;
}
