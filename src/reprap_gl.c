/*
	reprap_gl.c

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

#if __APPLE__
#define GL_GLEXT_PROTOTYPES
#include <GLUT/glut.h>
#include <OpenGL/gl.h>
#include <OpenGL/glext.h>
#else
#define GL_GLEXT_PROTOTYPES
#include <GL/gl.h>
#include <GL/glut.h>
#include <GL/glext.h>
#endif

#include <stdio.h>
#include <math.h>

#include "reprap.h"
#include "reprap_gl.h"

#include "c3.h"
#include "c3camera.h"
#include "c3driver_context.h"
#include "c3model_obj.h"
#include "c3lines.h"
#include "c3sphere.h"
#include "c3light.h"
#include "c3program.h"
#include "c3cube.h"
#include "c3gl.h"
#include "c3text.h"
#include "c3gl_fbo.h"

//#include <cairo/cairo.h>
#include "IL/il.h"

struct cairo_surface_t;

#define DEBUG_SHADOWMAP	0

int _w = 1280, _h = 768;

c3context_p c3 = NULL;
c3context_p hud = NULL;

c3object_p 	head = NULL; 	// hotend
c3texture_p fbo_c3;			// frame buffer object texture
c3program_p fxaa = NULL;	// full screen antialias shader
c3program_p scene = NULL;
c3gl_fbo_t 	fbo;
c3gl_fbo_t 	shadow;
c3geometry_p debug_shadowmap_decal = NULL;

uint16_t	visible_views = 0xffff;

enum {
	uniform_ShadowMap = 0,
	uniform_pixelOffset,
	uniform_tex0,
	uniform_shadowMatrix
};
const char *uniforms_scene[] = {
		"shadowMap",
		"pixelOffset",
		"tex0",
		"shadowMatrix",
		NULL
};

int glsl_version = 110;

extern reprap_t reprap;

static int dumpError(const char * what)
{
	GLenum e;
	int count = 0;
	while ((e = glGetError()) != GL_NO_ERROR) {
		printf("%s: %s\n", what, gluErrorString(e));
		count++;
	}
	return count;
}

#define GLCHECK(_w) {_w; dumpError(#_w);}


static void
_gl_reshape_cb(int w, int h)
{
    _w  = w;
    _h = h;

	c3vec2 size = c3vec2f(_w, _h);

	glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, _w, _h);
    c3gl_fbo_resize(&fbo, size);
    c3texture_resize(fbo_c3, size);
    c3context_view_get_at(c3, 0)->size = size;
    c3context_view_get_at(hud, 0)->size = size;

    if (fxaa) {
    	glUseProgram(C3APIO_INT(fxaa->pid));
    	GLCHECK(glUniform2fv(C3APIO_INT(fxaa->params.e[0].pid), 1, size.n));
    	glUseProgram(0);
    }

    glutPostRedisplay();
}

static void
_gl_key_cb(
		unsigned char key,
		int x,
		int y)	/* called on key press */
{
	switch (key) {
		case 'q':
		//	avr_vcd_stop(&vcd_file);
			c3context_dispose(c3);
			exit(0);
			break;
		case 'r':
			printf("Starting VCD trace; press 's' to stop\n");
		//	avr_vcd_start(&vcd_file);
			break;
		case 's':
			printf("Stopping VCD trace\n");
		//	avr_vcd_stop(&vcd_file);
			break;
		case '1':
			if (fbo_c3->geometry.mat.program)
				fbo_c3->geometry.mat.program = NULL;
			else
				fbo_c3->geometry.mat.program = fxaa;
			glutPostRedisplay();
			break;
		case 'm': {
			if (debug_shadowmap_decal->hidden) {
				debug_shadowmap_decal->hidden = 0;
				glBindTexture(GL_TEXTURE_2D, C3APIO_INT(shadow.buffers[C3GL_FBO_DEPTH_TEX].bid));
				glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_NONE );
				glTexParameteri( GL_TEXTURE_2D, GL_DEPTH_TEXTURE_MODE, GL_LUMINANCE );
				glBindTexture(GL_TEXTURE_2D, 0);
			} else {
				debug_shadowmap_decal->hidden = -1;
				glBindTexture(GL_TEXTURE_2D, C3APIO_INT(shadow.buffers[C3GL_FBO_DEPTH_TEX].bid));
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
				glTexParameteri(GL_TEXTURE_2D, GL_DEPTH_TEXTURE_MODE, GL_INTENSITY);
				glBindTexture(GL_TEXTURE_2D, 0);
			}
			c3->root->dirty = 1;
			hud->root->dirty = 1;
			glutPostRedisplay();
		}	break;
		case 'd': {
			if (visible_views == 0xffff) {
				visible_views = 2;
				c3->views.e[1].bid = fbo.fbo;
			} else {
				visible_views = -1;
				c3->views.e[1].bid = shadow.fbo;
			}
			glutPostRedisplay();
		}	break;
		case 'b': {
			printf("view has %d geometries\n", c3->views.e[1].projected.count);
			for (int i = 0; i < c3->views.e[1].projected.count; i++) {
				c3geometry_p g = c3->views.e[1].projected.e[i];
				printf("%s(%p) bbox = %.2f %.2f %.2f - %.2f %.2f %.2f\n",
						g->name ? g->name->str : "?", g,
						g->wbbox.min.x, g->wbbox.min.y, g->wbbox.min.z,
						g->wbbox.max.x, g->wbbox.max.y, g->wbbox.max.z);
			}
		}	break;
	}
}

