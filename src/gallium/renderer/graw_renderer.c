#include <GL/glew.h>
#include <GL/gl.h>

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
#include "graw_cursor.h"

#include "virgl_hw.h"
/* transfer boxes from the guest POV are in y = 0 = top orientation */
/* blit/copy operations from the guest POV are in y = 0 = top orientation */

/* since we are storing things in OpenGL FBOs we need to flip transfer operations by default */
static void grend_update_viewport_state(struct grend_context *ctx);
static void grend_update_scissor_state(struct grend_context *ctx);
static void grend_ctx_restart_queries(struct grend_context *ctx);

extern int graw_shader_use_explicit;
int localrender;
static int have_invert_mesa = 0;
static int draw_cursor = 0;

struct grend_fence {
   uint32_t fence_id;
   GLsync syncobj;
   struct list_head fences;
};

struct grend_nontimer_hw_query {
   struct list_head query_list;
   GLuint id;
   uint64_t result;
};

struct grend_query {
   struct list_head waiting_queries;
   struct list_head ctx_queries;

   struct list_head hw_queries;
   GLuint timer_query_id;
   GLuint type;
   GLuint gltype;
   int ctx_id;
   struct grend_resource *res;
   uint64_t current_total;
   boolean active_hw;
};

struct global_renderer_state {
   bool viewport_dirty;
   bool scissor_dirty;
   GLboolean blend_enabled;
   GLboolean depth_test_enabled;
   GLboolean alpha_test_enabled;
   GLboolean stencil_test_enabled;
   GLuint program_id;
   struct list_head fence_list;
   struct grend_context *current_ctx;

   struct list_head waiting_query_list;

   struct graw_cursor_info cursor_info;
};

static struct global_renderer_state grend_state;

struct grend_linked_shader_program {
  struct list_head head;
  GLuint id;

  struct grend_shader_state *vs;
  struct grend_shader_state *fs;

  GLuint num_samplers[PIPE_SHADER_TYPES];
  GLuint *samp_locs[PIPE_SHADER_TYPES];

  GLuint *const_locs[PIPE_SHADER_TYPES];

  GLuint *attrib_locs;
};

struct grend_shader_state {
   GLuint id;
   unsigned type;
   char *glsl_prog;
   int num_samplers;
   int num_consts;
   int num_inputs;
   struct pipe_stream_output_info so_info;
};

struct grend_buffer {
   struct grend_resource base;
};

struct grend_texture {
   struct grend_resource base;
   struct pipe_sampler_state state;
   GLenum cur_swizzle_a;
};

struct grend_surface {
   GLuint id;
   GLuint res_handle;
   GLuint format;
   GLuint val0, val1;
   struct grend_resource *texture;
};

struct grend_sampler {

};

struct grend_so_target {
   GLuint res_handle;
   unsigned buffer_offset;
   unsigned buffer_size;
   struct grend_resource *buffer;
};

struct grend_sampler_view {
   GLuint id;
   GLuint res_handle;
   GLuint format;
   GLuint val0, val1;
   GLuint swizzle_r:3;
   GLuint swizzle_g:3;
   GLuint swizzle_b:3;
   GLuint swizzle_a:3;
   GLuint gl_swizzle_a;
   GLuint cur_base, cur_max;
   struct grend_resource *texture;
};

struct grend_vertex_element {
   struct pipe_vertex_element base;
   GLenum type;
   GLboolean norm;
   GLuint nr_chan;
};

struct grend_vertex_element_array {
   unsigned count;
   struct grend_vertex_element elements[PIPE_MAX_ATTRIBS];
};

struct grend_constants {
   float *consts;
   uint32_t num_consts;
};

struct grend_shader_view {
   int num_views;
   struct grend_sampler_view *views[PIPE_MAX_SHADER_SAMPLER_VIEWS];
   uint32_t res_id[PIPE_MAX_SHADER_SAMPLER_VIEWS];
   uint32_t old_ids[PIPE_MAX_SHADER_SAMPLER_VIEWS];
};

struct grend_context {
   int ctx_id;
   GLuint vaoid;
   GLuint num_enabled_attribs;

   struct util_hash_table *object_hash;
   struct grend_vertex_element_array *ve;
   int num_vbos;
   struct pipe_vertex_buffer vbo[PIPE_MAX_ATTRIBS];
   uint32_t vbo_res_ids[PIPE_MAX_ATTRIBS];
   struct grend_shader_state *vs;
   struct grend_shader_state *fs;

   bool shader_dirty;
   struct grend_linked_shader_program *prog;

   struct grend_shader_view views[PIPE_SHADER_TYPES];

   struct pipe_index_buffer ib;
   uint32_t index_buffer_res_id;

   struct grend_constants consts[PIPE_SHADER_TYPES];
   bool const_dirty[PIPE_SHADER_TYPES];
   struct pipe_sampler_state *sampler_state[PIPE_SHADER_TYPES][PIPE_MAX_SAMPLERS];

   int num_sampler_states[PIPE_SHADER_TYPES];
   uint32_t fb_id;

   uint8_t stencil_refs[2];

   struct pipe_depth_stencil_alpha_state *dsa;
   boolean stencil_state_dirty;
   struct list_head programs;

   GLint view_cur_x, view_cur_y;
   GLsizei view_width, view_height;
   GLclampd view_near_val, view_far_val;
   GLboolean view_flipped;

   GLenum old_targets[PIPE_MAX_SAMPLERS];

   boolean scissor_state_dirty;
   boolean viewport_state_dirty;
   uint32_t fb_height;

   int nr_cbufs;
   struct grend_surface *zsurf;
   struct grend_surface *surf[8];
   
   struct pipe_scissor_state ss;

   struct pipe_blend_state blend_state;
   struct pipe_depth_stencil_alpha_state dsa_state;
   struct pipe_rasterizer_state rs_state;

   struct pipe_blend_color blend_color;

   int num_so_targets;
   struct grend_so_target *so_targets[16];

   struct list_head active_nontimer_query_list;
   boolean query_on_hw;
};

static struct grend_nontimer_hw_query *grend_create_hw_query(struct grend_query *query);

static struct grend_resource *frontbuffer;
static struct grend_format_table tex_conv_table[VIRGL_FORMAT_MAX];

void
grend_insert_format(struct grend_format_table *entry)
{
   tex_conv_table[entry->format] = *entry;
}

static boolean grend_is_timer_query(GLenum gltype)
{
	return gltype == GL_TIMESTAMP ||
               gltype == GL_TIME_ELAPSED;
}

void grend_use_program(GLuint program_id)
{
   if (grend_state.program_id != program_id) {
      glUseProgram(program_id);
      grend_state.program_id = program_id;
   }
}

void grend_blend_enable(GLboolean blend_enable)
{
   if (grend_state.blend_enabled != blend_enable) {
      grend_state.blend_enabled = blend_enable;
      if (blend_enable)
         glEnable(GL_BLEND);
      else
         glDisable(GL_BLEND);
   }
}

void grend_depth_test_enable(GLboolean depth_test_enable)
{
   if (grend_state.depth_test_enabled != depth_test_enable) {
      grend_state.depth_test_enabled = depth_test_enable;
      if (depth_test_enable)
         glEnable(GL_DEPTH_TEST);
      else
         glDisable(GL_DEPTH_TEST);
   }
}

static void grend_alpha_test_enable(GLboolean alpha_test_enable)
{
   if (grend_state.alpha_test_enabled != alpha_test_enable) {
      grend_state.alpha_test_enabled = alpha_test_enable;
      if (alpha_test_enable)
         glEnable(GL_ALPHA_TEST);
      else
         glDisable(GL_ALPHA_TEST);
   }
}
static void grend_stencil_test_enable(GLboolean stencil_test_enable)
{
   if (grend_state.stencil_test_enabled != stencil_test_enable) {
      grend_state.stencil_test_enabled = stencil_test_enable;
      if (stencil_test_enable)
         glEnable(GL_STENCIL_TEST);
      else
         glDisable(GL_STENCIL_TEST);
   }
}

static void set_stream_out_varyings(int prog_id, struct pipe_stream_output_info *vs_so)
{
   char *varyings[PIPE_MAX_SHADER_OUTPUTS];
   char tmp[64];
   int i;
   if (!vs_so->num_outputs)
      return;

   for (i = 0; i < vs_so->num_outputs; i++) {
      snprintf(tmp, 64, "tfout%d", i);

      varyings[i] = strdup(tmp);
   }

   glTransformFeedbackVaryings(prog_id, vs_so->num_outputs,
                               varyings, GL_INTERLEAVED_ATTRIBS_EXT);

   for (i = 0; i < vs_so->num_outputs; i++)
      if (varyings[i])
         free(varyings[i]);
}

static struct grend_linked_shader_program *add_shader_program(struct grend_context *ctx,
                                                              struct grend_shader_state *vs,
                                                              struct grend_shader_state *fs)
{
  struct grend_linked_shader_program *sprog = malloc(sizeof(struct grend_linked_shader_program));
  char name[16];
  int i;
  GLuint prog_id;
  GLenum ret;

  prog_id = glCreateProgram();
  glAttachShader(prog_id, vs->id);
  set_stream_out_varyings(prog_id, &vs->so_info);
  glAttachShader(prog_id, fs->id);
  ret = glGetError();
  glLinkProgram(prog_id);
  ret = glGetError();
  if (ret) {
     fprintf(stderr,"got error linking %d\n", ret);
  }

  sprog->vs = vs;
  sprog->fs = fs;
  sprog->id = prog_id;
  list_add(&sprog->head, &ctx->programs);

  if (vs->num_samplers) {
    sprog->samp_locs[PIPE_SHADER_VERTEX] = calloc(vs->num_samplers, sizeof(uint32_t));
    if (sprog->samp_locs[PIPE_SHADER_VERTEX]) {
      for (i = 0; i < vs->num_samplers; i++) {
	snprintf(name, 10, "vssamp%d", i);
	sprog->samp_locs[PIPE_SHADER_VERTEX][i] = glGetUniformLocation(prog_id, name);
      }
    }
  } else
    sprog->samp_locs[PIPE_SHADER_VERTEX] = NULL;
  sprog->num_samplers[PIPE_SHADER_VERTEX] = vs->num_samplers;
  if (fs->num_samplers) {
    sprog->samp_locs[PIPE_SHADER_FRAGMENT] = calloc(fs->num_samplers, sizeof(uint32_t));
    if (sprog->samp_locs[PIPE_SHADER_FRAGMENT]) {
      for (i = 0; i < fs->num_samplers; i++) {
	snprintf(name, 10, "fssamp%d", i);
	sprog->samp_locs[PIPE_SHADER_FRAGMENT][i] = glGetUniformLocation(prog_id, name);
      }
    }
  } else
    sprog->samp_locs[PIPE_SHADER_FRAGMENT] = NULL;
  sprog->num_samplers[PIPE_SHADER_FRAGMENT] = fs->num_samplers;
  
