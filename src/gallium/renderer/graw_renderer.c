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
#include "util/u_format.h"
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

struct grend_sampler {

};

struct grend_sampler_view {
   struct grend_resource *texture;
};

struct grend_vertex_element {
   unsigned count;
   struct pipe_vertex_element elements[PIPE_MAX_ATTRIBS];
   GLuint vboids[PIPE_MAX_ATTRIBS];
};

struct grend_constants {
   float consts[128];
   uint32_t num_consts;
};

struct grend_context {
   struct pipe_context base;
   GLuint vaoid;

   struct grend_vertex_element *ve;
   int num_vbos;
   struct pipe_vertex_buffer vbo[PIPE_MAX_ATTRIBS];

   struct grend_shader_state *vs;
   struct grend_shader_state *fs;

   int num_fs_views;
   /* frag samplers */
   struct grend_sampler_view fs_views[PIPE_MAX_SHADER_SAMPLER_VIEWS];

   struct pipe_rasterizer_state *rs_state;

   struct pipe_index_buffer ib;

   struct grend_constants vs_consts;
   struct grend_constants fs_consts;
};


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

void grend_set_framebuffer_state(struct grend_context *ctx,
                                 uint32_t nr_cbufs, uint32_t surf_handle)
{
   struct grend_surface *surf;

   surf = graw_object_lookup(surf_handle, GRAW_SURFACE);

   glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, surf->id);
   glDrawBuffer(GL_COLOR_ATTACHMENT0);
}

void grend_set_viewport_state(struct grend_context *ctx,
                              const struct pipe_viewport_state *state)
{
   /* convert back to glViewport */
   GLint x, y;
   GLsizei width, height;
   GLclampd near_val, far_val;

   width = state->scale[0] * 2.0f;
   height = abs(state->scale[1]) * 2.0f;
   x = state->translate[0] - state->scale[0];
   y = state->translate[1] - abs(state->scale[1]);
   near_val = state->translate[2] - state->scale[2];

   far_val = near_val + (state->scale[2] * 2.0f);

   glViewport(x, y, width, height);
   glDepthRange(near_val, far_val);
}

void grend_create_vertex_elements_state(struct grend_context *ctx,
                                        uint32_t handle,
                                        unsigned num_elements,
                                        const struct pipe_vertex_element *elements)
{
   struct grend_vertex_element *v = CALLOC_STRUCT(grend_vertex_element);
   int i;
   int max_vbo_index = 0;

   v->count = num_elements;
   memcpy(v->elements, elements, sizeof(struct pipe_vertex_element) * num_elements);

   graw_object_insert(v, sizeof(struct grend_vertex_element), handle,
                      GRAW_OBJECT_VERTEX_ELEMENTS);
}

void grend_bind_vertex_elements_state(struct grend_context *ctx,
                                      uint32_t handle)
{
   struct grend_vertex_element *v;
   int i;

   v = graw_object_lookup(handle, GRAW_OBJECT_VERTEX_ELEMENTS);
   if (!v) {
      fprintf(stderr, "illegal ve lookup\n");
      return;
   }
      
   ctx->ve = v;
}

void grend_set_constants(struct grend_context *ctx,
                         uint32_t shader,
                         uint32_t index,
                         uint32_t num_constant,
                         float *data)
{
   struct grend_constants *consts;
   int i;
   if (shader == 0)
      consts = &ctx->vs_consts;
   else
      consts = &ctx->fs_consts;
   consts->num_consts = num_constant;
   for (i = 0; i < num_constant; i++)
      consts->consts[i] = data[i];
}

void grend_set_index_buffer(struct grend_context *ctx,
                            uint32_t res_handle,
                            uint32_t index_size,
                            uint32_t offset)
{
   struct grend_resource *res;

   ctx->ib.index_size = index_size;
   ctx->ib.offset = offset;
   res = graw_object_lookup(res_handle, GRAW_RESOURCE);
   ctx->ib.buffer = &res->base;
}

void grend_set_single_vbo(struct grend_context *ctx,
                         int index,
                         uint32_t stride,
                         uint32_t buffer_offset,
                         uint32_t res_handle)
{
   struct grend_resource *res;
   ctx->vbo[index].stride = stride;
   ctx->vbo[index].buffer_offset = buffer_offset;

   res = graw_object_lookup(res_handle, GRAW_RESOURCE);
   ctx->vbo[index].buffer = &res->base;
}

void grend_set_num_vbo(struct grend_context *ctx,
                      int num_vbo)
{
   ctx->num_vbos = num_vbo;
}

void grend_set_single_fs_sampler_view(struct grend_context *ctx,
                                      int index,
                                      uint32_t res_handle)
{
   struct grend_resource *res;

   res = graw_object_lookup(res_handle, GRAW_RESOURCE);
   ctx->fs_views[index].texture = &res->base;
}

void grend_set_num_fs_sampler_views(struct grend_context *ctx,
                                    int num_fs_sampler_views)
{
   ctx->num_fs_views = num_fs_sampler_views;
}

