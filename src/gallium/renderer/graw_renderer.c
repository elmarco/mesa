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

struct grend_screen;

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
   struct pipe_surface base;
   GLuint id;
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

static struct pipe_surface *grend_create_surface(struct pipe_context *ctx,
                                                         struct pipe_resource *resource,
                                                         const struct pipe_surface *templat)
{
   struct grend_surface *surf;
   struct grend_texture *tex;

   surf = calloc(1, sizeof(struct grend_surface));

   surf->base = *templat;
   pipe_resource_reference(&surf->base.texture, resource);
   surf->base.context = ctx;
   glGenFramebuffers(1, &surf->id);

   tex = (struct grend_texture *)resource;

   glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, surf->id);
   glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT,
                             tex->base.target, tex->base.id, 0);
   glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, 0);
   return &surf->base;
}

static void *grend_create_blend_state(struct pipe_context *ctx,
                                              const struct pipe_blend_state *blend_state)
{
   struct pipe_blend_state *state = CALLOC_STRUCT(pipe_blend_state);

   *state = *blend_state;
   return state;

}

static void grend_bind_blend_state(struct pipe_context *ctx,
                                           void *blend_state)
{

}

static void *grend_create_depth_stencil_alpha_state(struct pipe_context *ctx,
                                                            const struct pipe_depth_stencil_alpha_state *blend_state)
{
   return NULL;

}

static void grend_bind_depth_stencil_alpha_state(struct pipe_context *ctx,
                                                         void *blend_state)
{

}

static void *grend_create_rasterizer_state(struct pipe_context *ctx,
                                                   const struct pipe_rasterizer_state *rs_state)
{
   return NULL;

}

static void grend_bind_rasterizer_state(struct pipe_context *ctx,
                                                void *blend_state)
{

}

static void grend_set_framebuffer_state(struct pipe_context *ctx,
                                                const struct pipe_framebuffer_state *state)
{
   struct grend_surface *surf;

   if (state->nr_cbufs != 1)
      return;

   surf = (struct grend_surface *)state->cbufs[0];
   
   glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, surf->id);
   glDrawBuffer(GL_COLOR_ATTACHMENT0);
   glViewport(0, 0, state->width, state->height);
}

static void grend_set_viewport_state(struct pipe_context *ctx,
                                             const struct pipe_viewport_state *state)
{

}

static void *grend_create_vertex_elements_state(struct pipe_context *ctx,
                                                        unsigned num_elements,
                                                        const struct pipe_vertex_element *elements)
{
   struct grend_vertex_element *v = CALLOC_STRUCT(grend_vertex_element);
   int i;
   int max_vbo_index = 0;
   v->count = num_elements;
   memcpy(v->elements, elements, sizeof(struct pipe_vertex_element) * num_elements);

   return v;
}

static void grend_bind_vertex_elements_state(struct pipe_context *ctx,
                                                     void *ve)
{
   struct grend_context *grctx = (struct grend_context *)ctx;
   struct grend_vertex_element *v = (struct grend_vertex_element *)ve;
   int i;

   grctx->ve = ve;
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

static void grend_bind_vs_state(struct pipe_context *ctx,
                                        void *vss)
{
   hack_shaders();
}

static void grend_bind_fs_state(struct pipe_context *ctx,
                                        void *vss)
{

}

static void grend_clear(struct pipe_context *pipe,
                                unsigned buffers,
                                const union pipe_color_union *color,
                                double depth, unsigned stencil)
{
   glUseProgram(0);
   glClearColor(color->f[0], color->f[1], color->f[2], color->f[3]);
   glClear(GL_COLOR_BUFFER_BIT);
   glUseProgram(ProgramId);

}

static void grend_draw_vbo(struct pipe_context *ctx,
                                   const struct pipe_draw_info *info)
{
   struct grend_context *grctx = (struct grend_context *)ctx;
   GLuint vaoid;
   int i;

   glGenVertexArrays(1, &vaoid);

   glBindVertexArray(vaoid);

   for (i = 0; i < grctx->ve->count; i++) {
      int vbo_index = grctx->ve->elements[i].vertex_buffer_index;
      struct grend_buffer *buf;
      
      buf = grctx->vbo[vbo_index].buffer;
      glBindBuffer(GL_ARRAY_BUFFER, buf->base.id);
      glVertexAttribPointer(i, 4, GL_FLOAT, GL_FALSE, 4, (void *)grctx->ve->elements[i].src_offset);
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
}

static void grend_flush(struct pipe_context *ctx,
                                struct pipe_fence_handle **fence)
{
   glFlush();
}

static struct pipe_context *grend_context_create(struct pipe_screen *pscreen,
                                                         void *priv)
{
   struct grend_context *gr_ctx;

   gr_ctx = CALLOC_STRUCT(grend_context);


   gr_ctx->base.create_surface = grend_create_surface;
   gr_ctx->base.set_framebuffer_state = grend_set_framebuffer_state;
   gr_ctx->base.create_blend_state = grend_create_blend_state;
   gr_ctx->base.bind_blend_state = grend_bind_blend_state;
   gr_ctx->base.create_depth_stencil_alpha_state = grend_create_depth_stencil_alpha_state;
   gr_ctx->base.bind_depth_stencil_alpha_state = grend_bind_depth_stencil_alpha_state;
   gr_ctx->base.create_rasterizer_state = grend_create_rasterizer_state;
   gr_ctx->base.bind_rasterizer_state = grend_bind_rasterizer_state;
   gr_ctx->base.set_viewport_state = grend_set_viewport_state;
   gr_ctx->base.create_vertex_elements_state = grend_create_vertex_elements_state;
   gr_ctx->base.bind_vertex_elements_state = grend_bind_vertex_elements_state;
   gr_ctx->base.set_vertex_buffers = grend_set_vertex_buffers;
   gr_ctx->base.transfer_inline_write = grend_transfer_inline_write;
   gr_ctx->base.bind_vs_state = grend_bind_vs_state;
   gr_ctx->base.bind_fs_state = grend_bind_fs_state;
   gr_ctx->base.clear = grend_clear;
   gr_ctx->base.draw_vbo = grend_draw_vbo;
   gr_ctx->base.flush = grend_flush;
   gr_ctx->base.screen = pscreen;
   return &gr_ctx->base;
}

static void grend_flush_frontbuffer(struct pipe_screen *screen,
					    struct pipe_resource *res,
					    unsigned level, unsigned layer,
					    void *winsys_drawable_handle)
{
   struct grend_texture *tex;

   tex = (struct grend_texture *)res;
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

   glutscreen.context_create = grend_context_create;
   glutscreen.resource_create = grend_resource_create;
   glutscreen.flush_frontbuffer = grend_flush_frontbuffer;
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
