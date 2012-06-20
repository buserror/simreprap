/*
	c3model_obj.h

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


#ifndef __C3MODEL_OBJ_H___
#define __C3MODEL_OBJ_H___

/*
 * Loads a OBJ file as a c3object with
 * a set of c3geometries with the triangles
 */
struct c3object_t *
c3obj_load(
		const char * filename,
		struct c3object_t * parent);

#endif /* __C3MODEL_OBJ_H___ */