  if (vs->num_consts) {
    sprog->const_locs[PIPE_SHADER_VERTEX] = calloc(vs->num_consts, sizeof(uint32_t));
    if (sprog->const_locs[PIPE_SHADER_VERTEX]) {
      for (i = 0; i < vs->num_consts; i++) {
	snprintf(name, 16, "vsconst[%d]", i);
	sprog->const_locs[PIPE_SHADER_VERTEX][i] = glGetUniformLocation(prog_id, name);
      }
    }
  } else
    sprog->const_locs[PIPE_SHADER_VERTEX] = NULL;

  if (fs->num_consts) {
    sprog->const_locs[PIPE_SHADER_FRAGMENT] = calloc(fs->num_consts, sizeof(uint32_t));
    if (sprog->const_locs[PIPE_SHADER_FRAGMENT]) {
      for (i = 0; i < fs->num_consts; i++) {
	snprintf(name, 16, "fsconst[%d]", i);
	sprog->const_locs[PIPE_SHADER_FRAGMENT][i] = glGetUniformLocation(prog_id, name);
      }
    }
  } else
    sprog->const_locs[PIPE_SHADER_FRAGMENT] = NULL;

  if (vs->num_inputs) {
    sprog->attrib_locs = calloc(vs->num_inputs, sizeof(uint32_t));
    if (sprog->attrib_locs) {
      for (i = 0; i < vs->num_inputs; i++) {
	snprintf(name, 10, "in_%d", i);
	sprog->attrib_locs[i] = glGetAttribLocation(prog_id, name);
      }
    }
  } else
    sprog->attrib_locs = NULL;
   
  return sprog;
}

static struct grend_linked_shader_program *lookup_shader_program(struct grend_context *ctx,
                                                                 GLuint vs_id, GLuint fs_id)
{
  struct grend_linked_shader_program *ent;
  LIST_FOR_EACH_ENTRY(ent, &ctx->programs, head) {
     if (ent->vs->id == vs_id && ent->fs->id == fs_id)
        return ent;
  }
  return 0;
}

static void grend_free_programs(struct grend_context *ctx)
{
   struct grend_linked_shader_program *ent, *tmp;
   int i;
   LIST_FOR_EACH_ENTRY_SAFE(ent, tmp, &ctx->programs, head) {
      glDeleteProgram(ent->id);
      list_del(&ent->head);

      for (i = PIPE_SHADER_VERTEX; i <= PIPE_SHADER_FRAGMENT; i++) {
         free(ent->samp_locs[i]);
         free(ent->const_locs[i]);
      }
      free(ent->attrib_locs);
      free(ent);
   }
}  

static void grend_apply_sampler_state(struct grend_context *ctx,
                                      struct grend_resource *res,
                                      uint32_t shader_type,
                                      int id);

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
   surf->texture = graw_lookup_resource(res_handle, ctx->ctx_id);

   if (!surf->texture) {
      fprintf(stderr,"%s: failed to find resource for surface %d\n", __func__, res_handle);
      free(surf);
      return;
   }
   graw_object_insert(ctx->object_hash, surf, sizeof(*surf), handle, GRAW_SURFACE);
}

void grend_create_sampler_view(struct grend_context *ctx,
                               uint32_t handle,
                               uint32_t res_handle, uint32_t format,
                               uint32_t val0, uint32_t val1, uint32_t swizzle_packed)
{
   struct grend_sampler_view *view;

   view = CALLOC_STRUCT(grend_sampler_view);
   view->res_handle = res_handle;
   view->format = format;
   view->val0 = val0;
   view->val1 = val1;
   view->swizzle_r = swizzle_packed & 0x7;
   view->swizzle_g = (swizzle_packed >> 3) & 0x7;
   view->swizzle_b = (swizzle_packed >> 6) & 0x7;
   view->swizzle_a = (swizzle_packed >> 9) & 0x7;
   view->cur_base = 0;
   view->cur_max = 1000;

   view->texture = graw_lookup_resource(res_handle, ctx->ctx_id);

   if (!view->texture) {
      fprintf(stderr,"%s: failed to find resource %d\n", __func__, res_handle);
      FREE(view);
      return;
   }
   
   if (view->format != view->texture->base.format) {
      fprintf(stderr,"%d %d swizzles %d %d %d %d\n", view->format, view->texture->base.format, view->swizzle_r, view->swizzle_g, view->swizzle_b, view->swizzle_a);
      /* hack to make tfp work for now */
      view->gl_swizzle_a = GL_ONE;
   } else {
      if (util_format_has_alpha(format))
         view->gl_swizzle_a = GL_ALPHA;
      else
         view->gl_swizzle_a = GL_ONE;
   }
   graw_object_insert(ctx->object_hash, view, sizeof(*view), handle, GRAW_OBJECT_SAMPLER_VIEW);
}

static void grend_hw_emit_framebuffer_state(struct grend_context *ctx)
{
   int i;
   struct grend_resource *tex;
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

   if (ctx->zsurf) {
      GLenum attachment;
      tex = ctx->zsurf->texture;
      if (!tex)
         return;
      if (tex->base.format == PIPE_FORMAT_S8_UINT)
         attachment = GL_STENCIL_ATTACHMENT;
      else if (tex->base.format == PIPE_FORMAT_Z24X8_UNORM || tex->base.format == PIPE_FORMAT_Z32_UNORM)
         attachment = GL_DEPTH_ATTACHMENT;
      else
         attachment = GL_DEPTH_STENCIL_ATTACHMENT;
      glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, attachment,
                                tex->target, tex->id, ctx->zsurf->val0);
   }

   for (i = 0; i < ctx->nr_cbufs; i++) {
      if (!ctx->surf[i])
         continue;

      tex = ctx->surf[i]->texture;

      if (tex->target == GL_TEXTURE_CUBE_MAP) {
         glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, buffers[i],
                                   GL_TEXTURE_CUBE_MAP_POSITIVE_X + (ctx->surf[i]->val1 & 0xffff), tex->id, ctx->surf[i]->val0);
      } else
         glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, buffers[i],
                                   tex->target, tex->id, ctx->surf[i]->val0);

      if (i == 0 && tex->base.height0 != ctx->fb_height) {
         ctx->fb_height = tex->base.height0;
         ctx->scissor_state_dirty = TRUE;
         ctx->viewport_state_dirty = TRUE;
      }
   }
   glDrawBuffers(ctx->nr_cbufs, buffers);
}

void grend_set_framebuffer_state(struct grend_context *ctx,
                                 uint32_t nr_cbufs, uint32_t surf_handle[8],
                                 uint32_t zsurf_handle)
{
   struct grend_surface *surf, *zsurf;
   int i;

   if (zsurf_handle) {
      zsurf = graw_object_lookup(ctx->object_hash, zsurf_handle, GRAW_SURFACE);
      if (!zsurf) {
         fprintf(stderr,"%s: can't find surface for Z %d\n", __func__, zsurf_handle);
         return;
      }
      ctx->zsurf = zsurf;
   } else
      ctx->zsurf = NULL;

   ctx->nr_cbufs = nr_cbufs;
   for (i = 0; i < nr_cbufs; i++) {
      surf = graw_object_lookup(ctx->object_hash, surf_handle[i], GRAW_SURFACE);
      if (!surf) {
         fprintf(stderr,"%s: can't find surface for cbuf %d %d\n", __func__, i, surf_handle[i]);
         return;
      }
      ctx->surf[i] = surf;
   }

   grend_hw_emit_framebuffer_state(ctx);
}

static void grend_hw_emit_depth_range(struct grend_context *ctx)
{
   glDepthRange(ctx->view_near_val, ctx->view_far_val);
}

void grend_set_viewport_state(struct grend_context *ctx,
                              const struct pipe_viewport_state *state)
{
   /* convert back to glViewport */
   GLint x, y;
   GLsizei width, height;
   GLclampd near_val, far_val;
   GLboolean view_flipped = state->scale[1] < 0 ? GL_TRUE : GL_FALSE;
   
   width = state->scale[0] * 2.0f;
   height = abs(state->scale[1]) * 2.0f;
   x = state->translate[0] - state->scale[0];
   y = state->translate[1] - abs(state->scale[1]);

   near_val = state->translate[2] - state->scale[2];
   far_val = near_val + (state->scale[2] * 2.0f);

   if (ctx->view_cur_x != x ||
       ctx->view_cur_y != y ||
       ctx->view_width != width ||
       ctx->view_height != height ||
      ctx->view_flipped != view_flipped) {
      ctx->viewport_state_dirty = TRUE;
      ctx->view_cur_x = x;
      ctx->view_cur_y = y;
      ctx->view_width = width;
      ctx->view_height = height;
      ctx->view_flipped = view_flipped;
   }

   if (ctx->view_near_val != near_val ||
       ctx->view_far_val != far_val) {
      ctx->view_near_val = near_val;
      ctx->view_far_val = far_val;
      grend_hw_emit_depth_range(ctx);
   }
}

void grend_create_vertex_elements_state(struct grend_context *ctx,
                                        uint32_t handle,
                                        unsigned num_elements,
                                        const struct pipe_vertex_element *elements)
{
   struct grend_vertex_element_array *v = CALLOC_STRUCT(grend_vertex_element_array);
   const struct util_format_description *desc;
   GLenum type = GL_FALSE;
   int i;

   v->count = num_elements;
   for (i = 0; i < num_elements; i++) {
      memcpy(&v->elements[i].base, &elements[i], sizeof(struct pipe_vertex_element));

      desc = util_format_description(elements[i].src_format);

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
      v->elements[i].type = type;
      if (desc->channel[0].normalized)
         v->elements[i].norm = GL_TRUE;
      v->elements[i].nr_chan = desc->nr_channels;
   }

   graw_object_insert(ctx->object_hash, v, sizeof(struct grend_vertex_element), handle,
                      GRAW_OBJECT_VERTEX_ELEMENTS);
}

