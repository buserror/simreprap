/*
	c3text.h

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


#ifndef __C3TEXT_H___
#define __C3TEXT_H___

#include "c3geometry.h"

#if __cplusplus
extern "C" {
#endif

enum {
	C3_TEXT_TYPE = C3_TYPE('t','e','x','t'),
};

enum {
	C3TEXT_ALIGN_LEFT = 0,
	C3TEXT_ALIGN_CENTER,
	C3TEXT_ALIGN_RIGHT
};

struct c3font_t;

typedef struct c3text_style_t {
	uint32_t align : 2, mutable : 1;
} c3text_style_t ;

typedef struct c3text_t {
	c3geometry_t geometry;
	str_p text;
	struct c3font_t * font;
	c3vec2 origin;
	c3vec2 size;
	c3text_style_t style;
} c3text_t, *c3text_p;

c3text_p
c3text_new(
		struct c3object_t * parent /* = NULL */);
c3text_p
c3text_init(
		c3text_p t,
		struct c3object_t * parent /* = NULL */);

int
c3text_set_font(
		c3text_p t,
		const char * font_name,
		float size,
		c3text_style_t style);
void
c3text_set(
		c3text_p t,
		c3vec2 origin,
		const char * str );

#if __cplusplus
}
#endif

#endif /* __C3TEXT_H___ */
