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
#include "util/u_double_list.h"
#include "util/u_format.h"
#include "tgsi/tgsi_text.h"

#include "state_tracker/graw.h"

#include "graw_protocol.h"
#include "graw_object.h"
#include "graw_shader.h"

#include "graw_renderer.h"
#include "graw_renderer_glx.h"
#include "graw_decode.h"

int localrender;

struct grend_screen;

struct grend_fence {
   uint32_t fence_id;
   GLsync syncobj;
   struct list_head fences;
};

struct list_head fence_list;

struct grend_shader_state {
   GLuint id;
   unsigned type;
   char *glsl_prog;
};
struct grend_resource {
   struct pipe_resource base;
   GLuint id;
   GLenum target;
   /* fb id if we need to readback this resource */
   GLuint readback_fb_id;
  GLuint readback_fb_level;
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
   GLuint format;
   GLuint val0, val1;
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

   bool shader_dirty;
   int program_id;
   int num_fs_views;
   /* frag samplers */
   struct grend_sampler_view fs_views[PIPE_MAX_SHADER_SAMPLER_VIEWS];

   int num_vs_views;
   /* frag samplers */
   struct grend_sampler_view vs_views[PIPE_MAX_SHADER_SAMPLER_VIEWS];

   struct pipe_rasterizer_state *rs_state;

   struct pipe_index_buffer ib;

   struct grend_constants vs_consts;
   struct grend_constants fs_consts;

   struct pipe_sampler_state *sampler_state[PIPE_MAX_SAMPLERS];
   int num_sampler_states;
   uint32_t fb_id;

   uint8_t stencil_refs[2];

   struct pipe_depth_stencil_alpha_state *dsa;
   boolean stencil_state_dirty;
   struct list_head programs;
};

static struct grend_resource *frontbuffer;
static struct {
   GLenum internalformat;
   GLenum glformat;
   GLenum gltype;
} tex_conv_table[PIPE_FORMAT_COUNT] =
   {
      [PIPE_FORMAT_B8G8R8X8_UNORM] = {GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE },
      [PIPE_FORMAT_B8G8R8A8_UNORM] = {GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE },

      [PIPE_FORMAT_R8G8B8A8_UNORM] = {GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE },
      [PIPE_FORMAT_R8G8B8X8_UNORM] = {GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE },

      [PIPE_FORMAT_B4G4R4A4_UNORM] = {GL_RGBA4, GL_RGBA, GL_UNSIGNED_SHORT_4_4_4_4 },

      [PIPE_FORMAT_S8_UINT] = {GL_DEPTH24_STENCIL8_EXT, GL_DEPTH_STENCIL, GL_UNSIGNED_BYTE},
      [PIPE_FORMAT_Z24_UNORM_S8_UINT] = {GL_DEPTH24_STENCIL8_EXT, GL_DEPTH_STENCIL, GL_UNSIGNED_INT_24_8},

      [PIPE_FORMAT_Z24X8_UNORM] = {GL_DEPTH_COMPONENT24, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT},
      [PIPE_FORMAT_Z32_UNORM] = {GL_DEPTH_COMPONENT32, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT},      

      [PIPE_FORMAT_A8_UNORM] = {GL_ALPHA8, GL_ALPHA, GL_UNSIGNED_BYTE },
      [PIPE_FORMAT_A16_UNORM] = {GL_ALPHA16, GL_ALPHA, GL_UNSIGNED_SHORT },

      [PIPE_FORMAT_L8_UNORM] = {GL_LUMINANCE8, GL_LUMINANCE, GL_UNSIGNED_BYTE },
      [PIPE_FORMAT_L16_UNORM] = {GL_LUMINANCE16, GL_LUMINANCE, GL_UNSIGNED_BYTE },

      [PIPE_FORMAT_L4A4_UNORM] = {GL_LUMINANCE4_ALPHA4, GL_LUMINANCE_ALPHA, GL_UNSIGNED_BYTE },
      [PIPE_FORMAT_L8A8_UNORM] = {GL_LUMINANCE8_ALPHA8, GL_LUMINANCE_ALPHA, GL_UNSIGNED_BYTE },
      [PIPE_FORMAT_L16A16_UNORM] = {GL_LUMINANCE16_ALPHA16, GL_LUMINANCE_ALPHA, GL_UNSIGNED_SHORT },
      [PIPE_FORMAT_I8_UNORM] = {GL_INTENSITY8, GL_RGB, GL_UNSIGNED_BYTE },

      [PIPE_FORMAT_B4G4R4X4_UNORM] = {GL_RGB4, GL_RGB, GL_UNSIGNED_SHORT_4_4_4_4 },
      [PIPE_FORMAT_B2G3R3_UNORM] = {GL_R3_G3_B2, GL_RGB, GL_UNSIGNED_BYTE_3_3_2 },

      [PIPE_FORMAT_B5G5R5X1_UNORM] = {GL_RGB5_A1, GL_RGBA, GL_UNSIGNED_BYTE },
      [PIPE_FORMAT_B5G5R5A1_UNORM] = {GL_RGB5_A1, GL_RGBA, GL_UNSIGNED_BYTE },

      [PIPE_FORMAT_R16G16B16X16_UNORM] = {GL_RGBA16, GL_RGBA, GL_UNSIGNED_SHORT },
      [PIPE_FORMAT_R16G16B16A16_UNORM] = {GL_RGBA16, GL_RGBA, GL_UNSIGNED_SHORT },
      [PIPE_FORMAT_B10G10R10X2_UNORM] = {GL_RGB10_A2, GL_RGBA, GL_UNSIGNED_INT },
      [PIPE_FORMAT_B10G10R10A2_UNORM] = {GL_RGB10_A2, GL_RGBA, GL_UNSIGNED_INT },

      [PIPE_FORMAT_R8_UNORM] = {GL_RED, GL_RED, GL_UNSIGNED_BYTE },
      [PIPE_FORMAT_R8G8_UNORM] = {GL_RG, GL_RG, GL_UNSIGNED_BYTE },

      [PIPE_FORMAT_R16_UNORM] = {GL_R16, GL_RED, GL_UNSIGNED_SHORT },
      [PIPE_FORMAT_R16G16_UNORM] = {GL_RG16, GL_RG, GL_UNSIGNED_SHORT },

      [PIPE_FORMAT_R16_FLOAT] = {GL_R16F, GL_RED, GL_FLOAT },
      [PIPE_FORMAT_R16G16_FLOAT] = {GL_RG16F, GL_RG, GL_FLOAT },

      [PIPE_FORMAT_B8G8R8X8_SRGB] = {GL_SRGB8, GL_RGB, GL_UNSIGNED_BYTE },
      [PIPE_FORMAT_B8G8R8A8_SRGB] = {GL_SRGB8_ALPHA8, GL_RGB, GL_UNSIGNED_BYTE },
   };

