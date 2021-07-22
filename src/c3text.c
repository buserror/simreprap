/*
	c3text.c

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

#include <stdio.h>
#include "c3text.h"
#include "c3driver_geometry.h"
#include "c3context.h"
#include "c3object.h"
#include "c3pixels.h"
#include "freetype-gl.h"


typedef struct c3font_t {
	str_p name;
	struct c3font_manager_t * manager;
	texture_font_t *font;
	int ref;	// reference count
} c3font_t, *c3font_p;

DECLARE_C_ARRAY(c3font_p, c3font_array, 4);
IMPLEMENT_C_ARRAY(c3font_array);

typedef struct c3font_manager_t {
	texture_atlas_t * atlas;
	c3pixels_p tex;
	c3font_array_t	fonts;
} c3font_manager_t, *c3font_manager_p;

c3text_p
c3text_new(
		struct c3object_t * parent /* = NULL */)
{
	c3text_p res = malloc(sizeof(*res));
	return c3text_init(res, parent);
}

c3text_p
c3text_init(
		c3text_p t,
		struct c3object_t * parent /* = NULL */)
{
	memset(t, 0, sizeof(*t));
	c3geometry_init(&t->geometry,
			c3geometry_type(C3_TEXT_TYPE, 0 /* GL_TRIANGLES */),
			parent);
#if 0
	static const c3driver_geometry_t * list[] = {
			&c3texture_driver, &c3geometry_driver, NULL,
	};
	t->geometry.driver = list;
#endif
	return t;

}

c3font_manager_p
c3font_manager_new()
{
	c3font_manager_p res = malloc(sizeof(*res));
	memset(res, 0, sizeof(*res));
	res->atlas = texture_atlas_new( 512, 512, 1 );
	res->tex = c3pixels_new(512, 512, 1, 512, NULL);
	res->tex->name = str_new(__func__);
	res->tex->texture = C3APIO(res->atlas->id);
	res->tex->format = C3PIXEL_LUMINANCE;
	printf("%s texture %d\n", __func__, (int)res->atlas->id);
	return res;
}

void
c3font_manager_update(
		c3font_manager_p m)
{
	texture_atlas_upload(m->atlas);
	m->tex->texture = C3APIO(m->atlas->id);
}

c3font_p
c3font_new(
		c3font_manager_p m,
		const char * name,
		float size)
{
	c3font_p res = malloc(sizeof(*res));
	memset(res, 0, sizeof(*res));
	res->name = str_new(name);
	res->manager = m;
	res->font = texture_font_new(m->atlas, res->name->str, size );
	printf("%s(%s, %f) = %p\n", __func__, name, size, res->font);
	c3font_array_add(&m->fonts, res);

    const wchar_t *cache = L" !\"#$%&'()*+,-./0123456789:;<=>?"
                           L"@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_"
                           L"`abcdefghijklmnopqrstuvwxyz{|}~";
    texture_font_load_glyphs( res->font, cache );
	c3font_manager_update(res->manager);
	printf("%s texture %d\n", __func__, (int)m->atlas->id);

	return res;
}

int
c3text_set_font(
		c3text_p t,
		const char * font_name,
		float size,
		c3text_style_t style)
{
	if (!t->geometry.object || !t->geometry.object->context)
		return -1;
	c3font_manager_p m = t->geometry.object->context->fonts;
	if (!m)
		m = t->geometry.object->context->fonts = c3font_manager_new();
	str_p name = str_new(font_name);
	c3font_p font = NULL;
	for (int i = 0; i < m->fonts.count && !font; i++)
		if (!str_cmp(m->fonts.e[i]->name, name) && m->fonts.e[i]->font->size == size)
			font = m->fonts.e[i];
	if (!font)
		font = c3font_new(m, font_name, size);
	font->ref++;
	if (t->font) {
		font->ref--;
		if (font->ref == 0) {
			// TODO purge
		}
	}
	t->geometry.vertice.buffer.mutable =
			t->geometry.textures.buffer.mutable =
			t->geometry.normals.buffer.mutable =
					style.mutable;
	t->geometry.mat.texture = m->tex;
	t->geometry.mat.color = c3vec4f(0,0,0,1.0);
	t->font = font;
	t->style = style;
	return 0;
}

static void
c3text_add(
		c3text_p t,
		c3vec4 color)
{
	c3vec2 origin = t->origin;
	const char *s = t->text->str;

	int kerning = 0;
	int gc = 0;
	int vbase = t->geometry.vertice.count;
	texture_glyph_t *glyph = NULL;
	for (int i = 0; s[i]; i++) {
		texture_glyph_t *gl = texture_font_get_glyph(t->font->font, s[i]);
		if (!gl)
			continue;
		glyph = gl;
		gc++;
		origin.x += kerning;
		kerning = texture_glyph_get_kerning(glyph, s[i]);
	}

        int x0  = (int)( origin.x + glyph->offset_x );
        int y0  = (int)( origin.y - glyph->offset_y );
        int x1  = (int)( x0 + glyph->width );
        int y1  = (int)( y0 + glyph->height );
        float s0 = glyph->s0;
        float t0 = glyph->t0;
        float s1 = glyph->s1;
        float t1 = glyph->t1;
        c3f vertices[4][9] = { { x0,y0,0,  s0,t0},
                                 { x0,y1,0,  s0,t1 },
                                 { x1,y1,0,  s1,t1 },
                                 { x1,y0,0,  s1,t0 } };
        GLuint indices[6] = {0,1,2, 0,2,3};
        int basev = t->geometry.vertice.count;
        for (int vi = 0; vi < 4; vi++) {
        	c3vertex_array_add(&t->geometry.vertice, *(c3vec3*)&vertices[vi][0]);
        	c3tex_array_add(&t->geometry.textures, *(c3vec2*)&vertices[vi][3]);
        	c3colorf_array_add(&t->geometry.colorf, color);
        }
        for (int vi = 0; vi < 6; vi++) {
        	c3indices_array_add(&t->geometry.indices, basev + indices[vi]);
		origin.x += glyph->advance_x;
	}
	if (glyph)
		origin.x += glyph->width;	// last character
	float offset = 0;
	switch (t->style.align) {
		case C3TEXT_ALIGN_CENTER:
			offset = -(origin.x - t->origin.x) / 2;
			break;
		case C3TEXT_ALIGN_RIGHT:
			offset = -(origin.x - t->origin.x);
			break;
	}
	if (offset != 0)
		for (int vi = vbase; vi < t->geometry.vertice.count; vi++)
			t->geometry.vertice.e[vi].x += offset;
	printf("Text is %d glyphs and %f wide-ish\n", gc, origin.x-t->origin.x);
}

void
c3text_set(
		c3text_p t,
		c3vec2 origin,
		const char * str )
{
	if (!t->font)
		return;

	if (t->text)
		str_free(t->text);
	t->text = str_new(str);
	t->origin = origin;

	c3vertex_array_clear(&t->geometry.vertice);
	c3tex_array_clear(&t->geometry.textures);
	c3colorf_array_clear(&t->geometry.colorf);
	c3indices_array_clear(&t->geometry.indices);

	c3text_add(t, t->geometry.mat.color);

	c3font_manager_update(t->font->manager);
}