static void
_gl_display_cb(void)		/* function called whenever redisplay needed */
{
	c3vec3 headp = c3vec3f(
			stepper_get_position_mm(&reprap.stepper[AXIS_X]),
			stepper_get_position_mm(&reprap.stepper[AXIS_Y]),
			stepper_get_position_mm(&reprap.stepper[AXIS_Z]));
	c3mat4 headmove = translation3D(headp);
	c3transform_set(head->transform.e[0], &headmove);

	int drawIndexes[] = { 1, 0 };
//	int drawViewStart = c3->views.e[1].dirty || c3->root->dirty ? 0 : 1;
	int drawViewStart = 0;

	for (int vi = drawViewStart; vi < 2; vi++)  if (visible_views & (1<<drawIndexes[vi])) {
		c3context_view_set(c3, drawIndexes[vi]);
		/*
		 * Draw in FBO object
		 */
		c3context_view_p view = c3context_view_get(c3);
		glBindFramebuffer(GL_FRAMEBUFFER, C3APIO_INT(view->bid));
		// draw (without glutSwapBuffers)
		dumpError("glBindFramebuffer fbo");
		glViewport(0, 0, view->size.x, view->size.y);

		//glClearColor(1.0f, 1.0f, 1.0f, 1.0f);
		glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

		c3context_project(c3);

		// Set up projection matrix
		glMatrixMode(GL_PROJECTION); // Select projection matrix
		glLoadMatrixf(view->projection.n);

		glEnable(GL_CULL_FACE);
		glDepthMask(GL_TRUE);
		glEnable(GL_DEPTH_TEST);

		glMatrixMode(GL_MODELVIEW);

		if (view->type == C3_CONTEXT_VIEW_EYE) {
			glCullFace(GL_BACK);
			glEnable(GL_BLEND); // Enable Blending

			c3context_view_p light = c3context_view_get_at(c3, 1);

			// This is matrix transform every coordinate x,y,z
			// x = x* 0.5 + 0.5
			// y = y* 0.5 + 0.5
			// z = z* 0.5 + 0.5
			// Moving from unit cube [-1,1] to [0,1]
			const c3f bias[16] = {
				0.5, 0.0, 0.0, 0.0,
				0.0, 0.5, 0.0, 0.0,
				0.0, 0.0, 0.5, 0.0,
				0.5, 0.5, 0.5, 1.0};

			c3mat4 b = c3mat4_mul((c3mat4p)bias, &light->projection);
			c3mat4 tex = c3mat4_mul(&b, &light->cam.mtx);

			GLCHECK(glUseProgram(C3APIO_INT(scene->pid)));
			glUniformMatrix4fv(
					C3APIO_INT(scene->params.e[uniform_shadowMatrix].pid),
					1, GL_FALSE, tex.n);
			/*
			 * Need to load the inverse matrix foe the camera to allow
			 * 'cancelling' the modelview matrix, otherwise translated/
			 * rotated objects will not apply
			 */
			c3mat4 notcam = c3mat4_inverse(&view->cam.mtx);
			glMatrixMode(GL_TEXTURE);
			glActiveTexture(GL_TEXTURE7);
			glLoadMatrixf(notcam.n);
			glActiveTexture(GL_TEXTURE0);
			glMatrixMode(GL_MODELVIEW);
		} else {
			glCullFace(GL_FRONT);
			glDisable(GL_BLEND); // Disable Blending
			GLCHECK(glUseProgram(0));
		}

		c3context_draw(c3);
#if 0
		if (c3->current == 0) {
			glLoadMatrixf(view->cam.mtx.n);
			for (int i = 0; i < view->projected.count; i++) {
				c3geometry_p g = view->projected.e[i];
				c3vec3	v[8];
				c3_bbox_vertices(&g->wbbox, v);
				glColor4f(0.0,0.0,1.0,1.0);
				glPointSize(5);
				glBegin(GL_POINTS);
				for (int i = 0; i < 8; i++) {
					glVertex3fv(v[i].n);
				}
				glEnd();
			}
		}
#endif
	}

	c3context_view_set(c3, 0);

	c3context_view_p view = c3context_view_get(hud);
	/*
	 * Draw back FBO over the screen
	 */
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	dumpError("glBindFramebuffer 0");
	glViewport(0, 0, view->size.x, view->size.y);

	glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	glDisable(GL_DEPTH_TEST);
	glDisable(GL_LIGHTING);
	glDisable(GL_ALPHA_TEST);
	glDisable(GL_CULL_FACE);

	glUseProgram(0);

	glMatrixMode(GL_PROJECTION); // Select projection matrix

	hud->views.e[0].cam.mtx = identity3D();
	hud->views.e[0].projection = screen_ortho3D(0, _w, 0, _h, 0, 10);
	glLoadMatrixf(hud->views.e[0].projection.n);

	glMatrixMode(GL_MODELVIEW); // Select modelview matrix

	c3context_project(hud);
	c3context_draw(hud);

    glutSwapBuffers();
}