struct shader_programs {
  struct list_head head;
  GLuint id;
  GLuint vs_id;
  GLuint fs_id;
};

static void add_shader_program(struct grend_context *ctx,
			int vs_id, int fs_id, int id) {
  struct shader_programs *sprog = malloc(sizeof(struct shader_programs));
  sprog->vs_id = vs_id;
  sprog->fs_id = fs_id;
  sprog->id = id;
  list_add(&sprog->head, &ctx->programs);
}

static GLuint lookup_shader_program(struct grend_context *ctx,
			   GLuint vs_id, GLuint fs_id)
{
  struct shader_programs *ent;
  LIST_FOR_EACH_ENTRY(ent, &ctx->programs, head) {
    if (ent->vs_id == vs_id && ent->fs_id == fs_id)
      return ent->id;
  }
  return 0;
}
  

static void grend_apply_sampler_state(struct grend_context *ctx, int id,
                                      int target);
void grend_update_stencil_state(struct grend_context *ctx);

void grend_create_surface(struct grend_context *ctx,
                          uint32_t handle,
                          uint32_t res_handle, uint32_t format,
                          uint32_t val0, uint32_t val1)
   
{
   struct grend_surface *surf;

   surf = CALLOC_STRUCT(grend_surface);

   surf->res_handle = res_handle;
   surf->format = format;
   surf->val0 = val0;
   surf->val1 = val1;

   graw_object_insert(surf, sizeof(*surf), handle, GRAW_SURFACE);
}

void grend_set_framebuffer_state(struct grend_context *ctx,
                                    uint32_t nr_cbufs, uint32_t surf_handle[8],
                                    uint32_t zsurf_handle)
{
   struct grend_surface *surf, *zsurf;
   struct grend_resource *tex;
   int i;
   static const GLenum buffers[8] = {
      GL_COLOR_ATTACHMENT0_EXT,
      GL_COLOR_ATTACHMENT1_EXT,
      GL_COLOR_ATTACHMENT2_EXT,
      GL_COLOR_ATTACHMENT3_EXT,
      GL_COLOR_ATTACHMENT4_EXT,
      GL_COLOR_ATTACHMENT5_EXT,
      GL_COLOR_ATTACHMENT6_EXT,
      GL_COLOR_ATTACHMENT7_EXT,
   };

   if (ctx->fb_id)
      glDeleteFramebuffers(1, &ctx->fb_id);
   glGenFramebuffers(1, &ctx->fb_id);

   glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, ctx->fb_id);

   if (zsurf_handle) {
      GLuint attachment;
      zsurf = graw_object_lookup(zsurf_handle, GRAW_SURFACE);

      tex = graw_object_lookup(zsurf->res_handle, GRAW_RESOURCE);
  
      if (tex->base.format == PIPE_FORMAT_S8_UINT)
         attachment = GL_STENCIL_ATTACHMENT;
      else if (tex->base.format == PIPE_FORMAT_Z24X8_UNORM || tex->base.format == PIPE_FORMAT_Z32_UNORM)
         attachment = GL_DEPTH_ATTACHMENT;
      else
         attachment = GL_DEPTH_STENCIL_ATTACHMENT;
      glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, attachment,
                                tex->target, tex->id, zsurf->val0);
   }

   for (i = 0; i < nr_cbufs; i++) {
      surf = graw_object_lookup(surf_handle[i], GRAW_SURFACE);
      tex = graw_object_lookup(surf->res_handle, GRAW_RESOURCE);

      if (tex->target == GL_TEXTURE_CUBE_MAP) {
         glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, buffers[i],
                                   GL_TEXTURE_CUBE_MAP_POSITIVE_X + (surf->val1 & 0xffff), tex->id, surf->val0);
      } else
         glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, buffers[i],
                                   tex->target, tex->id, surf->val0);
   }

   glDrawBuffers(nr_cbufs, buffers);
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

   v->count = num_elements;
   memcpy(v->elements, elements, sizeof(struct pipe_vertex_element) * num_elements);

   graw_object_insert(v, sizeof(struct grend_vertex_element), handle,
                      GRAW_OBJECT_VERTEX_ELEMENTS);
}

void grend_bind_vertex_elements_state(struct grend_context *ctx,
                                      uint32_t handle)
{
   struct grend_vertex_element *v;

   if (!handle) {
      ctx->ve = NULL;
      return;
   }
   v = graw_object_lookup(handle, GRAW_OBJECT_VERTEX_ELEMENTS);
   if (!v) {
      fprintf(stderr, "illegal ve lookup\n");
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
   ctx->fs_views[index].texture = res;
}

void grend_set_num_fs_sampler_views(struct grend_context *ctx,
                                    int num_fs_sampler_views)
{
   ctx->num_fs_views = num_fs_sampler_views;
}

void grend_set_single_vs_sampler_view(struct grend_context *ctx,
                                      int index,
                                      uint32_t res_handle)
{
   struct grend_resource *res;

   res = graw_object_lookup(res_handle, GRAW_RESOURCE);
   ctx->vs_views[index].texture = res;
}

void grend_set_num_vs_sampler_views(struct grend_context *ctx,
                                    int num_vs_sampler_views)
{
   ctx->num_vs_views = num_vs_sampler_views;
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

   res = graw_object_lookup(res_handle, GRAW_RESOURCE);
   if (res->target == GL_ELEMENT_ARRAY_BUFFER_ARB) {
      glBindBufferARB(GL_ELEMENT_ARRAY_BUFFER_ARB, res->id);
      glBufferSubData(GL_ELEMENT_ARRAY_BUFFER_ARB, box->x, box->width, data);
   } else if (res->target == GL_ARRAY_BUFFER_ARB) {
      glBindBufferARB(GL_ARRAY_BUFFER_ARB, res->id);
      glBufferSubData(GL_ARRAY_BUFFER_ARB, box->x, box->width, data);
   } else {
      GLenum glformat, gltype;
      glBindTexture(res->target, res->id);
      glformat = tex_conv_table[res->base.format].glformat;
      gltype = tex_conv_table[res->base.format].gltype; 

      glTexSubImage2D(res->target, level, box->x, box->y, box->width, box->height,
                      glformat, gltype, data);
   }
}


void grend_create_vs(struct grend_context *ctx,
                     uint32_t handle,
                     const struct pipe_shader_state *vs)
{
   struct grend_shader_state *state = CALLOC_STRUCT(grend_shader_state);
   const GLchar *glsl_prog;

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
      glShaderSource(state->id, 1, (const char **)&glsl_prog, NULL);
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

   if (ctx->vs != state)
      ctx->shader_dirty = true;
   ctx->vs = state;
}


void grend_bind_fs(struct grend_context *ctx,
                   uint32_t handle)
{
   struct grend_shader_state *state;

   state = graw_object_lookup(handle, GRAW_OBJECT_FS);

   if (ctx->fs != state)
      ctx->shader_dirty = true;
   ctx->fs = state;
}

void grend_clear(struct grend_context *ctx,
                 unsigned buffers,
                 const union pipe_color_union *color,
                 double depth, unsigned stencil)
{
   GLbitfield bits = 0;
   glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, ctx->fb_id);
   glUseProgram(0);
   glClearColor(color->f[0], color->f[1], color->f[2], color->f[3]);

   if (buffers & PIPE_CLEAR_DEPTH)
      glClearDepth(depth);

   if (buffers & PIPE_CLEAR_STENCIL)
      glClearStencil(stencil);

   if (buffers & PIPE_CLEAR_COLOR)
      bits |= GL_COLOR_BUFFER_BIT;
   if (buffers & PIPE_CLEAR_DEPTH)
      bits |= GL_DEPTH_BUFFER_BIT;
   if (buffers & PIPE_CLEAR_STENCIL)
      bits |= GL_STENCIL_BUFFER_BIT;
   glClear(bits);

}