void grend_bind_vertex_elements_state(struct grend_context *ctx,
                                      uint32_t handle)
{
   struct grend_vertex_element_array *v;

   if (!handle) {
      ctx->ve = NULL;
      return;
   }
   v = graw_object_lookup(ctx->object_hash, handle, GRAW_OBJECT_VERTEX_ELEMENTS);
   if (!v) {
      fprintf(stderr, "illegal ve lookup %d\n", handle);
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

   consts = &ctx->consts[shader];
   ctx->const_dirty[shader] = TRUE;

   consts->consts = realloc(consts->consts, num_constant * sizeof(float));
   if (!consts->consts)
      return;

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
   if (res_handle) {
      if (ctx->index_buffer_res_id != res_handle) {
         res = graw_lookup_resource(res_handle, ctx->ctx_id);
         ctx->ib.buffer = &res->base;
         ctx->index_buffer_res_id = res_handle;
      }
   } else {
      ctx->ib.buffer = NULL;
      ctx->index_buffer_res_id = 0;
   }
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

   if (res_handle == 0) {
      ctx->vbo[index].buffer = NULL;
      ctx->vbo_res_ids[index] = 0;
   } else if (ctx->vbo_res_ids[index] != res_handle) {
      res = graw_lookup_resource(res_handle, ctx->ctx_id);
      ctx->vbo[index].buffer = &res->base;
      ctx->vbo_res_ids[index] = res_handle;
   }
}

void grend_set_num_vbo(struct grend_context *ctx,
                      int num_vbo)
{
   ctx->num_vbos = num_vbo;
}

void grend_set_single_sampler_view(struct grend_context *ctx,
                                   uint32_t shader_type,
                                   int index,
                                   uint32_t handle)
{
   struct grend_sampler_view *view = NULL;
   struct grend_texture *tex;
   if (handle) {
      view = graw_object_lookup(ctx->object_hash, handle, GRAW_OBJECT_SAMPLER_VIEW);
      if (!view) {
         fprintf(stderr,"%s: failed to find view %d\n", __func__, handle);
         return;
      }
      glBindTexture(view->texture->target, view->texture->id);
      if (view->texture->target != PIPE_BUFFER) {
         tex = (struct grend_texture *)view->texture;
         if (view->cur_base != (view->val1 & 0xff)) {
            glTexParameteri(view->texture->target, GL_TEXTURE_BASE_LEVEL, (view->val1) & 0xff);
            view->cur_base = view->val1 & 0xff;
         }
         if (view->cur_max != ((view->val1 >> 8) & 0xff)) {
            glTexParameteri(view->texture->target, GL_TEXTURE_MAX_LEVEL, (view->val1 >> 8) & 0xff);
            view->cur_max = (view->val1 >> 8) & 0xff;
         }
         if (tex->cur_swizzle_a != view->gl_swizzle_a) {
            glTexParameteri(view->texture->target, GL_TEXTURE_SWIZZLE_A, view->gl_swizzle_a);
            tex->cur_swizzle_a = view->gl_swizzle_a;
         }
      }
   }

   ctx->views[shader_type].views[index] = view;
}

void grend_set_num_sampler_views(struct grend_context *ctx,
                                    uint32_t shader_type,
                                    int num_sampler_views)
{
   ctx->views[shader_type].num_views = num_sampler_views;
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

   res = graw_lookup_resource(res_handle, ctx->ctx_id);
   if (res->target == GL_ELEMENT_ARRAY_BUFFER_ARB ||
       res->target == GL_ARRAY_BUFFER_ARB ||
       res->target == GL_TRANSFORM_FEEDBACK_BUFFER) {
      glBindBufferARB(res->target, res->id);
      glBufferSubData(res->target, box->x, box->width, data);
   } else {
      GLenum glformat, gltype;
      glBindTexture(res->target, res->id);
      glformat = tex_conv_table[res->base.format].glformat;
      gltype = tex_conv_table[res->base.format].gltype; 

      glTexSubImage2D(res->target, level, box->x, box->y, box->width, box->height,
                      glformat, gltype, data);
   }
}

void grend_destroy_shader(struct grend_shader_state *state)
{
   glDeleteShader(state->id);
}

void grend_create_vs(struct grend_context *ctx,
                     uint32_t handle,
                     const struct pipe_shader_state *vs)
{
   struct grend_shader_state *state = CALLOC_STRUCT(grend_shader_state);
   const GLchar *glsl_prog;
   GLenum ret;
   state->id = glCreateShader(GL_VERTEX_SHADER);
   glsl_prog = tgsi_convert(vs->tokens, 0, &state->num_samplers, &state->num_consts, &state->num_inputs, &vs->stream_output);
   if (glsl_prog) {
      glShaderSource(state->id, 1, &glsl_prog, NULL);
      ret = glGetError();
      glCompileShader(state->id);
      ret = glGetError();
      if (ret) {
         fprintf(stderr,"got error compiling %d\n", ret);
         fprintf(stderr,"VS:\n%s\n", glsl_prog);
      }
   } else
     fprintf(stderr,"failed to convert VS prog\n");

   /* copy over stream out info */
   state->so_info = vs->stream_output;

   graw_object_insert(ctx->object_hash, state, sizeof(*state), handle, GRAW_OBJECT_VS);

   return;
}

void grend_create_fs(struct grend_context *ctx,
                     uint32_t handle,
                     const struct pipe_shader_state *fs)
{
   struct grend_shader_state *state = CALLOC_STRUCT(grend_shader_state);
   GLchar *glsl_prog;
   GLenum ret;
   state->id = glCreateShader(GL_FRAGMENT_SHADER);
   glsl_prog = tgsi_convert(fs->tokens, 0, &state->num_samplers, &state->num_consts, &state->num_inputs, NULL);
   if (glsl_prog) {
      glShaderSource(state->id, 1, (const char **)&glsl_prog, NULL);
      ret = glGetError();
      glCompileShader(state->id);
      ret = glGetError();
      if (ret) {
         fprintf(stderr,"got error compiling %d\n", ret);
         fprintf(stderr,"VS:\n%s\n", glsl_prog);
      }
      //      fprintf(stderr,"FS:\n%s\n", glsl_prog);
   } else
     fprintf(stderr,"failed to convert FS prog\n");
   graw_object_insert(ctx->object_hash, state, sizeof(*state), handle, GRAW_OBJECT_FS);

   return;
}

void grend_bind_vs(struct grend_context *ctx,
                   uint32_t handle)
{
   struct grend_shader_state *state;

   state = graw_object_lookup(ctx->object_hash, handle, GRAW_OBJECT_VS);

   if (ctx->vs != state)
      ctx->shader_dirty = true;
   ctx->vs = state;
}

void grend_bind_fs(struct grend_context *ctx,
                   uint32_t handle)
{
   struct grend_shader_state *state;

   state = graw_object_lookup(ctx->object_hash, handle, GRAW_OBJECT_FS);

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

   if (ctx->stencil_state_dirty)
      grend_update_stencil_state(ctx);
   if (ctx->scissor_state_dirty || grend_state.scissor_dirty)
      grend_update_scissor_state(ctx);
   if (ctx->viewport_state_dirty || grend_state.viewport_dirty)
      grend_update_viewport_state(ctx);

   grend_use_program(0);

   if (buffers & PIPE_CLEAR_COLOR)
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

static void grend_update_scissor_state(struct grend_context *ctx)
{
   struct pipe_scissor_state *ss = &ctx->ss;
   
   glScissor(ss->minx, ctx->fb_height - ss->maxy, ss->maxx - ss->minx, ss->maxy - ss->miny);
   ctx->scissor_state_dirty = FALSE;
   grend_state.scissor_dirty = FALSE;
}

static void grend_update_viewport_state(struct grend_context *ctx)
{
   int i;
   glViewport(ctx->view_cur_x, ctx->fb_height - ctx->view_height - ctx->view_cur_y, ctx->view_width, ctx->view_height);

   for (i = 0; i < ctx->nr_cbufs; i++) {
      if (ctx->surf[i])
         ctx->surf[i]->texture->renderer_flipped = ctx->view_flipped;
   }
   if (ctx->zsurf)
      ctx->zsurf->texture->renderer_flipped = ctx->view_flipped;
   ctx->viewport_state_dirty = FALSE;
   grend_state.viewport_dirty = FALSE;
}

void grend_draw_vbo(struct grend_context *ctx,
                    const struct pipe_draw_info *info)
{
   int i;
   int sampler_id;
   bool new_program = FALSE;
   uint32_t shader_type;
   uint32_t num_enable;

   if (ctx->stencil_state_dirty)
      grend_update_stencil_state(ctx);
   if (ctx->scissor_state_dirty || grend_state.scissor_dirty)
      grend_update_scissor_state(ctx);

   if (ctx->viewport_state_dirty || grend_state.viewport_dirty)
      grend_update_viewport_state(ctx);

   if (ctx->shader_dirty) {
     struct grend_linked_shader_program *prog;

     if (!ctx->vs || !ctx->fs) {
        fprintf(stderr,"dropping rendering due to missing shaders\n");
        return;
     }
     prog = lookup_shader_program(ctx, ctx->vs->id, ctx->fs->id);
     if (!prog) {
        prog = add_shader_program(ctx, ctx->vs, ctx->fs);
     }
     if (ctx->prog != prog) {
       new_program = TRUE;
       ctx->prog = prog;
     }
   }

   glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, ctx->fb_id);
   
   grend_use_program(ctx->prog->id);

   glBindVertexArray(ctx->vaoid);

   for (shader_type = PIPE_SHADER_VERTEX; shader_type <= PIPE_SHADER_FRAGMENT; shader_type++) {
      if (ctx->prog->const_locs[shader_type] && (ctx->const_dirty[shader_type] || new_program)) {
         int mval;
         if (shader_type == PIPE_SHADER_VERTEX) {
            if (ctx->consts[shader_type].num_consts / 4 > ctx->vs->num_consts + 10) {
               fprintf(stderr,"possible memory corruption vs consts TODO %d %d\n", ctx->consts[0].num_consts, ctx->vs->num_consts);
               return;
            }
         } else if (shader_type == PIPE_SHADER_FRAGMENT) {
            if (ctx->consts[shader_type].num_consts / 4 > ctx->fs->num_consts + 10) {
               fprintf(stderr,"possible memory corruption fs consts TODO\n");
               return;
            }
         }
         for (i = 0; i < (ctx->consts[shader_type].num_consts + 3) / 4; i++) {
            if (ctx->prog->const_locs[shader_type][i] != -1 && ctx->consts[shader_type].consts)
               glUniform4fv(ctx->prog->const_locs[shader_type][i], 1, &ctx->consts[shader_type].consts[i * 4]);
         }
         ctx->const_dirty[shader_type] = FALSE;
      }
   }

   sampler_id = 0;
   for (shader_type = PIPE_SHADER_VERTEX; shader_type <= PIPE_SHADER_FRAGMENT; shader_type++) {
      for (i = 0; i < ctx->views[shader_type].num_views; i++) {
         struct grend_resource *texture = NULL;

         if (ctx->views[shader_type].views[i]) {
            texture = ctx->views[shader_type].views[i]->texture;
         }
         if (i >= ctx->prog->num_samplers[shader_type])
             break;
         if (ctx->prog->samp_locs[shader_type])
            glUniform1i(ctx->prog->samp_locs[shader_type][i], sampler_id);
         glActiveTexture(GL_TEXTURE0 + sampler_id);
         if (texture) {
            ctx->old_targets[sampler_id] = texture->target;
            glBindTexture(texture->target, texture->id);
            if (ctx->views[shader_type].old_ids[i] != texture->id) {
               grend_apply_sampler_state(ctx, texture, shader_type, sampler_id);
               ctx->views[shader_type].old_ids[i] = texture->id;
            }
            if (ctx->rs_state.point_quad_rasterization) {
               if (ctx->rs_state.sprite_coord_enable & (1 << i))
                  glTexEnvi(GL_POINT_SPRITE_ARB, GL_COORD_REPLACE_ARB, GL_TRUE);
               else
                  glTexEnvi(GL_POINT_SPRITE_ARB, GL_COORD_REPLACE_ARB, GL_FALSE);
            }
            sampler_id++;
         
         } else {
            if (ctx->old_targets[sampler_id]) {
               ctx->old_targets[sampler_id] = 0;
            }
         }
      }
   } 

   if (!ctx->ve) {
      fprintf(stderr,"illegal VE setup - skipping renderering\n");
      return;
   }

   num_enable = ctx->ve->count;
   for (i = 0; i < ctx->ve->count; i++) {
      struct grend_vertex_element *ve = &ctx->ve->elements[i];
      int vbo_index = ctx->ve->elements[i].base.vertex_buffer_index;
      struct grend_buffer *buf;
      GLint loc;

      if (i >= ctx->prog->vs->num_inputs) {
         /* XYZZY: debug this? */
         num_enable = ctx->prog->vs->num_inputs;
         break;
      }
      buf = (struct grend_buffer *)ctx->vbo[vbo_index].buffer;
      glBindBuffer(GL_ARRAY_BUFFER, buf->base.id);

      if (graw_shader_use_explicit) {
         loc = i;
      } else {
	if (ctx->prog->attrib_locs) {
	  loc = ctx->prog->attrib_locs[i];
	} else loc = -1;

	if (loc == -1) {
           fprintf(stderr,"cannot find loc %d %d %d\n", i, ctx->ve->count, ctx->prog->vs->num_inputs);
          fprintf(stderr,"shader probably didn't compile - skipping rendering\n");
          num_enable--;
          if (i == 0)
             return;
          continue;
        }
      }
      glVertexAttribPointer(loc, ve->nr_chan, ve->type, ve->norm, ctx->vbo[vbo_index].stride, (void *)(unsigned long)(ctx->ve->elements[i].base.src_offset + ctx->vbo[vbo_index].buffer_offset));
      glVertexAttribDivisorARB(i, ctx->ve->elements[i].base.instance_divisor);
   }


   if (ctx->num_enabled_attribs != num_enable) {
      if (num_enable > ctx->num_enabled_attribs) {
         for (i = ctx->num_enabled_attribs; i < num_enable; i++)
            glEnableVertexAttribArray(i);
      } else if (num_enable < ctx->num_enabled_attribs) {
         for (i = num_enable; i < ctx->num_enabled_attribs; i++)
            glDisableVertexAttribArray(i);
      }
      ctx->num_enabled_attribs = num_enable;
   }

   if (info->indexed) {
      struct grend_resource *res = (struct grend_resource *)ctx->ib.buffer;
      glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, res->id);
   }

   grend_ctx_restart_queries(ctx);

   if (ctx->num_so_targets) {
      glBeginTransformFeedback(info->mode);
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

   if (ctx->num_so_targets)
      glEndTransformFeedback();

   glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
   glBindVertexArray(0);
   
   glActiveTexture(GL_TEXTURE0);
      
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

static void grend_hw_emit_blend(struct grend_context *ctx)
{
   struct pipe_blend_state *state = &ctx->blend_state;

   if (state->logicop_enable) {
      glEnable(GL_COLOR_LOGIC_OP);
      glLogicOp(translate_logicop(state->logicop_func));
   } else
      glDisable(GL_COLOR_LOGIC_OP);

   if (state->independent_blend_enable) {
      /* ARB_draw_buffers_blend is required for this */
      int i;

      for (i = 0; i < PIPE_MAX_COLOR_BUFS; i++) {
         if (state->rt[i].blend_enable) {
            glBlendFuncSeparateiARB(i, translate_blend_factor(state->rt[i].rgb_src_factor),
                                 translate_blend_factor(state->rt[i].rgb_dst_factor),
                                 translate_blend_factor(state->rt[i].alpha_src_factor),
                                 translate_blend_factor(state->rt[i].alpha_dst_factor));
            glBlendEquationSeparateiARB(i, translate_blend_func(state->rt[0].rgb_func),
                                     translate_blend_func(state->rt[0].alpha_func));
            glEnableIndexedEXT(GL_BLEND, i);
         } else
            glDisableIndexedEXT(GL_BLEND, i);

         glColorMaskIndexedEXT(i, state->rt[i].colormask & PIPE_MASK_R ? GL_TRUE : GL_FALSE,
                            state->rt[i].colormask & PIPE_MASK_G ? GL_TRUE : GL_FALSE,
                            state->rt[i].colormask & PIPE_MASK_B ? GL_TRUE : GL_FALSE,
                            state->rt[i].colormask & PIPE_MASK_A ? GL_TRUE : GL_FALSE);
      }
   } else {
      if (state->rt[0].blend_enable) {
         glBlendFuncSeparate(translate_blend_factor(state->rt[0].rgb_src_factor),
                             translate_blend_factor(state->rt[0].rgb_dst_factor),
                             translate_blend_factor(state->rt[0].alpha_src_factor),
                             translate_blend_factor(state->rt[0].alpha_dst_factor));
         glBlendEquationSeparate(translate_blend_func(state->rt[0].rgb_func),
                                 translate_blend_func(state->rt[0].alpha_func));
         grend_blend_enable(GL_TRUE);
      } 
      else
         grend_blend_enable(GL_FALSE);

      glColorMask(state->rt[0].colormask & PIPE_MASK_R ? GL_TRUE : GL_FALSE,
                  state->rt[0].colormask & PIPE_MASK_G ? GL_TRUE : GL_FALSE,
                  state->rt[0].colormask & PIPE_MASK_B ? GL_TRUE : GL_FALSE,
                  state->rt[0].colormask & PIPE_MASK_A ? GL_TRUE : GL_FALSE);
   }

}

void grend_object_bind_blend(struct grend_context *ctx,
                             uint32_t handle)
{
   struct pipe_blend_state *state;

   if (handle == 0) {
      memset(&ctx->blend_state, 0, sizeof(ctx->blend_state));
      grend_blend_enable(GL_FALSE);
      return;
   }
   state = graw_object_lookup(ctx->object_hash, handle, GRAW_OBJECT_BLEND);
   if (!state) {
      fprintf(stderr,"%s: got illegal handle %d\n", __func__, handle);
      return;
   }

   ctx->blend_state = *state;

   grend_hw_emit_blend(ctx);
}

static void grend_hw_emit_dsa(struct grend_context *ctx)
{
   struct pipe_depth_stencil_alpha_state *state = &ctx->dsa_state;

   if (state->depth.enabled) {
      grend_depth_test_enable(GL_TRUE);
      glDepthFunc(GL_NEVER + state->depth.func);
      if (state->depth.writemask)
         glDepthMask(GL_TRUE);
      else
         glDepthMask(GL_FALSE);
   } else
      grend_depth_test_enable(GL_FALSE);
 
   if (state->alpha.enabled) {
      grend_alpha_test_enable(GL_TRUE);
      glAlphaFunc(GL_NEVER + state->alpha.func, state->alpha.ref_value);
   } else
      grend_alpha_test_enable(GL_FALSE);


}
void grend_object_bind_dsa(struct grend_context *ctx,
                           uint32_t handle)
{
   struct pipe_depth_stencil_alpha_state *state;

   if (handle == 0) {
      memset(&ctx->dsa_state, 0, sizeof(ctx->dsa_state));
      ctx->dsa = NULL;
      ctx->stencil_state_dirty = TRUE;
      grend_hw_emit_dsa(ctx);
      return;
   }

   state = graw_object_lookup(ctx->object_hash, handle, GRAW_OBJECT_DSA);
   if (!state) {
      fprintf(stderr,"failed to find DSA state for handle %d\n", handle);
      return;
   }

   if (ctx->dsa != state)
      ctx->stencil_state_dirty = TRUE;
   ctx->dsa_state = *state;
   ctx->dsa = state;
   grend_hw_emit_dsa(ctx);
}
 
void grend_update_stencil_state(struct grend_context *ctx)
{
   struct pipe_depth_stencil_alpha_state *state = ctx->dsa;
   int i;
   if (!state)
      return;

   if (!state->stencil[1].enabled) {
      if (state->stencil[0].enabled) {
         grend_stencil_test_enable(GL_TRUE);

         glStencilOp(translate_stencil_op(state->stencil[0].fail_op), 
                     translate_stencil_op(state->stencil[0].zpass_op),
                     translate_stencil_op(state->stencil[0].zfail_op));
         glStencilFunc(GL_NEVER + state->stencil[0].func,
                       ctx->stencil_refs[0],
                       state->stencil[0].valuemask);
         glStencilMask(state->stencil[0].writemask);
      } else
         grend_stencil_test_enable(GL_FALSE);
   } else {
      grend_stencil_test_enable(GL_TRUE);

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

static void grend_hw_emit_rs(struct grend_context *ctx)
{
   struct pipe_rasterizer_state *state = &ctx->rs_state;

#if 0
   if (state->depth_clip) {
      glEnable(GL_DEPTH_CLAMP);
   } else {
      glDisable(GL_DEPTH_CLAMP);
   }
#endif
   if (state->point_size)
      glPointSize(state->point_size);

   if (state->rasterizer_discard)
      glEnable(GL_RASTERIZER_DISCARD);
   else
      glDisable(GL_RASTERIZER_DISCARD);

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

}
void grend_object_bind_rasterizer(struct grend_context *ctx,
                                  uint32_t handle)
{
   struct pipe_rasterizer_state *state;

   if (handle == 0) {
      memset(&ctx->rs_state, 0, sizeof(ctx->rs_state));
      return;
   }

   state = graw_object_lookup(ctx->object_hash, handle, GRAW_OBJECT_RASTERIZER);
   
   if (!state) {
      fprintf(stderr,"%s: cannot find object %d\n", __func__, handle);
      return;
   }

   ctx->rs_state = *state;
   grend_hw_emit_rs(ctx);
}

static GLuint convert_wrap(int wrap)
{
   switch(wrap){
   case PIPE_TEX_WRAP_REPEAT: return GL_REPEAT;
   case PIPE_TEX_WRAP_CLAMP: return GL_CLAMP;

   case PIPE_TEX_WRAP_CLAMP_TO_EDGE: return GL_CLAMP_TO_EDGE;
   case PIPE_TEX_WRAP_CLAMP_TO_BORDER: return GL_CLAMP_TO_BORDER;

   case PIPE_TEX_WRAP_MIRROR_REPEAT: return GL_MIRRORED_REPEAT;
   case PIPE_TEX_WRAP_MIRROR_CLAMP_TO_EDGE: return GL_MIRROR_CLAMP_TO_EDGE_EXT;
   case PIPE_TEX_WRAP_MIRROR_CLAMP_TO_BORDER: return GL_MIRROR_CLAMP_TO_BORDER_EXT;
   default:
      assert(0);
      return -1;
   }
} 

void grend_bind_sampler_states(struct grend_context *ctx,
                               uint32_t shader_type,
                               uint32_t num_states,
                               uint32_t *handles)
{
   int i;
   struct pipe_sampler_state *state;

   ctx->num_sampler_states[shader_type] = num_states;

   for (i = 0; i < num_states; i++) {
      if (handles[i] == 0)
         state = NULL;
      else
         state = graw_object_lookup(ctx->object_hash, handles[i], GRAW_OBJECT_SAMPLER_STATE);
      
      ctx->sampler_state[shader_type][i] = state;
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

static void grend_apply_sampler_state(struct grend_context *ctx, 
                                      struct grend_resource *res,
                                      uint32_t shader_type,
                                      int id)
{
   struct grend_texture *tex = (struct grend_texture *)res;
   struct pipe_sampler_state *state = ctx->sampler_state[shader_type][id];
   bool set_all = FALSE;
   GLenum target = tex->base.target;

   if (!state) {
      fprintf(stderr, "cannot find sampler state for %d %d\n", shader_type, id);
      return;
   }
   if (tex->state.max_lod == -1)
      set_all = TRUE;

   if (tex->state.wrap_s != state->wrap_s || set_all)
      glTexParameteri(target, GL_TEXTURE_WRAP_S, convert_wrap(state->wrap_s));
   if (tex->state.wrap_t != state->wrap_t || set_all)
      glTexParameteri(target, GL_TEXTURE_WRAP_T, convert_wrap(state->wrap_t));
   if (tex->state.wrap_r != state->wrap_r || set_all)
      glTexParameteri(target, GL_TEXTURE_WRAP_R, convert_wrap(state->wrap_r));
   if (tex->state.min_img_filter != state->min_img_filter ||
       tex->state.min_mip_filter != state->min_mip_filter || set_all)
      glTexParameterf(target, GL_TEXTURE_MIN_FILTER, convert_min_filter(state->min_img_filter, state->min_mip_filter));
   if (tex->state.min_img_filter != state->mag_img_filter || set_all)
      glTexParameterf(target, GL_TEXTURE_MAG_FILTER, convert_mag_filter(state->mag_img_filter));
   if (res->target != GL_TEXTURE_RECTANGLE) {
      if (tex->state.min_lod != state->min_lod || set_all)
         glTexParameterf(target, GL_TEXTURE_MIN_LOD, state->min_lod);
      if (tex->state.max_lod != state->max_lod || set_all)
         glTexParameterf(target, GL_TEXTURE_MAX_LOD, state->max_lod);
      if (tex->state.lod_bias != state->lod_bias || set_all)
         glTexParameterf(target, GL_TEXTURE_LOD_BIAS, state->lod_bias);
   }

   tex->state = *state;
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

   case PIPE_TEXTURE_1D_ARRAY:
      return GL_TEXTURE_1D_ARRAY;
   case PIPE_TEXTURE_2D_ARRAY:
      return GL_TEXTURE_2D_ARRAY;
   case PIPE_TEXTURE_CUBE_ARRAY:
      return GL_TEXTURE_CUBE_MAP_ARRAY;
   case PIPE_BUFFER:
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
      graw_object_init_resource_hash();
   }
   
   vrend_build_format_list();
   grend_state.viewport_dirty = grend_state.scissor_dirty = TRUE;
   grend_state.program_id = (GLuint)-1;
   list_inithead(&grend_state.fence_list);
   list_inithead(&grend_state.waiting_query_list);

   graw_cursor_init(&grend_state.cursor_info);
   /* create 0 context */
   graw_renderer_context_create_internal(0);
}

void
graw_renderer_fini(void)
{
   if (!inited)
      return;

   graw_object_fini_resource_hash();
   inited = 0;
}

bool grend_destroy_context(struct grend_context *ctx)
{
   bool switch_0 = (ctx == grend_state.current_ctx);
   int i;

   if (ctx->fb_id)
      glDeleteFramebuffers(1, &ctx->fb_id);

   grend_free_programs(ctx);

   glBindVertexArray(ctx->vaoid);

   for (i = 0; i < ctx->num_enabled_attribs; i++)
      glDisableVertexAttribArray(i);

   glDeleteVertexArrays(1, &ctx->vaoid);

   graw_fini_ctx_hash(ctx->object_hash);
   FREE(ctx);
   return switch_0;
}

struct grend_context *grend_create_context(int id)
{
   struct grend_context *grctx = CALLOC_STRUCT(grend_context);
   int i;

   grctx->ctx_id = id;
   list_inithead(&grctx->programs);
   list_inithead(&grctx->active_nontimer_query_list);
   glGenVertexArrays(1, &grctx->vaoid);

   grctx->object_hash = graw_init_ctx_hash();

   return grctx;
}


void graw_renderer_resource_create(uint32_t handle, enum pipe_texture_target target, uint32_t format, uint32_t bind, uint32_t width, uint32_t height, uint32_t depth, uint32_t array_size, uint32_t last_level, uint32_t nr_samples)
{
   struct grend_resource *gr = (struct grend_resource *)CALLOC_STRUCT(grend_texture);
   int level;

   gr->base.width0 = width;
   gr->base.height0 = height;
   gr->base.format = format;
   gr->base.target = target;
   if (bind == PIPE_BIND_CUSTOM) {
      /* custom shuold only be for buffers */
      gr->ptr = malloc(width);
   } else if (bind == PIPE_BIND_INDEX_BUFFER) {
      gr->target = GL_ELEMENT_ARRAY_BUFFER_ARB;
      glGenBuffersARB(1, &gr->id);
      glBindBufferARB(gr->target, gr->id);
      glBufferData(gr->target, width, NULL, GL_STREAM_DRAW);
   } else if (bind == PIPE_BIND_STREAM_OUTPUT) {
      gr->target = GL_TRANSFORM_FEEDBACK_BUFFER;
      glGenBuffersARB(1, &gr->id);
      glBindBuffer(gr->target, gr->id);
      glBufferData(gr->target, width, NULL, GL_STREAM_DRAW);
   } else if (target == PIPE_BUFFER) {
      gr->target = GL_ARRAY_BUFFER_ARB;
      glGenBuffersARB(1, &gr->id);
      glBindBufferARB(gr->target, gr->id);
      glBufferData(gr->target, width, NULL, GL_STREAM_DRAW);
   } else {
      struct grend_texture *gt = (struct grend_texture *)gr;
      GLenum internalformat, glformat, gltype;
      gr->target = tgsitargettogltarget(target);
      glGenTextures(1, &gr->id);
      glBindTexture(gr->target, gr->id);

      internalformat = tex_conv_table[format].internalformat;
      glformat = tex_conv_table[format].glformat;
      gltype = tex_conv_table[format].gltype;
      if (internalformat == 0) {
         fprintf(stderr,"unknown format is %d\n", format);
         return 0;
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
      } else if (gr->target == GL_TEXTURE_3D || gr->target == GL_TEXTURE_2D_ARRAY) {
         glTexImage3D(gr->target, 0, internalformat, width, height, gr->target == GL_TEXTURE_2D_ARRAY ? array_size : depth, 0,
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
            glTexImage2D(gr->target, level, internalformat, mwidth, gr->target == GL_TEXTURE_1D_ARRAY ? array_size : mheight, 0, glformat,
                         gltype, NULL);
         }
      }

      gt->state.max_lod = -1;
   }

   graw_insert_resource(gr, sizeof(*gr), handle);
}

void graw_renderer_resource_unref(uint32_t res_handle)
{
   struct grend_resource *res;

   res = graw_lookup_resource(res_handle, 0);

   if (!res)
      return;

   if (res->readback_fb_id)
      glDeleteFramebuffers(1, &res->readback_fb_id);

   if (res->ptr)
      free(res->ptr);
   if (res->target == GL_ELEMENT_ARRAY_BUFFER_ARB ||
       res->target == GL_ARRAY_BUFFER_ARB ||
       res->target == GL_TRANSFORM_FEEDBACK_BUFFER) {
      glDeleteBuffers(1, &res->id);
   } else
      glDeleteTextures(1, &res->id);

   graw_destroy_resource(res_handle);
}

static int use_sub_data = 0;
static void iov_element_upload(void *cookie, uint32_t doff, void *src, int len)
{
   struct pipe_box *d_box = cookie;
   glBufferSubData(GL_ELEMENT_ARRAY_BUFFER_ARB, d_box->x + doff, len, src);
}

static void iov_vertex_upload(void *cookie, uint32_t doff, void *src, int len)
{
   struct pipe_box *d_box = cookie;
   glBufferSubData(GL_ARRAY_BUFFER_ARB, d_box->x + doff, len, src);
}

static void copy_transfer_data(struct pipe_resource *res,
                               struct graw_iovec *iov,
                               unsigned int num_iovs,
                               void *data,
                               uint32_t src_stride,
                               struct pipe_box *box,
                               uint64_t offset)
{
   int elsize = util_format_get_blocksize(res->format);
   GLuint size = graw_iov_size(iov, num_iovs);
   GLuint send_size = box->width * box->height * box->depth * elsize;
   uint32_t myoffset = offset;

   int h;
   if (send_size == size)
      graw_iov_to_buf(iov, num_iovs, offset, data, size - offset);
   else {
      for (h = 0; h < box->height; h++) {
         void *ptr = data + (h * box->width * elsize);
         graw_iov_to_buf(iov, num_iovs, myoffset, ptr, box->width * elsize);
         myoffset += src_stride;
      }
   }
}

void graw_renderer_transfer_write_iov(uint32_t res_handle,
                                      uint32_t ctx_id,
                                      int level,
                                      uint32_t src_stride,
                                      struct pipe_box *dst_box,
                                      uint64_t offset,
                                      struct graw_iovec *iov,
                                      unsigned int num_iovs)
{
   struct grend_resource *res;
   void *data;
   int need_temp;

   res = graw_lookup_resource(res_handle, ctx_id);
   if (res == NULL) {
      fprintf(stderr,"transfer for non-existant res %d\n", res_handle);
      return;
   }
   if (res->target == GL_TRANSFORM_FEEDBACK_BUFFER ||
       res->target == GL_ELEMENT_ARRAY_BUFFER_ARB ||
       res->target == GL_ARRAY_BUFFER_ARB) {
      glBindBufferARB(res->target, res->id);

      if (use_sub_data == 1) {
         graw_iov_to_buf_cb(iov, num_iovs, offset, dst_box->width, &iov_element_upload, (void *)dst_box);
      } else {
         data = glMapBufferRange(res->target, dst_box->x, dst_box->width, GL_MAP_INVALIDATE_RANGE_BIT | GL_MAP_UNSYNCHRONIZED_BIT | GL_MAP_WRITE_BIT);
         if (data == NULL) {
            fprintf(stderr,"map failed for element buffer\n");
            graw_iov_to_buf_cb(iov, num_iovs, offset, dst_box->width, &iov_element_upload, (void *)dst_box);
         } else {
            graw_iov_to_buf(iov, num_iovs, offset, data, dst_box->width);
            glUnmapBuffer(res->target);
         }
      }
   } else {
      GLenum glformat;
      GLenum gltype;
      int need_temp = 0;
      int elsize = util_format_get_blocksize(res->base.format);
      int x = 0, y = 0;
      grend_use_program(0);

      if (num_iovs > 1) {
         need_temp = 1;
      }

      if (need_temp) {
         GLuint size = graw_iov_size(iov, num_iovs);
         GLuint send_size = dst_box->width * dst_box->height * dst_box->depth * util_format_get_blocksize(res->base.format);
//         data = malloc(size - offset);
         data = malloc(send_size);
         fprintf(stderr,"alloc size for transfer %d %d %d %d\n", size, offset, size - offset, send_size);
         copy_transfer_data(&res->base, iov, num_iovs, data, src_stride,
                            dst_box, offset);
      } else
         data = iov[0].iov_base + offset;

      if (src_stride && !need_temp) {
         glPixelStorei(GL_UNPACK_ROW_LENGTH, src_stride / elsize);
      }

      switch (elsize) {
      case 1:
         glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
         break;
      case 2:
         glPixelStorei(GL_UNPACK_ALIGNMENT, 2);
         break;
      case 4:
      default:
         glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
         break;
      }

      glformat = tex_conv_table[res->base.format].glformat;
      gltype = tex_conv_table[res->base.format].gltype; 

      if (res->is_front) {
         if (0) {// may be need later !res->is_front) {
            if (res->readback_fb_id == 0 || res->readback_fb_level != level) {
               GLuint fb_id;
               if (res->readback_fb_id)
                  glDeleteFramebuffers(1, &res->readback_fb_id);
            
               glGenFramebuffers(1, &fb_id);
               glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, fb_id);
               glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT,
                                         res->target, res->id, level);
               res->readback_fb_id = fb_id;
               res->readback_fb_level = level;
            } else {
               glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, res->readback_fb_id);
            }
            glDrawBuffer(GL_COLOR_ATTACHMENT0_EXT);
         } else {
            glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, 0);
            glDrawBuffer(GL_BACK);
         }
         grend_blend_enable(GL_FALSE);
         grend_depth_test_enable(GL_FALSE);
         grend_alpha_test_enable(GL_FALSE);
         grend_stencil_test_enable(GL_FALSE);
         glPixelZoom(1.0f, 1.0f);
         glWindowPos2i(dst_box->x, dst_box->y);
         glDrawPixels(dst_box->width, dst_box->height, glformat, gltype,
                      data);
      } else {
         uint32_t h = u_minify(res->base.height0, level);
         glBindTexture(res->target, res->id);

         glformat = tex_conv_table[res->base.format].glformat;
         gltype = tex_conv_table[res->base.format].gltype; 
         if (glformat == 0) {
            glformat = GL_BGRA;
            gltype = GL_UNSIGNED_BYTE;
         }

         x = dst_box->x;


         if (res->renderer_flipped)
            y = h - dst_box->y - dst_box->height;
         else
            y = dst_box->y;

         if (res->target == GL_TEXTURE_CUBE_MAP) {
            GLenum ctarget = GL_TEXTURE_CUBE_MAP_POSITIVE_X + dst_box->z;

            glTexSubImage2D(ctarget, level, x, y, dst_box->width, dst_box->height,
                            glformat, gltype, data);
         } else if (res->target == GL_TEXTURE_3D || res->target == GL_TEXTURE_2D_ARRAY) {
            glTexSubImage3D(res->target, level, x, y, dst_box->z,
                            dst_box->width, dst_box->height, dst_box->depth,
                            glformat, gltype, data);
         } else if (res->target == GL_TEXTURE_1D) {
            glTexSubImage1D(res->target, level, dst_box->x, dst_box->width,
                            glformat, gltype, data);
         } else {
            glTexSubImage2D(res->target, level, x, res->target == GL_TEXTURE_1D_ARRAY ? dst_box->z : y,
                            dst_box->width, dst_box->height,
                            glformat, gltype, data);
         }
      }
      if (src_stride && !need_temp)
         glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
      glPixelStorei(GL_UNPACK_ALIGNMENT, 4);

      if (need_temp)
         free(data);
   }

}

void graw_renderer_transfer_send_iov(uint32_t res_handle, uint32_t ctx_id,
                                     uint32_t level, uint32_t dst_stride,
                                     struct pipe_box *box,
                                     uint64_t offset, struct graw_iovec *iov,
                                     int num_iovs)
{
   struct grend_resource *res;
   void *myptr = iov[0].iov_base + offset;
   int need_temp = 0;

   res = graw_lookup_resource(res_handle, ctx_id);
   if (!res) {
      fprintf(stderr,"transfer for non-existant res %d\n", res_handle);
      return;
   }

   if (res->target == 0 && res->ptr) {
      uint32_t send_size = box->width * util_format_get_blocksize(res->base.format);      
      graw_transfer_write_return(res->ptr + box->x, send_size, offset, iov, num_iovs);
   } else if (res->target == GL_ELEMENT_ARRAY_BUFFER_ARB ||
       res->target == GL_ARRAY_BUFFER_ARB ||
       res->target == GL_TRANSFORM_FEEDBACK_BUFFER) {
      uint32_t send_size = box->width * util_format_get_blocksize(res->base.format);      
      void *data;
      glBindBufferARB(res->target, res->id);
      data = glMapBuffer(res->target, GL_READ_ONLY);
      if (!data)
         fprintf(stderr,"unable to open buffer for reading %d\n", res->target);
      else
         graw_transfer_write_return(data + box->x, send_size, offset, iov, num_iovs);
      glUnmapBuffer(res->target);
   } else {
      uint32_t h = u_minify(res->base.height0, level);
      GLenum format, type;
      GLuint fb_id;
      GLint  y1;
      uint32_t send_size = 0;
      void *data;
      boolean actually_invert, separate_invert = FALSE;

      grend_use_program(0);

      /* if we are asked to invert and reading from a front then don't */
      if (res->is_front)
         actually_invert = FALSE;
      else
         actually_invert = res->renderer_flipped;

      if (actually_invert && !have_invert_mesa)
         separate_invert = TRUE;

      if (num_iovs > 1 || separate_invert || dst_stride)
         need_temp = 1;

      if (need_temp) {
         send_size = box->width * box->height * box->depth * util_format_get_blocksize(res->base.format);
         data = malloc(send_size);
         if (!data)
            fprintf(stderr,"malloc failed %d\n", send_size);
      } else
         data = myptr;
//      fprintf(stderr,"TEXTURE TRANSFER %d %d %d %d %d, temp:%d\n", res_handle, res->readback_fb_id, box->width, box->height, level, need_temp);

      if (!res->is_front) {
         if (res->readback_fb_id == 0 || res->readback_fb_level != level) {
            if (res->readback_fb_id)
               glDeleteFramebuffers(1, &res->readback_fb_id);
            
            glGenFramebuffers(1, &fb_id);
            glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, fb_id);
            if (res->target == GL_TEXTURE_1D)
               glFramebufferTexture1DEXT(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT,
                                         res->target, res->id, level);
            else
               glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT,
                                         res->target, res->id, level);
            res->readback_fb_id = fb_id;
            res->readback_fb_level = level;
         } else {
            glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, res->readback_fb_id);
         }
         if (res->renderer_flipped)
            y1 = h - box->y - box->height;
         else
            y1 = box->y;

         if (have_invert_mesa && actually_invert)
            glPixelStorei(GL_PACK_INVERT_MESA, 1);
         glReadBuffer(GL_COLOR_ATTACHMENT0_EXT);      
      } else {
         glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, 0);
         y1 = box->y;
         glReadBuffer(GL_BACK);
      }
            
      format = tex_conv_table[res->base.format].glformat;
      type = tex_conv_table[res->base.format].gltype; 

      glReadPixels(box->x, y1, box->width, box->height, format, type, data);

      if (have_invert_mesa && actually_invert)
         glPixelStorei(GL_PACK_INVERT_MESA, 0);
      if (need_temp) {
         graw_transfer_write_tex_return(&res->base, box, level, dst_stride, offset, iov, num_iovs, data, send_size, separate_invert);
         free(data);
      }
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

