#include <GL/glew.h>
#include <GL/gl.h>
#include <GL/glut.h>

#include <stdio.h>
#include "pipe/p_shader_tokens.h"

#include "pipe/p_context.h"
#include "pipe/p_defines.h"
#include "pipe/p_screen.h"
#include "pipe/p_state.h"
#include "util/u_inlines.h"
#include "util/u_memory.h"
#include "util/u_transfer.h"
#include "tgsi/tgsi_text.h"

#include "state_tracker/graw.h"

#include "graw_protocol.h"
#include "graw_object.h"
#include "graw_shader.h"

#include "graw_renderer.h"
#include "graw_decode.h"

struct grend_screen;

struct grend_shader_state {
   GLuint id;
   unsigned type;
   char *glsl_prog;
};
struct grend_resource {
   struct pipe_resource base;
   GLuint id;
   GLenum target;
};

struct grend_buffer {
   struct grend_resource base;
};

struct grend_texture {
   struct grend_resource base;
};

struct grend_surface {
   GLuint id;
   GLuint res_handle;
};

struct grend_vertex_element {
   unsigned count;
   struct pipe_vertex_element elements[PIPE_MAX_ATTRIBS];
   GLuint vboids[PIPE_MAX_ATTRIBS];
};

struct grend_context {
   struct pipe_context base;
   GLuint vaoid;

   struct grend_vertex_element *ve;
   int num_vbos;
   struct pipe_vertex_buffer vbo[PIPE_MAX_ATTRIBS];

   struct grend_shader_state *vs;
   struct grend_shader_state *fs;
};

static void Reshape(int width, int height)
{

}

static void key_esc(unsigned char key, int x, int y)
{
   if (key == 27) exit(0);
}

void grend_create_surface(struct grend_context *ctx,
                          uint32_t handle,
                          uint32_t res_handle)
   
{
   struct grend_surface *surf;
   struct grend_resource *tex;

   surf = CALLOC_STRUCT(grend_surface);

   glGenFramebuffers(1, &surf->id);

   tex = graw_object_lookup(res_handle, GRAW_RESOURCE);

   glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, surf->id);
   glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT,
                             tex->target, tex->id, 0);
   glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, 0);
   graw_object_insert(surf, sizeof(*surf), handle, GRAW_SURFACE);
}

#if 0

   if (myrs_state->flatshade)
      glShadeModel(GL_FLAT);
   else
      glShadeModel(GL_SMOOTH);
       
#endif


void grend_set_framebuffer_state(struct grend_context *ctx,
                                 uint32_t nr_cbufs, uint32_t surf_handle)
{
   struct grend_surface *surf;

   surf = graw_object_lookup(surf_handle, GRAW_SURFACE);

   glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, surf->id);
   glDrawBuffer(GL_COLOR_ATTACHMENT0);
}

static void grend_set_viewport_state(struct pipe_context *ctx,
                                     const struct pipe_viewport_state *state)
{
   /* convert back to glViewport */
   GLint x, y;
   GLsizei width, height;
   GLclampd near_val, far_val;

   width = state->scale[0] * 2.0f;
   height = state->scale[1] * 2.0f;
   x = state->translate[0] - state->scale[0];
   y = state->translate[1] - state->scale[1];
   near_val = state->translate[2] - state->scale[2];

   far_val = near_val + (state->scale[2] * 2.0f);

   glViewport(x, y, width, height);
   glDepthRange(near_val, far_val);
}

static void *grend_create_vertex_elements_state(struct pipe_context *ctx,
                                                        unsigned num_elements,
                                                        const struct pipe_vertex_element *elements)
{
   struct grend_vertex_element *v = CALLOC_STRUCT(grend_vertex_element);
   int i;
   int max_vbo_index = 0;
   uint32_t handle;
   v->count = num_elements;
   memcpy(v->elements, elements, sizeof(struct pipe_vertex_element) * num_elements);

   handle = graw_object_create(v, sizeof(struct grend_vertex_element),
                               GRAW_OBJECT_VERTEX_ELEMENTS);
   
   return (void*)(unsigned long)handle;
}

static void grend_bind_vertex_elements_state(struct pipe_context *ctx,
                                                     void *ve)
{
   uint32_t handle = (unsigned long)ve;
   struct grend_context *grctx = (struct grend_context *)ctx;
   struct grend_vertex_element *v;
   int i;

   v = graw_object_lookup(handle, GRAW_OBJECT_VERTEX_ELEMENTS);
   if (!v) {
      fprintf(stderr, "illegal ve lookup\n");
      return;
   }
      
   grctx->ve = v;
}