void grend_draw_vbo(struct grend_context *ctx,
                    const struct pipe_draw_info *info)
{
   GLuint vaoid;
   int i;
   int sampler_id;
   GLuint prog_id;
  
   if (ctx->stencil_state_dirty)
      grend_update_stencil_state(ctx);

   if (ctx->shader_dirty) {
     prog_id = lookup_shader_program(ctx, ctx->vs->id, ctx->fs->id);
     if (prog_id == 0) {
       prog_id = glCreateProgram();
       glAttachShader(prog_id, ctx->vs->id);
       glAttachShader(prog_id, ctx->fs->id);
       glLinkProgram(prog_id);
       add_shader_program(ctx, ctx->vs->id, ctx->fs->id, prog_id);
     }
     ctx->program_id = prog_id;
   }

   glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, ctx->fb_id);

   glUseProgram(ctx->program_id);
   glGenVertexArrays(1, &vaoid);

   glBindVertexArray(vaoid);

   for (i = 0; i < ctx->vs_consts.num_consts / 4; i++) {
      GLint loc;
      char name[10];
      snprintf(name, 10, "vsconst%d", i);
      loc = glGetUniformLocation(ctx->program_id, name);
      if (loc != -1)
         glUniform4fv(loc, 1, &ctx->vs_consts.consts[i * 4]);
//      else
//         fprintf(stderr,"unknown constant %s\n", name);
   }
   for (i = 0; i < ctx->fs_consts.num_consts / 4; i++) {
      GLint loc;
      char name[10];
      snprintf(name, 10, "fsconst%d", i);
      loc = glGetUniformLocation(ctx->program_id, name);
      if (loc != -1)
         glUniform4fv(loc, 1, &ctx->fs_consts.consts[i * 4]);
//      else
//         fprintf(stderr,"unknown constant %s\n", name);

   }

   sampler_id = 0;
   for (i = 0; i < ctx->num_vs_views; i++) {
      GLint loc;
      char name[10];
      snprintf(name, 10, "samp%d", i);
      loc = glGetUniformLocation(ctx->program_id, name);
      glUniform1i(loc, sampler_id);

      glActiveTexture(GL_TEXTURE0 + sampler_id);
      glBindTexture(ctx->vs_views[i].texture->target, ctx->vs_views[i].texture->id);
      glEnable(ctx->vs_views[i].texture->target);
      grend_apply_sampler_state(ctx, sampler_id, ctx->vs_views[i].texture->target);
      sampler_id++;

   } 

   for (i = 0; i < ctx->num_fs_views; i++) {
      GLint loc;
      char name[10];
      snprintf(name, 10, "samp%d", i);
      loc = glGetUniformLocation(ctx->program_id, name);
      glUniform1i(loc, sampler_id);
      if (ctx->fs_views[i].texture) {
         glActiveTexture(GL_TEXTURE0 + sampler_id);
         glBindTexture(ctx->fs_views[i].texture->target, ctx->fs_views[i].texture->id);
         glEnable(ctx->fs_views[i].texture->target);
         grend_apply_sampler_state(ctx, sampler_id, ctx->fs_views[i].texture->target);
         if (ctx->rs_state->point_quad_rasterization) {
            if (ctx->rs_state->sprite_coord_enable & (1 << i))
               glTexEnvi(GL_POINT_SPRITE_ARB, GL_COORD_REPLACE_ARB, GL_TRUE);
            else
               glTexEnvi(GL_POINT_SPRITE_ARB, GL_COORD_REPLACE_ARB, GL_FALSE);
         }
      }
      sampler_id++;
   } 
