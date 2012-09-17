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

const GLchar* VertexShader =
{
    "#version 130\n"
    "#extension GL_ARB_explicit_attrib_location: enable\n"
    "layout(location=0) in vec4 in_Position;\n"
    "layout(location=1) in vec4 in_Color;\n"
    "out vec4 ex_Color;\n"
    "void main(void)\n"
    "{\n"
    "   gl_Position = in_Position;\n"
    "   ex_Color = in_Color;\n"
    "}\n"
};

const GLchar* FragmentShader =
{
	"#version 130\n"
	"in vec4 ex_Color;\n"
	"out vec4 out_Color;\n"
	"void main(void)\n"
	"{\n"
	"	out_Color = ex_Color;\n"
	"}\n"
};

GLuint VertexShaderId, FragmentShaderId, ProgramId;

static void hack_shaders(void)
{
   static GLuint shaders_hacked;

   if (shaders_hacked) {
      glUseProgram(ProgramId);
      return;
   }

   shaders_hacked = 1;
   VertexShaderId = glCreateShader(GL_VERTEX_SHADER);
   glShaderSource(VertexShaderId, 1, &VertexShader, NULL);
   glCompileShader(VertexShaderId);

   FragmentShaderId = glCreateShader(GL_FRAGMENT_SHADER);
   glShaderSource(FragmentShaderId, 1, &FragmentShader, NULL);
   glCompileShader(FragmentShaderId);

   ProgramId = glCreateProgram();
   glAttachShader(ProgramId, VertexShaderId);
   glAttachShader(ProgramId, FragmentShaderId);
   glLinkProgram(ProgramId);
}

struct graw_renderer_screen;

struct graw_renderer_resource {
   struct pipe_resource base;
   GLuint id;
   GLenum target;
};

struct graw_renderer_buffer {
   struct graw_renderer_resource base;
};

struct graw_renderer_texture {
   struct graw_renderer_resource base;
};

struct graw_renderer_surface {
   struct pipe_surface base;
   GLuint id;
};

struct graw_renderer_vertex_element {
   unsigned count;
   struct pipe_vertex_element elements[PIPE_MAX_ATTRIBS];
   GLuint vboids[PIPE_MAX_ATTRIBS];
};


struct graw_renderer_context {
   struct pipe_context base;
   GLuint vaoid;
   
   int num_vbos;
   struct pipe_vertex_buffer vbo[PIPE_MAX_ATTRIBS];
};

boolean tgsi_text_translate(const char *text,
			    struct tgsi_token *tokens,
                            uint num_tokens)
{
   return FALSE;
}


static void Reshape(int width, int height)
{

}

static void key_esc(unsigned char key, int x, int y)
{
   if (key == 27) exit(0);
}

static struct pipe_screen glutscreen;

static struct pipe_surface *graw_renderer_create_surface(struct pipe_context *ctx,
                                                         struct pipe_resource *resource,
                                                         const struct pipe_surface *templat)
{
   struct graw_renderer_surface *surf;
   struct graw_renderer_texture *tex;

   surf = calloc(1, sizeof(struct graw_renderer_surface));

   surf->base = *templat;
   pipe_resource_reference(&surf->base.texture, resource);
   surf->base.context = ctx;
   glGenFramebuffers(1, &surf->id);

   tex = (struct graw_renderer_texture *)resource;

   glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, surf->id);
   glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT,
                             tex->base.target, tex->base.id, 0);
   glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, 0);
   return &surf->base;
}

static void *graw_renderer_create_blend_state(struct pipe_context *ctx,
                                              const struct pipe_blend_state *blend_state)
{
   struct pipe_blend_state *state = CALLOC_STRUCT(pipe_blend_state);

   *state = *blend_state;
   return state;

}

static void graw_renderer_bind_blend_state(struct pipe_context *ctx,
                                           void *blend_state)
{

}

static void *graw_renderer_create_depth_stencil_alpha_state(struct pipe_context *ctx,
                                                            const struct pipe_depth_stencil_alpha_state *blend_state)
{
   return NULL;

}

static void graw_renderer_bind_depth_stencil_alpha_state(struct pipe_context *ctx,
                                                         void *blend_state)
{

}

static void *graw_renderer_create_rasterizer_state(struct pipe_context *ctx,
                                                   const struct pipe_rasterizer_state *rs_state)
{
   return NULL;

}

static void graw_renderer_bind_rasterizer_state(struct pipe_context *ctx,
                                                void *blend_state)
{

}