static void grend_set_vertex_buffers(struct pipe_context *ctx,
                                             unsigned num_buffers,
                                             const struct pipe_vertex_buffer *buffers)
{
   struct grend_context *grctx = (struct grend_context *)ctx;
   int i;

   for (i = 0; i < num_buffers; i++)
      grctx->vbo[i] = buffers[i];
   grctx->num_vbos = num_buffers;
}

static void grend_transfer_inline_write(struct pipe_context *ctx,
                                                struct pipe_resource *res,
                                                unsigned level,
                                                unsigned usage,
                                                const struct pipe_box *box,
                                                const void *data,
                                                unsigned stride,
                                                unsigned layer_stride)
{
   struct grend_resource *grres = (struct grend_resource *)res;
   void *ptr;

   glBindBufferARB(GL_ARRAY_BUFFER_ARB, grres->id);
   glBufferData(GL_ARRAY_BUFFER_ARB, box->width, data, GL_STATIC_DRAW);
}


void grend_create_vs(struct grend_context *ctx,
                     uint32_t handle,
                     const struct pipe_shader_state *vs)
{
   struct grend_shader_state *state = CALLOC_STRUCT(grend_shader_state);
   GLchar *glsl_prog;

   state->id = glCreateShader(GL_VERTEX_SHADER);
   glsl_prog = tgsi_convert(vs->tokens, 0);
   if (glsl_prog) {
      glShaderSource(state->id, 1, &glsl_prog, NULL);
      glCompileShader(state->id);
      fprintf(stderr,"VS:\n%s\n", glsl_prog);
   }
   graw_object_insert(state, sizeof(*state), handle, GRAW_OBJECT_VS);

   return;
}

void grend_create_fs(struct grend_context *ctx,
                     uint32_t handle,
                     const struct pipe_shader_state *fs)
{
   struct grend_shader_state *state = CALLOC_STRUCT(grend_shader_state);
   GLchar *glsl_prog;

   state->id = glCreateShader(GL_FRAGMENT_SHADER);
   glsl_prog = tgsi_convert(fs->tokens, 0);
   if (glsl_prog) {
      glShaderSource(state->id, 1, &glsl_prog, NULL);
      glCompileShader(state->id);
      fprintf(stderr,"FS:\n%s\n", glsl_prog);
   }
   graw_object_insert(state, sizeof(*state), handle, GRAW_OBJECT_FS);

   return;
}

void grend_bind_vs(struct grend_context *ctx,
                   uint32_t handle)
{
   struct grend_shader_state *state;

   state = graw_object_lookup(handle, GRAW_OBJECT_VS);

   ctx->vs = state;
}


void grend_bind_fs(struct grend_context *ctx,
                   uint32_t handle)
{
   struct grend_shader_state *state;

   state = graw_object_lookup(handle, GRAW_OBJECT_FS);

   ctx->fs = state;
}

void grend_clear(struct grend_context *ctx,
                 unsigned buffers,
                 const union pipe_color_union *color,
                 double depth, unsigned stencil)
{
   glUseProgram(0);
   glClearColor(color->f[0], color->f[1], color->f[2], color->f[3]);
   glClear(GL_COLOR_BUFFER_BIT);

}

void grend_draw_vbo(struct grend_context *ctx,
                    const struct pipe_draw_info *info)
{
   GLuint vaoid;
   int i;
   int program_id;
#if 0
   program_id = glCreateProgram();
   glAttachShader(program_id, ctx->vs->id);
   glAttachShader(program_id, ctx->fs->id);
   glLinkProgram(program_id);
   glUseProgram(program_id);
   glGenVertexArrays(1, &vaoid);

   glBindVertexArray(vaoid);

   for (i = 0; i < ctx->ve->count; i++) {
      int vbo_index = ctx->ve->elements[i].vertex_buffer_index;
      struct grend_buffer *buf;
      
      buf = ctx->vbo[vbo_index].buffer;
      glBindBuffer(GL_ARRAY_BUFFER, buf->base.id);
      glVertexAttribPointer(i, 4, GL_FLOAT, GL_FALSE, 4, (void *)ctx->ve->elements[i].src_offset);
      glEnableVertexAttribArray(i);
   }
   
   /* set the vertex state up now on a delay */
   if (!info->indexed) {
      GLenum mode = info->mode;
      glDrawArrays(mode, info->start, info->count);
   } else {
      fprintf(stderr,"indexed\n");
   }

   glBindVertexArray(0);
#endif
}