static void grend_hw_emit_blend_color(struct grend_context *ctx)
{
   struct pipe_blend_color *color = &ctx->blend_color;
   glBlendColor(color->color[0], color->color[1], color->color[2],
                color->color[3]);
}

void grend_set_blend_color(struct grend_context *ctx,
                           struct pipe_blend_color *color)
{
   ctx->blend_color = *color;
   grend_hw_emit_blend_color(ctx);
}

void grend_set_scissor_state(struct grend_context *ctx,
                             struct pipe_scissor_state *ss)
{
   ctx->ss = *ss;
   ctx->scissor_state_dirty = TRUE;
}

void grend_set_polygon_stipple(struct grend_context *ctx,
                               struct pipe_poly_stipple *ps)
{

}

void grend_set_clip_state(struct grend_context *ctx, struct pipe_clip_state *ucp)
{

}

void grend_set_sample_mask(struct grend_context *ctx, unsigned sample_mask)
{

}


static void grend_hw_emit_streamout_targets(struct grend_context *ctx)
{
   int i;

   for (i = 0; i < ctx->num_so_targets; i++) {
      glBindBufferBase(GL_TRANSFORM_FEEDBACK_BUFFER, i, ctx->so_targets[i]->buffer->id);
   }
}

void grend_set_streamout_targets(struct grend_context *ctx,
                                 uint32_t append_bitmask,
                                 uint32_t num_targets,
                                 uint32_t *handles)
{
   struct grend_so_target *target;
   int i;

   ctx->num_so_targets = num_targets;
   for (i = 0; i < num_targets; i++) {
      target = graw_object_lookup(ctx->object_hash, handles[i], GRAW_STREAMOUT_TARGET);
      if (!target) {
         fprintf(stderr,"can't find so target %d\n", i);
         return;
      }
      ctx->so_targets[i] = target;
   }
   grend_hw_emit_streamout_targets(ctx);
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
   src_res = graw_lookup_resource(src_handle, ctx->ctx_id);
   dst_res = graw_lookup_resource(dst_handle, ctx->ctx_id);

   if (!src_res || !dst_res) {
      fprintf(stderr,"illegal handle in copy region %d %d\n", src_handle, dst_handle);
      return;
   }

   glGenFramebuffers(2, fb_ids);
   glBindFramebuffer(GL_FRAMEBUFFER_EXT, fb_ids[0]);
   glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT,
                             src_res->target, src_res->id, src_level);
      
   if (!dst_res->is_front) {
      glBindFramebuffer(GL_FRAMEBUFFER_EXT, fb_ids[1]);
      glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT,
                                dst_res->target, dst_res->id, dst_level);
      glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fb_ids[1]);
   } else
      glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);

   glBindFramebuffer(GL_READ_FRAMEBUFFER, fb_ids[0]);

   glmask = GL_COLOR_BUFFER_BIT;
   glDisable(GL_SCISSOR_TEST);

   if (!src_res->is_front && !src_res->renderer_flipped) {
      sy1 = src_box->y;
      sy2 = src_box->y + src_box->height;
   } else {
      sy1 = src_res->base.height0 - src_box->y - src_box->height;
      sy2 = src_res->base.height0 - src_box->y;
   }

   if (!dst_res->is_front && !dst_res->renderer_flipped) {
      dy1 = dsty;
      dy2 = dsty + src_box->height;
   } else {
      dy1 = dst_res->base.height0 - dsty - src_box->height;
      dy2 = dst_res->base.height0 - dsty;
   }

   glBlitFramebuffer(src_box->x, sy1,
                     src_box->x + src_box->width,
                     sy2,
                     dstx, dy1,
                     dstx + src_box->width,
                     dy2,
                     glmask, GL_NEAREST);

   glDeleteFramebuffers(2, fb_ids);
}