static void graw_renderer_set_framebuffer_state(struct pipe_context *ctx,
                                                const struct pipe_framebuffer_state *state)
{
   struct graw_renderer_surface *surf;

   if (state->nr_cbufs != 1)
      return;

   surf = (struct graw_renderer_surface *)state->cbufs[0];
   
   glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, surf->id);
   glDrawBuffer(GL_COLOR_ATTACHMENT0);
   glViewport(0, 0, state->width, state->height);
}

static void graw_renderer_set_viewport_state(struct pipe_context *ctx,
                                             const struct pipe_viewport_state *state)
{

}

static void *graw_renderer_create_vertex_elements_state(struct pipe_context *ctx,
                                                        unsigned num_elements,
                                                        const struct pipe_vertex_element *elements)
{
   struct graw_renderer_vertex_element *v = CALLOC_STRUCT(graw_renderer_vertex_element);
   int i;
   int max_vbo_index = 0;
   v->count = num_elements;
   memcpy(v->elements, elements, sizeof(struct pipe_vertex_element) * num_elements);

   return v;
}

static void graw_renderer_bind_vertex_elements_state(struct pipe_context *ctx,
                                                     void *ve)
{
   struct graw_renderer_context *grctx = (struct graw_renderer_context *)ctx;
   struct graw_renderer_vertex_element *v = (struct graw_renderer_vertex_element *)ve;
   int i;

   if (!grctx->vaoid) {
      glGenVertexArrays(1, &grctx->vaoid);
   }

   glBindVertexArray(grctx->vaoid);
   for (i = 0; i < v->count; i++) {
//      glVertexAttribPointer(i, 4, GL_FLOAT, GL_FALSE, 4, v->elements[i].src_offset);
      glEnableVertexAttribArray(i);
   }
}

static void graw_renderer_set_vertex_buffers(struct pipe_context *ctx,
                                             unsigned num_buffers,
                                             const struct pipe_vertex_buffer *buffers)
{
   struct graw_renderer_context *grctx = (struct graw_renderer_context *)ctx;
   int i;

   if (!grctx->vaoid) {
      glGenVertexArrays(1, &grctx->vaoid);
   }

   glBindVertexArray(grctx->vaoid);
   for (i = 0; i < num_buffers; i++) {
      struct graw_renderer_buffer *buf = buffers[i].buffer;

      glBindBufferARB(GL_ARRAY_BUFFER_ARB, buf->base.id);
//      glVertexAttribPointer(i, 4, GL_FLOAT, GL_FALSE, 4, 0);
//      glEnableVertexAttribArray(i);
   }
}

static void graw_renderer_transfer_inline_write(struct pipe_context *ctx,
                                                struct pipe_resource *res,
                                                unsigned level,
                                                unsigned usage,
                                                const struct pipe_box *box,
                                                const void *data,
                                                unsigned stride,
                                                unsigned layer_stride)
{
   struct graw_renderer_resource *grres = (struct graw_renderer_resource *)res;
   void *ptr;

   glBindBufferARB(GL_ARRAY_BUFFER_ARB, grres->id);

   glBufferData(GL_ARRAY_BUFFER_ARB, box->width, data, GL_STATIC_DRAW);
}

static void graw_renderer_bind_vs_state(struct pipe_context *ctx,
                                        void *vss)
{
   hack_shaders();
}

static void graw_renderer_bind_fs_state(struct pipe_context *ctx,
                                        void *vss)
{

}

static void graw_renderer_clear(struct pipe_context *pipe,
                                unsigned buffers,
                                const union pipe_color_union *color,
                                double depth, unsigned stencil)
{
   glUseProgram(0);
   glClearColor(color->f[0], color->f[1], color->f[2], color->f[3]);
   glClear(GL_COLOR_BUFFER_BIT);
   glUseProgram(ProgramId);

}

static void graw_renderer_draw_vbo(struct pipe_context *ctx,
                                   const struct pipe_draw_info *info)
{
   if (!info->indexed) {
      GLenum mode = info->mode;
      glDrawArrays(mode, info->start, info->count);
   } else {
      fprintf(stderr,"indexed\n");
   }
}

static void graw_renderer_flush(struct pipe_context *ctx,
                                struct pipe_fence_handle **fence)
{
   glFlush();
}