void grend_flush(struct grend_context *ctx)
{
   glFlush();
}

void grend_flush_frontbuffer(uint32_t res_handle)
{
   struct grend_resource *res;

   res = graw_object_lookup(res_handle, GRAW_RESOURCE);

   glDrawBuffer(GL_NONE);
   glUseProgram(0);
   glBindTexture(res->target, res->id);
   glBindFramebuffer(GL_FRAMEBUFFER_EXT, 0);
   glEnable(res->target);
   glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
   glTexParameteri(res->target, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
   glTexParameteri(res->target, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

   glTexParameteri(res->target, GL_TEXTURE_BASE_LEVEL, 0);
   glTexParameteri(res->target, GL_TEXTURE_MAX_LEVEL, 0);
   glBegin(GL_QUADS);
#define VAL 300//res->width0
   glTexCoord2f(0, 0);
   glVertex2f(0, 0);
   glTexCoord2f(1, 0);
   glVertex2f(VAL, 0);
   glTexCoord2f(1, 1);
   glVertex2f(VAL, VAL);
   glTexCoord2f(0, 1);
   glVertex2f(0, VAL);
   glEnd();
   glutSwapBuffers();
}

static GLenum tgsitargettogltarget(const enum pipe_texture_target target)
{
   switch(target) {
   case PIPE_TEXTURE_1D:
      return GL_TEXTURE_1D;
   case PIPE_TEXTURE_2D:
      return GL_TEXTURE_2D;
   case PIPE_TEXTURE_3D:
      return GL_TEXTURE_3D;
   case PIPE_TEXTURE_RECT:
      return GL_TEXTURE_RECTANGLE_NV;
   }
   return PIPE_BUFFER;
}

static struct pipe_resource *grend_resource_create(struct pipe_screen *pscreen,
                                                           const struct pipe_resource *template)
{
   struct grend_buffer *buf;
   struct grend_texture *tex;
   
   if (template->target == PIPE_BUFFER) {
      buf = CALLOC_STRUCT(grend_buffer);
      buf->base.base = *template;
      buf->base.base.screen = pscreen;
      pipe_reference_init(&buf->base.base.reference, 1);
      glGenBuffersARB(1, &buf->base.id);
      return &buf->base.base;
   } else {
      tex = CALLOC_STRUCT(grend_texture);
      tex->base.base = *template;
      tex->base.base.screen = pscreen;
      pipe_reference_init(&tex->base.base.reference, 1);
      glGenTextures(1, &tex->base.id);
//      tex->base.target = tgsitargettogltarget(template);
      glBindTexture(tex->base.target, tex->base.id);

      glTexImage2D(tex->base.target, 0, GL_RGBA, template->width0,
                   template->height0, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
      return &tex->base.base;
   }
}


void
graw_renderer_init(int x, int y, int width, int height)
{
   int argc = 0;

   static int glut_inited;

   if (!glut_inited) {
      glut_inited = 1;
      graw_object_init_hash();
      glutInit(&argc, NULL);
   }
   
   glutInitWindowSize(width, height);
 
   glutInitDisplayMode(GLUT_RGB | GLUT_DOUBLE);

   glutCreateWindow("test");

   glewInit();
      
   glMatrixMode(GL_PROJECTION);
   glLoadIdentity();
   glOrtho(0, width, 0, height, -1, 1);
   glMatrixMode(GL_MODELVIEW);
   glLoadIdentity();

   glutReshapeFunc(Reshape);
   glutKeyboardFunc(key_esc);
   return 0;
}



void 
graw_set_display_func( void (*draw)( void ) )
{
   glutDisplayFunc(draw);
}


void
graw_main_loop( void )
{
   glutMainLoop();
}

struct grend_context *grend_create_context(void)
{
   return CALLOC_STRUCT(grend_context);
}

void graw_renderer_resource_create(uint32_t handle, enum pipe_texture_target target, uint32_t width, uint32_t height)
{
   struct grend_resource *gr = CALLOC_STRUCT(grend_resource);

   if (target == PIPE_BUFFER) {
      glGenBuffersARB(1, &gr->id);
   } else {
      gr->target = tgsitargettogltarget(target);
      glGenTextures(1, &gr->id);
      glBindTexture(gr->target, gr->id);

      glTexImage2D(gr->target, 0, GL_RGBA, width, height, 0, GL_RGBA,
                   GL_UNSIGNED_BYTE, NULL);
   }

   graw_object_insert(gr, sizeof(*gr), handle, GRAW_RESOURCE);
}