static void graw_renderer_blit_int(uint32_t ctx_id,
                                   uint32_t dst_handle, uint32_t src_handle,
                                   const struct pipe_blit_info *info)
{
   struct grend_resource *src_res, *dst_res;
   GLuint fb_ids[2];
   GLbitfield glmask = 0;
   int src_y1, src_y2, dst_y1, dst_y2;

   src_res = graw_lookup_resource(src_handle, ctx_id);
   dst_res = graw_lookup_resource(dst_handle, ctx_id);

   if (!src_res || !dst_res) {
      fprintf(stderr,"illegal handle in blit %d %d\n", src_handle, dst_handle);
      return;
   }
   glGenFramebuffers(2, fb_ids);
   glBindFramebuffer(GL_FRAMEBUFFER_EXT, fb_ids[0]);

   if (src_res->target == GL_TEXTURE_CUBE_MAP) {
      glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT,
                                GL_TEXTURE_CUBE_MAP_POSITIVE_X + info->src.level, src_res->id, 0);
   } else
      glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT,
                                src_res->target, src_res->id, info->src.level);


   if (!dst_res->is_front) {
      glBindFramebuffer(GL_FRAMEBUFFER_EXT, fb_ids[1]);
      glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT,
                                dst_res->target, dst_res->id, info->dst.level);
      
      glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fb_ids[1]);
   } else
      glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);

   glBindFramebuffer(GL_READ_FRAMEBUFFER, fb_ids[0]);

   if (info->mask & PIPE_MASK_Z)
      glmask |= GL_DEPTH_BUFFER_BIT;
   if (info->mask & PIPE_MASK_S)
      glmask |= GL_STENCIL_BUFFER_BIT;
   if (info->mask & PIPE_MASK_RGBA)
      glmask |= GL_COLOR_BUFFER_BIT;

   if (!dst_res->is_front && !dst_res->renderer_flipped) {
      dst_y1 = info->dst.box.y + info->dst.box.height;
      dst_y2 = info->dst.box.y;
   } else {
      dst_y1 = dst_res->base.height0 - info->dst.box.y - info->dst.box.height;
      dst_y2 = dst_res->base.height0 - info->dst.box.y;
   }

   if (!src_res->is_front && !src_res->renderer_flipped) {
      src_y1 = info->src.box.y + info->src.box.height;
      src_y2 = info->src.box.y;
   } else {
      src_y1 = src_res->base.height0 - info->src.box.y - info->src.box.height;
      src_y2 = src_res->base.height0 - info->src.box.y;
   }

   if (info->scissor_enable)
      glEnable(GL_SCISSOR_TEST);
   else
      glDisable(GL_SCISSOR_TEST);
      
   glBlitFramebuffer(info->src.box.x,
                     src_y1,
                     info->src.box.x + info->src.box.width,
                     src_y2,
                     info->dst.box.x,
                     dst_y1,
                     info->dst.box.x + info->dst.box.width,
                     dst_y2,
                     glmask, convert_mag_filter(info->filter));

   glDeleteFramebuffers(2, fb_ids);
}