#if !defined(GLUT_WHEEL_UP)
#  define GLUT_WHEEL_UP   3
#  define GLUT_WHEEL_DOWN 4
#endif


int button;
c3vec2 move;

static
void _gl_button_cb(
		int b,
		int s,
		int x,
		int y)
{
	button = s == GLUT_DOWN ? b : 0;
	move = c3vec2f(x, y);
	c3context_view_p view = c3context_view_get_at(c3, 0);
//	printf("button %d: %.1f,%.1f\n", b, move.x, move.y);
	switch (b) {
		case GLUT_LEFT_BUTTON:
		case GLUT_RIGHT_BUTTON:	// call motion
			break;
		case GLUT_WHEEL_UP:
		case GLUT_WHEEL_DOWN:
			if (view->cam.distance > 10) {
				const float d = 0.004;
				c3cam_set_distance(&view->cam,
						view->cam.distance * ((b == GLUT_WHEEL_DOWN) ? (1.0+d) : (1.0-d)));
				view->dirty = 1;	// resort the array
			}
			break;
	}
}

void
_gl_motion_cb(
		int x,
		int y)
{
	c3vec2 m = c3vec2f(x, y);
	c3vec2 delta = c3vec2_sub(move, m);
	c3context_view_p view = c3context_view_get_at(c3, 0);

//	printf("%s b%d click %.1f,%.1f now %d,%d delta %.1f,%.1f\n",
//			__func__, button, move.n[0], move.n[1], x, y, delta.x, delta.y);

	switch (button) {
		case GLUT_LEFT_BUTTON: {
			c3mat4 rotx = rotation3D(view->cam.side, delta.n[1] / 4);
			c3mat4 roty = rotation3D(c3vec3f(0.0, 0.0, 1.0), delta.n[0] / 4);
			c3mat4 rot = c3mat4_mul(&rotx, &roty);
			c3cam_rot_about_lookat(&view->cam, &rot);

			view->dirty = 1;	// resort the array
		}	break;
		case GLUT_RIGHT_BUTTON: {
			// offset both points, but following the plane
			c3vec3 f = c3vec3_mulf(
					c3vec3f(-view->cam.side.y, view->cam.side.x, 0),
					-delta.n[1] / 4);
			view->cam.eye = c3vec3_add(view->cam.eye, f);
			view->cam.lookat = c3vec3_add(view->cam.lookat, f);
			c3cam_movef(&view->cam, delta.n[0] / 8, 0, 0);

		    view->dirty = 1;	// resort the array
		}	break;
	}
	glutPostRedisplay();
	move = m;
}