//   glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
//   glPixelStorei(GL_UNPACK_ALIGNMENT, 4);

   for (i = 0; i < ctx->ve->count; i++) {
      int vbo_index = ctx->ve->elements[i].vertex_buffer_index;
      const struct util_format_description *desc = util_format_description(ctx->ve->elements[i].src_format);
      struct grend_buffer *buf;
      GLenum type = GL_FALSE;
      int sz;
      GLboolean norm = GL_FALSE;
      if (desc->channel[0].type == UTIL_FORMAT_TYPE_FLOAT) {
	 if (desc->channel[0].size == 32)
	    type = GL_FLOAT;
	 else if (desc->channel[0].size == 64)
	    type = GL_DOUBLE;
      } else if (desc->channel[0].type == UTIL_FORMAT_TYPE_UNSIGNED &&
               desc->channel[0].size == 8) 
         type = GL_UNSIGNED_BYTE;
      else if (desc->channel[0].type == UTIL_FORMAT_TYPE_SIGNED &&
               desc->channel[0].size == 8) 
         type = GL_BYTE;
      else if (desc->channel[0].type == UTIL_FORMAT_TYPE_UNSIGNED &&
               desc->channel[0].size == 16) 
         type = GL_UNSIGNED_SHORT;
      else if (desc->channel[0].type == UTIL_FORMAT_TYPE_SIGNED &&
               desc->channel[0].size == 16) 
         type = GL_SHORT;
      else if (desc->channel[0].type == UTIL_FORMAT_TYPE_UNSIGNED &&
               desc->channel[0].size == 32) 
         type = GL_UNSIGNED_INT;
      else if (desc->channel[0].type == UTIL_FORMAT_TYPE_SIGNED &&
               desc->channel[0].size == 32) 
         type = GL_INT;
      if (desc->channel[0].normalized)
         norm = GL_TRUE;
      sz = desc->nr_channels;

      buf = (struct grend_buffer *)ctx->vbo[vbo_index].buffer;
      glBindBuffer(GL_ARRAY_BUFFER, buf->base.id);
      glVertexAttribPointer(i, sz, type, norm, ctx->vbo[vbo_index].stride, (void *)(unsigned long)(ctx->ve->elements[i].src_offset + ctx->vbo[vbo_index].buffer_offset));
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
      if (info->instance_count <= 1)
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

      if (info->instance_count <= 1)
         glDrawElements(mode, info->count, elsz, (void *)(unsigned long)ctx->ib.offset);
      else
         glDrawElementsInstancedARB(mode, info->count, elsz, (void *)(unsigned long)ctx->ib.offset, info->instance_count);
   }

   glActiveTexture(GL_TEXTURE0);
   glDisable(GL_TEXTURE_2D);
   glBindVertexArray(0);
   //   glDeleteVertexArrays(1, &vaoid);
}

static GLenum translate_blend_func(uint32_t pipe_blend)
{
   switch(pipe_blend){
   case PIPE_BLEND_ADD: return GL_FUNC_ADD;
   case PIPE_BLEND_SUBTRACT: return GL_FUNC_SUBTRACT;
   case PIPE_BLEND_REVERSE_SUBTRACT: return GL_FUNC_REVERSE_SUBTRACT;
   case PIPE_BLEND_MIN: return GL_MIN;
   case PIPE_BLEND_MAX: return GL_MAX;
   default:
      assert("invalid blend token()" == NULL);
      return 0;
   }
}

static GLenum translate_blend_factor(uint32_t pipe_factor)
{
   switch (pipe_factor) {
   case PIPE_BLENDFACTOR_ONE: return GL_ONE;
   case PIPE_BLENDFACTOR_SRC_COLOR: return GL_SRC_COLOR;
   case PIPE_BLENDFACTOR_SRC_ALPHA: return GL_SRC_ALPHA;

   case PIPE_BLENDFACTOR_DST_COLOR: return GL_DST_COLOR;
   case PIPE_BLENDFACTOR_DST_ALPHA: return GL_DST_ALPHA;

   case PIPE_BLENDFACTOR_CONST_COLOR: return GL_CONSTANT_COLOR;
   case PIPE_BLENDFACTOR_CONST_ALPHA: return GL_CONSTANT_ALPHA;

   case PIPE_BLENDFACTOR_SRC1_COLOR: return GL_SRC1_COLOR;
   case PIPE_BLENDFACTOR_SRC1_ALPHA: return GL_SRC1_ALPHA;
   case PIPE_BLENDFACTOR_SRC_ALPHA_SATURATE: return GL_SRC_ALPHA_SATURATE;
   case PIPE_BLENDFACTOR_ZERO: return GL_ZERO;


   case PIPE_BLENDFACTOR_INV_SRC_COLOR: return GL_ONE_MINUS_SRC_COLOR;
   case PIPE_BLENDFACTOR_INV_SRC_ALPHA: return GL_ONE_MINUS_SRC_ALPHA;

   case PIPE_BLENDFACTOR_INV_DST_COLOR: return GL_ONE_MINUS_DST_COLOR;
   case PIPE_BLENDFACTOR_INV_DST_ALPHA: return GL_ONE_MINUS_DST_ALPHA;

   case PIPE_BLENDFACTOR_INV_CONST_COLOR: return GL_ONE_MINUS_CONSTANT_COLOR;
   case PIPE_BLENDFACTOR_INV_CONST_ALPHA: return GL_ONE_MINUS_CONSTANT_ALPHA;

   case PIPE_BLENDFACTOR_INV_SRC1_COLOR: return GL_ONE_MINUS_SRC1_COLOR;
   case PIPE_BLENDFACTOR_INV_SRC1_ALPHA: return GL_ONE_MINUS_SRC1_ALPHA;

   default:
      assert("invalid blend token()" == NULL);
      return 0;
   }
}

static GLenum
translate_logicop(GLuint pipe_logicop)
{
   switch (pipe_logicop) {
#define CASE(x) case PIPE_LOGICOP_##x: return GL_##x
      CASE(CLEAR);
      CASE(NOR);
      CASE(AND_INVERTED);
      CASE(COPY_INVERTED);
      CASE(AND_REVERSE);
      CASE(INVERT);
      CASE(XOR);
      CASE(NAND);
      CASE(AND);
      CASE(EQUIV);
      CASE(NOOP);
      CASE(OR_INVERTED);
      CASE(COPY);
      CASE(OR_REVERSE);
      CASE(OR);
      CASE(SET);
   default:
      assert("invalid logicop token()" == NULL);
      return 0;
   }
#undef CASE
}

static GLenum
translate_stencil_op(GLuint op)
{
   switch (op) {
#define CASE(x) case PIPE_STENCIL_OP_##x: return GL_##x   
      CASE(KEEP);
      CASE(ZERO);
      CASE(REPLACE);
      CASE(INCR);
      CASE(DECR);
      CASE(INCR_WRAP);
      CASE(DECR_WRAP);
      CASE(INVERT);
   default:
      assert("invalid stencilop token()" == NULL);
      return 0;
   }
#undef CASE
}