void graw_renderer_blit(struct grend_context *ctx,
                        uint32_t dst_handle, uint32_t src_handle,
                        const struct pipe_blit_info *info)
{
   graw_renderer_blit_int(ctx->ctx_id, dst_handle, src_handle, info);
}

int graw_renderer_set_scanout(uint32_t res_handle, uint32_t ctx_id,
                              struct pipe_box *box)
{
   struct grend_resource *res;
   res = graw_lookup_resource(res_handle, ctx_id);
   if (!res)
      return 0;

#if 0
   if (frontbuffer && frontbuffer != res) {
      struct pipe_blit_info bi;

      bi.dst.level = 0;
      bi.dst.box.x = bi.dst.box.y = bi.dst.box.z = 0;
      bi.dst.box.width = res->base.width0;
      bi.dst.box.height = res->base.height0;
      bi.dst.box.depth = 1;

      bi.src.level = 0;
      bi.src.box.x = bi.src.box.y = bi.src.box.z = 0;
      bi.src.box.width = res->base.width0;
      bi.src.box.height = res->base.height0;
      bi.src.box.depth = 1;

      bi.mask = PIPE_MASK_RGBA;
      bi.filter = PIPE_TEX_FILTER_NEAREST;
      bi.scissor_enable = 0;

      res->is_front = 1;
      graw_renderer_blit_int(0, res_handle, res_handle, &bi);
      
      frontbuffer->is_front = 0;
   }
#endif
   frontbuffer = res;
   fprintf(stderr,"setting frontbuffer to %d\n", res_handle);
   return 0;
}