// gl timer. if the lcd is dirty, refresh display
static void
_gl_timer_cb(
		int i)
{
	glutTimerFunc(1000 / 24, _gl_timer_cb, 0);
	glutPostRedisplay();
}

static c3pixels_p
_c3pixels_load_image(
		c3context_p context,
		const char * filename,
		int type)
{
	ILuint ImageName = 0;
	ilBindImage(ImageName);
	ilLoadImage(filename);
	static const struct {
		int bpp;
		int iltype;
	} map[] = {
		[C3PIXEL_ARGB] = { .bpp = 4, .iltype = IL_RGBA, },
		[C3PIXEL_RGB] = { .bpp = 3, .iltype = IL_RGB, },
		[C3PIXEL_LUMINANCE] = { .bpp = 1, .iltype = IL_LUMINANCE, },
		[C3PIXEL_ALPHA] = { .bpp = 1, .iltype = IL_LUMINANCE, },
	};

	ilConvertImage(map[type].iltype, IL_UNSIGNED_BYTE);

	int rowsize = map[type].bpp * ilGetInteger(IL_IMAGE_WIDTH);
	printf("pad = %d = %d\n", rowsize, rowsize % 4);
	c3pixels_p dst = c3pixels_new(
			ilGetInteger(IL_IMAGE_WIDTH),
	        ilGetInteger(IL_IMAGE_HEIGHT),
	        map[type].bpp,
	        rowsize,
	        NULL);
	dst->format = type;
	ilCopyPixels(0, 0, 0, dst->w, dst->h, 1,
			map[type].iltype, IL_UNSIGNED_BYTE, dst->base);
	printf("loaded %s %dx%dx%d pix %p\n", filename, dst->w, dst->h, dst->psize, dst->base);
	dst->name = str_new(filename);
	c3pixels_array_add(&context->pixels, dst);
	return dst;
}

const c3driver_context_t * c3_driver_list[3] = { NULL, NULL };