void grend_object_bind_blend(struct grend_context *ctx,
                             uint32_t handle)
{
   struct pipe_blend_state *state;

   if (handle == 0) {
      glDisable(GL_BLEND);
      return;
   }
   state = graw_object_lookup(handle, GRAW_OBJECT_BLEND);

   if (state->logicop_enable) {
      glEnable(GL_COLOR_LOGIC_OP);
      glLogicOp(translate_logicop(state->logicop_func));
   } else
      glDisable(GL_COLOR_LOGIC_OP);

   if (state->independent_blend_enable) {
      assert(0);
      exit(-1);
   } else {
      if (state->rt[0].blend_enable) {
         glBlendFunc(translate_blend_factor(state->rt[0].rgb_src_factor),
                     translate_blend_factor(state->rt[0].rgb_dst_factor));
         glBlendEquation(translate_blend_func(state->rt[0].rgb_func));
         glEnable(GL_BLEND);
      } 
      else
         glDisable(GL_BLEND);
   }
}

void grend_object_bind_dsa(struct grend_context *ctx,
                           uint32_t handle)
{
   struct pipe_depth_stencil_alpha_state *state;

   if (handle == 0) {
      glDisable(GL_DEPTH_TEST);
      glDisable(GL_ALPHA_TEST);
      glDisable(GL_STENCIL_TEST);
      ctx->dsa = NULL;
      return;
   }

   state = graw_object_lookup(handle, GRAW_OBJECT_DSA);

   if (state->depth.enabled) {
      glEnable(GL_DEPTH_TEST);
      glDepthFunc(GL_NEVER + state->depth.func);
   } else
      glDisable(GL_DEPTH_TEST);
 
   if (state->alpha.enabled) {
      glEnable(GL_ALPHA_TEST);
      glAlphaFunc(GL_NEVER + state->alpha.func, state->alpha.ref_value);
   } else
      glDisable(GL_ALPHA_TEST);

   if (ctx->dsa != state)
      ctx->stencil_state_dirty = TRUE;
   ctx->dsa = state;
}
 
void grend_update_stencil_state(struct grend_context *ctx)
{
   struct pipe_depth_stencil_alpha_state *state = ctx->dsa;
   int i;
   if (!state)
      return;

   if (!state->stencil[1].enabled) {
      if (state->stencil[0].enabled) {
         glEnable(GL_STENCIL_TEST);

         glStencilOp(translate_stencil_op(state->stencil[0].fail_op), 
                     translate_stencil_op(state->stencil[0].zpass_op),
                     translate_stencil_op(state->stencil[0].zfail_op));
         glStencilFunc(GL_NEVER + state->stencil[0].func,
                       ctx->stencil_refs[0],
                       state->stencil[0].valuemask);
         glStencilMask(state->stencil[0].writemask);
      } else
         glDisable(GL_STENCIL_TEST);
   } else {
      glEnable(GL_STENCIL_TEST);

      for (i = 0; i < 2; i++) {
         GLenum face = (i == 1) ? GL_BACK : GL_FRONT;
         glStencilOpSeparate(face, translate_stencil_op(state->stencil[i].fail_op), 
                             translate_stencil_op(state->stencil[i].zpass_op),
                             translate_stencil_op(state->stencil[i].zfail_op));
         glStencilFuncSeparate(face, GL_NEVER + state->stencil[i].func,
                               ctx->stencil_refs[i],
                               state->stencil[i].valuemask);
         glStencilMaskSeparate(face, state->stencil[i].writemask);
      }
   }
   ctx->stencil_state_dirty = FALSE;
}

static inline GLenum translate_fill(uint32_t mode)
{
   switch (mode) {
   case PIPE_POLYGON_MODE_POINT:
      return GL_POINT;
   case PIPE_POLYGON_MODE_LINE:
      return GL_LINE;
   case PIPE_POLYGON_MODE_FILL:
      return GL_FILL;
   }
   assert(0);
   return 0;
}
void grend_object_bind_rasterizer(struct grend_context *ctx,
                                  uint32_t handle)
{
   struct pipe_rasterizer_state *state;

   if (handle == 0)
      return;
   state = graw_object_lookup(handle, GRAW_OBJECT_RASTERIZER);
   
#if 0
   if (state->depth_clip) {
      glEnable(GL_DEPTH_CLAMP);
   } else {
      glDisable(GL_DEPTH_CLAMP);
   }
#endif
   if (state->point_size)
      glPointSize(state->point_size);

   glPolygonMode(GL_FRONT, translate_fill(state->fill_front));
   glPolygonMode(GL_BACK, translate_fill(state->fill_back));
   if (state->flatshade) {
      glShadeModel(GL_FLAT);
   } else {
      glShadeModel(GL_SMOOTH);
   }

   if (state->front_ccw)
      glFrontFace(GL_CCW);
   else
      glFrontFace(GL_CW);

   if (state->scissor)
      glEnable(GL_SCISSOR_TEST);
   else
      glDisable(GL_SCISSOR_TEST);

   if (state->point_quad_rasterization) {
      glEnable(GL_POINT_SPRITE);

      if (state->sprite_coord_mode)
         glPointParameteri(GL_POINT_SPRITE_COORD_ORIGIN, state->sprite_coord_mode ? GL_LOWER_LEFT : GL_UPPER_LEFT);
   } else
      glDisable(GL_POINT_SPRITE);

   if (state->cull_face != PIPE_FACE_NONE) {
      switch (state->cull_face) {
      case PIPE_FACE_FRONT:
         glCullFace(GL_FRONT);
         break;
      case PIPE_FACE_BACK:
         glCullFace(GL_BACK);
         break;
      case PIPE_FACE_FRONT_AND_BACK:
         glCullFace(GL_FRONT_AND_BACK);
         break;
      }
      glEnable(GL_CULL_FACE);
   } else
      glDisable(GL_CULL_FACE);

   ctx->rs_state = state;
}

static GLuint convert_wrap(int wrap)
{
   switch(wrap){
   case PIPE_TEX_WRAP_REPEAT: return GL_REPEAT;
   case PIPE_TEX_WRAP_CLAMP: return GL_CLAMP;

   case PIPE_TEX_WRAP_CLAMP_TO_EDGE: return GL_CLAMP_TO_EDGE;
   case PIPE_TEX_WRAP_CLAMP_TO_BORDER: return GL_CLAMP_TO_BORDER;

   case PIPE_TEX_WRAP_MIRROR_REPEAT: return GL_MIRRORED_REPEAT;
   default:
      assert(0);
      return -1;
   }
} 

void grend_object_bind_sampler_states(struct grend_context *ctx,
                                      uint32_t num_states,
                                      uint32_t *handles)
{
   int i;
   struct pipe_sampler_state *state;

   ctx->num_sampler_states = num_states;

   for (i = 0; i < num_states; i++) {
      state = graw_object_lookup(handles[i], GRAW_OBJECT_SAMPLER_STATE);

      ctx->sampler_state[i] = state;
   }
}