int graw_renderer_flush_buffer_res(struct grend_resource *res,
                                   struct pipe_box *box)
{
   GLuint fb_id;

   if (!res->is_front) {
      glGenFramebuffers(1, &fb_id);

      glBindFramebuffer(GL_FRAMEBUFFER_EXT, fb_id);
      glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT,
                                res->target, res->id, 0);

      glBindFramebuffer(GL_READ_FRAMEBUFFER, fb_id);
      glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
      
      /* force full scissor and viewport - should probably use supplied box */
      glViewport(0, 0, res->base.width0, res->base.height0);
      glScissor(box->x, box->y, box->width, box->height);

      grend_state.viewport_dirty = TRUE;
      grend_state.scissor_dirty = TRUE;
      /* justification for inversion here required */
      glBlitFramebuffer(0, 0, res->base.width0, res->base.height0,
                        0, res->base.height0, res->base.width0, 0,
                        GL_COLOR_BUFFER_BIT, GL_NEAREST);
      glDeleteFramebuffers(1, &fb_id);

      if (draw_cursor)
         graw_renderer_paint_cursor(&grend_state.cursor_info,
                                    res);
   } else {
      glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
      glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
   }

   swap_buffers();
   return 0;
}

int graw_renderer_flush_buffer(uint32_t res_handle,
                               uint32_t ctx_id,
                               struct pipe_box *box)
{
   struct grend_resource *res;

   if (!localrender)
      return 0;

   res = graw_lookup_resource(res_handle, ctx_id);
   if (!res)
      return 0;

   if (res != frontbuffer) {
      fprintf(stderr,"not the frontbuffer %d\n", res_handle);
      return 0;
   }

   return graw_renderer_flush_buffer_res(res, box);
}

int graw_renderer_create_fence(int client_fence_id)
{
   struct grend_fence *fence;

   fence = malloc(sizeof(struct grend_fence));
   if (!fence)
      return -1;

   fence->fence_id = client_fence_id;
   fence->syncobj = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
   list_addtail(&fence->fences, &grend_state.fence_list);
   return 0;
}