int
gl_init(
		int argc,
		char *argv[] )
{
	glutInit(&argc, argv);		/* initialize GLUT system */

	glutInitDisplayMode(GLUT_RGBA | GLUT_DOUBLE | GLUT_DEPTH/* | GLUT_ALPHA*/);
	glutInitWindowSize(_w, _h);		/* width=400pixels height=500pixels */
	/*window =*/ glutCreateWindow("Press 'q' to quit");	/* create window */

	glutDisplayFunc(_gl_display_cb);		/* set window's display callback */
	glutKeyboardFunc(_gl_key_cb);		/* set window's key callback */
	glutTimerFunc(1000 / 24, _gl_timer_cb, 0);

	glutMouseFunc(_gl_button_cb);
	glutMotionFunc(_gl_motion_cb);
    glutReshapeFunc(_gl_reshape_cb);

	glHint(GL_PERSPECTIVE_CORRECTION_HINT, GL_NICEST);
	glHint(GL_GENERATE_MIPMAP_HINT, GL_NICEST);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); // Type Of Blending To Use
	glTexEnvf( GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);

	// enable color tracking
	//glEnable(GL_COLOR_MATERIAL);
	// set material properties which will be assigned by glColor
	//glColorMaterial(GL_FRONT, GL_AMBIENT_AND_DIFFUSE);
	glColorMaterial(GL_FRONT, GL_SPECULAR);

	/* setup some lights */
	GLfloat global_ambient[] = { 0.2f, 0.2f, 0.2f, 1.0f };
	glLightModelfv(GL_LIGHT_MODEL_AMBIENT, global_ambient);

	/*
	 * Extract the GLSL version as a numeric value for later
	 */
	str_p glsl = str_new((const char *)glGetString(GL_SHADING_LANGUAGE_VERSION));
	{
		char * dup = strdup(glsl->str);
		char * base = dup;
		char * vs = strsep(&base, "");
		char * vendor = strsep(&base, " ");
		int M = 0, m = 0;
		if (sscanf(vs, "%d.%d", &M, &m) == 2)
			glsl_version = (M * 100) + m;
		free(dup);
	}
	if (glsl_version > 130)
		glsl_version = 130;
	printf("GL_SHADING_LANGUAGE_VERSION '%s' = %d\n", glsl->str, glsl_version);

	c3gl_fbo_create(&fbo, c3vec2f(_w, _h), (1 << C3GL_FBO_COLOR)|(1 << C3GL_FBO_DEPTH));
	// shadow buffer

	c3_driver_list[0] = c3gl_getdriver();

    c3 = c3context_new(_w, _h);
    c3->driver = c3_driver_list;

    c3cam_p cam = &c3context_view_get_at(c3, 0)->cam;
	cam->lookat = c3vec3f(100.0, 100.0, 0.0);
	cam->eye = c3vec3f(100.0, -100.0, 100.0);
	// associate the framebuffer object with this view
	c3context_view_get_at(c3, 0)->bid = fbo.fbo;

    c3pixels_p white_tex = NULL;
    c3pixels_p brass_tex = NULL;

	{
    	const int ws = 1;
		c3pixels_p dst = c3pixels_new(ws, ws, 4, ws*4, NULL);
		dst->name = str_new("white");
		for (int i = 0; i < (ws*ws); i++)
			((uint32_t*) dst->base)[i] = 0xffffffff;
		c3pixels_array_add(&c3->pixels, dst);
		white_tex = dst;
	}
	/*
	 * Create a light, attach it to a movable object, and attach a sphere
	 * to it too so it's visible.
	 */
    c3vec3 lightpos = c3vec3f(-50.0f, -50.0f, 200.0f);
	{
		c3object_p ligthhook = c3object_new(c3->root);
	    c3transform_p pos = c3transform_new(ligthhook);

	    pos->matrix = translation3D(lightpos);

		c3light_p light = c3light_new(ligthhook);
		light->geometry.name = str_new("light0");
		light->color.specular = c3vec4f(.5f, .5f, .5f, 1.0f);
		light->color.ambiant = c3vec4f( 0.5f, 0.5f, 0.5f, 1.0f);
		light->position = c3vec4f(0, 0, 0, 1.0f );
		light->geometry.hidden = (1 << 1); // hidden from light view. not that it matters

	    {	// light bulb
	    	c3geometry_p g = c3sphere_uv(ligthhook, c3vec3f(0,0,0), 3, 10, 10);
	    	g->mat.color = c3vec4f(1.0, 1.0, 0.0, 1.0);
	    	g->mat.texture = white_tex;
	    	g->hidden = (1 << 1);	// hidden from light scene
			g->name = str_new("light bulb");
	    }
	}
	{
		c3vec2 size = c3vec2f(1024, 1024);
		c3gl_fbo_create(&shadow, size, (1 << C3GL_FBO_DEPTH_TEX));

		c3context_view_t v = {
				.type = C3_CONTEXT_VIEW_LIGHT,
				.size = size,
				.dirty = 1,
				.index = c3->views.count,
				.bid = shadow.fbo,
		};
		c3cam_init(&v.cam);
		c3vec3 listpos = lightpos;
		v.cam.eye = listpos;
		v.cam.lookat = c3vec3f(100.0, 100.0, 0.0);
		//c3cam_reset_up(&v.cam);
		c3context_view_array_add(&c3->views, v);
	}
	ilInit();
    {
    	const char *path = "gfx/hb.png";
    	c3pixels_p dst = _c3pixels_load_image(c3, path, C3PIXEL_RGB);

		c3geometry_p g = c3cube_new(c3vec3f(100, 100, -1), c3vec3f(200, 200, 2),
				C3CUBE_CENTER | C3CUBE_FACE_ALL, c3->root);
		g->mat.color = c3vec4f(1.0, 1.0, 1.0, 1.0);
		g->mat.texture = dst;
		g->name = str_new("hotbed");
    }