void grend_transfer_inline_write(struct grend_context *ctx,
                                 uint32_t res_handle,
                                 unsigned level,
                                 unsigned usage,
                                 const struct pipe_box *box,
                                 const void *data,
                                 unsigned stride,
                                 unsigned layer_stride)
{
   struct grend_resource *res;
   void *ptr;

   res = graw_object_lookup(res_handle, GRAW_RESOURCE);
   if (res->target == GL_ELEMENT_ARRAY_BUFFER_ARB) {
      glBindBufferARB(GL_ELEMENT_ARRAY_BUFFER_ARB, res->id);
      glBufferData(GL_ELEMENT_ARRAY_BUFFER_ARB, box->width, data, GL_STATIC_DRAW);
   } else if (res->target == GL_ARRAY_BUFFER_ARB) {
      glBindBufferARB(GL_ARRAY_BUFFER_ARB, res->id);
      glBufferData(GL_ARRAY_BUFFER_ARB, box->width, data, GL_STATIC_DRAW);
   } else {
      glBindTexture(res->target, res->id);
      glEnable(res->target);
      glTexSubImage2D(res->target, level, box->x, box->y, box->width, box->height,
                      GL_RGBA, GL_UNSIGNED_BYTE, data);
      glDisable(res->target);

   }
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
#if 1
   program_id = glCreateProgram();
   glAttachShader(program_id, ctx->vs->id);
   glAttachShader(program_id, ctx->fs->id);
   glLinkProgram(program_id);
   glUseProgram(program_id);
   glGenVertexArrays(1, &vaoid);

   glBindVertexArray(vaoid);

   for (i = 0; i < ctx->vs_consts.num_consts / 4; i++) {
      GLint loc;
      char name[10];
      snprintf(name, 10, "const%d", i);
      loc = glGetUniformLocation(program_id, name);
      if (loc == -1)
         fprintf(stderr,"unknown constant\n");
      glUniform4fv(loc, 1, &ctx->vs_consts.consts[i * 4]);
   }


   for (i = 0; i < ctx->num_fs_views; i++) {
      glActiveTexture(GL_TEXTURE0 + i);
      glBindTexture(ctx->fs_views[i].texture->target, ctx->fs_views[i].texture->id);
      glEnable(ctx->fs_views[i].texture->target);
   } 
   glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
   glPixelStorei(GL_UNPACK_ALIGNMENT, 4);

   for (i = 0; i < ctx->ve->count; i++) {
      int vbo_index = ctx->ve->elements[i].vertex_buffer_index;
      const struct util_format_description *desc = util_format_description(ctx->ve->elements[i].src_format);
      struct grend_buffer *buf;
      GLenum type;
      int sz;
      if (desc->channel[0].type == UTIL_FORMAT_TYPE_FLOAT)
         type = GL_FLOAT;
      sz = desc->nr_channels;

      buf = ctx->vbo[vbo_index].buffer;
      glBindBuffer(GL_ARRAY_BUFFER, buf->base.id);
      glVertexAttribPointer(i, sz, type, GL_FALSE, ctx->vbo[vbo_index].stride, (void *)ctx->ve->elements[i].src_offset);
      if (ctx->ve->elements[i].instance_divisor)
         glVertexAttribDivisorARB(i, ctx->ve->elements[i].instance_divisor);
      glEnableVertexAttribArray(i);
   }

   if (info->indexed) {
      struct grend_resource *res = (struct grend_resource *)ctx->ib.buffer;
      glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, res->id);
   }
   
   /* set the vertex state up now on a delay */
   if (!info->indexed) {
      GLenum mode = info->mode;
      if (info->instance_count == 0)
         glDrawArrays(mode, info->start, info->count);
      else
         glDrawArraysInstancedARB(mode, info->start, info->count, info->instance_count);
   } else {
      GLenum elsz;
      GLenum mode = info->mode;
      switch (ctx->ib.index_size) {
      case 1:
         elsz = GL_UNSIGNED_BYTE;
         break;
      case 2: 
         elsz = GL_UNSIGNED_SHORT;
         break;
      case 4: 
         elsz = GL_UNSIGNED_INT;
         break;
      }

      if (info->instance_count == 0)        
         glDrawElements(mode, info->count, elsz, 0);
      else
         glDrawElementsInstancedARB(mode, info->count, elsz, 0, info->instance_count);
   }

   glActiveTexture(GL_TEXTURE0);
   glDisable(GL_TEXTURE_2D);
   glBindVertexArray(0);
#endif
}
   
void grend_object_bind_rasterizer(struct grend_context *ctx,
                                  uint32_t handle)
{
   struct pipe_rasterizer_state *state;

   state = graw_object_lookup(handle, GRAW_OBJECT_RASTERIZER);
   
#if 0
   if (state->depth_clip) {
      glEnable(GL_DEPTH_CLAMP);
   } else {
      glDisable(GL_DEPTH_CLAMP);
   }
#endif
   if (state->flatshade) {
      glShadeModel(GL_FLAT);
   } else {
      glShadeModel(GL_SMOOTH);
   }
   ctx->rs_state = state;
}