void graw_renderer_check_fences(void)
{
   struct grend_fence *fence, *stor;
   uint32_t latest_id = 0;
   GLenum glret;

   if (!inited)
      return;

   LIST_FOR_EACH_ENTRY_SAFE(fence, stor, &grend_state.fence_list, fences) {
      glret = glClientWaitSync(fence->syncobj, 0, 0);
      if (glret == GL_ALREADY_SIGNALED){
         latest_id = fence->fence_id;
         list_del(&fence->fences);
         glDeleteSync(fence->syncobj);
         free(fence);
      }
      /* don't bother checking any subsequent ones */
      else if (glret == GL_TIMEOUT_EXPIRED) {
         break;
      }
   }

   if (latest_id == 0)
      return;
   graw_write_fence(latest_id);
}

static boolean graw_get_one_query_result(GLuint query_id, bool use_64, uint64_t *result)
{
   GLint ready;
   GLuint passed;
   GLuint64 pass64;

   glGetQueryObjectiv(query_id, GL_QUERY_RESULT_AVAILABLE_ARB, &ready);

   if (!ready)
      return FALSE;

   if (use_64) {
      glGetQueryObjectui64v(query_id, GL_QUERY_RESULT_ARB, &pass64);
      *result = pass64;
   } else {
      glGetQueryObjectuiv(query_id, GL_QUERY_RESULT_ARB, &passed);
      *result = passed;
   }
   return TRUE;
}

static boolean graw_check_query(struct grend_query *query)
{
   GLint ready;
   uint64_t result;
   boolean use_64 = FALSE;
   struct graw_host_query_state *state;
   struct grend_nontimer_hw_query *hwq, *stor;
   boolean ret;

   if (grend_is_timer_query(query->gltype)) {
       ret = graw_get_one_query_result(query->timer_query_id, TRUE, &result);
       if (ret == FALSE)
           return FALSE;
       goto out_write_val;
   }

   /* for non-timer queries we have to iterate over all hw queries and remove and total them */
   LIST_FOR_EACH_ENTRY_SAFE(hwq, stor, &query->hw_queries, query_list) {
       ret = graw_get_one_query_result(hwq->id, FALSE, &result);
       if (ret == FALSE)
           return FALSE;
       
       /* if this query is done drop it from the list */
       list_del(&hwq->query_list);
       glDeleteQueries(1, &hwq->id);
       FREE(hwq);

       query->current_total += result;
   }
   result = query->current_total;

out_write_val:
   state = query->res->ptr;

   state->result = result;
   state->query_state = VIRGL_QUERY_STATE_DONE;

   return TRUE;
}

void graw_renderer_check_queries(void)
{
   struct grend_query *query, *stor;
   if (!inited)
      return;   
   
   LIST_FOR_EACH_ENTRY_SAFE(query, stor, &grend_state.waiting_query_list, waiting_queries) {
      if (graw_check_query(query) == TRUE)
         list_delinit(&query->waiting_queries);
   }
}

static void grend_do_end_query(struct grend_query *q)
{
   glEndQuery(q->gltype);
   q->active_hw = FALSE;
}

static void grend_ctx_finish_queries(struct grend_context *ctx)
{
   struct grend_query *query;

   LIST_FOR_EACH_ENTRY(query, &ctx->active_nontimer_query_list, ctx_queries) {
      if (query->active_hw == TRUE)
         grend_do_end_query(query);
   }
}

static void grend_ctx_restart_queries(struct grend_context *ctx)
{
   struct grend_query *query;
   struct grend_nontimer_hw_query *hwq;

   ctx->query_on_hw = TRUE;
   LIST_FOR_EACH_ENTRY(query, &ctx->active_nontimer_query_list, ctx_queries) {
      if (query->active_hw == FALSE) {
         hwq = grend_create_hw_query(query);
         glBeginQuery(query->gltype, hwq->id);
         query->active_hw = TRUE;
      }
   }
}

/* stop all the nontimer queries running in the current context */
void grend_stop_current_queries(void)
{
   if (grend_state.current_ctx && grend_state.current_ctx->query_on_hw)
      grend_ctx_finish_queries(grend_state.current_ctx);
}

void grend_hw_switch_context(struct grend_context *ctx)
{

   if (ctx == grend_state.current_ctx)
      return;

   if (grend_state.current_ctx) {
      grend_ctx_finish_queries(grend_state.current_ctx);
   }
   /* re-emit all the state */
   grend_hw_emit_framebuffer_state(ctx);
   grend_hw_emit_depth_range(ctx);
   grend_hw_emit_blend(ctx);
   grend_hw_emit_dsa(ctx);
   grend_hw_emit_rs(ctx);
   grend_hw_emit_blend_color(ctx);
   grend_hw_emit_streamout_targets(ctx);

   ctx->stencil_state_dirty = TRUE;
   ctx->scissor_state_dirty = TRUE;
   ctx->viewport_state_dirty = TRUE;
   ctx->shader_dirty = TRUE;

   grend_state.current_ctx = ctx;
}


void
graw_renderer_object_destroy(struct grend_context *ctx, uint32_t handle)
{
   graw_object_destroy(ctx->object_hash, handle, 0);
}

void graw_renderer_object_insert(struct grend_context *ctx, void *data,
                                 uint32_t size, uint32_t handle, enum graw_object_type type)
{
   graw_object_insert(ctx->object_hash, data, size, handle, type);
}

static struct grend_nontimer_hw_query *grend_create_hw_query(struct grend_query *query)
{
   struct grend_nontimer_hw_query *hwq;

   hwq = CALLOC_STRUCT(grend_nontimer_hw_query);
   if (!hwq)
      return NULL;

   glGenQueries(1, &hwq->id);
   
   list_add(&hwq->query_list, &query->hw_queries);
   return hwq;
}


void grend_create_query(struct grend_context *ctx, uint32_t handle,
                        uint32_t query_type, uint32_t res_handle,
                        uint32_t offset)
{
   struct grend_query *q;

   q = CALLOC_STRUCT(grend_query);
   if (!q)
      return;

   list_inithead(&q->waiting_queries);
   list_inithead(&q->ctx_queries);
   list_inithead(&q->hw_queries);
   q->type = query_type;

   q->res = graw_lookup_resource(res_handle, ctx->ctx_id);
   if (!q->res)
      fprintf(stderr,"cannot lookup query resource %d\n", res_handle);

   switch (q->type) {
   case PIPE_QUERY_OCCLUSION_COUNTER:
      q->gltype = GL_SAMPLES_PASSED_ARB;      
      break;
   case PIPE_QUERY_OCCLUSION_PREDICATE:
      q->gltype = GL_ANY_SAMPLES_PASSED;
      break;
   case PIPE_QUERY_TIMESTAMP:
      q->gltype = GL_TIMESTAMP;
      break;
   case PIPE_QUERY_TIME_ELAPSED:
      q->gltype = GL_TIME_ELAPSED;
      break;
   default:
      fprintf(stderr,"unknown query object received %d\n", q->type);
      break;
   }

   if (grend_is_timer_query(q->gltype))
      glGenQueries(1, &q->timer_query_id);

   graw_renderer_object_insert(ctx, q, sizeof(struct grend_query), handle,
                               GRAW_QUERY);
}

void grend_destroy_query(struct grend_query *query)
{
   struct grend_nontimer_hw_query *hwq, *stor;

   list_del(&query->ctx_queries);
   list_del(&query->waiting_queries);
   if (grend_is_timer_query(query->gltype)) {
       glDeleteQueries(1, &query->timer_query_id);
       return;
   }
   LIST_FOR_EACH_ENTRY_SAFE(hwq, stor, &query->hw_queries, query_list) {
      glDeleteQueries(1, &hwq->id);
      FREE(hwq);
   }
}

void grend_begin_query(struct grend_context *ctx, uint32_t handle)
{
   struct grend_query *q;
   GLenum qtype;
   struct grend_nontimer_hw_query *hwq;

   q = graw_object_lookup(ctx->object_hash, handle, GRAW_QUERY);
   if (!q)
      return;

   if (q->gltype == GL_TIMESTAMP)
      return;

   if (grend_is_timer_query(q->gltype)) {
      glBeginQuery(q->gltype, q->timer_query_id);
      return;
   }
   hwq = grend_create_hw_query(q);
   
   /* add to active query list for this context */
   glBeginQuery(q->gltype, hwq->id);

   q->active_hw = TRUE;
   list_addtail(&q->ctx_queries, &ctx->active_nontimer_query_list);
}

void grend_end_query(struct grend_context *ctx, uint32_t handle)
{
   struct grend_query *q;
   q = graw_object_lookup(ctx->object_hash, handle, GRAW_QUERY);
   if (!q)
      return;

   if (grend_is_timer_query(q->gltype)) {
      if (q->gltype == GL_TIMESTAMP)
         glQueryCounter(q->timer_query_id, q->gltype);
         /* remove from active query list for this context */
      else
         glEndQuery(q->gltype);
      return;
   }

   if (q->active_hw)
      grend_do_end_query(q);

   list_delinit(&q->ctx_queries);
}

void grend_get_query_result(struct grend_context *ctx, uint32_t handle,
                            uint32_t wait)
{
   struct grend_query *q;
   boolean ret;

   q = graw_object_lookup(ctx->object_hash, handle, GRAW_QUERY);
   if (!q)
      return;

   ret = graw_check_query(q);
   if (ret == FALSE)
      list_addtail(&q->waiting_queries, &grend_state.waiting_query_list);
}

void grend_set_cursor_info(uint32_t cursor_handle, int x, int y)
{
   grend_state.cursor_info.res_handle = cursor_handle;
   grend_state.cursor_info.x = x;
   grend_state.cursor_info.y = y;

   if (frontbuffer && draw_cursor)
      graw_renderer_remove_cursor(&grend_state.cursor_info, frontbuffer);
}

void grend_create_so_target(struct grend_context *ctx,
                            uint32_t handle,
                            uint32_t res_handle,
                            uint32_t buffer_offset,
                            uint32_t buffer_size)
{
   struct grend_so_target *target;

   target = CALLOC_STRUCT(grend_so_target);
   if (!target)
      return;

   target->res_handle = res_handle;
   target->buffer_offset = buffer_offset;
   target->buffer_size = buffer_size;
   target->buffer = graw_lookup_resource(res_handle, ctx->ctx_id);

   graw_object_insert(ctx->object_hash, target, sizeof(*target), handle,
                      GRAW_STREAMOUT_TARGET);
}