#if 1
    {
    	const char *path = "gfx/brass.png";
    	c3pixels_p dst = _c3pixels_load_image(c3, path, C3PIXEL_RGB);
		brass_tex = dst;
    }
#endif
    c3pixels_p line_aa_tex = NULL;
    {
    	const char *path = "gfx/BlurryCircle.png";
    	c3pixels_p dst = _c3pixels_load_image(c3, path, C3PIXEL_ALPHA);
    	line_aa_tex = dst;
    }
    c3object_p grid = c3object_new(c3->root);
    {
    	grid->hidden = (1 << 1);
    	grid->name = str_new("grid");
        for (int x = 1; x < 20; x++) {
        	for (int y = 1; y < 20; y++) {
        		c3vec3 p[4] = {
        			c3vec3f(-1+x*10,y*10,0.01), c3vec3f(1+x*10,y*10,0.01),
        			c3vec3f(x*10,-1+y*10,0.02), c3vec3f(x*10,1+y*10,0.02),
        		};
            	c3geometry_p g = c3geometry_new(
            			c3geometry_type(C3_LINES_TYPE, 0), grid);
            	g->mat.color = c3vec4f(0.0, 0.0, 0.0, 0.9);
            	g->mat.texture = line_aa_tex;
        		c3lines_init(g, p, 4, 0.18);
        	}
        }
    }

   if (0) {
		c3vec3 p[4] = {
			c3vec3f(-5,-5,1), c3vec3f(205,-5,1),
		};
    	c3geometry_p g = c3geometry_new(
    			c3geometry_type(C3_LINES_TYPE, 0), grid);
    	g->mat.color = c3vec4f(0.0, 0.0, 0.0, 1.0);
    	g->mat.texture = line_aa_tex;
    	g->line.width = 2;

		c3vertex_array_insert(&g->vertice,
				g->vertice.count, p, 2);

    }
    head = c3obj_load("gfx/buserror-nozzle-model.obj", c3->root);
    c3transform_new(head);

    if (head->geometry.count > 0) {
    	c3geometry_p g = head->geometry.e[0];
    	c3geometry_factor(g, 0.1, (25 * M_PI) / 180.0);
    //	g->mat.color = c3vec4f(0.6, 0.5, 0.0, 1.0);
    //	g->mat.texture = white_tex;
    //	head->geometry.e[0]->mat.color = c3vec4f(1.0, 1.0, 1.0, 1.0);
    	head->geometry.e[0]->mat.texture = brass_tex;
		g->mat.shininess = 5.0;
    }

#if 0
    {
		c3geometry_p g = c3cube_new(c3vec3f(120, 80, 10), c3vec3f(10, 10, 10),
				C3CUBE_CENTER | C3CUBE_FACE_ALL, c3->root);
		g->mat.color = c3vec4f(1.0, 1.0, 1.0, 1.0);
		g->mat.texture = brass_tex;
		g->name = str_new("debug cube");
		g->mat.shininess = 5.0;
    }
