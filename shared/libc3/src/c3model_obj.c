/*
	c3model_obj.c

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
#include "c3algebra.h"
#include "c3geometry.h"
#include "c3object.h"
#include "c3model_obj.h"

struct c3object_t *
c3obj_load(
		const char * filename,
		struct c3object_t * parent)
{
	FILE *f = fopen(filename, "r");
	if (!f) {
		perror(filename);
		return NULL;
	}

	c3object_p		o = c3object_new(parent);
	c3geometry_p	store = c3geometry_new(c3geometry_type(0, 0), NULL);
	c3geometry_p	g = c3geometry_new(c3geometry_type(C3_TRIANGLE_TYPE, 0), o);

	o->name = str_new(filename);

	while (!feof(f)) {
		char line[256];

		fgets(line, sizeof(line), f);

		int l = strlen(line);
		while (l && line[l-1] < ' ')
			line[--l] = 0;
		if (!l)
			continue;
		char * base = line;
		while (*base && *base <= ' ')
			base++;
		if (*base == '#')
			continue;
		l = strlen(base);

		char * keyword = strsep(&base, " ");

		if (!strcmp(keyword, "v") || !strcmp(keyword, "vn") ||
				!strcmp(keyword, "vt")) {
			c3vec3 v;
			int ci = 0;
			char * cs;
			while ((cs = strsep(&base, " ")) != NULL && ci < 3)
				sscanf(cs, "%f", &v.n[ci++]);

			switch (keyword[1]) {
				case 0:
					c3vertex_array_add(&store->vertice, v);
					if (store->vertice.count == 0)
						g->bbox.min = g->bbox.max = v;
					else {
						g->bbox.min = c3vec3_min(g->bbox.min, v);
						g->bbox.max = c3vec3_max(g->bbox.max, v);
					}
					break;
				case 'n':
					c3vertex_array_add(&store->normals, v);
					break;
				case 't':
					c3tex_array_add(&store->textures, c3vec2f(v.x, v.y));
					break;
			}
		} else if (!strcmp(keyword, "f")) {
			char * cs;
			while ((cs = strsep(&base, " ")) != NULL) {
				int vi, ti, ni;
				if (sscanf(cs, "%d/%d/%d", &vi, &ti, &ni) == 3) {
					c3vertex_array_add(&g->vertice, store->vertice.e[vi - 1]);
					c3tex_array_add(&g->textures, store->textures.e[ti - 1]);
					c3vertex_array_add(&g->normals, store->normals.e[ni]);
				} else if (sscanf(cs, "%d/%d", &vi,&ti) == 2) {
					c3vertex_array_add(&g->vertice, store->vertice.e[vi - 1]);
					c3tex_array_add(&g->textures, store->textures.e[ti - 1]);
				} else if (sscanf(cs, "%d//%d", &vi,&ni) == 2) {
					c3vertex_array_add(&g->vertice, store->vertice.e[vi - 1]);
					c3vertex_array_add(&g->normals, store->normals.e[ni]);
				} else if (*cs){
					printf("%s: unknown facet format '%s'\n", filename, cs);
				}
			}
		} else if (!strcmp(keyword, "g")) {
			char * cs = strsep(&base, " ");
			g->name = str_new(cs);
		} else if (!strcmp(keyword, "o")) {
			char * cs = strsep(&base, " ");
			o->name = str_new(cs);
		} else {
			printf("%s %s unknpwn keyword '%s'\n", __func__, filename, keyword);
		}
	}
	fclose(f);
	c3geometry_dispose(store);

	printf("%s %s(%p) bbox = %.2f %.2f %.2f - %.2f %.2f %.2f\n",
			filename,
			g->name ? g->name->str : "?", g,
			g->bbox.min.x, g->bbox.min.y, g->bbox.min.z,
			g->bbox.max.x, g->bbox.max.y, g->bbox.max.z);
	return o;
}
