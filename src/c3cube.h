/*
	c3cube.h

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


#ifndef __C3CUBE_H___
#define __C3CUBE_H___

#include "c3algebra.h"

/* order of faces, and flags */
enum {
	C3CUBE_TOP_BIT = 0,
	C3CUBE_FRONT_BIT,
	C3CUBE_RIGHT_BIT,
	C3CUBE_BACK_BIT,
	C3CUBE_LEFT_BIT,
	C3CUBE_BOTTOM_BIT,

	C3CUBE_CENTER_BIT,

	C3CUBE_CENTER	 = (1 << C3CUBE_CENTER_BIT),
	C3CUBE_FACE_ALL = 0x3f,
};

c3geometry_p
c3cube_new(
		c3vec3 position,
		c3vec3 size,
		uint16_t	flags,
		struct c3object_t * parent);

c3geometry_p
c3cube_add(
		c3geometry_p g,
		c3vec3 position,
		c3vec3 size,
		uint16_t	flags,
		struct c3object_t * parent);

#endif /* __C3CUBE_H___ */