static inline GLenum convert_mag_filter(unsigned int filter)
{
   if (filter == PIPE_TEX_FILTER_NEAREST)
      return GL_NEAREST;
   return GL_LINEAR;
}

static inline GLenum convert_min_filter(unsigned int filter, unsigned int mip_filter)
{
   if (mip_filter == PIPE_TEX_MIPFILTER_NONE)
      return convert_mag_filter(filter);
   else if (mip_filter == PIPE_TEX_MIPFILTER_LINEAR) {
      if (filter == PIPE_TEX_FILTER_NEAREST)
         return GL_NEAREST_MIPMAP_LINEAR;
      else
         return GL_LINEAR_MIPMAP_LINEAR;
   } else if (mip_filter == PIPE_TEX_MIPFILTER_NEAREST) {
      if (filter == PIPE_TEX_FILTER_NEAREST)
         return GL_NEAREST_MIPMAP_NEAREST;
      else
         return GL_LINEAR_MIPMAP_NEAREST;
   }
   assert(0);
   return 0;
}

static void grend_apply_sampler_state(struct grend_context *ctx, int id,
                                      int target)
{
   struct pipe_sampler_state *state = ctx->sampler_state[id];

   glTexParameteri(target, GL_TEXTURE_WRAP_S, convert_wrap(state->wrap_s));
   glTexParameteri(target, GL_TEXTURE_WRAP_T, convert_wrap(state->wrap_t));
   glTexParameteri(target, GL_TEXTURE_WRAP_R, convert_wrap(state->wrap_r));
   glTexParameterf(target, GL_TEXTURE_MIN_FILTER, convert_min_filter(state->min_img_filter, state->min_mip_filter));
   glTexParameterf(target, GL_TEXTURE_MAG_FILTER, convert_mag_filter(state->mag_img_filter));
   glTexParameterf(target, GL_TEXTURE_MIN_LOD, state->min_lod);
   glTexParameterf(target, GL_TEXTURE_MAX_LOD, state->max_lod);
   glTexParameterf(target, GL_TEXTURE_LOD_BIAS, state->lod_bias);
}

void grend_flush(struct grend_context *ctx)
{
   glFlush();
}

void grend_flush_frontbuffer(uint32_t res_handle)
{
   struct grend_resource *res;

   if (!localrender)
      return;
       
#if 1
   res = graw_object_lookup(res_handle, GRAW_RESOURCE);

   glDrawBuffer(GL_NONE);
   glUseProgram(0);
   
   glMatrixMode(GL_PROJECTION);
   glLoadIdentity();
   glOrtho(0, res->base.width0, 0, res->base.height0, -1, 1);
   glMatrixMode(GL_MODELVIEW);
   glLoadIdentity();
   glDisable(GL_BLEND);
   glBindTexture(res->target, res->id);
   glBindFramebuffer(GL_FRAMEBUFFER_EXT, 0);
   glEnable(res->target);
   glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
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
   swap_buffers();
#endif
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
   case PIPE_TEXTURE_CUBE:
      return GL_TEXTURE_CUBE_MAP;
   case PIPE_BUFFER:
   case PIPE_TEXTURE_1D_ARRAY:
   case PIPE_TEXTURE_2D_ARRAY:
   case PIPE_TEXTURE_CUBE_ARRAY:
   default:
      return PIPE_BUFFER;
   }
   return PIPE_BUFFER;
}

static int inited;

void
graw_renderer_init(void)
{
   if (!inited) {
      inited = 1;
      graw_object_init_hash();
   }
   
   glewInit();
   list_inithead(&fence_list);
}

void
graw_renderer_fini(void)
{
   if (!inited)
      return;

   graw_object_fini_hash();
   inited = 0;
}


struct grend_context *grend_create_context(void)
{
   struct grend_context *grctx = CALLOC_STRUCT(grend_context);
   list_inithead(&grctx->programs);
   return grctx;
}


void graw_renderer_resource_create(uint32_t handle, enum pipe_texture_target target, uint32_t format, uint32_t bind, uint32_t width, uint32_t height, uint32_t depth, uint32_t array_size, uint32_t last_level, uint32_t nr_samples)
{
   struct grend_resource *gr = CALLOC_STRUCT(grend_resource);
   int level;

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
      GLenum internalformat, glformat, gltype;
      gr->target = tgsitargettogltarget(target);
      glGenTextures(1, &gr->id);
      glBindTexture(gr->target, gr->id);

      fprintf(stderr,"format is %d\n", format);
      internalformat = tex_conv_table[format].internalformat;
      glformat = tex_conv_table[format].glformat;
      gltype = tex_conv_table[format].gltype;
      if (internalformat == 0) {
         fprintf(stderr,"unknown format is %d\n", format);
         assert(0);
         internalformat = GL_RGBA;
         glformat = GL_RGBA;
         gltype = GL_UNSIGNED_BYTE;
      }

      if (gr->target == GL_TEXTURE_CUBE_MAP) {
         int i;
         for (i = 0; i < 6; i++) {
            GLenum ctarget = GL_TEXTURE_CUBE_MAP_POSITIVE_X + i;
            for (level = 0; level <= last_level; level++) {
               unsigned mwidth = u_minify(width, level);
               unsigned mheight = u_minify(height, level);
               glTexImage2D(ctarget, level, internalformat, mwidth, mheight, 0, glformat,
                            gltype, NULL);
            }
         }
      } else if (gr->target == GL_TEXTURE_3D) {
         glTexImage3D(gr->target, 0, internalformat, width, height, depth, 0,
                      glformat,
                      gltype, NULL);
      } else if (gr->target == GL_TEXTURE_1D) {
         glTexImage1D(gr->target, 0, internalformat, width, 0,
                      glformat,
                      gltype, NULL);
      } else {
         for (level = 0; level <= last_level; level++) {
            unsigned mwidth = u_minify(width, level);
            unsigned mheight = u_minify(height, level);
            glTexImage2D(gr->target, level, internalformat, mwidth, mheight, 0, glformat,
                         gltype, NULL);
         }
      }
   }

   graw_object_insert(gr, sizeof(*gr), handle, GRAW_RESOURCE);
}