void grend_object_bind_sampler_states(struct grend_context *ctx,
                                      uint32_t num_states,
                                      uint32_t *handles)
{
   int i;
   struct pipe_sampler_state *state;
   for (i = 0; i < num_states; i++) {
      state = graw_object_lookup(handles[0], GRAW_OBJECT_SAMPLER_STATE);

      glEnable(GL_TEXTURE_2D);
      glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
      glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
   }
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
   glMatrixMode(GL_PROJECTION);
   glLoadIdentity();
   glOrtho(0, res->base.width0, 0, res->base.height0, -1, 1);
   glMatrixMode(GL_MODELVIEW);
   glLoadIdentity();

   glBindTexture(res->target, res->id);
   glBindFramebuffer(GL_FRAMEBUFFER_EXT, 0);
   glEnable(res->target);
   glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
   glTexParameteri(res->target, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
   glTexParameteri(res->target, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

   glTexParameteri(res->target, GL_TEXTURE_BASE_LEVEL, 0);
   glTexParameteri(res->target, GL_TEXTURE_MAX_LEVEL, 0);
   glBegin(GL_QUADS);
#define VAL res->base.width0
   glTexCoord2f(0, 0);
   glVertex2f(0, 0);
   glTexCoord2f(1, 0);
   glVertex2f(VAL, 0);
   glTexCoord2f(1, -1);
   glVertex2f(VAL, VAL);
   glTexCoord2f(0, -1);
   glVertex2f(0, VAL);
   glEnd();
   glDisable(res->target);
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

void
graw_renderer_init(int x, int y, int width, int height)
{
   static int inited;

   if (!inited) {
      inited = 1;
      graw_object_init_hash();
   }
   
   glewInit();
}

struct grend_context *grend_create_context(void)
{
   return CALLOC_STRUCT(grend_context);
}

void graw_renderer_resource_create(uint32_t handle, enum pipe_texture_target target, uint32_t format, uint32_t bind, uint32_t width, uint32_t height)
{
   struct grend_resource *gr = CALLOC_STRUCT(grend_resource);

   gr->base.width0 = width;
   gr->base.height0 = height;
   gr->base.format = format;
   gr->base.target = target;
   if (bind == PIPE_BIND_INDEX_BUFFER) {
      gr->target = GL_ELEMENT_ARRAY_BUFFER_ARB;
      glGenBuffersARB(1, &gr->id);
      glBindBufferARB(gr->target, gr->id);
      glBufferData(gr->target, width, NULL, GL_STATIC_DRAW);
   } else if (target == PIPE_BUFFER) {
      gr->target = GL_ARRAY_BUFFER_ARB;
      glGenBuffersARB(1, &gr->id);
      glBindBufferARB(gr->target, gr->id);
      glBufferData(gr->target, width, NULL, GL_STATIC_DRAW);
   } else {
      gr->target = tgsitargettogltarget(target);
      glGenTextures(1, &gr->id);
      glBindTexture(gr->target, gr->id);

      glTexImage2D(gr->target, 0, GL_RGBA, width, height, 0, GL_RGBA,
                   GL_UNSIGNED_BYTE, NULL);
   }

   graw_object_insert(gr, sizeof(*gr), handle, GRAW_RESOURCE);
}

void graw_renderer_transfer_write(uint32_t res_handle, struct pipe_box *box,
                                  void *data)
{
   struct grend_resource *res;
   void *ptr;

   res = graw_object_lookup(res_handle, GRAW_RESOURCE);
   if (res->target == GL_ELEMENT_ARRAY_BUFFER_ARB) {
      fprintf(stderr,"TRANSFER FOR IB\n");
   } else if (res->target == GL_ARRAY_BUFFER_ARB) {
      glBindBufferARB(GL_ARRAY_BUFFER_ARB, res->id);
      glBufferSubData(GL_ARRAY_BUFFER_ARB, box->x, box->width, data);
   } else {
      fprintf(stderr,"TRANSFER FOR TEXTURE\n");
   }
   

}

void graw_renderer_transfer_send(uint32_t res_handle, struct pipe_box *box)
{
   struct grend_resource *res;

   res = graw_object_lookup(res_handle, GRAW_RESOURCE);

   if (res->target == GL_ELEMENT_ARRAY_BUFFER_ARB) {
   } else if (res->target == GL_ARRAY_BUFFER_ARB) {

   } else {
      fprintf(stderr,"TEXTURE TRANSFER %d %d\n", box->width, box->height);
      void *data;
      uint32_t alloc_size = res->base.width0 * res->base.height0 * util_format_get_blocksize(res->base.format);
      uint32_t send_size = box->width * box->height * util_format_get_blocksize(res->base.format);
      data = malloc(alloc_size);

      if (!data)
         fprintf(stderr,"malloc failed %d\n", send_size);

      fprintf(stderr,"writing %d %d\n", alloc_size, send_size);
      glBindTexture(res->target, res->id);
      glGetTexImage(res->target, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
      graw_transfer_write_return(data, send_size);
   }
}