#endif

    hud = c3context_new(_w, _h);
    hud->driver = c3_driver_list;
    hud->views.e[0].cam.fov = 0;	// ortho
    /*
     * This is the offscreen framebuffer where the 3D scene is drawn
     */
    {
    	/*
    	 * need to insert a header since there is nothing to detect the version number
    	 * reliably without it, and __VERSION__ returns idiocy
    	 */
    	char head[128];
    	sprintf(head, "#version %d\n#define GLSL_VERSION %d\n", glsl_version, glsl_version);

    	const char *uniforms[] = { "g_Resolution", NULL };
        fxaa = c3program_new("fxaa", uniforms);
        c3program_array_add(&hud->programs, fxaa);
        c3program_load_shader(fxaa, GL_VERTEX_SHADER, head,
        		"gfx/postproc.vs", C3_PROGRAM_LOAD_UNIFORM);
        c3program_load_shader(fxaa, GL_FRAGMENT_SHADER, head,
        		"gfx/postproc.fs", C3_PROGRAM_LOAD_UNIFORM);

        c3texture_p b = c3texture_new(hud->root);

    	c3pixels_p dst = c3pixels_new(_w, _h, 4, _w * 4, NULL);
		dst->name = str_new("fbo");
		dst->texture = fbo.buffers[C3GL_FBO_COLOR].bid;
		dst->dirty = 0;
	//	dst->trace = 1;
    	b->geometry.mat.texture = dst;
    	b->geometry.mat.program = fxaa;
    	b->geometry.vertice.buffer.mutable = 1;
    	b->size = c3vec2f(_w, _h);
		b->geometry.mat.color = c3vec4f(1.0, 1.0, 1.0, 1.0);
		fbo_c3 = b;
    }

    {
    	/*
    	 * need to insert a header since there is nothing to detect the version number
    	 * reliably without it, and __VERSION__ returns idiocy
    	 */
    	char head[128];
    	sprintf(head, "#version %d\n#define GLSL_VERSION %d\n", glsl_version, glsl_version);

        scene = c3program_new("scene", uniforms_scene);
        // scene->verbose = 1;
        c3program_load_shader(scene, GL_VERTEX_SHADER, head,
        		"gfx/scene.vs", C3_PROGRAM_LOAD_UNIFORM);
        c3program_load_shader(scene, GL_FRAGMENT_SHADER, head,
        		"gfx/scene.fs", C3_PROGRAM_LOAD_UNIFORM);
        c3program_array_add(&c3->programs, scene);
        c3gl_program_load(scene);

		GLCHECK(glUseProgram(C3APIO_INT(scene->pid)));
        GLCHECK(glUniform1i(
        		C3APIO_INT(scene->params.e[uniform_ShadowMap].pid), 7));
		GLCHECK(glUniform1i(
				C3APIO_INT(scene->params.e[uniform_tex0].pid), 0));
		c3vec2 isize = c3vec2f(1.0f / c3->views.e[1].size.x,
					1.0f / c3->views.e[1].size.y);
		GLCHECK(glUniform2fv(
				C3APIO_INT(scene->params.e[uniform_pixelOffset].pid), 1,
					isize.n));
		glActiveTexture(GL_TEXTURE7);
		GLCHECK(glBindTexture(GL_TEXTURE_2D,
				C3APIO_INT(shadow.buffers[C3GL_FBO_DEPTH_TEX].bid)));
		glActiveTexture(GL_TEXTURE0);
    }
    {
		c3vec3 p[4] = {
			c3vec3f(10,10,0), c3vec3f(800-10,10,0),
		};
    	c3geometry_p g = c3geometry_new(
    			c3geometry_type(C3_LINES_TYPE, 0), hud->root);
    	g->mat.color = c3vec4f(0.5, 0.5, 1.0, 0.3f);
    	g->mat.texture = line_aa_tex;
		c3lines_init(g, p, 2, 10);
    }
     {
    	c3object_p hook = c3object_new(hud->root);
    	c3transform_p pos = c3transform_new(hook);
	 	pos->matrix = translation3D(c3vec3f(1.0f, 100.0f, 0.0f));

		c3texture_p b = c3texture_new(hook);

    	c3pixels_p dst = c3pixels_new(shadow.size.x, shadow.size.y, 4,
    			shadow.size.x * 4, NULL);
		dst->name = str_new("shadow fbo");
		dst->texture = shadow.buffers[C3GL_FBO_DEPTH_TEX].bid;
		dst->dirty = 0;
	//	dst->trace = 1;
    	b->geometry.mat.texture = dst;
    	b->size = c3vec2f(256, 256);
		b->geometry.mat.color = c3vec4f(1.0, 1.0, 1.0, 1.0);
		b->geometry.hidden = -1;

		debug_shadowmap_decal = &b->geometry;
    }
    {
    	c3text_p t = c3text_new(hud->root);
    	c3text_style_t style = { .align = C3TEXT_ALIGN_LEFT, .mutable = 0 };

    	c3text_set_font(t, "gfx/VeraMono.ttf", 18, style);
    	c3text_set(t, c3vec2f(1, 20), "Hello World!");
    	t->geometry.mat.color = c3vec4f(0.5,0.5,0.5,1.0);
    }
	return 1;
}

void
gl_dispose()
{
	c3context_dispose(c3);
	c3context_dispose(hud);
	c3gl_fbo_dispose(&fbo);
}

int
gl_runloop()
{
	glutMainLoop();
	gl_dispose();
	return 0;
}