void graw_renderer_resource_unref(uint32_t res_handle)
{
   struct grend_resource *res;

   res = graw_object_lookup(res_handle, GRAW_RESOURCE);

   if (res->target == GL_ELEMENT_ARRAY_BUFFER_ARB) {
      glDeleteBuffers(1, &res->id);
   } else if (res->target == GL_ARRAY_BUFFER_ARB) {
      glDeleteBuffers(1, &res->id);
   } else
      glDeleteTextures(1, &res->id);

   graw_object_destroy(res_handle, GRAW_RESOURCE);
}

void graw_renderer_transfer_write(uint32_t res_handle,
                                  int level,
                                  uint32_t src_stride,
                                  struct pipe_box *dst_box,
                                  void *data)
{
   struct grend_resource *res;

   res = graw_object_lookup(res_handle, GRAW_RESOURCE);
   if (res->target == GL_ELEMENT_ARRAY_BUFFER_ARB) {
      glBindBufferARB(GL_ELEMENT_ARRAY_BUFFER_ARB, res->id);
      glBufferSubData(GL_ELEMENT_ARRAY_BUFFER_ARB, dst_box->x, dst_box->width, data);
   } else if (res->target == GL_ARRAY_BUFFER_ARB) {
      glBindBufferARB(GL_ARRAY_BUFFER_ARB, res->id);
      glBufferSubData(GL_ARRAY_BUFFER_ARB, dst_box->x, dst_box->width, data);
   } else {
      GLenum glformat;
      GLenum gltype;
      int old_stride = 0;

      glBindTexture(res->target, res->id);

      glformat = tex_conv_table[res->base.format].glformat;
      gltype = tex_conv_table[res->base.format].gltype; 
      if (glformat == 0) {
	 glformat = GL_BGRA;
	 gltype = GL_UNSIGNED_BYTE;
      }
	 
      if (src_stride) {
         glGetIntegerv(GL_UNPACK_ROW_LENGTH, &old_stride);
         glPixelStorei(GL_UNPACK_ROW_LENGTH, src_stride / 4);
      }
      if (res->target == GL_TEXTURE_CUBE_MAP) {
         GLenum ctarget = GL_TEXTURE_CUBE_MAP_POSITIVE_X + dst_box->z;

         glTexSubImage2D(ctarget, level, dst_box->x, dst_box->y, dst_box->width, dst_box->height,
                         glformat, gltype, data);
      } else if (res->target == GL_TEXTURE_3D) {
         glTexSubImage3D(res->target, level, dst_box->x, dst_box->y, dst_box->z,
                         dst_box->width, dst_box->height, dst_box->depth,
                         glformat, gltype, data);
      } else if (res->target == GL_TEXTURE_1D) {
         glTexSubImage1D(res->target, level, dst_box->x, dst_box->width,
                         glformat, gltype, data);
      } else {
         glTexSubImage2D(res->target, level, dst_box->x, dst_box->y,
                         dst_box->width, dst_box->height,
                         glformat, gltype, data);
      }
      if (src_stride)
         glPixelStorei(GL_UNPACK_ROW_LENGTH, old_stride);
   }
}

void graw_renderer_transfer_send(uint32_t res_handle, uint32_t level, struct pipe_box *box, void *myptr)
{
   struct grend_resource *res;

   res = graw_object_lookup(res_handle, GRAW_RESOURCE);

   if (res->target == GL_ELEMENT_ARRAY_BUFFER_ARB) {
      uint32_t send_size = box->width * util_format_get_blocksize(res->base.format);      
      void *data;
      glBindBufferARB(GL_ELEMENT_ARRAY_BUFFER_ARB, res->id);
      data = glMapBuffer(GL_ELEMENT_ARRAY_BUFFER_ARB, GL_READ_ONLY);
      graw_transfer_write_return(data + box->x, send_size, myptr);
      glUnmapBuffer(GL_ELEMENT_ARRAY_BUFFER_ARB);
   } else if (res->target == GL_ARRAY_BUFFER_ARB) {
      uint32_t send_size = box->width * util_format_get_blocksize(res->base.format);      
      void *data;
      glBindBufferARB(GL_ARRAY_BUFFER_ARB, res->id);
      data = glMapBuffer(GL_ARRAY_BUFFER_ARB, GL_READ_ONLY);
      graw_transfer_write_return(data + box->x, send_size, myptr);
      glUnmapBuffer(GL_ARRAY_BUFFER_ARB);
   } else {
      uint32_t h = u_minify(res->base.height0, level);
      GLenum format, type;
      GLuint fb_id;
      GLint  y1;
//      data = malloc(send_size);
      fprintf(stderr,"TEXTURE TRANSFER %d %d %d %d %d\n", res_handle, res->readback_fb_id, box->width, box->height, level);
//      if (!data)
      //       fprintf(stderr,"malloc failed %d\n", send_size);
      if (res->readback_fb_id == 0 || res->readback_fb_level != level) {
	if (res->readback_fb_id)
	  glDeleteFramebuffers(1, &res->readback_fb_id);

	glGenFramebuffers(1, &fb_id);
	glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, fb_id);
	glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT,
				  res->target, res->id, level);
	res->readback_fb_id = fb_id;
	res->readback_fb_level = level;
      } else
	glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, res->readback_fb_id);

      glPixelStorei(GL_PACK_INVERT_MESA, 1);
      glReadBuffer(GL_COLOR_ATTACHMENT0_EXT);

      switch (res->base.format) {
      case PIPE_FORMAT_B8G8R8A8_UNORM:
      default:
         format = GL_BGRA;
         type = GL_UNSIGNED_BYTE;
         break;
      }

      y1 = h - box->y - box->height;
      glReadPixels(box->x, y1, box->width, box->height, format, type, myptr);

      glPixelStorei(GL_PACK_INVERT_MESA, 0);
      //      graw_transfer_write_tex_return(&res->base, box, level, data, myptr);
//      free(data);
   }
}

void grend_set_stencil_ref(struct grend_context *ctx,
                           struct pipe_stencil_ref *ref)
{
   if (ctx->stencil_refs[0] != ref->ref_value[0] ||
       ctx->stencil_refs[1] != ref->ref_value[1]) {
      ctx->stencil_refs[0] = ref->ref_value[0];
      ctx->stencil_refs[1] = ref->ref_value[1];
      ctx->stencil_state_dirty = TRUE;
   }
   
}

void grend_set_blend_color(struct grend_context *ctx,
                           struct pipe_blend_color *color)
{
   glBlendColor(color->color[0], color->color[1], color->color[2],
                color->color[3]);
}