static struct pipe_context *graw_renderer_context_create(struct pipe_screen *pscreen,
                                                         void *priv)
{
   struct graw_renderer_context *gr_ctx;

   gr_ctx = CALLOC_STRUCT(graw_renderer_context);


   gr_ctx->base.create_surface = graw_renderer_create_surface;
   gr_ctx->base.set_framebuffer_state = graw_renderer_set_framebuffer_state;
   gr_ctx->base.create_blend_state = graw_renderer_create_blend_state;
   gr_ctx->base.bind_blend_state = graw_renderer_bind_blend_state;
   gr_ctx->base.create_depth_stencil_alpha_state = graw_renderer_create_depth_stencil_alpha_state;
   gr_ctx->base.bind_depth_stencil_alpha_state = graw_renderer_bind_depth_stencil_alpha_state;
   gr_ctx->base.create_rasterizer_state = graw_renderer_create_rasterizer_state;
   gr_ctx->base.bind_rasterizer_state = graw_renderer_bind_rasterizer_state;
   gr_ctx->base.set_viewport_state = graw_renderer_set_viewport_state;
   gr_ctx->base.create_vertex_elements_state = graw_renderer_create_vertex_elements_state;
   gr_ctx->base.bind_vertex_elements_state = graw_renderer_bind_vertex_elements_state;
   gr_ctx->base.set_vertex_buffers = graw_renderer_set_vertex_buffers;
   gr_ctx->base.transfer_inline_write = graw_renderer_transfer_inline_write;
   gr_ctx->base.bind_vs_state = graw_renderer_bind_vs_state;
   gr_ctx->base.bind_fs_state = graw_renderer_bind_fs_state;
   gr_ctx->base.clear = graw_renderer_clear;
   gr_ctx->base.draw_vbo = graw_renderer_draw_vbo;
   gr_ctx->base.flush = graw_renderer_flush;
   gr_ctx->base.screen = pscreen;
   return &gr_ctx->base;
}

static void graw_renderer_flush_frontbuffer(struct pipe_screen *screen,
					    struct pipe_resource *res,
					    unsigned level, unsigned layer,
					    void *winsys_drawable_handle)
{
   struct graw_renderer_texture *tex;

   tex = (struct graw_renderer_texture *)res;
   glDrawBuffer(GL_NONE);
   glUseProgram(0);
   glBindTexture(tex->base.target, tex->base.id);
   glBindFramebuffer(GL_FRAMEBUFFER_EXT, 0);
   glEnable(tex->base.target);
   glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
   glTexParameteri(tex->base.target, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
   glTexParameteri(tex->base.target, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

   glTexParameteri(tex->base.target, GL_TEXTURE_BASE_LEVEL, 0);
   glTexParameteri(tex->base.target, GL_TEXTURE_MAX_LEVEL, 0);
   glBegin(GL_QUADS);
#define VAL res->width0
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

static GLenum tgsitargettogltarget(const struct pipe_resource *res)
{
   switch(res->target) {
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
static struct pipe_resource *graw_renderer_resource_create(struct pipe_screen *pscreen,
                                                           const struct pipe_resource *template)
{
   struct graw_renderer_buffer *buf;
   struct graw_renderer_texture *tex;
   
   if (template->target == PIPE_BUFFER) {
      buf = CALLOC_STRUCT(graw_renderer_buffer);
      buf->base.base = *template;
      buf->base.base.screen = pscreen;
      pipe_reference_init(&buf->base.base.reference, 1);
      glGenBuffersARB(1, &buf->base.id);
      return &buf->base.base;
   } else {
      tex = CALLOC_STRUCT(graw_renderer_texture);
      tex->base.base = *template;
      tex->base.base.screen = pscreen;
      pipe_reference_init(&tex->base.base.reference, 1);
      glGenTextures(1, &tex->base.id);
      tex->base.target = tgsitargettogltarget(template);
      glBindTexture(tex->base.target, tex->base.id);

      glTexImage2D(tex->base.target, 0, GL_RGBA, template->width0,
                   template->height0, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
      return &tex->base.base;
   }
}


struct pipe_screen *
graw_create_window_and_screen( int x,
                               int y,
                               unsigned width,
                               unsigned height,
                               enum pipe_format format,
                               void **handle)
{
   int argc = 0;
   void *ghandle;

   static int glut_inited;

   *handle = 5;
   if (!glut_inited) {
      glut_inited = 1;

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

   glutscreen.context_create = graw_renderer_context_create;
   glutscreen.resource_create = graw_renderer_resource_create;
   glutscreen.flush_frontbuffer = graw_renderer_flush_frontbuffer;
   return &glutscreen;
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