void grend_set_scissor_state(struct grend_context *ctx,
                             struct pipe_scissor_state *ss)
{
   glScissor(ss->minx, ss->miny, ss->maxx - ss->minx, ss->maxy - ss->miny);
}

void graw_renderer_resource_copy_region(struct grend_context *ctx,
                                        uint32_t dst_handle, uint32_t dst_level,
                                        uint32_t dstx, uint32_t dsty, uint32_t dstz,
                                        uint32_t src_handle, uint32_t src_level,
                                        const struct pipe_box *src_box)
{
   struct grend_resource *src_res, *dst_res;   
   GLuint fb_ids[2];
   GLbitfield glmask = 0;
   GLint sy1, sy2, dy1, dy2;
   src_res = graw_object_lookup(src_handle, GRAW_RESOURCE);
   dst_res = graw_object_lookup(dst_handle, GRAW_RESOURCE);

   glGenFramebuffers(2, fb_ids);
   glBindFramebuffer(GL_FRAMEBUFFER_EXT, fb_ids[0]);
   glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT,
                             src_res->target, src_res->id, src_level);

   glBindFramebuffer(GL_FRAMEBUFFER_EXT, fb_ids[1]);
   glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT,
                             dst_res->target, dst_res->id, dst_level);

   glBindFramebuffer(GL_READ_FRAMEBUFFER, fb_ids[0]);
   glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fb_ids[1]);

   glmask = GL_COLOR_BUFFER_BIT;

   sy1 = src_box->y;
   sy2 = sy1 + src_box->height;
   dy1 = dsty;
   dy2 = dy1 + src_box->height;

   glBlitFramebuffer(src_box->x, sy1,
                     src_box->x + src_box->width,
                     sy2,
                     dstx, dy1,
                     dstx + src_box->width,
                     dy2,
                     glmask, GL_NEAREST);

   glDeleteFramebuffers(2, fb_ids);
}

void graw_renderer_blit(struct grend_context *ctx,
                        uint32_t dst_handle, uint32_t src_handle,
                        const struct pipe_blit_info *info)
{
   struct grend_resource *src_res, *dst_res;
   GLuint fb_ids[2];
   GLbitfield glmask = 0;
   int y1, y2;

   src_res = graw_object_lookup(src_handle, GRAW_RESOURCE);
   dst_res = graw_object_lookup(dst_handle, GRAW_RESOURCE);

   glGenFramebuffers(2, fb_ids);
   glBindFramebuffer(GL_FRAMEBUFFER_EXT, fb_ids[0]);

   if (src_res->target == GL_TEXTURE_CUBE_MAP) {
      glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT,
                                GL_TEXTURE_CUBE_MAP_POSITIVE_X + info->src.level, src_res->id, 0);
   } else
      glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT,
                                src_res->target, src_res->id, info->src.level);

   glBindFramebuffer(GL_FRAMEBUFFER_EXT, fb_ids[1]);
   glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT,
                             dst_res->target, dst_res->id, info->dst.level);

   glBindFramebuffer(GL_READ_FRAMEBUFFER, fb_ids[0]);
   glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fb_ids[1]);

   if (info->mask & PIPE_MASK_Z)
      glmask |= GL_DEPTH_BUFFER_BIT;
   if (info->mask & PIPE_MASK_S)
      glmask |= GL_STENCIL_BUFFER_BIT;
   if (info->mask & PIPE_MASK_RGBA)
      glmask |= GL_COLOR_BUFFER_BIT;

   y1 = info->src.box.y;
   y2 = (info->src.box.y + info->src.box.height);

   if (info->scissor_enable)
      glEnable(GL_SCISSOR_TEST);
   else
      glDisable(GL_SCISSOR_TEST);
      
   glBlitFramebuffer(info->src.box.x,
                     y1,
                     info->src.box.x + info->src.box.width,
                     y2,
                     info->dst.box.x, info->dst.box.y,
                     info->dst.box.x + info->dst.box.width,
                     info->dst.box.y + info->dst.box.height,
                     glmask, convert_mag_filter(info->filter));

   glDeleteFramebuffers(2, fb_ids);
}

int graw_renderer_set_scanout(uint32_t res_handle,
                              struct pipe_box *box)
{
   struct grend_resource *res;
   res = graw_object_lookup(res_handle, GRAW_RESOURCE);
   if (!res)
      return 0;

   frontbuffer = res;
   fprintf(stderr,"setting frontbuffer to %d\n", res_handle);
   return 0;
}

int graw_renderer_flush_buffer(uint32_t res_handle,
                               struct pipe_box *box)
{
   struct grend_resource *res;
   GLuint fb_id;

   if (!localrender)
      return 0;

   res = graw_object_lookup(res_handle, GRAW_RESOURCE);
   if (!res)
      return 0;

   if (res != frontbuffer) {
      fprintf(stderr,"not the frontbuffer %d\n", res_handle);
      return 0;
   }

   glGenFramebuffers(1, &fb_id);

   glBindFramebuffer(GL_FRAMEBUFFER_EXT, fb_id);
   glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT,
                             res->target, res->id, 0);

   glBindFramebuffer(GL_READ_FRAMEBUFFER, fb_id);
   glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);

   glBlitFramebuffer(0, 0, res->base.width0, res->base.height0,
                     0, res->base.height0, res->base.width0, 0,
                     GL_COLOR_BUFFER_BIT, GL_NEAREST);
   glDeleteFramebuffers(1, &fb_id);
   swap_buffers();
   return 0;
}

int graw_renderer_create_fence(int client_fence_id)
{
   struct grend_fence *fence;

   fence = malloc(sizeof(struct grend_fence));
   if (!fence)
      return -1;

   fence->fence_id = client_fence_id;
   fence->syncobj = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
   list_addtail(&fence->fences, &fence_list);
   return 0;
}

void graw_renderer_check_fences(void)
{
   struct grend_fence *fence, *stor;
   uint32_t latest_id = 0;
   GLenum glret;

   if (!inited)
      return;

   LIST_FOR_EACH_ENTRY_SAFE(fence, stor, &fence_list, fences) {
      glret = glClientWaitSync(fence->syncobj, 0, 0);
      if (glret == GL_ALREADY_SIGNALED){
         latest_id = fence->fence_id;
         list_del(&fence->fences);
         free(fence);
      }
      /* don't bother checking any subsequent ones */
      if (glret == GL_TIMEOUT_EXPIRED) {
         break;
      }
   }

   if (latest_id == 0)
      return;
   graw_write_fence(latest_id);
}
