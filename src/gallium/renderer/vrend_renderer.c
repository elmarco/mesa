/**************************************************************************
 *
 * Copyright (C) 2014 Red Hat Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 **************************************************************************/

#include <epoxy/gl.h>

#include <stdio.h>
#include <errno.h>
#include "pipe/p_shader_tokens.h"

#include "pipe/p_context.h"
#include "pipe/p_defines.h"
#include "pipe/p_screen.h"
#include "pipe/p_state.h"
#include "util/u_inlines.h"
#include "util/u_memory.h"
#include "util/u_dual_blend.h"

#include "util/u_double_list.h"
#include "util/u_format.h"
#include "tgsi/tgsi_parse.h"

#include "vrend_object.h"
#include "vrend_shader.h"

#include "vrend_renderer.h"

#include "virgl_hw.h"
/* transfer boxes from the guest POV are in y = 0 = top orientation */
/* blit/copy operations from the guest POV are in y = 0 = top orientation */

/* since we are storing things in OpenGL FBOs we need to flip transfer operations by default */

static struct vrend_resource *vrend_renderer_ctx_res_lookup(struct vrend_context *ctx, int res_handle);
static void vrend_update_viewport_state(struct vrend_context *ctx);
static void vrend_update_scissor_state(struct vrend_context *ctx);
static void vrend_destroy_query_object(void *obj_ptr);
static void vrend_finish_context_switch(struct vrend_context *ctx);
static void vrend_patch_blend_func(struct vrend_context *ctx);
static void vrend_update_frontface_state(struct vrend_context *ctx);
static void vrender_get_glsl_version(int *glsl_version);
extern int vrend_shader_use_explicit;
static int have_invert_mesa = 0;
static int use_core_profile = 0;

static int renderer_gl_major, renderer_gl_minor;
int vrend_dump_shaders;

struct vrend_if_cbs *vrend_clicbs;

struct vrend_fence {
   uint32_t fence_id;
   uint32_t ctx_id;
   GLsync syncobj;
   struct list_head fences;
};

struct vrend_nontimer_hw_query {
   struct list_head query_list;
   GLuint id;
   uint64_t result;
};

struct vrend_query {
   struct list_head waiting_queries;
   struct list_head ctx_queries;

   struct list_head hw_queries;
   GLuint timer_query_id;
   GLuint type;
   GLuint gltype;
   int ctx_id;
   struct vrend_resource *res;
   uint64_t current_total;
   boolean active_hw;
};

#define VIRGL_INVALID_RESOURCE 1
struct global_error_state {
   enum virgl_errors last_error;
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
   struct vrend_context *current_ctx;
   struct vrend_context *current_hw_ctx;
   struct list_head waiting_query_list;

   boolean have_samplers;
   boolean have_robustness;
   boolean have_multisample;
   boolean have_ms_scaled_blit;
   GLuint vaoid;

   struct pipe_rasterizer_state hw_rs_state;
   struct pipe_depth_stencil_alpha_state hw_dsa_state;
   struct pipe_blend_state hw_blend_state;

   boolean have_nv_prim_restart, have_gl_prim_restart, have_bit_encoding;

   uint32_t max_uniform_blocks;
};

static struct global_renderer_state vrend_state;

struct vrend_linked_shader_program {
  struct list_head head;
  GLuint id;

  boolean dual_src_linked;
  struct vrend_shader *ss[PIPE_SHADER_TYPES];

  uint32_t samplers_used_mask[PIPE_SHADER_TYPES];
  GLuint *samp_locs[PIPE_SHADER_TYPES];

  GLuint *shadow_samp_mask_locs[PIPE_SHADER_TYPES];
  GLuint *shadow_samp_add_locs[PIPE_SHADER_TYPES];

  GLuint *const_locs[PIPE_SHADER_TYPES];

  GLuint *attrib_locs;
  uint32_t shadow_samp_mask[PIPE_SHADER_TYPES];

  GLuint *ubo_locs[PIPE_SHADER_TYPES];
  GLuint vs_ws_adjust_loc;

  GLuint fs_stipple_loc;

  GLuint clip_locs[8];
};

struct vrend_shader {
   struct vrend_shader *next_variant;
   struct vrend_shader_selector *sel;

   GLchar *glsl_prog;
   GLuint id;
   GLuint compiled_fs_id;
   struct vrend_shader_key key;
};

struct vrend_shader_selector {
   struct pipe_reference reference;
   struct vrend_shader *current;
   struct tgsi_token *tokens;

   struct vrend_shader_info sinfo;

   unsigned num_shaders;
   unsigned type;
};

struct vrend_buffer {
   struct vrend_resource base;
};

struct vrend_texture {
   struct vrend_resource base;
   struct pipe_sampler_state state;
   GLenum cur_swizzle_r;
   GLenum cur_swizzle_g;
   GLenum cur_swizzle_b;
   GLenum cur_swizzle_a;
   GLuint srgb_decode;
};

struct vrend_surface {
   struct pipe_reference reference;
   GLuint id;
   GLuint res_handle;
   GLuint format;
   GLuint val0, val1;
   struct vrend_resource *texture;
};

struct vrend_sampler_state {
   struct pipe_sampler_state base;
   GLuint id;
};

struct vrend_so_target {
   struct pipe_reference reference;
   GLuint res_handle;
   unsigned buffer_offset;
   unsigned buffer_size;
   struct vrend_resource *buffer;
};

struct vrend_sampler_view {
   struct pipe_reference reference;
   GLuint id;
   GLuint res_handle;
   GLuint format;
   GLuint val0, val1;
   GLuint swizzle_r:3;
   GLuint swizzle_g:3;
   GLuint swizzle_b:3;
   GLuint swizzle_a:3;
   GLuint gl_swizzle_r;
   GLuint gl_swizzle_g;
   GLuint gl_swizzle_b;
   GLuint gl_swizzle_a;
   GLuint cur_base, cur_max;
   struct vrend_resource *texture;
   GLenum depth_texture_mode;
   GLuint srgb_decode;
};

struct vrend_vertex_element {
   struct pipe_vertex_element base;
   GLenum type;
   GLboolean norm;
   GLuint nr_chan;
};

struct vrend_vertex_element_array {
   unsigned count;
   struct vrend_vertex_element elements[PIPE_MAX_ATTRIBS];
};

struct vrend_constants {
   unsigned int *consts;
   uint32_t num_consts;
};

struct vrend_shader_view {
   int num_views;
   struct vrend_sampler_view *views[PIPE_MAX_SHADER_SAMPLER_VIEWS];
   uint32_t res_id[PIPE_MAX_SHADER_SAMPLER_VIEWS];
   uint32_t old_ids[PIPE_MAX_SHADER_SAMPLER_VIEWS];
};

struct vrend_sub_context {

   struct list_head head;

   virgl_gl_context gl_context;

   int sub_ctx_id;

   GLuint vaoid;
   uint32_t enabled_attribs_bitmask;

   struct util_hash_table *object_hash;
   struct list_head programs;

   struct vrend_vertex_element_array *ve;
   int num_vbos;
   struct pipe_vertex_buffer vbo[PIPE_MAX_ATTRIBS];
   uint32_t vbo_res_ids[PIPE_MAX_ATTRIBS];
   struct vrend_shader_selector *vs;
   struct vrend_shader_selector *gs;
   struct vrend_shader_selector *fs;

   bool shader_dirty;
   struct vrend_linked_shader_program *prog;

   struct vrend_shader_view views[PIPE_SHADER_TYPES];

   struct pipe_index_buffer ib;
   uint32_t index_buffer_res_id;

   struct vrend_constants consts[PIPE_SHADER_TYPES];
   bool const_dirty[PIPE_SHADER_TYPES];
   struct vrend_sampler_state *sampler_state[PIPE_SHADER_TYPES][PIPE_MAX_SAMPLERS];

   struct pipe_constant_buffer cbs[PIPE_SHADER_TYPES][PIPE_MAX_CONSTANT_BUFFERS];
   uint32_t const_bufs_used_mask[PIPE_SHADER_TYPES];

   int num_sampler_states[PIPE_SHADER_TYPES];
   boolean sampler_state_dirty;

   uint32_t fb_id;
   int nr_cbufs, old_nr_cbufs;
   struct vrend_surface *zsurf;
   struct vrend_surface *surf[8];

   GLint view_cur_x, view_cur_y;
   GLsizei view_width, view_height;
   GLclampd view_near_val, view_far_val;
   float depth_transform, depth_scale;
   /* viewport is negative */
   GLboolean viewport_is_negative;
   /* this is set if the contents of the FBO look upside down when viewed
      with 0,0 as the bottom corner */
   GLboolean inverted_fbo_content;
   boolean scissor_state_dirty;
   boolean viewport_state_dirty;
   boolean stencil_state_dirty;
   uint32_t fb_height;

   struct pipe_scissor_state ss;

   struct pipe_blend_state blend_state;
   struct pipe_depth_stencil_alpha_state dsa_state;
   struct pipe_rasterizer_state rs_state;

   struct pipe_blend_color blend_color;

   int num_so_targets;
   struct vrend_so_target *so_targets[16];

   uint8_t stencil_refs[2];

   GLuint blit_fb_ids[2];

   struct pipe_depth_stencil_alpha_state *dsa;

   struct pipe_clip_state ucp_state;
};

struct vrend_context {
   char debug_name[64];

   struct list_head sub_ctxs;

   struct vrend_sub_context *sub;
   struct vrend_sub_context *sub0;

   int ctx_id;

   /* resource bounds to this context */
   struct util_hash_table *res_hash;

   struct list_head active_nontimer_query_list;
   boolean query_on_hw;

   /* has this ctx gotten an error? */
   boolean in_error;
   enum virgl_ctx_errors last_error;

   boolean ctx_switch_pending;


   boolean pstip_inited;
   GLuint pstipple_tex_id;

   struct vrend_shader_cfg shader_cfg;
};

static struct vrend_nontimer_hw_query *vrend_create_hw_query(struct vrend_query *query);

static struct vrend_format_table tex_conv_table[VIRGL_FORMAT_MAX];

static INLINE boolean vrend_format_can_sample(enum virgl_formats format)
{
   return tex_conv_table[format].bindings & VREND_BIND_SAMPLER;
}
static INLINE boolean vrend_format_can_render(enum virgl_formats format)
{
   return tex_conv_table[format].bindings & VREND_BIND_RENDER;
}

static INLINE boolean vrend_format_is_ds(enum virgl_formats format)
{
   return tex_conv_table[format].bindings & VREND_BIND_DEPTHSTENCIL;
}

bool vrend_is_ds_format(enum virgl_formats format)
{
   return vrend_format_is_ds(format);
}

static inline const char *pipe_shader_to_prefix(int shader_type)
{
   switch (shader_type) {
   case PIPE_SHADER_VERTEX: return "vs";
   case PIPE_SHADER_FRAGMENT: return "fs";
   case PIPE_SHADER_GEOMETRY: return "gs";
   };
   return NULL;
}

static const char *vrend_ctx_error_strings[] = { "None", "Unknown", "Illegal shader", "Illegal handle", "Illegal resource", "Illegal surface", "Illegal vertex format" };

static void __report_context_error(const char *fname, struct vrend_context *ctx, enum virgl_ctx_errors error, uint32_t value)
{
   ctx->in_error = TRUE;
   ctx->last_error = error;
   fprintf(stderr,"%s: context error reported %d \"%s\" %s %d\n", fname, ctx->ctx_id, ctx->debug_name, vrend_ctx_error_strings[error], value);
}
#define report_context_error(ctx, error, value) __report_context_error(__func__, ctx, error, value)

void vrend_report_buffer_error(struct vrend_context *ctx, int cmd)
{
   report_context_error(ctx, VIRGL_ERROR_CTX_ILLEGAL_CMD_BUFFER, cmd);
}

#define CORE_PROFILE_WARN_NONE 0
#define CORE_PROFILE_WARN_STIPPLE 1
#define CORE_PROFILE_WARN_POLYGON_MODE 2
#define CORE_PROFILE_WARN_TWO_SIDE 3
#define CORE_PROFILE_WARN_CLAMP 4
#define CORE_PROFILE_WARN_SHADE_MODEL 5

static const char *vrend_core_profile_warn_strings[] = { "None", "Stipple", "Polygon Mode", "Two Side", "Clamping", "Shade Model" };

static void __report_core_warn(const char *fname, struct vrend_context *ctx, enum virgl_ctx_errors error, uint32_t value)
{
   fprintf(stderr,"%s: core profile violation reported %d \"%s\" %s %d\n", fname, ctx->ctx_id, ctx->debug_name, vrend_core_profile_warn_strings[error], value);
}
#define report_core_warn(ctx, error, value) __report_core_warn(__func__, ctx, error, value)
static INLINE boolean should_invert_viewport(struct vrend_context *ctx)
{
   /* if we have a negative viewport then gallium wanted to invert it,
      however since we are rendering to GL FBOs we need to invert it
      again unless we are rendering upside down already
      - confused? 
      so if gallium asks for a negative viewport */
   return !(ctx->sub->viewport_is_negative ^ ctx->sub->inverted_fbo_content);
}

static void vrend_destroy_surface(struct vrend_surface *surf)
{
   vrend_resource_reference(&surf->texture, NULL);
   free(surf);
}

static INLINE void
vrend_surface_reference(struct vrend_surface **ptr, struct vrend_surface *surf)
{
   struct vrend_surface *old_surf = *ptr;

   if (pipe_reference(&(*ptr)->reference, &surf->reference))
      vrend_destroy_surface(old_surf);
   *ptr = surf;
}

static void vrend_destroy_sampler_view(struct vrend_sampler_view *samp)
{
   vrend_resource_reference(&samp->texture, NULL);
   free(samp);
}

static INLINE void
vrend_sampler_view_reference(struct vrend_sampler_view **ptr, struct vrend_sampler_view *view)
{
   struct vrend_sampler_view *old_view = *ptr;

   if (pipe_reference(&(*ptr)->reference, &view->reference))
      vrend_destroy_sampler_view(old_view);
   *ptr = view;
}

static void vrend_destroy_so_target(struct vrend_so_target *target)
{
   vrend_resource_reference(&target->buffer, NULL);
   free(target);
}

static INLINE void
vrend_so_target_reference(struct vrend_so_target **ptr, struct vrend_so_target *target)
{
   struct vrend_so_target *old_target = *ptr;

   if (pipe_reference(&(*ptr)->reference, &target->reference))
      vrend_destroy_so_target(old_target);
   *ptr = target;
}

static void vrend_shader_destroy(struct vrend_shader *shader)
{
   glDeleteShader(shader->id);
   free(shader->glsl_prog);
   free(shader);
}

static void vrend_destroy_shader_selector(struct vrend_shader_selector *sel)
{
   struct vrend_shader *p = sel->current, *c;
   int i;
   while (p) {
      c = p->next_variant;
      vrend_shader_destroy(p);
      p = c;
   }
   for (i = 0; i < sel->sinfo.so_info.num_outputs; i++)
      free(sel->sinfo.so_names[i]);
   free(sel->sinfo.so_names);
   free(sel->sinfo.interpinfo);
   free(sel->tokens);
   free(sel);
}

static boolean vrend_compile_shader(struct vrend_context *ctx,
                                struct vrend_shader *shader)
{
   GLint param;
   glShaderSource(shader->id, 1, (const char **)&shader->glsl_prog, NULL);
   glCompileShader(shader->id);
   glGetShaderiv(shader->id, GL_COMPILE_STATUS, &param);
   if (param == GL_FALSE) {
      char infolog[65536];
      int len;
      glGetShaderInfoLog(shader->id, 65536, &len, infolog);
      report_context_error(ctx, VIRGL_ERROR_CTX_ILLEGAL_SHADER, 0);
      fprintf(stderr,"shader failed to compile\n%s\n", infolog);
      fprintf(stderr,"GLSL:\n%s\n", shader->glsl_prog);
      return FALSE;
   }
   return TRUE;
}

static INLINE void
vrend_shader_state_reference(struct vrend_shader_selector **ptr, struct vrend_shader_selector *shader)
{
   struct vrend_shader_selector *old_shader = *ptr;

   if (pipe_reference(&(*ptr)->reference, &shader->reference))
      vrend_destroy_shader_selector(old_shader);
   *ptr = shader;
}

void
vrend_insert_format(struct vrend_format_table *entry, uint32_t bindings)
{
   tex_conv_table[entry->format] = *entry;
   tex_conv_table[entry->format].bindings = bindings;
}

void
vrend_insert_format_swizzle(int override_format, struct vrend_format_table *entry, uint32_t bindings, uint8_t swizzle[4])
{
  int i;
   tex_conv_table[override_format] = *entry;
   tex_conv_table[override_format].bindings = bindings;
   tex_conv_table[override_format].flags = VREND_BIND_NEED_SWIZZLE;
   for (i = 0; i < 4; i++)
     tex_conv_table[override_format].swizzle[i] = swizzle[i];
}

static boolean vrend_is_timer_query(GLenum gltype)
{
	return gltype == GL_TIMESTAMP ||
               gltype == GL_TIME_ELAPSED;
}

void vrend_use_program(GLuint program_id)
{
   if (vrend_state.program_id != program_id) {
      glUseProgram(program_id);
      vrend_state.program_id = program_id;
   }
}

static void vrend_init_pstipple_texture(struct vrend_context *ctx)
{
   glGenTextures(1, &ctx->pstipple_tex_id);
   glBindTexture(GL_TEXTURE_2D, ctx->pstipple_tex_id);
   glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, 32, 32, 0, GL_RED, GL_UNSIGNED_BYTE, NULL);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

   ctx->pstip_inited = true;
}

void vrend_bind_va(GLuint vaoid)
{
   glBindVertexArray(vaoid);
}

void vrend_blend_enable(GLboolean blend_enable)
{
   if (vrend_state.blend_enabled != blend_enable) {
      vrend_state.blend_enabled = blend_enable;
      if (blend_enable)
         glEnable(GL_BLEND);
      else
         glDisable(GL_BLEND);
   }
}

void vrend_depth_test_enable(GLboolean depth_test_enable)
{
   if (vrend_state.depth_test_enabled != depth_test_enable) {
      vrend_state.depth_test_enabled = depth_test_enable;
      if (depth_test_enable)
         glEnable(GL_DEPTH_TEST);
      else
         glDisable(GL_DEPTH_TEST);
   }
}

static void vrend_alpha_test_enable(struct vrend_context *ctx,
                                    GLboolean alpha_test_enable)
{
   if (use_core_profile) {
       /* handled in shaders */
       return;
   }
   if (vrend_state.alpha_test_enabled != alpha_test_enable) {
      vrend_state.alpha_test_enabled = alpha_test_enable;
      if (alpha_test_enable)
         glEnable(GL_ALPHA_TEST);
      else
         glDisable(GL_ALPHA_TEST);
   }
}
static void vrend_stencil_test_enable(GLboolean stencil_test_enable)
{
   if (vrend_state.stencil_test_enabled != stencil_test_enable) {
      vrend_state.stencil_test_enabled = stencil_test_enable;
      if (stencil_test_enable)
         glEnable(GL_STENCIL_TEST);
      else
         glDisable(GL_STENCIL_TEST);
   }
}

static void set_stream_out_varyings(int prog_id, struct vrend_shader_info *sinfo)
{
   struct pipe_stream_output_info *so = &sinfo->so_info;
   char *varyings[PIPE_MAX_SHADER_OUTPUTS];
   int i;
   
   if (!so->num_outputs)
      return;

   for (i = 0; i < so->num_outputs; i++) {
      varyings[i] = strdup(sinfo->so_names[i]);
   }

   glTransformFeedbackVaryings(prog_id, so->num_outputs,
                               (const GLchar **)varyings, GL_INTERLEAVED_ATTRIBS_EXT);

   for (i = 0; i < so->num_outputs; i++)
      if (varyings[i])
         free(varyings[i]);
}

static struct vrend_linked_shader_program *add_shader_program(struct vrend_context *ctx,
                                                              struct vrend_shader *vs,
                                                              struct vrend_shader *fs,
                                                              struct vrend_shader *gs)
{
   struct vrend_linked_shader_program *sprog = CALLOC_STRUCT(vrend_linked_shader_program);
  char name[16];
  int i;
  GLuint prog_id;
  GLint lret;
  int id;

  if (!sprog)
     return NULL;

  /* need to rewrite VS code to add interpolation params */
  if ((gs && gs->compiled_fs_id != fs->id) ||
      (!gs && vs->compiled_fs_id != fs->id)) {
     boolean ret;

     if (gs)
        vrend_patch_vertex_shader_interpolants(gs->glsl_prog,
                                               &gs->sel->sinfo,
                                               &fs->sel->sinfo, true, fs->key.flatshade);
     else
        vrend_patch_vertex_shader_interpolants(vs->glsl_prog,
                                               &vs->sel->sinfo,
                                               &fs->sel->sinfo, false, fs->key.flatshade);
     ret = vrend_compile_shader(ctx, gs ? gs : vs);
     if (ret == FALSE) {
        glDeleteShader(gs ? gs->id : vs->id);
        free(sprog);
        return NULL;
     }
     if (gs)
        gs->compiled_fs_id = fs->id;
     else
        vs->compiled_fs_id = fs->id;
  }

  prog_id = glCreateProgram();
  glAttachShader(prog_id, vs->id);
  if (gs) {
     if (gs->id > 0)
        glAttachShader(prog_id, gs->id);
     set_stream_out_varyings(prog_id, &gs->sel->sinfo);
  }
  else
     set_stream_out_varyings(prog_id, &vs->sel->sinfo);
  glAttachShader(prog_id, fs->id);

  if (fs->sel->sinfo.num_outputs > 1) {
     if (util_blend_state_is_dual(&ctx->sub->blend_state, 0)) {
        glBindFragDataLocationIndexed(prog_id, 0, 0, "fsout_c0");
        glBindFragDataLocationIndexed(prog_id, 0, 1, "fsout_c1");
        sprog->dual_src_linked = true;
     } else {
        glBindFragDataLocationIndexed(prog_id, 0, 0, "fsout_c0");
        glBindFragDataLocationIndexed(prog_id, 1, 0, "fsout_c1");
        sprog->dual_src_linked = false;
     }
  } else
     sprog->dual_src_linked = false;

  glLinkProgram(prog_id);

  glGetProgramiv(prog_id, GL_LINK_STATUS, &lret);
  if (lret == GL_FALSE) {
     char infolog[65536];
     int len;
     glGetProgramInfoLog(prog_id, 65536, &len, infolog);
     fprintf(stderr,"got error linking\n%s\n", infolog);
     /* dump shaders */
     report_context_error(ctx, VIRGL_ERROR_CTX_ILLEGAL_SHADER, 0);
     fprintf(stderr,"vert shader: %d GLSL\n%s\n", vs->id, vs->glsl_prog);
     if (gs)
        fprintf(stderr,"geom shader: %d GLSL\n%s\n", gs->id, gs->glsl_prog);
     fprintf(stderr,"frag shader: %d GLSL\n%s\n", fs->id, fs->glsl_prog);
     glDeleteProgram(prog_id);
     return NULL;
  }

  sprog->ss[0] = vs;
  sprog->ss[1] = fs;
  sprog->ss[2] = gs;

  sprog->id = prog_id;

  list_add(&sprog->head, &ctx->sub->programs);

  if (fs->key.pstipple_tex)
     sprog->fs_stipple_loc = glGetUniformLocation(prog_id, "pstipple_sampler");
  else
     sprog->fs_stipple_loc = -1;
  sprog->vs_ws_adjust_loc = glGetUniformLocation(prog_id, "winsys_adjust");
  for (id = PIPE_SHADER_VERTEX; id <= (gs ? PIPE_SHADER_GEOMETRY : PIPE_SHADER_FRAGMENT); id++) {
    if (sprog->ss[id]->sel->sinfo.samplers_used_mask) {
       uint32_t mask = sprog->ss[id]->sel->sinfo.samplers_used_mask;
       int nsamp = util_bitcount(sprog->ss[id]->sel->sinfo.samplers_used_mask);
       int index;
       sprog->shadow_samp_mask[id] = sprog->ss[id]->sel->sinfo.shadow_samp_mask;
       if (sprog->ss[id]->sel->sinfo.shadow_samp_mask) {
          sprog->shadow_samp_mask_locs[id] = calloc(nsamp, sizeof(uint32_t));
          sprog->shadow_samp_add_locs[id] = calloc(nsamp, sizeof(uint32_t));
       } else {
          sprog->shadow_samp_mask_locs[id] = sprog->shadow_samp_add_locs[id] = NULL;
       }
       sprog->samp_locs[id] = calloc(nsamp, sizeof(uint32_t));
       if (sprog->samp_locs[id]) {
          const char *prefix = pipe_shader_to_prefix(id);
          index = 0;
          while(mask) {
             i = u_bit_scan(&mask);
             snprintf(name, 10, "%ssamp%d", prefix, i);
             sprog->samp_locs[id][index] = glGetUniformLocation(prog_id, name);
             if (sprog->ss[id]->sel->sinfo.shadow_samp_mask & (1 << i)) {
                snprintf(name, 14, "%sshadmask%d", prefix, i);
                sprog->shadow_samp_mask_locs[id][index] = glGetUniformLocation(prog_id, name);
                snprintf(name, 14, "%sshadadd%d", prefix, i);
                sprog->shadow_samp_add_locs[id][index] = glGetUniformLocation(prog_id, name);
             }
             index++;
          }
       }
    } else {
       sprog->samp_locs[id] = NULL;
       sprog->shadow_samp_mask_locs[id] = NULL;
       sprog->shadow_samp_add_locs[id] = NULL;
       sprog->shadow_samp_mask[id] = 0;
    }
    sprog->samplers_used_mask[id] = sprog->ss[id]->sel->sinfo.samplers_used_mask;
  }
  
  for (id = PIPE_SHADER_VERTEX; id <= (gs ? PIPE_SHADER_GEOMETRY : PIPE_SHADER_FRAGMENT); id++) {
     if (sprog->ss[id]->sel->sinfo.num_consts) {
        sprog->const_locs[id] = calloc(sprog->ss[id]->sel->sinfo.num_consts, sizeof(uint32_t));
        if (sprog->const_locs[id]) {
           const char *prefix = pipe_shader_to_prefix(id);
           for (i = 0; i < sprog->ss[id]->sel->sinfo.num_consts; i++) {
              snprintf(name, 16, "%sconst0[%d]", prefix, i);
              sprog->const_locs[id][i] = glGetUniformLocation(prog_id, name);
           }
        }
     } else
        sprog->const_locs[id] = NULL;
  }
  
  if (vs->sel->sinfo.num_inputs) {
    sprog->attrib_locs = calloc(vs->sel->sinfo.num_inputs, sizeof(uint32_t));
    if (sprog->attrib_locs) {
      for (i = 0; i < vs->sel->sinfo.num_inputs; i++) {
	snprintf(name, 10, "in_%d", i);
	sprog->attrib_locs[i] = glGetAttribLocation(prog_id, name);
      }
    }
  } else
    sprog->attrib_locs = NULL;

  for (id = PIPE_SHADER_VERTEX; id <= (gs ? PIPE_SHADER_GEOMETRY : PIPE_SHADER_FRAGMENT); id++) {
     sprog->ubo_locs[id] = calloc(sprog->ss[id]->sel->sinfo.num_ubos, sizeof(uint32_t));
     if (sprog->ss[id]->sel->sinfo.num_ubos) {
        const char *prefix = pipe_shader_to_prefix(id);

        for (i = 0; i < sprog->ss[id]->sel->sinfo.num_ubos; i++) {
           snprintf(name, 16, "%subo%d", prefix, i + 1);
           sprog->ubo_locs[id][i] = glGetUniformBlockIndex(prog_id, name);
        }
     } else
        sprog->ubo_locs[id] = NULL;
  }

  if (vs->sel->sinfo.num_ucp) {
     for (i = 0; i < vs->sel->sinfo.num_ucp; i++) {
        snprintf(name, 10, "clipp[%d]", i);
        sprog->clip_locs[i] = glGetUniformLocation(prog_id, name);
     }
  }
  return sprog;
}

static struct vrend_linked_shader_program *lookup_shader_program(struct vrend_context *ctx,
                                                                 GLuint vs_id, GLuint fs_id, GLuint gs_id, GLboolean dual_src)
{
  struct vrend_linked_shader_program *ent;
  LIST_FOR_EACH_ENTRY(ent, &ctx->sub->programs, head) {
     if (ent->dual_src_linked != dual_src)
        continue;
     if (ent->ss[PIPE_SHADER_VERTEX]->id == vs_id && ent->ss[PIPE_SHADER_FRAGMENT]->id == fs_id) {
        if (!ent->ss[PIPE_SHADER_GEOMETRY] && gs_id == 0)
           return ent;
        if (ent->ss[PIPE_SHADER_GEOMETRY] && ent->ss[PIPE_SHADER_GEOMETRY]->id == gs_id)
           return ent;
     }
  }
  return 0;
}

static void vrend_free_programs(struct vrend_sub_context *sub)
{
   struct vrend_linked_shader_program *ent, *tmp;
   int i;
   if (LIST_IS_EMPTY(&sub->programs))
	return;

   LIST_FOR_EACH_ENTRY_SAFE(ent, tmp, &sub->programs, head) {
      glDeleteProgram(ent->id);
      list_del(&ent->head);

      for (i = PIPE_SHADER_VERTEX; i <= PIPE_SHADER_GEOMETRY; i++) {
//         vrend_shader_state_reference(&ent->ss[i], NULL);
         free(ent->shadow_samp_mask_locs[i]);
         free(ent->shadow_samp_add_locs[i]);
         free(ent->samp_locs[i]);
         free(ent->const_locs[i]);
         free(ent->ubo_locs[i]);

      }
      free(ent->attrib_locs);
      free(ent);
   }
}  

static void vrend_apply_sampler_state(struct vrend_context *ctx,
                                      struct vrend_resource *res,
                                      uint32_t shader_type,
                                      int id, uint32_t srgb_decode);

void vrend_update_stencil_state(struct vrend_context *ctx);

int vrend_create_surface(struct vrend_context *ctx,
                         uint32_t handle,
                         uint32_t res_handle, uint32_t format,
                         uint32_t val0, uint32_t val1)
   
{
   struct vrend_surface *surf;
   struct vrend_resource *res;
   uint32_t ret_handle;

   res = vrend_renderer_ctx_res_lookup(ctx, res_handle);
   if (!res) {
      report_context_error(ctx, VIRGL_ERROR_CTX_ILLEGAL_RESOURCE, res_handle);
      return EINVAL;
   }

   surf = CALLOC_STRUCT(vrend_surface);
   if (!surf)
      return ENOMEM;

   surf->res_handle = res_handle;
   surf->format = format;
   surf->val0 = val0;
   surf->val1 = val1;
   pipe_reference_init(&surf->reference, 1);

   vrend_resource_reference(&surf->texture, res);

   ret_handle = vrend_renderer_object_insert(ctx, surf, sizeof(*surf), handle, VIRGL_OBJECT_SURFACE);
   if (ret_handle == 0) {
      FREE(surf);
      return ENOMEM;
   }
   return 0;
}

static void vrend_destroy_surface_object(void *obj_ptr)
{
   struct vrend_surface *surface = obj_ptr;

   vrend_surface_reference(&surface, NULL);
}

static void vrend_destroy_sampler_view_object(void *obj_ptr)
{
   struct vrend_sampler_view *samp = obj_ptr;

   vrend_sampler_view_reference(&samp, NULL);
}

static void vrend_destroy_so_target_object(void *obj_ptr)
{
   struct vrend_so_target *target = obj_ptr;

   vrend_so_target_reference(&target, NULL);
}

static void vrend_destroy_sampler_state_object(void *obj_ptr)
{
   struct vrend_sampler_state *state = obj_ptr;

   glDeleteSamplers(1, &state->id);
   FREE(state);
}

static GLuint convert_wrap(int wrap)
{
   switch(wrap){
   case PIPE_TEX_WRAP_REPEAT: return GL_REPEAT;
   case PIPE_TEX_WRAP_CLAMP: if (use_core_profile == 0) return GL_CLAMP; else return GL_CLAMP_TO_EDGE;

   case PIPE_TEX_WRAP_CLAMP_TO_EDGE: return GL_CLAMP_TO_EDGE;
   case PIPE_TEX_WRAP_CLAMP_TO_BORDER: return GL_CLAMP_TO_BORDER;

   case PIPE_TEX_WRAP_MIRROR_REPEAT: return GL_MIRRORED_REPEAT;
   case PIPE_TEX_WRAP_MIRROR_CLAMP: return GL_MIRROR_CLAMP_EXT;
   case PIPE_TEX_WRAP_MIRROR_CLAMP_TO_EDGE: return GL_MIRROR_CLAMP_TO_EDGE_EXT;
   case PIPE_TEX_WRAP_MIRROR_CLAMP_TO_BORDER: return GL_MIRROR_CLAMP_TO_BORDER_EXT;
   default:
      assert(0);
      return -1;
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

int vrend_create_sampler_state(struct vrend_context *ctx,
                               uint32_t handle,
                               struct pipe_sampler_state *templ)
{
   struct vrend_sampler_state *state = CALLOC_STRUCT(vrend_sampler_state);
   int ret_handle;

   if (!state)
      return ENOMEM;

   state->base = *templ;

   if (vrend_state.have_samplers) {
      glGenSamplers(1, &state->id);

      glSamplerParameteri(state->id, GL_TEXTURE_WRAP_S, convert_wrap(templ->wrap_s));
      glSamplerParameteri(state->id, GL_TEXTURE_WRAP_T, convert_wrap(templ->wrap_t));
      glSamplerParameteri(state->id, GL_TEXTURE_WRAP_R, convert_wrap(templ->wrap_r));
      glSamplerParameterf(state->id, GL_TEXTURE_MIN_FILTER, convert_min_filter(templ->min_img_filter, templ->min_mip_filter));
      glSamplerParameterf(state->id, GL_TEXTURE_MAG_FILTER, convert_mag_filter(templ->mag_img_filter));
      glSamplerParameterf(state->id, GL_TEXTURE_MIN_LOD, templ->min_lod);
      glSamplerParameterf(state->id, GL_TEXTURE_MAX_LOD, templ->max_lod);
      glSamplerParameterf(state->id, GL_TEXTURE_LOD_BIAS, templ->lod_bias);
      glSamplerParameteri(state->id, GL_TEXTURE_COMPARE_MODE, templ->compare_mode ? GL_COMPARE_R_TO_TEXTURE : GL_NONE);
      glSamplerParameteri(state->id, GL_TEXTURE_COMPARE_FUNC, GL_NEVER + templ->compare_func);

      glSamplerParameterIuiv(state->id, GL_TEXTURE_BORDER_COLOR, templ->border_color.ui);
   }
   ret_handle = vrend_renderer_object_insert(ctx, state, sizeof(struct vrend_sampler_state), handle,
                                             VIRGL_OBJECT_SAMPLER_STATE);
   if (!ret_handle) {
      if (vrend_state.have_samplers)
         glDeleteSamplers(1, &state->id);
      FREE(state);
      return ENOMEM;
   }
   return 0;
}

static inline GLenum to_gl_swizzle(int swizzle)
{
   switch (swizzle) {
   case PIPE_SWIZZLE_RED: return GL_RED;
   case PIPE_SWIZZLE_GREEN: return GL_GREEN;
   case PIPE_SWIZZLE_BLUE: return GL_BLUE;
   case PIPE_SWIZZLE_ALPHA: return GL_ALPHA;
   case PIPE_SWIZZLE_ZERO: return GL_ZERO;
   case PIPE_SWIZZLE_ONE: return GL_ONE;
   }
   assert(0);
   return 0;
}

int vrend_create_sampler_view(struct vrend_context *ctx,
                              uint32_t handle,
                              uint32_t res_handle, uint32_t format,
                              uint32_t val0, uint32_t val1, uint32_t swizzle_packed)
{
   struct vrend_sampler_view *view;
   struct vrend_resource *res;
   int ret_handle;
   res = vrend_renderer_ctx_res_lookup(ctx, res_handle);
   if (!res) {
      report_context_error(ctx, VIRGL_ERROR_CTX_ILLEGAL_RESOURCE, res_handle);
      return EINVAL;
   }
   
   view = CALLOC_STRUCT(vrend_sampler_view);
   if (!view)
      return ENOMEM;

   pipe_reference_init(&view->reference, 1);
   view->res_handle = res_handle;
   view->format = format;
   view->val0 = val0;
   view->val1 = val1;
   view->swizzle_r = swizzle_packed & 0x7;
   view->swizzle_g = (swizzle_packed >> 3) & 0x7;
   view->swizzle_b = (swizzle_packed >> 6) & 0x7;
   view->swizzle_a = (swizzle_packed >> 9) & 0x7;
   view->cur_base = -1;
   view->cur_max = 10000;

   vrend_resource_reference(&view->texture, res);

   view->srgb_decode = GL_DECODE_EXT;
   if (view->format != view->texture->base.format) {
      if (util_format_is_srgb(view->texture->base.format) &&
          !util_format_is_srgb(view->format))
         view->srgb_decode = GL_SKIP_DECODE_EXT;
   }
   if (util_format_has_alpha(format) || util_format_is_depth_or_stencil(format))
      view->gl_swizzle_a = to_gl_swizzle(view->swizzle_a);
   else
      view->gl_swizzle_a = GL_ONE;
   view->gl_swizzle_r = to_gl_swizzle(view->swizzle_r);
   view->gl_swizzle_g = to_gl_swizzle(view->swizzle_g);
   view->gl_swizzle_b = to_gl_swizzle(view->swizzle_b);

   if (tex_conv_table[format].flags & VREND_BIND_NEED_SWIZZLE) {
     view->gl_swizzle_r = to_gl_swizzle(tex_conv_table[format].swizzle[0]);
     view->gl_swizzle_g = to_gl_swizzle(tex_conv_table[format].swizzle[1]);
     view->gl_swizzle_b = to_gl_swizzle(tex_conv_table[format].swizzle[2]);
     view->gl_swizzle_a = to_gl_swizzle(tex_conv_table[format].swizzle[3]);
   }
   ret_handle = vrend_renderer_object_insert(ctx, view, sizeof(*view), handle, VIRGL_OBJECT_SAMPLER_VIEW);
   if (ret_handle == 0) {
      FREE(view);
      return ENOMEM;
   }
   return 0;
}

void vrend_fb_bind_texture(struct vrend_resource *res,
                           int idx,
                           uint32_t level, uint32_t layer)
{
    const struct util_format_description *desc = util_format_description(res->base.format);
    GLenum attachment = GL_COLOR_ATTACHMENT0_EXT + idx;

    if (vrend_format_is_ds(res->base.format)) { {
            if (util_format_has_stencil(desc)) {
                if (util_format_has_depth(desc))
                    attachment = GL_DEPTH_STENCIL_ATTACHMENT;
                else
                    attachment = GL_STENCIL_ATTACHMENT;
            } else
                attachment = GL_DEPTH_ATTACHMENT;
        }
    }

    switch (res->target) {
    case GL_TEXTURE_1D_ARRAY:
    case GL_TEXTURE_2D_ARRAY:
    case GL_TEXTURE_2D_MULTISAMPLE_ARRAY:
    case GL_TEXTURE_CUBE_MAP_ARRAY:
        if (layer == 0xffffffff)
            glFramebufferTexture(GL_FRAMEBUFFER_EXT, attachment,
                                 res->id, level);
        else
            glFramebufferTextureLayer(GL_FRAMEBUFFER_EXT, attachment,
                                      res->id, level, layer);
        break;
    case GL_TEXTURE_3D:
        if (layer == 0xffffffff)
            glFramebufferTexture(GL_FRAMEBUFFER_EXT, attachment,
                                 res->id, level);
        else
            glFramebufferTexture3DEXT(GL_FRAMEBUFFER_EXT, attachment,
                                      res->target, res->id, level, layer);
        break;
    case GL_TEXTURE_CUBE_MAP:
        if (layer == 0xffffffff)
            glFramebufferTexture(GL_FRAMEBUFFER_EXT, attachment,
                                 res->id, level);
        else
            glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, attachment,
                                      GL_TEXTURE_CUBE_MAP_POSITIVE_X + layer, res->id, level);
        break;
    case GL_TEXTURE_1D:
        glFramebufferTexture1DEXT(GL_FRAMEBUFFER_EXT, attachment,
                                  res->target, res->id, level);
        break;
    case GL_TEXTURE_2D:
    default:
        glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, attachment,
                                  res->target, res->id, level);
        break;
    }

    if (attachment == GL_DEPTH_ATTACHMENT)
        glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, GL_STENCIL_ATTACHMENT,
                                  0, 0, 0);
}

static void vrend_hw_set_zsurf_texture(struct vrend_context *ctx)
{
   struct vrend_resource *tex;

   if (!ctx->sub->zsurf) {
      glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, GL_DEPTH_STENCIL_ATTACHMENT,
                                GL_TEXTURE_2D, 0, 0);
      return;

   }
   tex = ctx->sub->zsurf->texture;
   if (!tex)
      return;

   vrend_fb_bind_texture(tex, 0, ctx->sub->zsurf->val0, ctx->sub->zsurf->val1 & 0xffff);
}

static void vrend_hw_set_color_surface(struct vrend_context *ctx, int index)
{
   struct vrend_resource *tex;

   if (!ctx->sub->surf[index]) {
      GLenum attachment = GL_COLOR_ATTACHMENT0 + index;

      glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, attachment,
                                GL_TEXTURE_2D, 0, 0);
   } else {
       int first_layer = ctx->sub->surf[index]->val1 & 0xffff;
       int last_layer = (ctx->sub->surf[index]->val1 >> 16) & 0xffff;
       tex = ctx->sub->surf[index]->texture;


       vrend_fb_bind_texture(tex, index, ctx->sub->surf[index]->val0,
                             first_layer != last_layer ? 0xffffffff : first_layer);
   }


}

static void vrend_hw_emit_framebuffer_state(struct vrend_context *ctx)
{
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
   glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, ctx->sub->fb_id);

   if (ctx->sub->nr_cbufs == 0)
       glReadBuffer(GL_NONE);
   glDrawBuffers(ctx->sub->nr_cbufs, buffers);
}

void vrend_set_framebuffer_state(struct vrend_context *ctx,
                                 uint32_t nr_cbufs, uint32_t surf_handle[8],
                                 uint32_t zsurf_handle)
{
   struct vrend_surface *surf, *zsurf;
   int i;
   int old_num;
   GLenum status;
   GLint new_height = -1;
   boolean new_ibf = GL_FALSE;

   glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, ctx->sub->fb_id);

   if (zsurf_handle) {
      zsurf = vrend_object_lookup(ctx->sub->object_hash, zsurf_handle, VIRGL_OBJECT_SURFACE);
      if (!zsurf) {
         report_context_error(ctx, VIRGL_ERROR_CTX_ILLEGAL_SURFACE, zsurf_handle);
         return;
      }
   } else
      zsurf = NULL;

   if (ctx->sub->zsurf != zsurf) {
      vrend_surface_reference(&ctx->sub->zsurf, zsurf);
      vrend_hw_set_zsurf_texture(ctx);
   }

   old_num = ctx->sub->nr_cbufs;
   ctx->sub->nr_cbufs = nr_cbufs;
   ctx->sub->old_nr_cbufs = old_num;

   for (i = 0; i < nr_cbufs; i++) {
      if (surf_handle[i] != 0) {
         surf = vrend_object_lookup(ctx->sub->object_hash, surf_handle[i], VIRGL_OBJECT_SURFACE);
         if (!surf) {
            report_context_error(ctx, VIRGL_ERROR_CTX_ILLEGAL_SURFACE, surf_handle[i]);
            return;
         }
      } else
         surf = NULL;

      if (ctx->sub->surf[i] != surf) {
         vrend_surface_reference(&ctx->sub->surf[i], surf);
         vrend_hw_set_color_surface(ctx, i);
      }
   }

   if (old_num > ctx->sub->nr_cbufs) {
      for (i = ctx->sub->nr_cbufs; i < old_num; i++) {
         vrend_surface_reference(&ctx->sub->surf[i], NULL);
         vrend_hw_set_color_surface(ctx, i);
      }
   }

   /* find a buffer to set fb_height from */
   if (ctx->sub->nr_cbufs == 0 && !ctx->sub->zsurf) {
       new_height = 0;
       new_ibf = FALSE;
   } else if (ctx->sub->nr_cbufs == 0) {
       new_height = u_minify(ctx->sub->zsurf->texture->base.height0, ctx->sub->zsurf->val0);
       new_ibf = ctx->sub->zsurf->texture->y_0_top ? TRUE : FALSE;
   } 
   else {
       surf = NULL;
       for (i = 0; i < ctx->sub->nr_cbufs; i++) {
           if (ctx->sub->surf[i]) {
               surf = ctx->sub->surf[i];
               break;
           }
       }
       assert(surf);
       new_height = u_minify(surf->texture->base.height0, surf->val0);
       new_ibf = surf->texture->y_0_top ? TRUE : FALSE;
   }

   if (new_height != -1) {
      if (ctx->sub->fb_height != new_height || ctx->sub->inverted_fbo_content != new_ibf) {
         ctx->sub->fb_height = new_height;
         ctx->sub->inverted_fbo_content = new_ibf;
         ctx->sub->scissor_state_dirty = TRUE;
         ctx->sub->viewport_state_dirty = TRUE;
      }
   }

   vrend_hw_emit_framebuffer_state(ctx);

   if (ctx->sub->nr_cbufs > 0 || ctx->sub->zsurf) {
      status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
      if (status != GL_FRAMEBUFFER_COMPLETE)
         fprintf(stderr,"failed to complete framebuffer 0x%x %s\n", status, ctx->debug_name);
   }
}

static void vrend_hw_emit_depth_range(struct vrend_context *ctx)
{
   glDepthRange(ctx->sub->view_near_val, ctx->sub->view_far_val);
}

/*
 * if the viewport Y scale factor is > 0 then we are rendering to
 * an FBO already so don't need to invert rendering?
 */
void vrend_set_viewport_state(struct vrend_context *ctx,
                              const struct pipe_viewport_state *state)
{
   /* convert back to glViewport */
   GLint x, y;
   GLsizei width, height;
   GLclampd near_val, far_val;
   GLboolean viewport_is_negative = (state->scale[1] < 0) ? GL_TRUE : GL_FALSE;
   GLfloat abs_s1 = fabsf(state->scale[1]);

   width = state->scale[0] * 2.0f;
   height = abs_s1 * 2.0f;
   x = state->translate[0] - state->scale[0];
   y = state->translate[1] - state->scale[1];

   near_val = state->translate[2] - state->scale[2];
   far_val = near_val + (state->scale[2] * 2.0);

   if (ctx->sub->view_cur_x != x ||
       ctx->sub->view_cur_y != y ||
       ctx->sub->view_width != width ||
       ctx->sub->view_height != height) {
      ctx->sub->viewport_state_dirty = TRUE;
      ctx->sub->view_cur_x = x;
      ctx->sub->view_cur_y = y;
      ctx->sub->view_width = width;
      ctx->sub->view_height = height;
   }

   if (ctx->sub->viewport_is_negative != viewport_is_negative)
      ctx->sub->viewport_is_negative = viewport_is_negative;

   ctx->sub->depth_scale = fabsf(far_val - near_val);
   ctx->sub->depth_transform = near_val;

   if (ctx->sub->view_near_val != near_val ||
       ctx->sub->view_far_val != far_val) {
      ctx->sub->view_near_val = near_val;
      ctx->sub->view_far_val = far_val;
      vrend_hw_emit_depth_range(ctx);
   }
}

int vrend_create_vertex_elements_state(struct vrend_context *ctx,
                                       uint32_t handle,
                                       unsigned num_elements,
                                       const struct pipe_vertex_element *elements)
{
   struct vrend_vertex_element_array *v = CALLOC_STRUCT(vrend_vertex_element_array);
   const struct util_format_description *desc;
   GLenum type;
   int i;
   uint32_t ret_handle;

   if (!v)
      return ENOMEM;

   v->count = num_elements;
   for (i = 0; i < num_elements; i++) {
      memcpy(&v->elements[i].base, &elements[i], sizeof(struct pipe_vertex_element));

      desc = util_format_description(elements[i].src_format);
      
      type = GL_FALSE;
      if (desc->channel[0].type == UTIL_FORMAT_TYPE_FLOAT) {
	 if (desc->channel[0].size == 32)
	    type = GL_FLOAT;
	 else if (desc->channel[0].size == 64)
	    type = GL_DOUBLE;
         else if (desc->channel[0].size == 16)
            type = GL_HALF_FLOAT;
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
      else if (elements[i].src_format == PIPE_FORMAT_R10G10B10A2_SSCALED ||
               elements[i].src_format == PIPE_FORMAT_R10G10B10A2_SNORM ||
               elements[i].src_format == PIPE_FORMAT_B10G10R10A2_SNORM)
         type = GL_INT_2_10_10_10_REV;
      else if (elements[i].src_format == PIPE_FORMAT_R10G10B10A2_USCALED ||
               elements[i].src_format == PIPE_FORMAT_R10G10B10A2_UNORM ||
               elements[i].src_format == PIPE_FORMAT_B10G10R10A2_UNORM)
         type = GL_UNSIGNED_INT_2_10_10_10_REV;

      if (type == GL_FALSE) {
         report_context_error(ctx, VIRGL_ERROR_CTX_ILLEGAL_VERTEX_FORMAT, elements[i].src_format);
         FREE(v);
         return EINVAL;
      }

      v->elements[i].type = type;
      if (desc->channel[0].normalized)
         v->elements[i].norm = GL_TRUE;
      if (desc->nr_channels == 4 && desc->swizzle[0] == UTIL_FORMAT_SWIZZLE_Z)
         v->elements[i].nr_chan = GL_BGRA;
      else
         v->elements[i].nr_chan = desc->nr_channels;
   }

   ret_handle = vrend_renderer_object_insert(ctx, v, sizeof(struct vrend_vertex_element), handle,
                              VIRGL_OBJECT_VERTEX_ELEMENTS);
   if (!ret_handle) {
      FREE(v);
      return ENOMEM;
   }
   return 0;
}

void vrend_bind_vertex_elements_state(struct vrend_context *ctx,
                                      uint32_t handle)
{
   struct vrend_vertex_element_array *v;

   if (!handle) {
      ctx->sub->ve = NULL;
      return;
   }
   v = vrend_object_lookup(ctx->sub->object_hash, handle, VIRGL_OBJECT_VERTEX_ELEMENTS);
   if (!v) {
      report_context_error(ctx, VIRGL_ERROR_CTX_ILLEGAL_HANDLE, handle);
      return;
   }
   ctx->sub->ve = v;
}

void vrend_set_constants(struct vrend_context *ctx,
                         uint32_t shader,
                         uint32_t index,
                         uint32_t num_constant,
                         float *data)
{
   struct vrend_constants *consts;
   int i;

   consts = &ctx->sub->consts[shader];
   ctx->sub->const_dirty[shader] = TRUE;

   consts->consts = realloc(consts->consts, num_constant * sizeof(float));
   if (!consts->consts)
      return;

   consts->num_consts = num_constant;
   for (i = 0; i < num_constant; i++)
      consts->consts[i] = ((unsigned int *)data)[i];
}

void vrend_set_uniform_buffer(struct vrend_context *ctx,
                              uint32_t shader,
                              uint32_t index,
                              uint32_t offset,
                              uint32_t length,
                              uint32_t res_handle)
{
   struct vrend_resource *res;

   if (res_handle) {
      res = vrend_renderer_ctx_res_lookup(ctx, res_handle);

      if (!res) {
            report_context_error(ctx, VIRGL_ERROR_CTX_ILLEGAL_RESOURCE, res_handle);
            return;
      }
      vrend_resource_reference((struct vrend_resource **)&ctx->sub->cbs[shader][index].buffer, res);
      ctx->sub->cbs[shader][index].buffer_offset = offset;
      ctx->sub->cbs[shader][index].buffer_size = length;

      ctx->sub->const_bufs_used_mask[shader] |= (1 << index);
   } else {
      vrend_resource_reference((struct vrend_resource **)&ctx->sub->cbs[shader][index].buffer, NULL);
      ctx->sub->cbs[shader][index].buffer_offset = 0;
      ctx->sub->cbs[shader][index].buffer_size = 0;
      ctx->sub->const_bufs_used_mask[shader] &= ~(1 << index);
   }
}
void vrend_set_index_buffer(struct vrend_context *ctx,
                            uint32_t res_handle,
                            uint32_t index_size,
                            uint32_t offset)
{
   struct vrend_resource *res;

   ctx->sub->ib.index_size = index_size;
   ctx->sub->ib.offset = offset;
   if (res_handle) {
      if (ctx->sub->index_buffer_res_id != res_handle) {
         res = vrend_renderer_ctx_res_lookup(ctx, res_handle);
         if (!res) {
            vrend_resource_reference((struct vrend_resource **)&ctx->sub->ib.buffer, NULL);
            ctx->sub->index_buffer_res_id = 0;
            report_context_error(ctx, VIRGL_ERROR_CTX_ILLEGAL_RESOURCE, res_handle);
            return;
         }
         vrend_resource_reference((struct vrend_resource **)&ctx->sub->ib.buffer, res);
         ctx->sub->index_buffer_res_id = res_handle;
      }
   } else {
      vrend_resource_reference((struct vrend_resource **)&ctx->sub->ib.buffer, NULL);
      ctx->sub->index_buffer_res_id = 0;
   }
}

void vrend_set_single_vbo(struct vrend_context *ctx,
                         int index,
                         uint32_t stride,
                         uint32_t buffer_offset,
                         uint32_t res_handle)
{
   struct vrend_resource *res;
   ctx->sub->vbo[index].stride = stride;
   ctx->sub->vbo[index].buffer_offset = buffer_offset;

   if (res_handle == 0) {
      vrend_resource_reference((struct vrend_resource **)&ctx->sub->vbo[index].buffer, NULL);
      ctx->sub->vbo_res_ids[index] = 0;
   } else if (ctx->sub->vbo_res_ids[index] != res_handle) {
      res = vrend_renderer_ctx_res_lookup(ctx, res_handle);
      if (!res) {
         report_context_error(ctx, VIRGL_ERROR_CTX_ILLEGAL_RESOURCE, res_handle);
         ctx->sub->vbo_res_ids[index] = 0;
         return;
      }
      vrend_resource_reference((struct vrend_resource **)&ctx->sub->vbo[index].buffer, res);
      ctx->sub->vbo_res_ids[index] = res_handle;
   }
}

void vrend_set_num_vbo(struct vrend_context *ctx,
                      int num_vbo)
{                                              
   int old_num = ctx->sub->num_vbos;
   int i;
   ctx->sub->num_vbos = num_vbo;

   for (i = num_vbo; i < old_num; i++) {
      vrend_resource_reference((struct vrend_resource **)&ctx->sub->vbo[i].buffer, NULL);
      ctx->sub->vbo_res_ids[i] = 0;
   }

}

void vrend_set_single_sampler_view(struct vrend_context *ctx,
                                   uint32_t shader_type,
                                   int index,
                                   uint32_t handle)
{
   struct vrend_sampler_view *view = NULL;
   struct vrend_texture *tex;

   if (handle) {
      view = vrend_object_lookup(ctx->sub->object_hash, handle, VIRGL_OBJECT_SAMPLER_VIEW);
      if (!view) {
         ctx->sub->views[shader_type].views[index] = NULL;
         report_context_error(ctx, VIRGL_ERROR_CTX_ILLEGAL_HANDLE, handle);
         return;
      }
      tex = (struct vrend_texture *)vrend_renderer_ctx_res_lookup(ctx, view->res_handle);
      if (!tex) {
         fprintf(stderr,"cannot find texture to back resource view %d %d\n", handle, view->res_handle);
         return;
      }
      if (view->texture->target != GL_TEXTURE_BUFFER) {
         tex = (struct vrend_texture *)view->texture;
         glBindTexture(view->texture->target, view->texture->id);

         if (util_format_is_depth_or_stencil(view->format)) {
            if (use_core_profile == 0) {
               /* setting depth texture mode is deprecated in core profile */
               if (view->depth_texture_mode != GL_RED) {
                  glTexParameteri(view->texture->target, GL_DEPTH_TEXTURE_MODE, GL_RED);
                  view->depth_texture_mode = GL_RED;
               }
            }
         }

         if (view->cur_base != (view->val1 & 0xff)) {
            view->cur_base = view->val1 & 0xff;
            glTexParameteri(view->texture->target, GL_TEXTURE_BASE_LEVEL, view->cur_base);
         }
         if (view->cur_max != ((view->val1 >> 8) & 0xff)) {
            view->cur_max = (view->val1 >> 8) & 0xff;
            glTexParameteri(view->texture->target, GL_TEXTURE_MAX_LEVEL, view->cur_max);
         }
         if (tex->cur_swizzle_r != view->gl_swizzle_r) {
            glTexParameteri(view->texture->target, GL_TEXTURE_SWIZZLE_R, view->gl_swizzle_r);
            tex->cur_swizzle_r = view->gl_swizzle_r;
         }
         if (tex->cur_swizzle_g != view->gl_swizzle_g) {
            glTexParameteri(view->texture->target, GL_TEXTURE_SWIZZLE_G, view->gl_swizzle_g);
            tex->cur_swizzle_g = view->gl_swizzle_g;
         }
         if (tex->cur_swizzle_b != view->gl_swizzle_b) {
            glTexParameteri(view->texture->target, GL_TEXTURE_SWIZZLE_B, view->gl_swizzle_b);
            tex->cur_swizzle_b = view->gl_swizzle_b;
         }
         if (tex->cur_swizzle_a != view->gl_swizzle_a) {
            glTexParameteri(view->texture->target, GL_TEXTURE_SWIZZLE_A, view->gl_swizzle_a);
            tex->cur_swizzle_a = view->gl_swizzle_a;
         }
         if (tex->srgb_decode != view->srgb_decode && util_format_is_srgb(tex->base.base.format)) {
            glTexParameteri(view->texture->target, GL_TEXTURE_SRGB_DECODE_EXT,
                            view->srgb_decode);
            tex->srgb_decode = view->srgb_decode;
         }
      } else {
	GLenum internalformat;
         tex = (struct vrend_texture *)view->texture;

         glBindTexture(GL_TEXTURE_BUFFER, view->texture->tbo_tex_id);
         internalformat = tex_conv_table[view->format].internalformat;
         glTexBuffer(GL_TEXTURE_BUFFER, internalformat, view->texture->id);
      }
   }

   vrend_sampler_view_reference(&ctx->sub->views[shader_type].views[index], view);
}

void vrend_set_num_sampler_views(struct vrend_context *ctx,
                                    uint32_t shader_type,
                                    uint32_t start_slot,
                                    int num_sampler_views)
{
   if (start_slot + num_sampler_views < ctx->sub->views[shader_type].num_views) {
      int i;
      for (i = start_slot + num_sampler_views; i < ctx->sub->views[shader_type].num_views; i++)
         vrend_sampler_view_reference(&ctx->sub->views[shader_type].views[i], NULL);
   }
   ctx->sub->views[shader_type].num_views = start_slot + num_sampler_views;
}

void vrend_transfer_inline_write(struct vrend_context *ctx,
                                 uint32_t res_handle,
                                 unsigned level,
                                 unsigned usage,
                                 const struct pipe_box *box,
                                 const void *data,
                                 unsigned stride,
                                 unsigned layer_stride)
{
   struct vrend_resource *res;

   res = vrend_renderer_ctx_res_lookup(ctx, res_handle);
   if (!res) {
      report_context_error(ctx, VIRGL_ERROR_CTX_ILLEGAL_RESOURCE, res_handle);
      return;
   }
   if (res->ptr) {
      memcpy(res->ptr + box->x, data, box->width);
   } else if (res->target == GL_ELEMENT_ARRAY_BUFFER_ARB ||
              res->target == GL_ARRAY_BUFFER_ARB ||
              res->target == GL_TEXTURE_BUFFER ||
              res->target == GL_UNIFORM_BUFFER ||
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


static void vrend_destroy_shader_object(void *obj_ptr)
{
   struct vrend_shader_selector *state = obj_ptr;

   vrend_shader_state_reference(&state, NULL);
}

static INLINE void vrend_fill_shader_key(struct vrend_context *ctx,
                                         struct vrend_shader_key *key)
{
   if (use_core_profile == 1) {
      int i;
      boolean add_alpha_test = true;
      for (i = 0; i < ctx->sub->nr_cbufs; i++) {
         if (!ctx->sub->surf[i])
            continue;
         if (util_format_is_pure_integer(ctx->sub->surf[i]->format))
            add_alpha_test = false;
      }
      if (add_alpha_test) {
         key->add_alpha_test = ctx->sub->dsa_state.alpha.enabled;
         key->alpha_test = ctx->sub->dsa_state.alpha.func;
         key->alpha_ref_val = ctx->sub->dsa_state.alpha.ref_value;
      }

      key->pstipple_tex = ctx->sub->rs_state.poly_stipple_enable;
      key->color_two_side = ctx->sub->rs_state.light_twoside;

      key->clip_plane_enable = ctx->sub->rs_state.clip_plane_enable;
      key->flatshade = ctx->sub->rs_state.flatshade ? TRUE : FALSE;
   } else {
      key->add_alpha_test = 0;
      key->pstipple_tex = 0;
   }
   key->invert_fs_origin = !ctx->sub->inverted_fbo_content;
   key->coord_replace = ctx->sub->rs_state.point_quad_rasterization ? ctx->sub->rs_state.sprite_coord_enable : 0;

   if (ctx->sub->gs)
      key->gs_present = true;
}

static INLINE int conv_shader_type(int type)
{
   switch (type) {
   case PIPE_SHADER_VERTEX: return GL_VERTEX_SHADER;
   case PIPE_SHADER_FRAGMENT: return GL_FRAGMENT_SHADER;
   case PIPE_SHADER_GEOMETRY: return GL_GEOMETRY_SHADER;
   };
   return 0;
}

static int vrend_shader_create(struct vrend_context *ctx,
                               struct vrend_shader *shader,
                               struct vrend_shader_key key)
{

   shader->id = glCreateShader(conv_shader_type(shader->sel->type));
   shader->compiled_fs_id = 0;
   shader->glsl_prog = vrend_convert_shader(&ctx->shader_cfg, shader->sel->tokens, &key, &shader->sel->sinfo);
   if (!shader->glsl_prog) {
      report_context_error(ctx, VIRGL_ERROR_CTX_ILLEGAL_SHADER, 0);
      glDeleteShader(shader->id);
      return -1;
   }
   shader->key = key;
   if (1) {//shader->sel->type == PIPE_SHADER_FRAGMENT || shader->sel->type == PIPE_SHADER_GEOMETRY) {
      boolean ret;

      ret = vrend_compile_shader(ctx, shader);
      if (ret == FALSE) {
         glDeleteShader(shader->id);
         free(shader->glsl_prog);
         return -1;
      }
   }
   return 0;
}

static int vrend_shader_select(struct vrend_context *ctx,
                               struct vrend_shader_selector *sel,
                               boolean *dirty)
{
   struct vrend_shader_key key;
   struct vrend_shader *shader = NULL;
   int r;

   memset(&key, 0, sizeof(key));
   vrend_fill_shader_key(ctx, &key);

   if (sel->current && !memcmp(&sel->current->key, &key, sizeof(key)))
      return 0;

   if (sel->num_shaders > 1) {
      struct vrend_shader *p = sel->current, *c = p->next_variant;
      while (c && memcmp(&c->key, &key, sizeof(key)) != 0) {
         p = c;
         c = c->next_variant;
      }
      if (c) {
         p->next_variant = c->next_variant;
         shader = c;
      }
   }

   if (!shader) {
      shader = CALLOC_STRUCT(vrend_shader);
      shader->sel = sel;

      r = vrend_shader_create(ctx, shader, key);
      if (r) {
         sel->current = NULL;
         FREE(shader);
         return r;
      }
      sel->num_shaders++;
   }
   if (dirty)
      *dirty = true;

   shader->next_variant = sel->current;
   sel->current = shader;
   return 0;
}

static void *vrend_create_shader_state(struct vrend_context *ctx,
                                       const struct pipe_shader_state *state,
                                       unsigned pipe_shader_type)
{
   struct vrend_shader_selector *sel = CALLOC_STRUCT(vrend_shader_selector);
   int r;

   if (!sel)
      return NULL;

   sel->type = pipe_shader_type;
   sel->sinfo.so_info = state->stream_output;
   sel->tokens = tgsi_dup_tokens(state->tokens);
   pipe_reference_init(&sel->reference, 1);

   r = vrend_shader_select(ctx, sel, NULL);
   if (r) {
      vrend_destroy_shader_selector(sel);
      return NULL;
   }
   return sel;
}

static inline int shader_type_to_pipe_type(int type)
{
   switch (type) {
   case VIRGL_OBJECT_GS:
      return PIPE_SHADER_GEOMETRY;
   case VIRGL_OBJECT_VS:
      return PIPE_SHADER_VERTEX;
   case VIRGL_OBJECT_FS:
      return PIPE_SHADER_FRAGMENT;
   }
   return 0;
}

int vrend_create_shader(struct vrend_context *ctx,
                        uint32_t handle, const struct pipe_shader_state *ss,
                        int type)
{
   struct vrend_shader_selector *sel;
   int ret_handle;

   sel = vrend_create_shader_state(ctx, ss, shader_type_to_pipe_type(type));
   if (sel == NULL)
      return ENOMEM;

   ret_handle = vrend_renderer_object_insert(ctx, sel, sizeof(*sel), handle, type);
   if (ret_handle == 0) {
      vrend_destroy_shader_selector(sel);
      return ENOMEM;
   }

   return 0;

}

void vrend_bind_vs(struct vrend_context *ctx,
                   uint32_t handle)
{
   struct vrend_shader_selector *sel;

   sel = vrend_object_lookup(ctx->sub->object_hash, handle, VIRGL_OBJECT_VS);

   if (ctx->sub->vs != sel)
      ctx->sub->shader_dirty = true;
   vrend_shader_state_reference(&ctx->sub->vs, sel);
}

void vrend_bind_gs(struct vrend_context *ctx,
                   uint32_t handle)
{
   struct vrend_shader_selector *sel;

   sel = vrend_object_lookup(ctx->sub->object_hash, handle, VIRGL_OBJECT_GS);

   if (ctx->sub->gs != sel)
      ctx->sub->shader_dirty = true;
   vrend_shader_state_reference(&ctx->sub->gs, sel);
}

void vrend_bind_fs(struct vrend_context *ctx,
                   uint32_t handle)
{
   struct vrend_shader_selector *sel;

   sel = vrend_object_lookup(ctx->sub->object_hash, handle, VIRGL_OBJECT_FS);

   if (ctx->sub->fs != sel)
      ctx->sub->shader_dirty = true;
   vrend_shader_state_reference(&ctx->sub->fs, sel);
}

void vrend_clear(struct vrend_context *ctx,
                 unsigned buffers,
                 const union pipe_color_union *color,
                 double depth, unsigned stencil)
{
   GLbitfield bits = 0;

   if (ctx->in_error)
      return;

   if (ctx->ctx_switch_pending)
      vrend_finish_context_switch(ctx);

   glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, ctx->sub->fb_id);

   vrend_update_frontface_state(ctx);
   if (ctx->sub->stencil_state_dirty)
      vrend_update_stencil_state(ctx);
   if (ctx->sub->scissor_state_dirty || vrend_state.scissor_dirty)
      vrend_update_scissor_state(ctx);
   if (ctx->sub->viewport_state_dirty || vrend_state.viewport_dirty)
      vrend_update_viewport_state(ctx);

   vrend_use_program(0);

   if (buffers & PIPE_CLEAR_COLOR)
      glClearColor(color->f[0], color->f[1], color->f[2], color->f[3]);

   if (buffers & PIPE_CLEAR_DEPTH) {
      /* gallium clears don't respect depth mask */
      glDepthMask(GL_TRUE);
      glClearDepth(depth);
   }

   if (buffers & PIPE_CLEAR_STENCIL)
      glClearStencil(stencil);

   if (buffers & PIPE_CLEAR_COLOR)
      bits |= GL_COLOR_BUFFER_BIT;
   if (buffers & PIPE_CLEAR_DEPTH)
      bits |= GL_DEPTH_BUFFER_BIT;
   if (buffers & PIPE_CLEAR_STENCIL)
      bits |= GL_STENCIL_BUFFER_BIT;
   glClear(bits);

   if (buffers & PIPE_CLEAR_DEPTH)
      if (!ctx->sub->dsa_state.depth.writemask)
         glDepthMask(GL_FALSE);
}

static void vrend_update_scissor_state(struct vrend_context *ctx)
{
   struct pipe_scissor_state *ss = &ctx->sub->ss;
   struct pipe_rasterizer_state *state = &ctx->sub->rs_state;
   GLint y;

   if (ctx->sub->viewport_is_negative)
      y = ss->miny;
   else
      y = ss->maxy;
   if (state->scissor)
      glEnable(GL_SCISSOR_TEST);
   else
      glDisable(GL_SCISSOR_TEST);

   glScissor(ss->minx, y, ss->maxx - ss->minx, ss->maxy - ss->miny);
   ctx->sub->scissor_state_dirty = FALSE;
   vrend_state.scissor_dirty = FALSE;
}

static void vrend_update_viewport_state(struct vrend_context *ctx)
{
   GLint cy;
   if (ctx->sub->viewport_is_negative)
      cy = ctx->sub->view_cur_y - ctx->sub->view_height;
   else
      cy = ctx->sub->view_cur_y;
   glViewport(ctx->sub->view_cur_x, cy, ctx->sub->view_width, ctx->sub->view_height);

   ctx->sub->viewport_state_dirty = FALSE;
   vrend_state.viewport_dirty = FALSE;
}

static GLenum get_gs_xfb_mode(GLenum mode)
{
   switch (mode) {
   case GL_POINTS:
      return GL_POINTS;
   case GL_LINE_STRIP:
      return GL_LINES;
   case GL_TRIANGLE_STRIP:
      return GL_TRIANGLES;
   default:
      fprintf(stderr, "illegal gs transform feedback mode %d\n", mode);
      return GL_POINTS;
   }
}

static GLenum get_xfb_mode(GLenum mode)
{
   switch (mode) {
   case GL_POINTS:
      return GL_POINTS;
   case GL_TRIANGLES:
   case GL_TRIANGLE_STRIP:
   case GL_TRIANGLE_FAN:
   case GL_QUADS:
   case GL_QUAD_STRIP:
   case GL_POLYGON:
      return GL_TRIANGLES;
   case GL_LINES:
   case GL_LINE_LOOP:
   case GL_LINE_STRIP:
      return GL_LINES;
   }
   fprintf(stderr, "failed to translate TFB %d\n", mode);
   return GL_POINTS;
}

void vrend_draw_vbo(struct vrend_context *ctx,
                    const struct pipe_draw_info *info)
{
   int i;
   int sampler_id;
   int ubo_id;
   bool new_program = FALSE;
   uint32_t shader_type;
   uint32_t num_enable;
   uint32_t enable_bitmask;
   uint32_t disable_bitmask;

   if (ctx->in_error)
      return;

   if (ctx->ctx_switch_pending)
      vrend_finish_context_switch(ctx);

   vrend_update_frontface_state(ctx);
   if (ctx->sub->stencil_state_dirty)
      vrend_update_stencil_state(ctx);
   if (ctx->sub->scissor_state_dirty || vrend_state.scissor_dirty)
      vrend_update_scissor_state(ctx);

   if (ctx->sub->viewport_state_dirty || vrend_state.viewport_dirty)
      vrend_update_viewport_state(ctx);

   vrend_patch_blend_func(ctx);

   if (ctx->sub->shader_dirty) {
     struct vrend_linked_shader_program *prog;
     boolean fs_dirty, vs_dirty, gs_dirty;
     boolean dual_src = util_blend_state_is_dual(&ctx->sub->blend_state, 0);
     if (!ctx->sub->vs || !ctx->sub->fs) {
        fprintf(stderr,"dropping rendering due to missing shaders\n");
        return;
     }

     vrend_shader_select(ctx, ctx->sub->fs, &fs_dirty);
     vrend_shader_select(ctx, ctx->sub->vs, &vs_dirty);
     if (ctx->sub->gs)
        vrend_shader_select(ctx, ctx->sub->gs, &gs_dirty);

     if (!ctx->sub->vs->current || !ctx->sub->fs->current) {
        fprintf(stderr, "failure to compile shader variants\n");
        return;
     }
     prog = lookup_shader_program(ctx, ctx->sub->vs->current->id, ctx->sub->fs->current->id, ctx->sub->gs ? ctx->sub->gs->current->id : 0, dual_src);
     if (!prog) {
        prog = add_shader_program(ctx, ctx->sub->vs->current, ctx->sub->fs->current, ctx->sub->gs ? ctx->sub->gs->current : NULL);
        if (!prog)
           return;
     }
     if (ctx->sub->prog != prog) {
       new_program = TRUE;
       ctx->sub->prog = prog;
     }
   }

   glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, ctx->sub->fb_id);
   
   vrend_use_program(ctx->sub->prog->id);

   for (shader_type = PIPE_SHADER_VERTEX; shader_type <= (ctx->sub->gs ? PIPE_SHADER_GEOMETRY : PIPE_SHADER_FRAGMENT); shader_type++) {
      if (ctx->sub->prog->const_locs[shader_type] && (ctx->sub->const_dirty[shader_type] || new_program)) {
	 int nc;
         if (shader_type == PIPE_SHADER_VERTEX) {
	    nc = ctx->sub->vs->sinfo.num_consts;
         } else if (shader_type == PIPE_SHADER_GEOMETRY) {
	    nc = ctx->sub->gs->sinfo.num_consts;
         } else if (shader_type == PIPE_SHADER_FRAGMENT) {
	    nc = ctx->sub->fs->sinfo.num_consts;
         }
         for (i = 0; i < nc; i++) {
            if (ctx->sub->prog->const_locs[shader_type][i] != -1 && ctx->sub->consts[shader_type].consts)
               glUniform4uiv(ctx->sub->prog->const_locs[shader_type][i], 1, &ctx->sub->consts[shader_type].consts[i * 4]);
         }
         ctx->sub->const_dirty[shader_type] = FALSE;
      }
   }

   sampler_id = 0;
   for (shader_type = PIPE_SHADER_VERTEX; shader_type <= (ctx->sub->gs ? PIPE_SHADER_GEOMETRY : PIPE_SHADER_FRAGMENT); shader_type++) {
      int index = 0;
      for (i = 0; i < ctx->sub->views[shader_type].num_views; i++) {
         struct vrend_resource *texture = NULL;

         if (ctx->sub->views[shader_type].views[i]) {
            texture = ctx->sub->views[shader_type].views[i]->texture;
         }

         if (!(ctx->sub->prog->samplers_used_mask[shader_type] & (1 << i)))
             continue;

         if (ctx->sub->prog->samp_locs[shader_type])
            glUniform1i(ctx->sub->prog->samp_locs[shader_type][index], sampler_id);

         if (ctx->sub->prog->shadow_samp_mask[shader_type] & (1 << i)) {
            struct vrend_sampler_view *tview = ctx->sub->views[shader_type].views[i];
            glUniform4f(ctx->sub->prog->shadow_samp_mask_locs[shader_type][index], 
                        tview->gl_swizzle_r == GL_ZERO ? 0.0 : 1.0, 
                        tview->gl_swizzle_g == GL_ZERO ? 0.0 : 1.0, 
                        tview->gl_swizzle_b == GL_ZERO ? 0.0 : 1.0, 
                        tview->gl_swizzle_a == GL_ZERO ? 0.0 : 1.0);
            glUniform4f(ctx->sub->prog->shadow_samp_add_locs[shader_type][index], 
                        tview->gl_swizzle_r == GL_ONE ? 1.0 : 0.0, 
                        tview->gl_swizzle_g == GL_ONE ? 1.0 : 0.0, 
                        tview->gl_swizzle_b == GL_ONE ? 1.0 : 0.0, 
                        tview->gl_swizzle_a == GL_ONE ? 1.0 : 0.0);
         }
            
         glActiveTexture(GL_TEXTURE0 + sampler_id);
         if (texture) {
            int id;

            if (texture->target == GL_TEXTURE_BUFFER)
               id = texture->tbo_tex_id;
            else
               id = texture->id;

            glBindTexture(texture->target, id);
            if (ctx->sub->views[shader_type].old_ids[i] != id || ctx->sub->sampler_state_dirty) {
               vrend_apply_sampler_state(ctx, texture, shader_type, i, ctx->sub->views[shader_type].views[i]->srgb_decode);
               ctx->sub->views[shader_type].old_ids[i] = id;
            }
            if (ctx->sub->rs_state.point_quad_rasterization) {
               if (use_core_profile == 0) {
                  if (ctx->sub->rs_state.sprite_coord_enable & (1 << i))
                     glTexEnvi(GL_POINT_SPRITE_ARB, GL_COORD_REPLACE_ARB, GL_TRUE);
                  else
                     glTexEnvi(GL_POINT_SPRITE_ARB, GL_COORD_REPLACE_ARB, GL_FALSE);
               }
            }
            sampler_id++;
         }
         index++;
      }
   } 

   ubo_id = 0;
   for (shader_type = PIPE_SHADER_VERTEX; shader_type <= (ctx->sub->gs ? PIPE_SHADER_GEOMETRY : PIPE_SHADER_FRAGMENT); shader_type++) {
      uint32_t mask;
      int shader_ubo_idx = 0;
      struct pipe_constant_buffer *cb;
      struct vrend_resource *res;
      if (!ctx->sub->const_bufs_used_mask[shader_type])
         continue;

      if (!ctx->sub->prog->ubo_locs[shader_type])
         continue;

      mask = ctx->sub->const_bufs_used_mask[shader_type];
      while (mask) {
         i = u_bit_scan(&mask);

         cb = &ctx->sub->cbs[shader_type][i];
         res = (struct vrend_resource *)cb->buffer;
         glBindBufferRange(GL_UNIFORM_BUFFER, ubo_id, res->id,
                           cb->buffer_offset, cb->buffer_size);
         glUniformBlockBinding(ctx->sub->prog->id, ctx->sub->prog->ubo_locs[shader_type][shader_ubo_idx], ubo_id);
         shader_ubo_idx++;
         ubo_id++;
      }
   }

   ctx->sub->sampler_state_dirty = FALSE;

   if (!ctx->sub->ve) {
      fprintf(stderr,"illegal VE setup - skipping renderering\n");
      return;
   }
   glUniform4f(ctx->sub->prog->vs_ws_adjust_loc, 0.0, ctx->sub->viewport_is_negative ? -1.0 : 1.0, ctx->sub->depth_scale, ctx->sub->depth_transform);

   if (use_core_profile && ctx->sub->prog->fs_stipple_loc != -1) {
      glActiveTexture(GL_TEXTURE0 + sampler_id);
      glBindTexture(GL_TEXTURE_2D, ctx->pstipple_tex_id);
      glUniform1i(ctx->sub->prog->fs_stipple_loc, sampler_id);
   }
   num_enable = ctx->sub->ve->count;
   enable_bitmask = 0;
   disable_bitmask = ~((1ull << num_enable) - 1);
   for (i = 0; i < ctx->sub->ve->count; i++) {
      struct vrend_vertex_element *ve = &ctx->sub->ve->elements[i];
      int vbo_index = ctx->sub->ve->elements[i].base.vertex_buffer_index;
      struct vrend_buffer *buf;
      GLint loc;

      if (i >= ctx->sub->prog->ss[PIPE_SHADER_VERTEX]->sel->sinfo.num_inputs) {
         /* XYZZY: debug this? */
         num_enable = ctx->sub->prog->ss[PIPE_SHADER_VERTEX]->sel->sinfo.num_inputs;
         break;
      }
      buf = (struct vrend_buffer *)ctx->sub->vbo[vbo_index].buffer;

      if (!buf) {
           fprintf(stderr,"cannot find vbo buf %d %d %d\n", i, ctx->sub->ve->count, ctx->sub->prog->ss[PIPE_SHADER_VERTEX]->sel->sinfo.num_inputs);
           continue;
      }

      if (vrend_shader_use_explicit) {
         loc = i;
      } else {
	if (ctx->sub->prog->attrib_locs) {
	  loc = ctx->sub->prog->attrib_locs[i];
	} else loc = -1;

	if (loc == -1) {
           fprintf(stderr,"cannot find loc %d %d %d\n", i, ctx->sub->ve->count, ctx->sub->prog->ss[PIPE_SHADER_VERTEX]->sel->sinfo.num_inputs);
          num_enable--;
          if (i == 0) {
             fprintf(stderr,"shader probably didn't compile - skipping rendering\n");
             return;
          }
          continue;
        }
      }

      if (ve->type == GL_FALSE) {
	fprintf(stderr,"failed to translate vertex type - skipping render\n");
	return;
      }

      glBindBuffer(GL_ARRAY_BUFFER, buf->base.id);

      if (ctx->sub->vbo[vbo_index].stride == 0) {
         void *data;
         /* for 0 stride we are kinda screwed */
         data = glMapBufferRange(GL_ARRAY_BUFFER, ctx->sub->vbo[vbo_index].buffer_offset, ve->nr_chan * sizeof(GLfloat), GL_MAP_READ_BIT);
         
         switch (ve->nr_chan) {
         case 1:
            glVertexAttrib1fv(loc, data);
            break;
         case 2:
            glVertexAttrib2fv(loc, data);
            break;
         case 3:
            glVertexAttrib3fv(loc, data);
            break;
         case 4:
         default:
            glVertexAttrib4fv(loc, data);
            break;
         }
         glUnmapBuffer(GL_ARRAY_BUFFER);
         disable_bitmask |= (1 << loc);
      } else {
         enable_bitmask |= (1 << loc);
         if (util_format_is_pure_integer(ve->base.src_format)) {
            glVertexAttribIPointer(loc, ve->nr_chan, ve->type, ctx->sub->vbo[vbo_index].stride, (void *)(unsigned long)(ve->base.src_offset + ctx->sub->vbo[vbo_index].buffer_offset));
         } else {
            glVertexAttribPointer(loc, ve->nr_chan, ve->type, ve->norm, ctx->sub->vbo[vbo_index].stride, (void *)(unsigned long)(ve->base.src_offset + ctx->sub->vbo[vbo_index].buffer_offset));
         }
         glVertexAttribDivisorARB(loc, ve->base.instance_divisor);
      }
   }

   if (ctx->sub->rs_state.clip_plane_enable) {
      for (i = 0 ; i < 8; i++) {
         glUniform4fv(ctx->sub->prog->clip_locs[i], 1, (const GLfloat *)&ctx->sub->ucp_state.ucp[i]);
      }
   }
   if (ctx->sub->enabled_attribs_bitmask != enable_bitmask) {
      uint32_t mask = ctx->sub->enabled_attribs_bitmask & disable_bitmask;

      while (mask) {
         i = u_bit_scan(&mask);
         glDisableVertexAttribArray(i);
      }
      ctx->sub->enabled_attribs_bitmask &= ~disable_bitmask;

      mask = ctx->sub->enabled_attribs_bitmask ^ enable_bitmask;
      while (mask) {
         i = u_bit_scan(&mask);
         glEnableVertexAttribArray(i);
      }

      ctx->sub->enabled_attribs_bitmask = enable_bitmask;
   }

   if (info->indexed) {
      struct vrend_resource *res = (struct vrend_resource *)ctx->sub->ib.buffer;
      glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, res->id);
   } else
      glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);

//   vrend_ctx_restart_queries(ctx);

   if (ctx->sub->num_so_targets) {
      if (ctx->sub->gs)
         glBeginTransformFeedback(get_gs_xfb_mode(ctx->sub->gs->sinfo.gs_out_prim));
      else
         glBeginTransformFeedback(get_xfb_mode(info->mode));
   }

   if (info->primitive_restart) {
      if (vrend_state.have_nv_prim_restart) {
         glEnableClientState(GL_PRIMITIVE_RESTART_NV);
         glPrimitiveRestartIndexNV(info->restart_index);
      } else {
         glEnable(GL_PRIMITIVE_RESTART);
         glPrimitiveRestartIndex(info->restart_index);
      }
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
      switch (ctx->sub->ib.index_size) {
      case 1:
         elsz = GL_UNSIGNED_BYTE;
         break;
      case 2: 
         elsz = GL_UNSIGNED_SHORT;
         break;
      case 4:
      default:
         elsz = GL_UNSIGNED_INT;
         break;
      }

      if (info->index_bias) {
         if (info->min_index != 0 || info->max_index != -1)
            glDrawRangeElementsBaseVertex(mode, info->min_index, info->max_index, info->count, elsz, (void *)(unsigned long)ctx->sub->ib.offset, info->index_bias);
         else
            glDrawElementsBaseVertex(mode, info->count, elsz, (void *)(unsigned long)ctx->sub->ib.offset, info->index_bias);
      } else if (info->min_index != 0 || info->max_index != -1)
         glDrawRangeElements(mode, info->min_index, info->max_index, info->count, elsz, (void *)(unsigned long)ctx->sub->ib.offset);                  
      else if (info->instance_count > 1)
         glDrawElementsInstancedARB(mode, info->count, elsz, (void *)(unsigned long)ctx->sub->ib.offset, info->instance_count);
      else
         glDrawElements(mode, info->count, elsz, (void *)(unsigned long)ctx->sub->ib.offset);
   }

   if (ctx->sub->num_so_targets)
      glEndTransformFeedback();

   if (info->primitive_restart) {
      if (vrend_state.have_nv_prim_restart)
         glDisableClientState(GL_PRIMITIVE_RESTART_NV);
      else if (vrend_state.have_gl_prim_restart)
         glDisable(GL_PRIMITIVE_RESTART);
   }
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

static INLINE boolean is_dst_blend(int blend_factor)
{
   return (blend_factor == PIPE_BLENDFACTOR_DST_ALPHA ||
           blend_factor == PIPE_BLENDFACTOR_INV_DST_ALPHA);
}

static INLINE int conv_dst_blend(int blend_factor)
{
   if (blend_factor == PIPE_BLENDFACTOR_DST_ALPHA)
      return PIPE_BLENDFACTOR_ONE;
   if (blend_factor == PIPE_BLENDFACTOR_INV_DST_ALPHA)
      return PIPE_BLENDFACTOR_ZERO;
   return blend_factor;
}

static void vrend_patch_blend_func(struct vrend_context *ctx)
{
   struct pipe_blend_state *state = &ctx->sub->blend_state;
   int i;
   int rsf, rdf, asf, adf;
   if (ctx->sub->nr_cbufs == 0)
      return;

   for (i = 0; i < ctx->sub->nr_cbufs; i++) {
      if (!ctx->sub->surf[i])
         continue;
      if (!util_format_has_alpha(ctx->sub->surf[i]->format))
         break;
   }

   if (i == ctx->sub->nr_cbufs)
      return;

   if (state->independent_blend_enable) {
      /* ARB_draw_buffers_blend is required for this */
      for (i = 0; i < PIPE_MAX_COLOR_BUFS; i++) {
         if (state->rt[i].blend_enable) {
            if (!(is_dst_blend(state->rt[i].rgb_src_factor) ||
                  is_dst_blend(state->rt[i].rgb_dst_factor) ||
                  is_dst_blend(state->rt[i].alpha_src_factor) ||
                  is_dst_blend(state->rt[i].alpha_dst_factor)))
               continue;
            
            rsf = translate_blend_factor(conv_dst_blend(state->rt[i].rgb_src_factor));
            rdf = translate_blend_factor(conv_dst_blend(state->rt[i].rgb_dst_factor));
            asf = translate_blend_factor(conv_dst_blend(state->rt[i].alpha_src_factor));
            adf = translate_blend_factor(conv_dst_blend(state->rt[i].alpha_dst_factor));
               
            glBlendFuncSeparateiARB(i, rsf, rdf, asf, adf);
         }
      }
   } else {
      if (state->rt[0].blend_enable) {
            if (!(is_dst_blend(state->rt[0].rgb_src_factor) ||
                  is_dst_blend(state->rt[0].rgb_dst_factor) ||
                  is_dst_blend(state->rt[0].alpha_src_factor) ||
                  is_dst_blend(state->rt[0].alpha_dst_factor)))
               return;

            rsf = translate_blend_factor(conv_dst_blend(state->rt[i].rgb_src_factor));
            rdf = translate_blend_factor(conv_dst_blend(state->rt[i].rgb_dst_factor));
            asf = translate_blend_factor(conv_dst_blend(state->rt[i].alpha_src_factor));
            adf = translate_blend_factor(conv_dst_blend(state->rt[i].alpha_dst_factor));

            glBlendFuncSeparate(rsf, rdf, asf, adf);
      }
   }
}

static void vrend_hw_emit_blend(struct vrend_context *ctx)
{
   struct pipe_blend_state *state = &ctx->sub->blend_state;

   if (state->logicop_enable != vrend_state.hw_blend_state.logicop_enable) {
      vrend_state.hw_blend_state.logicop_enable = state->logicop_enable;
      if (state->logicop_enable) {
         glEnable(GL_COLOR_LOGIC_OP);
         glLogicOp(translate_logicop(state->logicop_func));
      } else
         glDisable(GL_COLOR_LOGIC_OP);
   }

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

         if (state->rt[i].colormask != vrend_state.hw_blend_state.rt[i].colormask) {
            vrend_state.hw_blend_state.rt[i].colormask = state->rt[i].colormask;
            glColorMaskIndexedEXT(i, state->rt[i].colormask & PIPE_MASK_R ? GL_TRUE : GL_FALSE,
                                  state->rt[i].colormask & PIPE_MASK_G ? GL_TRUE : GL_FALSE,
                                  state->rt[i].colormask & PIPE_MASK_B ? GL_TRUE : GL_FALSE,
                                  state->rt[i].colormask & PIPE_MASK_A ? GL_TRUE : GL_FALSE);
         }
      }
   } else {
      if (state->rt[0].blend_enable) {
         glBlendFuncSeparate(translate_blend_factor(state->rt[0].rgb_src_factor),
                             translate_blend_factor(state->rt[0].rgb_dst_factor),
                             translate_blend_factor(state->rt[0].alpha_src_factor),
                             translate_blend_factor(state->rt[0].alpha_dst_factor));
         glBlendEquationSeparate(translate_blend_func(state->rt[0].rgb_func),
                                 translate_blend_func(state->rt[0].alpha_func));
         vrend_blend_enable(GL_TRUE);
      } 
      else
         vrend_blend_enable(GL_FALSE);

      if (state->rt[0].colormask != vrend_state.hw_blend_state.rt[0].colormask) {
         int i;
         for (i = 0; i < PIPE_MAX_COLOR_BUFS; i++)
            vrend_state.hw_blend_state.rt[i].colormask = state->rt[i].colormask;
         glColorMask(state->rt[0].colormask & PIPE_MASK_R ? GL_TRUE : GL_FALSE,
                     state->rt[0].colormask & PIPE_MASK_G ? GL_TRUE : GL_FALSE,
                     state->rt[0].colormask & PIPE_MASK_B ? GL_TRUE : GL_FALSE,
                     state->rt[0].colormask & PIPE_MASK_A ? GL_TRUE : GL_FALSE);
      }
   }

   if (vrend_state.have_multisample) {
      if (state->alpha_to_coverage)
         glEnable(GL_SAMPLE_ALPHA_TO_COVERAGE);
      else
         glDisable(GL_SAMPLE_ALPHA_TO_COVERAGE);
      
      if (state->alpha_to_one)
         glEnable(GL_SAMPLE_ALPHA_TO_ONE);
      else
         glDisable(GL_SAMPLE_ALPHA_TO_ONE);
   }
}

void vrend_object_bind_blend(struct vrend_context *ctx,
                             uint32_t handle)
{
   struct pipe_blend_state *state;

   if (handle == 0) {
      memset(&ctx->sub->blend_state, 0, sizeof(ctx->sub->blend_state));
      vrend_blend_enable(GL_FALSE);
      return;
   }
   state = vrend_object_lookup(ctx->sub->object_hash, handle, VIRGL_OBJECT_BLEND);
   if (!state) {
      report_context_error(ctx, VIRGL_ERROR_CTX_ILLEGAL_HANDLE, handle);
      return;
   }

   ctx->sub->blend_state = *state;

   vrend_hw_emit_blend(ctx);
}

static void vrend_hw_emit_dsa(struct vrend_context *ctx)
{
   struct pipe_depth_stencil_alpha_state *state = &ctx->sub->dsa_state;

   if (state->depth.enabled) {
      vrend_depth_test_enable(GL_TRUE);
      glDepthFunc(GL_NEVER + state->depth.func);
      if (state->depth.writemask)
         glDepthMask(GL_TRUE);
      else
         glDepthMask(GL_FALSE);
   } else
      vrend_depth_test_enable(GL_FALSE);
 
   if (state->alpha.enabled) {
      vrend_alpha_test_enable(ctx, GL_TRUE);
      if (!use_core_profile)
         glAlphaFunc(GL_NEVER + state->alpha.func, state->alpha.ref_value);
   } else
      vrend_alpha_test_enable(ctx, GL_FALSE);


}
void vrend_object_bind_dsa(struct vrend_context *ctx,
                           uint32_t handle)
{
   struct pipe_depth_stencil_alpha_state *state;

   if (handle == 0) {
      memset(&ctx->sub->dsa_state, 0, sizeof(ctx->sub->dsa_state));
      ctx->sub->dsa = NULL;
      ctx->sub->stencil_state_dirty = TRUE;
      ctx->sub->shader_dirty = TRUE;
      vrend_hw_emit_dsa(ctx);
      return;
   }

   state = vrend_object_lookup(ctx->sub->object_hash, handle, VIRGL_OBJECT_DSA);
   if (!state) {
      report_context_error(ctx, VIRGL_ERROR_CTX_ILLEGAL_HANDLE, handle);
      return;
   }

   if (ctx->sub->dsa != state) {
      ctx->sub->stencil_state_dirty = TRUE;
      ctx->sub->shader_dirty = TRUE;
   }
   ctx->sub->dsa_state = *state;
   ctx->sub->dsa = state;

   vrend_hw_emit_dsa(ctx);
}

static void vrend_update_frontface_state(struct vrend_context *ctx)
{
   struct pipe_rasterizer_state *state = &ctx->sub->rs_state;
   int front_ccw = state->front_ccw;

   front_ccw ^= (ctx->sub->inverted_fbo_content ? 0 : 1);
   if (front_ccw)
      glFrontFace(GL_CCW);
   else
      glFrontFace(GL_CW);
}

void vrend_update_stencil_state(struct vrend_context *ctx)
{
   struct pipe_depth_stencil_alpha_state *state = ctx->sub->dsa;
   int i;
   if (!state)
      return;

   if (!state->stencil[1].enabled) {
      if (state->stencil[0].enabled) {
         vrend_stencil_test_enable(GL_TRUE);

         glStencilOp(translate_stencil_op(state->stencil[0].fail_op), 
                     translate_stencil_op(state->stencil[0].zfail_op),
                     translate_stencil_op(state->stencil[0].zpass_op));

         glStencilFunc(GL_NEVER + state->stencil[0].func,
                       ctx->sub->stencil_refs[0],
                       state->stencil[0].valuemask);
         glStencilMask(state->stencil[0].writemask);
      } else
         vrend_stencil_test_enable(GL_FALSE);
   } else {
      vrend_stencil_test_enable(GL_TRUE);

      for (i = 0; i < 2; i++) {
         GLenum face = (i == 1) ? GL_BACK : GL_FRONT;
         glStencilOpSeparate(face,
                             translate_stencil_op(state->stencil[i].fail_op),
                             translate_stencil_op(state->stencil[i].zfail_op),
                             translate_stencil_op(state->stencil[i].zpass_op));

         glStencilFuncSeparate(face, GL_NEVER + state->stencil[i].func,
                               ctx->sub->stencil_refs[i],
                               state->stencil[i].valuemask);
         glStencilMaskSeparate(face, state->stencil[i].writemask);
      }
   }
   ctx->sub->stencil_state_dirty = FALSE;
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

static void vrend_hw_emit_rs(struct vrend_context *ctx)
{
   struct pipe_rasterizer_state *state = &ctx->sub->rs_state;
   int i;

   if (state->depth_clip) {
      glDisable(GL_DEPTH_CLAMP);
   } else {
      glEnable(GL_DEPTH_CLAMP);
   }

   if (state->point_size_per_vertex) {
      glEnable(GL_PROGRAM_POINT_SIZE);
   } else {
      glDisable(GL_PROGRAM_POINT_SIZE);
      if (state->point_size)
          glPointSize(state->point_size);
   }

   if (state->rasterizer_discard != vrend_state.hw_rs_state.rasterizer_discard) {
      vrend_state.hw_rs_state.rasterizer_discard = state->rasterizer_discard;
      if (state->rasterizer_discard)
         glEnable(GL_RASTERIZER_DISCARD);
      else
         glDisable(GL_RASTERIZER_DISCARD);
   }

   if (use_core_profile == 0) {
      glPolygonMode(GL_FRONT, translate_fill(state->fill_front));
      glPolygonMode(GL_BACK, translate_fill(state->fill_back));
   } else if (state->fill_front == state->fill_back) {
      glPolygonMode(GL_FRONT_AND_BACK, translate_fill(state->fill_front));
   } else
      report_core_warn(ctx, CORE_PROFILE_WARN_POLYGON_MODE, 0);

   if (state->offset_tri)
      glEnable(GL_POLYGON_OFFSET_FILL);
   else
      glDisable(GL_POLYGON_OFFSET_FILL);

   if (state->offset_line)
      glEnable(GL_POLYGON_OFFSET_LINE);
   else
      glDisable(GL_POLYGON_OFFSET_LINE);

   if (state->offset_point)
      glEnable(GL_POLYGON_OFFSET_POINT);
   else
      glDisable(GL_POLYGON_OFFSET_POINT);


   if (state->flatshade != vrend_state.hw_rs_state.flatshade) {
      vrend_state.hw_rs_state.flatshade = state->flatshade;
      if (use_core_profile == 0) {
         if (state->flatshade) {
            glShadeModel(GL_FLAT);
         } else {
            glShadeModel(GL_SMOOTH);
         }
      }
   }

   if (state->flatshade_first != vrend_state.hw_rs_state.flatshade_first) {
      vrend_state.hw_rs_state.flatshade_first = state->flatshade_first;
      if (state->flatshade_first)
         glProvokingVertexEXT(GL_FIRST_VERTEX_CONVENTION_EXT);
      else
         glProvokingVertexEXT(GL_LAST_VERTEX_CONVENTION_EXT);
   }
   glPolygonOffset(state->offset_scale, state->offset_units);

   if (use_core_profile == 0) {
      if (state->poly_stipple_enable)
         glEnable(GL_POLYGON_STIPPLE);
      else
         glDisable(GL_POLYGON_STIPPLE);
   } else if (state->poly_stipple_enable) {
      if (!ctx->pstip_inited)
         vrend_init_pstipple_texture(ctx);
   }

   if (state->point_quad_rasterization) {
      if (use_core_profile == 0)
         glEnable(GL_POINT_SPRITE);

      glPointParameteri(GL_POINT_SPRITE_COORD_ORIGIN, state->sprite_coord_mode ? GL_UPPER_LEFT : GL_LOWER_LEFT);
   } else {
      if (use_core_profile == 0)
         glDisable(GL_POINT_SPRITE);
   }
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

   /* two sided lighting handled in shader for core profile */
   if (use_core_profile == 0) {
      if (state->light_twoside)
         glEnable(GL_VERTEX_PROGRAM_TWO_SIDE);
      else
         glDisable(GL_VERTEX_PROGRAM_TWO_SIDE);
   }

   if (state->clip_plane_enable != vrend_state.hw_rs_state.clip_plane_enable) {
      vrend_state.hw_rs_state.clip_plane_enable = state->clip_plane_enable;
      for (i = 0; i < 8; i++) {
         if (state->clip_plane_enable & (1 << i))
            glEnable(GL_CLIP_PLANE0 + i);
         else
            glDisable(GL_CLIP_PLANE0 + i);
      }
   }
   if (use_core_profile == 0) {
      glLineStipple(state->line_stipple_factor, state->line_stipple_pattern);
      if (state->line_stipple_enable)
         glEnable(GL_LINE_STIPPLE);
      else
         glDisable(GL_LINE_STIPPLE);
   } else if (state->line_stipple_enable)
         report_core_warn(ctx, CORE_PROFILE_WARN_STIPPLE, 0);

   if (state->line_smooth)
      glEnable(GL_LINE_SMOOTH);
   else
      glDisable(GL_LINE_SMOOTH);

   if (state->poly_smooth)
      glEnable(GL_POLYGON_SMOOTH);
   else
      glDisable(GL_POLYGON_SMOOTH);

   if (use_core_profile == 0) {
      if (state->clamp_vertex_color)
         glClampColor(GL_CLAMP_VERTEX_COLOR_ARB, GL_TRUE);
      else
         glClampColor(GL_CLAMP_VERTEX_COLOR_ARB, GL_FALSE);

      if (state->clamp_fragment_color)
         glClampColor(GL_CLAMP_FRAGMENT_COLOR_ARB, GL_TRUE);
      else
         glClampColor(GL_CLAMP_FRAGMENT_COLOR_ARB, GL_FALSE);
   } else {
      if (state->clamp_vertex_color || state->clamp_fragment_color)
         report_core_warn(ctx, CORE_PROFILE_WARN_CLAMP, 0);
   }

   if (vrend_state.have_multisample) {
      if (state->multisample) {
         glEnable(GL_MULTISAMPLE);
         glEnable(GL_SAMPLE_MASK);
      } else {
         glDisable(GL_MULTISAMPLE);
         glDisable(GL_SAMPLE_MASK);
      }
   }
}

void vrend_object_bind_rasterizer(struct vrend_context *ctx,
                                  uint32_t handle)
{
   struct pipe_rasterizer_state *state;

   if (handle == 0) {
      memset(&ctx->sub->rs_state, 0, sizeof(ctx->sub->rs_state));
      return;
   }

   state = vrend_object_lookup(ctx->sub->object_hash, handle, VIRGL_OBJECT_RASTERIZER);
   
   if (!state) {
      report_context_error(ctx, VIRGL_ERROR_CTX_ILLEGAL_HANDLE, handle);
      return;
   }

   ctx->sub->rs_state = *state;
   ctx->sub->scissor_state_dirty = TRUE;
   ctx->sub->shader_dirty = TRUE;
   vrend_hw_emit_rs(ctx);
}

void vrend_bind_sampler_states(struct vrend_context *ctx,
                               uint32_t shader_type,
                               uint32_t start_slot,
                               uint32_t num_states,
                               uint32_t *handles)
{
   int i;
   struct vrend_sampler_state *state;

   ctx->sub->num_sampler_states[shader_type] = num_states;

   for (i = 0; i < num_states; i++) {
      if (handles[i] == 0)
         state = NULL;
      else
         state = vrend_object_lookup(ctx->sub->object_hash, handles[i], VIRGL_OBJECT_SAMPLER_STATE);
      
      ctx->sub->sampler_state[shader_type][i + start_slot] = state;
   }
   ctx->sub->sampler_state_dirty = TRUE;
}



static void vrend_apply_sampler_state(struct vrend_context *ctx, 
                                      struct vrend_resource *res,
                                      uint32_t shader_type,
                                      int id,
                                      uint32_t srgb_decode)
{
   struct vrend_texture *tex = (struct vrend_texture *)res;
   struct vrend_sampler_state *vstate = ctx->sub->sampler_state[shader_type][id];
   struct pipe_sampler_state *state = &vstate->base;
   bool set_all = FALSE;
   GLenum target = tex->base.target;

   if (!state) {
      fprintf(stderr, "cannot find sampler state for %d %d\n", shader_type, id);
      return;
   }
   if (res->base.nr_samples > 1) {
      tex->state = *state;
      return;
   }

   if (target == GL_TEXTURE_BUFFER) {
      tex->state = *state;
      return;
   }

   if (vrend_state.have_samplers) {
      glBindSampler(id, vstate->id);
      glSamplerParameteri(vstate->id, GL_TEXTURE_SRGB_DECODE_EXT,
                      srgb_decode);
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
   if (tex->state.mag_img_filter != state->mag_img_filter || set_all)
      glTexParameterf(target, GL_TEXTURE_MAG_FILTER, convert_mag_filter(state->mag_img_filter));
   if (res->target != GL_TEXTURE_RECTANGLE) {
      if (tex->state.min_lod != state->min_lod || set_all)
         glTexParameterf(target, GL_TEXTURE_MIN_LOD, state->min_lod);
      if (tex->state.max_lod != state->max_lod || set_all)
         glTexParameterf(target, GL_TEXTURE_MAX_LOD, state->max_lod);
      if (tex->state.lod_bias != state->lod_bias || set_all)
         glTexParameterf(target, GL_TEXTURE_LOD_BIAS, state->lod_bias);
   }

   if (tex->state.compare_mode != state->compare_mode || set_all)
      glTexParameteri(target, GL_TEXTURE_COMPARE_MODE, state->compare_mode ? GL_COMPARE_R_TO_TEXTURE : GL_NONE);
   if (tex->state.compare_func != state->compare_func || set_all)
      glTexParameteri(target, GL_TEXTURE_COMPARE_FUNC, GL_NEVER + state->compare_func);

   if (memcmp(&tex->state.border_color, &state->border_color, 16) || set_all)
      glTexParameterIuiv(target, GL_TEXTURE_BORDER_COLOR, state->border_color.ui);
   tex->state = *state;
}

void vrend_flush(struct vrend_context *ctx)
{
   glFlush();
}

void vrend_flush_frontbuffer(uint32_t res_handle)
{
}

static GLenum tgsitargettogltarget(const enum pipe_texture_target target, int nr_samples)
{
   switch(target) {
   case PIPE_TEXTURE_1D:
      return GL_TEXTURE_1D;
   case PIPE_TEXTURE_2D:
      return (nr_samples > 1) ? GL_TEXTURE_2D_MULTISAMPLE : GL_TEXTURE_2D;
   case PIPE_TEXTURE_3D:
      return GL_TEXTURE_3D;
   case PIPE_TEXTURE_RECT:
      return GL_TEXTURE_RECTANGLE_NV;
   case PIPE_TEXTURE_CUBE:
      return GL_TEXTURE_CUBE_MAP;

   case PIPE_TEXTURE_1D_ARRAY:
      return GL_TEXTURE_1D_ARRAY;
   case PIPE_TEXTURE_2D_ARRAY:
      return (nr_samples > 1) ? GL_TEXTURE_2D_MULTISAMPLE_ARRAY : GL_TEXTURE_2D_ARRAY;
   case PIPE_TEXTURE_CUBE_ARRAY:
      return GL_TEXTURE_CUBE_MAP_ARRAY;
   case PIPE_BUFFER:
   default:
      return PIPE_BUFFER;
   }
   return PIPE_BUFFER;
}

static int inited;

#define glewIsSupported epoxy_has_gl_extension

void vrend_renderer_init(struct vrend_if_cbs *cbs)
{
   int gl_ver;
   virgl_gl_context gl_context;
   struct virgl_gl_ctx_param ctx_params;
   if (!inited) {
      inited = 1;
      vrend_object_init_resource_table();
      vrend_clicbs = cbs;
   }

   ctx_params.shared = false;
   ctx_params.major_ver = VREND_GL_VER_MAJOR;
   ctx_params.minor_ver = VREND_GL_VER_MINOR;

   gl_context = vrend_clicbs->create_gl_context(0, &ctx_params);
   vrend_clicbs->make_current(0, gl_context);
   gl_ver = epoxy_gl_version();

   renderer_gl_major = gl_ver / 10;
   renderer_gl_minor = gl_ver % 10;
   if (gl_ver > 30 && !glewIsSupported("GL_ARB_compatibility")) {
      fprintf(stderr, "gl_version %d - core profile enabled\n", gl_ver);
      use_core_profile = 1;
   } else {
      fprintf(stderr, "gl_version %d - compat profile\n", gl_ver);
   }

   if (glewIsSupported("GL_ARB_robustness"))
      vrend_state.have_robustness = TRUE;
   else
      fprintf(stderr,"WARNING: running without ARB robustness in place may crash\n");

   if (gl_ver >= 33 || glewIsSupported("GL_ARB_sampler_objects"))
      vrend_state.have_samplers = TRUE;
   if (gl_ver >= 33 || glewIsSupported("GL_ARB_shader_bit_encoding"))
      vrend_state.have_bit_encoding = TRUE;
   if (gl_ver >= 31)
      vrend_state.have_gl_prim_restart = TRUE;
   else if (glewIsSupported("GL_NV_primitive_restart"))
      vrend_state.have_nv_prim_restart = TRUE;
   
   if (glewIsSupported("GL_EXT_framebuffer_multisample") && glewIsSupported("GL_ARB_texture_multisample")) {
      vrend_state.have_multisample = true;
      if (glewIsSupported("GL_EXT_framebuffer_multisample_blit_scaled"))
         vrend_state.have_ms_scaled_blit = TRUE;
   }

   /* callbacks for when we are cleaning up the object table */
   vrend_object_set_destroy_callback(VIRGL_OBJECT_QUERY, vrend_destroy_query_object);
   vrend_object_set_destroy_callback(VIRGL_OBJECT_SURFACE, vrend_destroy_surface_object);
   vrend_object_set_destroy_callback(VIRGL_OBJECT_VS, vrend_destroy_shader_object);
   vrend_object_set_destroy_callback(VIRGL_OBJECT_FS, vrend_destroy_shader_object);
   vrend_object_set_destroy_callback(VIRGL_OBJECT_GS, vrend_destroy_shader_object);
   vrend_object_set_destroy_callback(VIRGL_OBJECT_SAMPLER_VIEW, vrend_destroy_sampler_view_object);
   vrend_object_set_destroy_callback(VIRGL_OBJECT_STREAMOUT_TARGET, vrend_destroy_so_target_object);
   vrend_object_set_destroy_callback(VIRGL_OBJECT_SAMPLER_STATE, vrend_destroy_sampler_state_object);

   vrend_build_format_list();

   vrend_clicbs->destroy_gl_context(gl_context);
   vrend_state.viewport_dirty = vrend_state.scissor_dirty = TRUE;
   vrend_state.program_id = (GLuint)-1;
   list_inithead(&vrend_state.fence_list);
   list_inithead(&vrend_state.waiting_query_list);

   /* create 0 context */
   vrend_renderer_context_create_internal(0, 0, NULL);
}

void
vrend_renderer_fini(void)
{
   if (!inited)
      return;

   vrend_object_fini_resource_table();
   inited = 0;
}

static void vrend_destroy_sub_context(struct vrend_sub_context *sub)

{
   int i;
   if (sub->fb_id)
      glDeleteFramebuffers(1, &sub->fb_id);
 
   if (sub->blit_fb_ids[0])
      glDeleteFramebuffers(2, sub->blit_fb_ids);

   glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
   while (sub->enabled_attribs_bitmask) {
      i = u_bit_scan(&sub->enabled_attribs_bitmask);
      
      glDisableVertexAttribArray(i);
   }

   glDeleteVertexArrays(1, &sub->vaoid);

   vrend_free_programs(sub);

   glDeleteVertexArrays(1, &sub->vaoid);

   vrend_object_fini_ctx_table(sub->object_hash);
   vrend_clicbs->destroy_gl_context(sub->gl_context);

   list_del(&sub->head);
   FREE(sub);

}

bool vrend_destroy_context(struct vrend_context *ctx)
{
   bool switch_0 = (ctx == vrend_state.current_ctx);

   if (switch_0) {
      vrend_state.current_ctx = NULL;
      vrend_state.current_hw_ctx = NULL;
   }

   if (use_core_profile) {
      if (ctx->pstip_inited)
         glDeleteTextures(1, &ctx->pstipple_tex_id);
      ctx->pstip_inited = false;
   }
   /* reset references on framebuffers */
   vrend_set_framebuffer_state(ctx, 0, NULL, 0);

   vrend_set_num_sampler_views(ctx, PIPE_SHADER_VERTEX, 0, 0);
   vrend_set_num_sampler_views(ctx, PIPE_SHADER_FRAGMENT, 0, 0);
   vrend_set_num_sampler_views(ctx, PIPE_SHADER_GEOMETRY, 0, 0);

   vrend_set_streamout_targets(ctx, 0, 0, NULL);
   vrend_set_num_vbo(ctx, 0);

   vrend_set_index_buffer(ctx, 0, 0, 0);

   vrend_destroy_sub_context(ctx->sub0);
   glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
   vrend_bind_va(0);

   FREE(ctx);

   return switch_0;
}

struct vrend_context *vrend_create_context(int id, uint32_t nlen, const char *debug_name)
{
   struct vrend_context *grctx = CALLOC_STRUCT(vrend_context);

   if (!grctx)
      return NULL;

   if (nlen) {
      strncpy(grctx->debug_name, debug_name, 64);
   }

   grctx->ctx_id = id;

   list_inithead(&grctx->sub_ctxs);
   list_inithead(&grctx->active_nontimer_query_list);

   grctx->res_hash = vrend_object_init_ctx_table();

   grctx->shader_cfg.use_core_profile = use_core_profile;

   vrend_renderer_create_sub_ctx(grctx, 0);
   vrend_renderer_set_sub_ctx(grctx, 0);

   vrender_get_glsl_version(&grctx->shader_cfg.glsl_version);

   return grctx;
}

int vrend_renderer_resource_attach_iov(int res_handle, struct iovec *iov,
                                      int num_iovs)
{
   struct vrend_resource *res;
   
   res = vrend_resource_lookup(res_handle, 0);
   if (!res)
      return -1;

   /* work out size and max resource size */
   res->iov = iov;
   res->num_iovs = num_iovs;
   return 0;
}

void vrend_renderer_resource_detach_iov(int res_handle,
                                    struct iovec **iov_p,
                                    int *num_iovs_p)
{
   struct vrend_resource *res;
   res = vrend_resource_lookup(res_handle, 0);
   if (!res) {
      return;
   }
   *iov_p = res->iov;
   *num_iovs_p = res->num_iovs;

   res->iov = NULL;
   res->num_iovs = 0;
}

int vrend_renderer_resource_create(struct vrend_renderer_resource_create_args *args, struct iovec *iov, uint32_t num_iovs)
{
   struct vrend_resource *gr = (struct vrend_resource *)CALLOC_STRUCT(vrend_texture);
   int level;
   int ret;

   if (!gr)
      return ENOMEM;

   gr->handle = args->handle;
   gr->iov = iov;
   gr->num_iovs = num_iovs;
   gr->base.width0 = args->width;
   gr->base.height0 = args->height;
   gr->base.depth0 = args->depth;
   gr->base.format = args->format;
   gr->base.target = args->target;
   gr->base.last_level = args->last_level;
   gr->base.nr_samples = args->nr_samples;
   gr->base.array_size = args->array_size;

   if (args->flags & VIRGL_RESOURCE_Y_0_TOP)
      gr->y_0_top = TRUE;

   pipe_reference_init(&gr->base.reference, 1);

   if (args->bind == PIPE_BIND_CUSTOM) {
      /* custom shuold only be for buffers */
      gr->ptr = malloc(args->width);
      if (!gr->ptr) {
         FREE(gr);
         return ENOMEM;
      }
   } else if (args->bind == PIPE_BIND_INDEX_BUFFER) {
      gr->target = GL_ELEMENT_ARRAY_BUFFER_ARB;
      glGenBuffersARB(1, &gr->id);
      glBindBufferARB(gr->target, gr->id);
      glBufferData(gr->target, args->width, NULL, GL_STREAM_DRAW);
   } else if (args->bind == PIPE_BIND_STREAM_OUTPUT) {
      gr->target = GL_TRANSFORM_FEEDBACK_BUFFER;
      glGenBuffersARB(1, &gr->id);
      glBindBuffer(gr->target, gr->id);
      glBufferData(gr->target, args->width, NULL, GL_STREAM_DRAW);
   } else if (args->bind == PIPE_BIND_VERTEX_BUFFER) {
      gr->target = GL_ARRAY_BUFFER_ARB;
      glGenBuffersARB(1, &gr->id);
      glBindBufferARB(gr->target, gr->id);
      glBufferData(gr->target, args->width, NULL, GL_STREAM_DRAW);
   } else if (args->bind == PIPE_BIND_CONSTANT_BUFFER) {
      gr->target = GL_UNIFORM_BUFFER;
      glGenBuffersARB(1, &gr->id);
      glBindBufferARB(gr->target, gr->id);
      glBufferData(gr->target, args->width, NULL, GL_STREAM_DRAW);
   } else if (args->target == PIPE_BUFFER && args->bind == 0) {
      gr->target = GL_ARRAY_BUFFER_ARB;
      glGenBuffersARB(1, &gr->id);
      glBindBufferARB(gr->target, gr->id);
      glBufferData(gr->target, args->width, NULL, GL_STREAM_DRAW);
   } else if (args->target == PIPE_BUFFER && (args->bind & PIPE_BIND_SAMPLER_VIEW)) {
      GLenum internalformat;
      /* need to check GL version here */
      gr->target = GL_TEXTURE_BUFFER;
      glGenBuffersARB(1, &gr->id);
      glBindBufferARB(gr->target, gr->id);
      glGenTextures(1, &gr->tbo_tex_id);
      glBufferData(gr->target, args->width, NULL, GL_STREAM_DRAW);

      glBindTexture(gr->target, gr->tbo_tex_id);
      internalformat = tex_conv_table[args->format].internalformat;
      glTexBuffer(gr->target, internalformat, gr->id);

   } else {
      struct vrend_texture *gt = (struct vrend_texture *)gr;
      GLenum internalformat, glformat, gltype;
      gr->target = tgsitargettogltarget(args->target, args->nr_samples);
      glGenTextures(1, &gr->id);
      glBindTexture(gr->target, gr->id);

      internalformat = tex_conv_table[args->format].internalformat;
      glformat = tex_conv_table[args->format].glformat;
      gltype = tex_conv_table[args->format].gltype;
      if (internalformat == 0) {
         fprintf(stderr,"unknown format is %d\n", args->format);
         return EINVAL;
      }

      if (args->nr_samples > 1) {
         if (gr->target == GL_TEXTURE_2D_MULTISAMPLE) {
            glTexImage2DMultisample(gr->target, args->nr_samples,
                                    internalformat, args->width, args->height,
                                    TRUE);
         } else {
            glTexImage3DMultisample(gr->target, args->nr_samples,
                                    internalformat, args->width, args->height, args->array_size,
                                    TRUE);
         }

      } else if (gr->target == GL_TEXTURE_CUBE_MAP) {
         int i;
         for (i = 0; i < 6; i++) {
            GLenum ctarget = GL_TEXTURE_CUBE_MAP_POSITIVE_X + i;
            for (level = 0; level <= args->last_level; level++) {
               unsigned mwidth = u_minify(args->width, level);
               unsigned mheight = u_minify(args->height, level);
               glTexImage2D(ctarget, level, internalformat, mwidth, mheight, 0, glformat,
                            gltype, NULL);
            }
         }
      } else if (gr->target == GL_TEXTURE_3D ||
                 gr->target == GL_TEXTURE_2D_ARRAY ||
                 gr->target == GL_TEXTURE_CUBE_MAP_ARRAY) {
         for (level = 0; level <= args->last_level; level++) {
            unsigned depth_param = (gr->target == GL_TEXTURE_2D_ARRAY || gr->target == GL_TEXTURE_CUBE_MAP_ARRAY) ? args->array_size : u_minify(args->depth, level);
            unsigned mwidth = u_minify(args->width, level);
            unsigned mheight = u_minify(args->height, level);
            glTexImage3D(gr->target, level, internalformat, mwidth, mheight, depth_param, 0,
                         glformat,
                         gltype, NULL);
         }
      } else if (gr->target == GL_TEXTURE_1D) {
         for (level = 0; level <= args->last_level; level++) {
            unsigned mwidth = u_minify(args->width, level);
            glTexImage1D(gr->target, level, internalformat, mwidth, 0,
                         glformat,
                         gltype, NULL);
         }
      } else {
         for (level = 0; level <= args->last_level; level++) {
            unsigned mwidth = u_minify(args->width, level);
            unsigned mheight = u_minify(args->height, level);
            glTexImage2D(gr->target, level, internalformat, mwidth, gr->target == GL_TEXTURE_1D_ARRAY ? args->array_size : mheight, 0, glformat,
                         gltype, NULL);
         }
      }

      gt->state.max_lod = -1;
      gt->cur_swizzle_r = gt->cur_swizzle_g = gt->cur_swizzle_b = gt->cur_swizzle_a = -1;
   }

   ret = vrend_resource_insert(gr, sizeof(*gr), args->handle);
   if (ret == 0) {
      vrend_renderer_resource_destroy(gr);
      return ENOMEM;
   }
   return 0;
}

void vrend_renderer_resource_destroy(struct vrend_resource *res)
{
//   if (res->scannedout) TODO
//      (*vrend_clicbs->scanout_resource_info)(0, res->id, 0, 0, 0, 0, 0);

   if (res->readback_fb_id)
      glDeleteFramebuffers(1, &res->readback_fb_id);

   if (res->ptr)
      free(res->ptr);
   if (res->id) {
      if (res->target == GL_ELEMENT_ARRAY_BUFFER_ARB ||
          res->target == GL_ARRAY_BUFFER_ARB ||
          res->target == GL_UNIFORM_BUFFER||
          res->target == GL_TEXTURE_BUFFER||
          res->target == GL_TRANSFORM_FEEDBACK_BUFFER) {
         glDeleteBuffers(1, &res->id);
         if (res->target == GL_TEXTURE_BUFFER)
            glDeleteTextures(1, &res->tbo_tex_id);
      } else
         glDeleteTextures(1, &res->id);
   }

   if (res->handle)
      vrend_resource_remove(res->handle);
   free(res);
}


void vrend_renderer_resource_unref(uint32_t res_handle)
{
   struct vrend_resource *res;

   res = vrend_resource_lookup(res_handle, 0);
   if (!res)
      return;

   vrend_resource_remove(res->handle);
   res->handle = 0;

   vrend_resource_reference(&res, NULL);
}

static int use_sub_data = 0;
struct virgl_sub_upload_data {
   GLenum target;
   struct pipe_box *box;
};

static void iov_buffer_upload(void *cookie, uint32_t doff, void *src, int len)
{
   struct virgl_sub_upload_data *d = cookie;
   glBufferSubData(d->target, d->box->x + doff, len, src);
}

static void vrend_scale_depth(void *ptr, int size, float scale_val)
{
   GLuint *ival = ptr;
   const GLfloat myscale = 1.0f / 0xffffff;
   int i;
   for (i = 0; i < size / 4; i++) {
      GLuint value = ival[i];
      GLfloat d = ((float)(value >> 8) * myscale) * scale_val;
      d = CLAMP(d, 0.0F, 1.0F);
      ival[i] = (int)(d / myscale) << 8;
   }
}

static void copy_transfer_data(struct pipe_resource *res,
                               struct iovec *iov,
                               unsigned int num_iovs,
                               void *data,
                               uint32_t src_stride,
                               struct pipe_box *box,
                               uint64_t offset, bool invert)
{
   int blsize = util_format_get_blocksize(res->format);
   GLuint size = vrend_get_iovec_size(iov, num_iovs);
   GLuint send_size = util_format_get_nblocks(res->format, box->width,
                                              box->height) * blsize * box->depth;
   GLuint bwx = util_format_get_nblocksx(res->format, box->width) * blsize;
   GLuint bh = util_format_get_nblocksy(res->format, box->height);
   int h;
   uint32_t myoffset = offset;

   if ((send_size == size || bh == 1) && !invert)
      vrend_read_from_iovec(iov, num_iovs, offset, data, send_size);
   else {
      if (invert) {
	 for (h = bh - 1; h >= 0; h--) {
	    void *ptr = data + (h * bwx);
	    vrend_read_from_iovec(iov, num_iovs, myoffset, ptr, bwx);
	    myoffset += src_stride;
	 }
      } else {
	 for (h = 0; h < bh; h++) {
	    void *ptr = data + (h * bwx);
	    vrend_read_from_iovec(iov, num_iovs, myoffset, ptr, bwx);
	    myoffset += src_stride;
	 }
      }
   }
}

void vrend_renderer_transfer_write_iov(uint32_t res_handle,
                                      uint32_t ctx_id,
                                      int level,
                                      uint32_t stride,
                                      uint32_t layer_stride,
                                      struct pipe_box *box,
                                      uint64_t offset,
                                      struct iovec *iov,
                                      unsigned int num_iovs)
{
   struct vrend_resource *res;

   void *data;

   res = vrend_resource_lookup(res_handle, ctx_id);
   if (res == NULL) {
      struct vrend_context *ctx = vrend_lookup_renderer_ctx(ctx_id);
      report_context_error(ctx, VIRGL_ERROR_CTX_ILLEGAL_RESOURCE, res_handle);
      return;
   }

   if ((res->iov && !iov) || num_iovs == 0) {
      iov = res->iov;
      num_iovs = res->num_iovs;
   }

   if (!iov) {
      struct vrend_context *ctx = vrend_lookup_renderer_ctx(ctx_id);
      report_context_error(ctx, VIRGL_ERROR_CTX_ILLEGAL_RESOURCE, res_handle);
      return;
   }

   vrend_hw_switch_context(vrend_lookup_renderer_ctx(0), TRUE);

   if (res->target == 0 && res->ptr) {
      vrend_read_from_iovec(iov, num_iovs, offset, res->ptr + box->x, box->width);
      return;
   }
   if (res->target == GL_TRANSFORM_FEEDBACK_BUFFER ||
       res->target == GL_ELEMENT_ARRAY_BUFFER_ARB ||
       res->target == GL_ARRAY_BUFFER_ARB ||
       res->target == GL_TEXTURE_BUFFER ||
       res->target == GL_UNIFORM_BUFFER) {
      struct virgl_sub_upload_data d;
      d.box = box;
      d.target = res->target;

      glBindBufferARB(res->target, res->id);
      if (use_sub_data == 1) {
         vrend_read_from_iovec_cb(iov, num_iovs, offset, box->width, &iov_buffer_upload, &d);
      } else {
         data = glMapBufferRange(res->target, box->x, box->width, GL_MAP_INVALIDATE_RANGE_BIT | GL_MAP_UNSYNCHRONIZED_BIT | GL_MAP_WRITE_BIT);
         if (data == NULL) {
            fprintf(stderr,"map failed for element buffer\n");
            vrend_read_from_iovec_cb(iov, num_iovs, offset, box->width, &iov_buffer_upload, &d);
         } else {
            vrend_read_from_iovec(iov, num_iovs, offset, data, box->width);
            glUnmapBuffer(res->target);
         }
      }
   } else {
      struct vrend_context *ctx = vrend_lookup_renderer_ctx(ctx_id);
      GLenum glformat;
      GLenum gltype;
      int need_temp = 0;
      int elsize = util_format_get_blocksize(res->base.format);
      int x = 0, y = 0;
      boolean compressed;
      bool invert = false;
      float depth_scale;
      GLuint send_size = 0;
      vrend_use_program(0);

      if (!stride)
         stride = util_format_get_nblocksx(res->base.format, u_minify(res->base.width0, level)) * elsize;

      compressed = util_format_is_compressed(res->base.format);
      if (num_iovs > 1 || compressed) {
         need_temp = true;
      }

      if (use_core_profile == 1 && (res->y_0_top || (res->base.format == (enum pipe_format)VIRGL_FORMAT_Z24X8_UNORM))) {
	 need_temp = true;
         if (res->y_0_top)
            invert = true;
      }

      if (need_temp) {
         send_size = util_format_get_nblocks(res->base.format, box->width,
                                             box->height) * elsize * box->depth;
         data = malloc(send_size);
         if (!data)
            return;
         copy_transfer_data(&res->base, iov, num_iovs, data, stride,
                            box, offset, invert);
      } else {
         data = iov[0].iov_base + offset;
      }

      if (stride && !need_temp) {
         glPixelStorei(GL_UNPACK_ROW_LENGTH, stride / elsize);
      } else
         glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);

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
      case 8:
         glPixelStorei(GL_UNPACK_ALIGNMENT, 8);
         break;
      }

      glformat = tex_conv_table[res->base.format].glformat;
      gltype = tex_conv_table[res->base.format].gltype; 

      if ((!use_core_profile) && (res->y_0_top)) {
         if (res->readback_fb_id == 0 || res->readback_fb_level != level) {
            GLuint fb_id;
            if (res->readback_fb_id)
               glDeleteFramebuffers(1, &res->readback_fb_id);
            
            glGenFramebuffers(1, &fb_id);
            glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, fb_id);
            vrend_fb_bind_texture(res, 0, level, 0);

            res->readback_fb_id = fb_id;
            res->readback_fb_level = level;
         } else {
            glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, res->readback_fb_id);
         }
         glDrawBuffer(GL_COLOR_ATTACHMENT0_EXT);
         vrend_blend_enable(GL_FALSE);
         vrend_depth_test_enable(GL_FALSE);
         vrend_alpha_test_enable(ctx, GL_FALSE);
         vrend_stencil_test_enable(GL_FALSE);
         glPixelZoom(1.0f, res->y_0_top ? -1.0f : 1.0f);
         glWindowPos2i(box->x, res->y_0_top ? res->base.height0 - box->y : box->y);
         glDrawPixels(box->width, box->height, glformat, gltype,
                      data);
      } else {
         uint32_t comp_size;
         glBindTexture(res->target, res->id);

         if (compressed) {
            glformat = tex_conv_table[res->base.format].internalformat;
            comp_size = util_format_get_nblocks(res->base.format, box->width,
                                                box->height) * util_format_get_blocksize(res->base.format);
         }

         if (glformat == 0) {
            glformat = GL_BGRA;
            gltype = GL_UNSIGNED_BYTE;
         }

         x = box->x;
         y = invert ? res->base.height0 - box->y - box->height : box->y;

         if (res->base.format == (enum pipe_format)VIRGL_FORMAT_Z24X8_UNORM) {
            /* we get values from the guest as 24-bit scaled integers
               but we give them to the host GL and it interprets them
               as 32-bit scaled integers, so we need to scale them here */
            depth_scale = 256.0;
            if (!use_core_profile)
               glPixelTransferf(GL_DEPTH_SCALE, depth_scale);
            else
               vrend_scale_depth(data, send_size, depth_scale);
         }
         if (res->target == GL_TEXTURE_CUBE_MAP) {
            GLenum ctarget = GL_TEXTURE_CUBE_MAP_POSITIVE_X + box->z;
            if (compressed) {
               glCompressedTexSubImage2D(ctarget, level, x, y,
                                         box->width, box->height,
                                         glformat, comp_size, data);
            } else {
               glTexSubImage2D(ctarget, level, x, y, box->width, box->height,
                               glformat, gltype, data);
            }
         } else if (res->target == GL_TEXTURE_3D || res->target == GL_TEXTURE_2D_ARRAY || res->target == GL_TEXTURE_CUBE_MAP_ARRAY) {
            if (compressed) {
               glCompressedTexSubImage3D(res->target, level, x, y, box->z,
                                         box->width, box->height, box->depth,
                                         glformat, comp_size, data);
            } else {
               glTexSubImage3D(res->target, level, x, y, box->z,
                               box->width, box->height, box->depth,
                               glformat, gltype, data);
            }
         } else if (res->target == GL_TEXTURE_1D) {
            if (compressed) {
               glCompressedTexSubImage1D(res->target, level, box->x,
                                         box->width,
                                         glformat, comp_size, data);
            } else {
               glTexSubImage1D(res->target, level, box->x, box->width,
                               glformat, gltype, data);
            }
         } else {
            if (compressed) {
               glCompressedTexSubImage2D(res->target, level, x, res->target == GL_TEXTURE_1D_ARRAY ? box->z : y,
                                         box->width, box->height,
                                         glformat, comp_size, data);
            } else {
               glTexSubImage2D(res->target, level, x, res->target == GL_TEXTURE_1D_ARRAY ? box->z : y,
                               box->width, box->height,
                               glformat, gltype, data);
            }
         }
         if (res->base.format == (enum pipe_format)VIRGL_FORMAT_Z24X8_UNORM) {
            if (!use_core_profile)
               glPixelTransferf(GL_DEPTH_SCALE, 1.0);
         }
      }
      if (stride && !need_temp)
         glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
      glPixelStorei(GL_UNPACK_ALIGNMENT, 4);

      if (need_temp)
         free(data);
   }

}

static void vrend_transfer_send_getteximage(struct vrend_resource *res,
                                            uint32_t level, uint32_t stride,
                                            struct pipe_box *box, uint64_t offset,
                                            struct iovec *iov, int num_iovs)
{
   GLenum format, type;
   uint32_t send_size, tex_size;
   void *data;
   int elsize = util_format_get_blocksize(res->base.format);
   int compressed = util_format_is_compressed(res->base.format);
   GLenum target;
   uint32_t depth = 1;
   uint32_t send_offset = 0;
   format = tex_conv_table[res->base.format].glformat;
   type = tex_conv_table[res->base.format].gltype; 

   if (compressed)
      format = tex_conv_table[res->base.format].internalformat;

   if (res->target == GL_TEXTURE_3D)
      depth = u_minify(res->base.depth0, level);
   else if (res->target == GL_TEXTURE_2D_ARRAY || res->target == GL_TEXTURE_1D_ARRAY || res->target == GL_TEXTURE_CUBE_MAP_ARRAY)
      depth = res->base.array_size;

   tex_size = util_format_get_nblocks(res->base.format, u_minify(res->base.width0, level), u_minify(res->base.height0, level)) * util_format_get_blocksize(res->base.format) * depth;
   
   send_size = util_format_get_nblocks(res->base.format, box->width, box->height) * util_format_get_blocksize(res->base.format) * box->depth;

   if (box->z && res->target != GL_TEXTURE_CUBE_MAP) {
      send_offset = util_format_get_nblocks(res->base.format, u_minify(res->base.width0, level), u_minify(res->base.height0, level)) * util_format_get_blocksize(res->base.format) * box->z;
   }

   data = malloc(tex_size);
   if (!data)
      return;

   switch (elsize) {
   case 1:
      glPixelStorei(GL_PACK_ALIGNMENT, 1);
      break;
   case 2:
      glPixelStorei(GL_PACK_ALIGNMENT, 2);
      break;
   case 4:
   default:
      glPixelStorei(GL_PACK_ALIGNMENT, 4);
      break;
   case 8:
      glPixelStorei(GL_PACK_ALIGNMENT, 8);
      break;
   }

   glBindTexture(res->target, res->id);
   if (res->target == GL_TEXTURE_CUBE_MAP) {
      target = GL_TEXTURE_CUBE_MAP_POSITIVE_X + box->z;
   } else
      target = res->target;
      
   if (compressed) {
      if (vrend_state.have_robustness)
         glGetnCompressedTexImageARB(target, level, tex_size, data);
      else
         glGetCompressedTexImage(target, level, data);
   } else {
      if (vrend_state.have_robustness)
         glGetnTexImageARB(target, level, format, type, tex_size, data);
      else
         glGetTexImage(target, level, format, type, data);
   }
      
   glPixelStorei(GL_PACK_ALIGNMENT, 4);

   vrend_transfer_write_tex_return(&res->base, box, level, stride, offset, iov, num_iovs, data + send_offset, send_size, FALSE);
   free(data);
}

static void vrend_transfer_send_readpixels(struct vrend_resource *res,
                                           uint32_t level, uint32_t stride,
                                           struct pipe_box *box, uint64_t offset,
                                           struct iovec *iov, int num_iovs)
{
   void *myptr = iov[0].iov_base + offset;
   int need_temp = 0;
   GLuint fb_id;
   void *data;
   boolean actually_invert, separate_invert = FALSE;
   GLenum format, type;
   GLint y1;
   uint32_t send_size = 0;
   uint32_t h = u_minify(res->base.height0, level);
   int elsize = util_format_get_blocksize(res->base.format);
   float depth_scale;
   vrend_use_program(0);

   format = tex_conv_table[res->base.format].glformat;
   type = tex_conv_table[res->base.format].gltype; 
   /* if we are asked to invert and reading from a front then don't */

   actually_invert = res->y_0_top;

   if (actually_invert && !have_invert_mesa)
      separate_invert = TRUE;

   if (num_iovs > 1 || separate_invert)
      need_temp = 1;

   send_size = box->width * box->height * box->depth * util_format_get_blocksize(res->base.format);

   if (need_temp) {
      data = malloc(send_size);
      if (!data) {
         fprintf(stderr,"malloc failed %d\n", send_size);
         return;
      }
   } else
      data = myptr;

   if (res->readback_fb_id == 0 || res->readback_fb_level != level || res->readback_fb_z != box->z) {

      if (res->readback_fb_id)
         glDeleteFramebuffers(1, &res->readback_fb_id);
         
      glGenFramebuffers(1, &fb_id);
      glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, fb_id);

      vrend_fb_bind_texture(res, 0, level, box->z);

      res->readback_fb_id = fb_id;
      res->readback_fb_level = level;
      res->readback_fb_z = box->z;
   } else
      glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, res->readback_fb_id);
   if (actually_invert)
      y1 = h - box->y - box->height;
   else
      y1 = box->y;

   if (have_invert_mesa && actually_invert)
      glPixelStorei(GL_PACK_INVERT_MESA, 1);
   if (!vrend_format_is_ds(res->base.format))
      glReadBuffer(GL_COLOR_ATTACHMENT0_EXT);
   if (!need_temp && stride)
      glPixelStorei(GL_PACK_ROW_LENGTH, stride);

   switch (elsize) {
   case 1:
      glPixelStorei(GL_PACK_ALIGNMENT, 1);
      break;
   case 2:
      glPixelStorei(GL_PACK_ALIGNMENT, 2);
      break;
   case 4:
   default:
      glPixelStorei(GL_PACK_ALIGNMENT, 4);
      break;
   case 8:
      glPixelStorei(GL_PACK_ALIGNMENT, 8);
      break;
   }  

   if (res->base.format == (enum pipe_format)VIRGL_FORMAT_Z24X8_UNORM) {
      /* we get values from the guest as 24-bit scaled integers
         but we give them to the host GL and it interprets them
         as 32-bit scaled integers, so we need to scale them here */
      depth_scale = 1.0 / 256.0;
      if (!use_core_profile) {
         glPixelTransferf(GL_DEPTH_SCALE, depth_scale);
      }
   }
   if (vrend_state.have_robustness)
      glReadnPixelsARB(box->x, y1, box->width, box->height, format, type, send_size, data);
   else
      glReadPixels(box->x, y1, box->width, box->height, format, type, data);

   if (res->base.format == (enum pipe_format)VIRGL_FORMAT_Z24X8_UNORM) {
      if (!use_core_profile)
         glPixelTransferf(GL_DEPTH_SCALE, 1.0);
      else
         vrend_scale_depth(data, send_size, depth_scale);
   }
   if (have_invert_mesa && actually_invert)
      glPixelStorei(GL_PACK_INVERT_MESA, 0);
   if (!need_temp && stride)
      glPixelStorei(GL_PACK_ROW_LENGTH, 0);
   glPixelStorei(GL_PACK_ALIGNMENT, 4);
   if (need_temp) {
      vrend_transfer_write_tex_return(&res->base, box, level, stride, offset, iov, num_iovs, data, send_size, separate_invert);
      free(data);
   }
}

static bool check_tsend_bounds(struct vrend_resource *res,
                               uint32_t level, struct pipe_box *box)
{

   if (box->width > u_minify(res->base.width0, level))
       return false;
   if (box->x > u_minify(res->base.width0, level))
       return false;
   if (box->width + box->x > u_minify(res->base.width0, level))
       return false;

   if (box->height > u_minify(res->base.height0, level))
       return false;
   if (box->y > u_minify(res->base.height0, level))
       return false;
   if (box->height + box->y > u_minify(res->base.height0, level))
       return false;

   /* bounds checks TODO,
      box depth / box->z and array layers */
   return true;
}

void vrend_renderer_transfer_send_iov(uint32_t res_handle, uint32_t ctx_id,
                                     uint32_t level, uint32_t stride,
                                     uint32_t layer_stride,
                                     struct pipe_box *box,
                                     uint64_t offset, struct iovec *iov,
                                     int num_iovs)
{
   struct vrend_resource *res;
   struct vrend_context *ctx = vrend_lookup_renderer_ctx(ctx_id);

   res = vrend_resource_lookup(res_handle, ctx_id);
   if (!res) {
      report_context_error(ctx, VIRGL_ERROR_CTX_ILLEGAL_RESOURCE, res_handle);
      return;
   }

   if (res->iov && (!iov || num_iovs == 0)) {
      iov = res->iov;
      num_iovs = res->num_iovs;
   }

   if (!iov) {
      report_context_error(ctx, VIRGL_ERROR_CTX_ILLEGAL_RESOURCE, res_handle);
      return;
   }

   if (!check_tsend_bounds(res, level, box))
      return;

   if (res->target == 0 && res->ptr) {
      uint32_t send_size = box->width * util_format_get_blocksize(res->base.format);      
      vrend_transfer_write_return(res->ptr + box->x, send_size, offset, iov, num_iovs);
      return;
   }

   vrend_hw_switch_context(vrend_lookup_renderer_ctx(0), TRUE);

   if (res->target == GL_ELEMENT_ARRAY_BUFFER_ARB ||
       res->target == GL_ARRAY_BUFFER_ARB ||
       res->target == GL_TRANSFORM_FEEDBACK_BUFFER ||
       res->target == GL_TEXTURE_BUFFER ||
       res->target == GL_UNIFORM_BUFFER) {
      uint32_t send_size = box->width * util_format_get_blocksize(res->base.format);      
      void *data;

      glBindBufferARB(res->target, res->id);
      data = glMapBufferRange(res->target, box->x, box->width, GL_MAP_READ_BIT);
      if (!data)
         fprintf(stderr,"unable to open buffer for reading %d\n", res->target);
      else
         vrend_transfer_write_return(data, send_size, offset, iov, num_iovs);
      glUnmapBuffer(res->target);
   } else {
      boolean can_readpixels = TRUE;

      can_readpixels = vrend_format_can_render(res->base.format) || vrend_format_is_ds(res->base.format);

      if (can_readpixels) {
         vrend_transfer_send_readpixels(res, level, stride, box, offset,
                                        iov, num_iovs);
         return;
      }

      vrend_transfer_send_getteximage(res, level, stride, box, offset,
                                      iov, num_iovs);

   }
}

void vrend_set_stencil_ref(struct vrend_context *ctx,
                           struct pipe_stencil_ref *ref)
{
   if (ctx->sub->stencil_refs[0] != ref->ref_value[0] ||
       ctx->sub->stencil_refs[1] != ref->ref_value[1]) {
      ctx->sub->stencil_refs[0] = ref->ref_value[0];
      ctx->sub->stencil_refs[1] = ref->ref_value[1];
      ctx->sub->stencil_state_dirty = TRUE;
   }
   
}

static void vrend_hw_emit_blend_color(struct vrend_context *ctx)
{
   struct pipe_blend_color *color = &ctx->sub->blend_color;
   glBlendColor(color->color[0], color->color[1], color->color[2],
                color->color[3]);
}

void vrend_set_blend_color(struct vrend_context *ctx,
                           struct pipe_blend_color *color)
{
   ctx->sub->blend_color = *color;
   vrend_hw_emit_blend_color(ctx);
}

void vrend_set_scissor_state(struct vrend_context *ctx,
                             struct pipe_scissor_state *ss)
{
   ctx->sub->ss = *ss;
   ctx->sub->scissor_state_dirty = TRUE;
}

void vrend_set_polygon_stipple(struct vrend_context *ctx,
                               struct pipe_poly_stipple *ps)
{
   if (use_core_profile) {
      static const unsigned bit31 = 1 << 31;
      GLubyte *stip = calloc(1, 1024);
      int i, j;

      if (!ctx->pstip_inited)
         vrend_init_pstipple_texture(ctx);

      if (!stip)
         return;

      for (i = 0; i < 32; i++) {
         for (j = 0; j < 32; j++) {
            if (ps->stipple[i] & (bit31 >> j))
               stip[i * 32 + j] = 0;
            else
               stip[i * 32 + j] = 255;
         }
      }

      glBindTexture(GL_TEXTURE_2D, ctx->pstipple_tex_id);
      glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 32, 32,
                      GL_RED, GL_UNSIGNED_BYTE, stip);

      free(stip);
      return;
   }
   glPolygonStipple((const GLubyte *)ps->stipple);
}

void vrend_set_clip_state(struct vrend_context *ctx, struct pipe_clip_state *ucp)
{
   if (use_core_profile) {
      ctx->sub->ucp_state = *ucp;
   } else {
      int i, j;
      GLdouble val[4];

      for (i = 0; i < 8; i++) {
         for (j = 0; j < 4; j++)
            val[j] = ucp->ucp[i][j];
         glClipPlane(GL_CLIP_PLANE0 + i, val);
      }
   }
}

void vrend_set_sample_mask(struct vrend_context *ctx, unsigned sample_mask)
{
   glSampleMaski(0, sample_mask);
}


static void vrend_hw_emit_streamout_targets(struct vrend_context *ctx)
{
   int i;

   for (i = 0; i < ctx->sub->num_so_targets; i++) {
      if (ctx->sub->so_targets[i]->buffer_offset)
         glBindBufferRange(GL_TRANSFORM_FEEDBACK_BUFFER, i, ctx->sub->so_targets[i]->buffer->id, ctx->sub->so_targets[i]->buffer_offset, ctx->sub->so_targets[i]->buffer_size);
      else
         glBindBufferBase(GL_TRANSFORM_FEEDBACK_BUFFER, i, ctx->sub->so_targets[i]->buffer->id);
   }
}

void vrend_set_streamout_targets(struct vrend_context *ctx,
                                 uint32_t append_bitmask,
                                 uint32_t num_targets,
                                 uint32_t *handles)
{
   struct vrend_so_target *target;
   int i;
   int old_num = ctx->sub->num_so_targets;

   ctx->sub->num_so_targets = num_targets;
   for (i = 0; i < num_targets; i++) {
      target = vrend_object_lookup(ctx->sub->object_hash, handles[i], VIRGL_OBJECT_STREAMOUT_TARGET);
      if (!target) {
         report_context_error(ctx, VIRGL_ERROR_CTX_ILLEGAL_HANDLE, handles[i]);
         return;
      }
      vrend_so_target_reference(&ctx->sub->so_targets[i], target);
   }

   for (i = num_targets; i < old_num; i++)
      vrend_so_target_reference(&ctx->sub->so_targets[i], NULL);

   vrend_hw_emit_streamout_targets(ctx);
}

static void vrend_resource_buffer_copy(struct vrend_context *ctx,
                                       struct vrend_resource *src_res,
                                       struct vrend_resource *dst_res,
                                       uint32_t dstx, uint32_t srcx,
                                       uint32_t width)
{

   glBindBuffer(GL_COPY_READ_BUFFER, src_res->id);
   glBindBuffer(GL_COPY_WRITE_BUFFER, dst_res->id);

   glCopyBufferSubData(GL_COPY_READ_BUFFER, GL_COPY_WRITE_BUFFER, srcx, dstx, width);
   glBindBuffer(GL_COPY_READ_BUFFER, 0);
   glBindBuffer(GL_COPY_WRITE_BUFFER, 0);
}

static void vrend_resource_copy_fallback(struct vrend_context *ctx,
                                         struct vrend_resource *src_res,
                                         struct vrend_resource *dst_res,
                                         uint32_t dst_level,
                                         uint32_t dstx, uint32_t dsty,
                                         uint32_t dstz, uint32_t src_level,
                                         const struct pipe_box *src_box)
{
   void *tptr;
   uint32_t transfer_size;
   GLenum glformat, gltype;
   int elsize = util_format_get_blocksize(dst_res->base.format);
   int compressed = util_format_is_compressed(dst_res->base.format);
   int cube_slice = 1;
   uint32_t slice_size, slice_offset;
   int i;
   if (src_res->target == GL_TEXTURE_CUBE_MAP)
      cube_slice = 6;

   if (src_res->base.format != dst_res->base.format) {
      fprintf(stderr, "copy fallback failed due to mismatched formats %d %d\n", src_res->base.format, dst_res->base.format);
      return;
   }

   /* this is ugly need to do a full GetTexImage */
   slice_size = util_format_get_nblocks(src_res->base.format, u_minify(src_res->base.width0, src_level), u_minify(src_res->base.height0, src_level)) *
      u_minify(src_res->base.depth0, src_level) * util_format_get_blocksize(src_res->base.format);
   transfer_size = slice_size * src_res->base.array_size;

   tptr = malloc(transfer_size);
   if (!tptr)
      return;

   glformat = tex_conv_table[src_res->base.format].glformat;
   gltype = tex_conv_table[src_res->base.format].gltype; 

   if (compressed)
      glformat = tex_conv_table[src_res->base.format].internalformat;
      
   switch (elsize) {
   case 1:
      glPixelStorei(GL_PACK_ALIGNMENT, 1);
      break;
   case 2:
      glPixelStorei(GL_PACK_ALIGNMENT, 2);
      break;
   case 4:
   default:
      glPixelStorei(GL_PACK_ALIGNMENT, 4);
      break;
   case 8:
      glPixelStorei(GL_PACK_ALIGNMENT, 8);
      break;
   }
   glBindTexture(src_res->target, src_res->id);

   slice_offset = 0;
   for (i = 0; i < cube_slice; i++) {
      GLenum ctarget = src_res->target == GL_TEXTURE_CUBE_MAP ? GL_TEXTURE_CUBE_MAP_POSITIVE_X + i : src_res->target;
      if (compressed) {
         if (vrend_state.have_robustness)
            glGetnCompressedTexImageARB(ctarget, src_level, transfer_size, tptr + slice_offset);
         else
            glGetCompressedTexImage(ctarget, src_level, tptr + slice_offset);
      } else {
         if (vrend_state.have_robustness)
            glGetnTexImageARB(ctarget, src_level, glformat, gltype, transfer_size, tptr + slice_offset);
         else
            glGetTexImage(ctarget, src_level, glformat, gltype, tptr + slice_offset);
      }
      slice_offset += slice_size;
   }

   glPixelStorei(GL_PACK_ALIGNMENT, 4);
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
   case 8:
      glPixelStorei(GL_UNPACK_ALIGNMENT, 8);
      break;
   }

   glBindTexture(dst_res->target, dst_res->id);
   slice_offset = 0;
   for (i = 0; i < cube_slice; i++) {
      GLenum ctarget = dst_res->target == GL_TEXTURE_CUBE_MAP ? GL_TEXTURE_CUBE_MAP_POSITIVE_X + i : dst_res->target;
      if (compressed) {
         if (ctarget == GL_TEXTURE_1D) {
            glCompressedTexSubImage1D(ctarget, dst_level, dstx,
                                      src_box->width,
                                      glformat, transfer_size, tptr + slice_offset);
         } else {
            glCompressedTexSubImage2D(ctarget, dst_level, dstx, dsty,
                                      src_box->width, src_box->height,
                                      glformat, transfer_size, tptr + slice_offset);
         }
      } else {
         if (ctarget == GL_TEXTURE_1D) {
            glTexSubImage1D(ctarget, dst_level, dstx, src_box->width, glformat, gltype, tptr + slice_offset);
         } else if (ctarget == GL_TEXTURE_3D ||
                    ctarget == GL_TEXTURE_2D_ARRAY ||
                    ctarget == GL_TEXTURE_CUBE_MAP_ARRAY) {
            glTexSubImage3D(ctarget, dst_level, dstx, dsty, 0,src_box->width, src_box->height, src_box->depth, glformat, gltype, tptr + slice_offset);
         } else {
            glTexSubImage2D(ctarget, dst_level, dstx, dsty, src_box->width, src_box->height, glformat, gltype, tptr + slice_offset);
         }
      }
      slice_offset += slice_size;
   }

   glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
   free(tptr);
}

void vrend_renderer_resource_copy_region(struct vrend_context *ctx,
                                        uint32_t dst_handle, uint32_t dst_level,
                                        uint32_t dstx, uint32_t dsty, uint32_t dstz,
                                        uint32_t src_handle, uint32_t src_level,
                                        const struct pipe_box *src_box)
{
   struct vrend_resource *src_res, *dst_res;   
   GLbitfield glmask = 0;
   GLint sy1, sy2, dy1, dy2;

   if (ctx->in_error)
      return;

   src_res = vrend_renderer_ctx_res_lookup(ctx, src_handle);
   dst_res = vrend_renderer_ctx_res_lookup(ctx, dst_handle);

   if (!src_res) {
      report_context_error(ctx, VIRGL_ERROR_CTX_ILLEGAL_RESOURCE, src_handle);
      return;
   }
   if (!dst_res) {
      report_context_error(ctx, VIRGL_ERROR_CTX_ILLEGAL_RESOURCE, dst_handle);
      return;
   }

   if (src_res->base.target == PIPE_BUFFER && dst_res->base.target == PIPE_BUFFER) {
      /* do a buffer copy */
      vrend_resource_buffer_copy(ctx, src_res, dst_res, dstx,
                                 src_box->x, src_box->width);
      return;
   }

   if (!vrend_format_can_render(src_res->base.format) ||
       !vrend_format_can_render(dst_res->base.format)) {
      vrend_resource_copy_fallback(ctx, src_res, dst_res, dst_level, dstx,
                                   dsty, dstz, src_level, src_box);

      return;
   }

   glBindFramebuffer(GL_FRAMEBUFFER_EXT, ctx->sub->blit_fb_ids[0]);
   /* clean out fb ids */
   glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, GL_DEPTH_STENCIL_ATTACHMENT,
                             GL_TEXTURE_2D, 0, 0);
   vrend_fb_bind_texture(src_res, 0, src_level, src_box->z);
      
   glBindFramebuffer(GL_FRAMEBUFFER_EXT, ctx->sub->blit_fb_ids[1]);
   glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, GL_DEPTH_STENCIL_ATTACHMENT,
                             GL_TEXTURE_2D, 0, 0);
   vrend_fb_bind_texture(dst_res, 0, dst_level, dstz);
   glBindFramebuffer(GL_DRAW_FRAMEBUFFER, ctx->sub->blit_fb_ids[1]);

   glBindFramebuffer(GL_READ_FRAMEBUFFER, ctx->sub->blit_fb_ids[0]);

   glmask = GL_COLOR_BUFFER_BIT;
   glDisable(GL_SCISSOR_TEST);

   if (!src_res->y_0_top) {
      sy1 = src_box->y;
      sy2 = src_box->y + src_box->height;
   } else {
      sy1 = src_res->base.height0 - src_box->y - src_box->height;
      sy2 = src_res->base.height0 - src_box->y;
   }

   if (!dst_res->y_0_top) {
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

}

static void vrend_renderer_blit_int(struct vrend_context *ctx,
                                    struct vrend_resource *src_res,
                                    struct vrend_resource *dst_res,
                                    const struct pipe_blit_info *info)
{
   GLbitfield glmask = 0;
   int src_y1, src_y2, dst_y1, dst_y2;
   GLenum filter;
   int n_layers = 1, i;
   bool use_gl = false;

   filter = convert_mag_filter(info->filter);

   /* if we can't make FBO's use the fallback path */
   if (!vrend_format_can_render(src_res->base.format) &&
       !vrend_format_is_ds(src_res->base.format))
      use_gl = true;
   if (!vrend_format_can_render(dst_res->base.format) &&
       !vrend_format_is_ds(dst_res->base.format))
      use_gl = true;

   /* different depth formats */
   if (vrend_format_is_ds(src_res->base.format) &&
       vrend_format_is_ds(dst_res->base.format)) {
      if (src_res->base.format != dst_res->base.format) {
         if (!(src_res->base.format == PIPE_FORMAT_S8_UINT_Z24_UNORM &&
               (dst_res->base.format == PIPE_FORMAT_Z24X8_UNORM))) {
            use_gl = true;
         }
      }
   }
   /* glBlitFramebuffer - can support depth stencil with NEAREST
      which we use for mipmaps */
   if ((info->mask & (PIPE_MASK_Z | PIPE_MASK_S)) && info->filter == PIPE_TEX_FILTER_LINEAR)
       use_gl = true;

   /* for scaled MS blits we either need extensions or hand roll */
   if (src_res->base.nr_samples > 1 &&
       src_res->base.nr_samples != dst_res->base.nr_samples &&
       (info->src.box.width != info->dst.box.width ||
        info->src.box.height != info->dst.box.height)) {
      if (vrend_state.have_ms_scaled_blit)
         filter = GL_SCALED_RESOLVE_NICEST_EXT;
      else
         use_gl = true;
   }

   /* for 3D mipmapped blits - hand roll time */
   if (info->src.box.depth != info->dst.box.depth)
      use_gl = true;

   if (use_gl) {
      vrend_renderer_blit_gl(ctx, src_res, dst_res, info);
      vrend_clicbs->make_current(0, ctx->sub->gl_context);
      return;
   }

   if (info->mask & PIPE_MASK_Z)
      glmask |= GL_DEPTH_BUFFER_BIT;
   if (info->mask & PIPE_MASK_S)
      glmask |= GL_STENCIL_BUFFER_BIT;
   if (info->mask & PIPE_MASK_RGBA)
      glmask |= GL_COLOR_BUFFER_BIT;

   if (!dst_res->y_0_top) {
      dst_y1 = info->dst.box.y + info->dst.box.height;
      dst_y2 = info->dst.box.y;
   } else {
      dst_y1 = dst_res->base.height0 - info->dst.box.y - info->dst.box.height;
      dst_y2 = dst_res->base.height0 - info->dst.box.y;
   }

   if (!src_res->y_0_top) {
      src_y1 = info->src.box.y + info->src.box.height;
      src_y2 = info->src.box.y;
   } else {
      src_y1 = src_res->base.height0 - info->src.box.y - info->src.box.height;
      src_y2 = src_res->base.height0 - info->src.box.y;
   }

   if (info->scissor_enable) {
      glScissor(info->scissor.minx, info->scissor.miny, info->scissor.maxx - info->scissor.minx, info->scissor.maxy - info->scissor.miny);
      ctx->sub->scissor_state_dirty = TRUE;
      glEnable(GL_SCISSOR_TEST);
   } else
      glDisable(GL_SCISSOR_TEST);

   glBindFramebuffer(GL_FRAMEBUFFER_EXT, ctx->sub->blit_fb_ids[0]);
   if (info->mask & PIPE_MASK_RGBA)
      glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, GL_DEPTH_STENCIL_ATTACHMENT,
                                GL_TEXTURE_2D, 0, 0);
   else
      glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0,
                                GL_TEXTURE_2D, 0, 0);
   glBindFramebuffer(GL_FRAMEBUFFER_EXT, ctx->sub->blit_fb_ids[1]);
   if (info->mask & PIPE_MASK_RGBA)
      glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, GL_DEPTH_STENCIL_ATTACHMENT,
                                GL_TEXTURE_2D, 0, 0);
   else if (info->mask & (PIPE_MASK_Z | PIPE_MASK_S))
      glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0,
                                GL_TEXTURE_2D, 0, 0);
   if (info->src.box.depth == info->dst.box.depth)
      n_layers = info->dst.box.depth;
   for (i = 0; i < n_layers; i++) {
      glBindFramebuffer(GL_FRAMEBUFFER_EXT, ctx->sub->blit_fb_ids[0]);
      glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, GL_DEPTH_STENCIL_ATTACHMENT,
                                GL_TEXTURE_2D, 0, 0);
      vrend_fb_bind_texture(src_res, 0, info->src.level, info->src.box.z + i);

      glBindFramebuffer(GL_FRAMEBUFFER_EXT, ctx->sub->blit_fb_ids[1]);

      vrend_fb_bind_texture(dst_res, 0, info->dst.level, info->dst.box.z + i);
      glBindFramebuffer(GL_DRAW_FRAMEBUFFER, ctx->sub->blit_fb_ids[1]);

      glBindFramebuffer(GL_READ_FRAMEBUFFER, ctx->sub->blit_fb_ids[0]);

      glBlitFramebuffer(info->src.box.x,
                        src_y1,
                        info->src.box.x + info->src.box.width,
                        src_y2,
                        info->dst.box.x,
                        dst_y1,
                        info->dst.box.x + info->dst.box.width,
                        dst_y2,
                        glmask, filter);
   }

}

void vrend_renderer_blit(struct vrend_context *ctx,
                        uint32_t dst_handle, uint32_t src_handle,
                        const struct pipe_blit_info *info)
{
   struct vrend_resource *src_res, *dst_res;
   src_res = vrend_renderer_ctx_res_lookup(ctx, src_handle);
   dst_res = vrend_renderer_ctx_res_lookup(ctx, dst_handle);

   if (!src_res) {
      report_context_error(ctx, VIRGL_ERROR_CTX_ILLEGAL_RESOURCE, src_handle);
      return;
   }
   if (!dst_res) {
      report_context_error(ctx, VIRGL_ERROR_CTX_ILLEGAL_RESOURCE, dst_handle);
      return;
   }

   if (ctx->in_error)
      return;

   vrend_renderer_blit_int(ctx, src_res, dst_res, info);
}

int vrend_renderer_create_fence(int client_fence_id, uint32_t ctx_id)
{
   struct vrend_fence *fence;

   fence = malloc(sizeof(struct vrend_fence));
   if (!fence)
      return -1;

   fence->ctx_id = ctx_id;
   fence->fence_id = client_fence_id;
   fence->syncobj = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
   list_addtail(&fence->fences, &vrend_state.fence_list);
   return 0;
}

void vrend_renderer_check_fences(void)
{
   struct vrend_fence *fence, *stor;
   uint32_t latest_id = 0;
   GLenum glret;

   if (!inited)
      return;

   LIST_FOR_EACH_ENTRY_SAFE(fence, stor, &vrend_state.fence_list, fences) {
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
   vrend_clicbs->write_fence(latest_id);
}

static boolean vrend_get_one_query_result(GLuint query_id, bool use_64, uint64_t *result)
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

static boolean vrend_check_query(struct vrend_query *query)
{
   uint64_t result;
   struct virgl_host_query_state *state;
   struct vrend_nontimer_hw_query *hwq, *stor;
   boolean ret;

   if (vrend_is_timer_query(query->gltype)) {
       ret = vrend_get_one_query_result(query->timer_query_id, TRUE, &result);
       if (ret == FALSE)
           return FALSE;
       goto out_write_val;
   }

   /* for non-timer queries we have to iterate over all hw queries and remove and total them */
   LIST_FOR_EACH_ENTRY_SAFE(hwq, stor, &query->hw_queries, query_list) {
       ret = vrend_get_one_query_result(hwq->id, FALSE, &result);
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

   query->current_total = 0;
   return TRUE;
}

void vrend_renderer_check_queries(void)
{
   struct vrend_query *query, *stor;
   if (!inited)
      return;   
   
   LIST_FOR_EACH_ENTRY_SAFE(query, stor, &vrend_state.waiting_query_list, waiting_queries) {
      vrend_hw_switch_context(vrend_lookup_renderer_ctx(query->ctx_id), TRUE);
      if (vrend_check_query(query) == TRUE)
         list_delinit(&query->waiting_queries);
   }
}

static void vrend_do_end_query(struct vrend_query *q)
{
   glEndQuery(q->gltype);
   q->active_hw = FALSE;
}

static void vrend_ctx_finish_queries(struct vrend_context *ctx)
{
   struct vrend_query *query;

   LIST_FOR_EACH_ENTRY(query, &ctx->active_nontimer_query_list, ctx_queries) {
      if (query->active_hw == TRUE)
         vrend_do_end_query(query);
   }
}

#if 0
static void vrend_ctx_restart_queries(struct vrend_context *ctx)
{
   struct vrend_query *query;
   struct vrend_nontimer_hw_query *hwq;

   if (ctx->query_on_hw == TRUE)
      return;

   ctx->query_on_hw = TRUE;
   LIST_FOR_EACH_ENTRY(query, &ctx->active_nontimer_query_list, ctx_queries) {
      if (query->active_hw == FALSE) {
         hwq = vrend_create_hw_query(query);
         glBeginQuery(query->gltype, hwq->id);
         query->active_hw = TRUE;
      }
   }
}
#endif

/* stop all the nontimer queries running in the current context */
void vrend_stop_current_queries(void)
{
   if (vrend_state.current_ctx && vrend_state.current_ctx->query_on_hw) {
      vrend_ctx_finish_queries(vrend_state.current_ctx);
      vrend_state.current_ctx->query_on_hw = FALSE;
   }
}

boolean vrend_hw_switch_context(struct vrend_context *ctx, boolean now)
{
   if (ctx == vrend_state.current_ctx && ctx->ctx_switch_pending == FALSE)
      return TRUE;

   if (ctx->ctx_id != 0 && ctx->in_error) {
      return FALSE;
   }

   ctx->ctx_switch_pending = TRUE;
   if (now == TRUE) {
      vrend_finish_context_switch(ctx);
   }
   vrend_state.current_ctx = ctx;
   return TRUE;
}

static void vrend_finish_context_switch(struct vrend_context *ctx)
{
   if (ctx->ctx_switch_pending == FALSE)
      return;
   ctx->ctx_switch_pending = FALSE;

   if (vrend_state.current_hw_ctx == ctx)
      return;

   vrend_state.current_hw_ctx = ctx;

   vrend_clicbs->make_current(0, ctx->sub->gl_context);

#if 0
   /* re-emit all the state */
   vrend_hw_emit_framebuffer_state(ctx);
   vrend_hw_emit_depth_range(ctx);
   vrend_hw_emit_blend(ctx);
   vrend_hw_emit_dsa(ctx);
   vrend_hw_emit_rs(ctx);
   vrend_hw_emit_blend_color(ctx);
   vrend_hw_emit_streamout_targets(ctx);

   ctx->sub->stencil_state_dirty = TRUE;
   ctx->sub->scissor_state_dirty = TRUE;
   ctx->sub->viewport_state_dirty = TRUE;
   ctx->sub->shader_dirty = TRUE;
#endif
}


void
vrend_renderer_object_destroy(struct vrend_context *ctx, uint32_t handle)
{
   vrend_object_remove(ctx->sub->object_hash, handle, 0);
}

uint32_t vrend_renderer_object_insert(struct vrend_context *ctx, void *data,
                                 uint32_t size, uint32_t handle, enum virgl_object_type type)
{
   return vrend_object_insert(ctx->sub->object_hash, data, size, handle, type);
}

static struct vrend_nontimer_hw_query *vrend_create_hw_query(struct vrend_query *query)
{
   struct vrend_nontimer_hw_query *hwq;

   hwq = CALLOC_STRUCT(vrend_nontimer_hw_query);
   if (!hwq)
      return NULL;

   glGenQueries(1, &hwq->id);
   
   list_add(&hwq->query_list, &query->hw_queries);
   return hwq;
}


int vrend_create_query(struct vrend_context *ctx, uint32_t handle,
                       uint32_t query_type, uint32_t res_handle,
                       uint32_t offset)
{
   struct vrend_query *q;
   struct vrend_resource *res;
   uint32_t ret_handle;
   res = vrend_renderer_ctx_res_lookup(ctx, res_handle);
   if (!res) {
      report_context_error(ctx, VIRGL_ERROR_CTX_ILLEGAL_RESOURCE, res_handle);
      return EINVAL;
   }

   q = CALLOC_STRUCT(vrend_query);
   if (!q)
      return ENOMEM;

   list_inithead(&q->waiting_queries);
   list_inithead(&q->ctx_queries);
   list_inithead(&q->hw_queries);
   q->type = query_type;
   q->ctx_id = ctx->ctx_id;

   vrend_resource_reference(&q->res, res);

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
   case PIPE_QUERY_PRIMITIVES_GENERATED:
      q->gltype = GL_PRIMITIVES_GENERATED;
      break;
   case PIPE_QUERY_PRIMITIVES_EMITTED:
      q->gltype = GL_TRANSFORM_FEEDBACK_PRIMITIVES_WRITTEN;
      break;
   default:
      fprintf(stderr,"unknown query object received %d\n", q->type);
      break;
   }

   if (vrend_is_timer_query(q->gltype))
      glGenQueries(1, &q->timer_query_id);

   ret_handle = vrend_renderer_object_insert(ctx, q, sizeof(struct vrend_query), handle,
                                             VIRGL_OBJECT_QUERY);
   if (!ret_handle) {
      FREE(q);
      return ENOMEM;
   }
   return 0;
}

static void vrend_destroy_query(struct vrend_query *query)
{
   struct vrend_nontimer_hw_query *hwq, *stor;

   vrend_resource_reference(&query->res, NULL);
   list_del(&query->ctx_queries);
   list_del(&query->waiting_queries);
   if (vrend_is_timer_query(query->gltype)) {
       glDeleteQueries(1, &query->timer_query_id);
       return;
   }
   LIST_FOR_EACH_ENTRY_SAFE(hwq, stor, &query->hw_queries, query_list) {
      glDeleteQueries(1, &hwq->id);
      FREE(hwq);
   }
   free(query);
}

static void vrend_destroy_query_object(void *obj_ptr)
{
   struct vrend_query *query = obj_ptr;
   vrend_destroy_query(query);
}

void vrend_begin_query(struct vrend_context *ctx, uint32_t handle)
{
   struct vrend_query *q;
   struct vrend_nontimer_hw_query *hwq;

   q = vrend_object_lookup(ctx->sub->object_hash, handle, VIRGL_OBJECT_QUERY);
   if (!q)
      return;

   if (q->gltype == GL_TIMESTAMP)
      return;

   if (vrend_is_timer_query(q->gltype)) {
      glBeginQuery(q->gltype, q->timer_query_id);
      return;
   }
   hwq = vrend_create_hw_query(q);
   
   /* add to active query list for this context */
   glBeginQuery(q->gltype, hwq->id);

   q->active_hw = TRUE;
   list_addtail(&q->ctx_queries, &ctx->active_nontimer_query_list);
}

void vrend_end_query(struct vrend_context *ctx, uint32_t handle)
{
   struct vrend_query *q;
   q = vrend_object_lookup(ctx->sub->object_hash, handle, VIRGL_OBJECT_QUERY);
   if (!q)
      return;

   if (vrend_is_timer_query(q->gltype)) {
      if (q->gltype == GL_TIMESTAMP)
         glQueryCounter(q->timer_query_id, q->gltype);
         /* remove from active query list for this context */
      else
         glEndQuery(q->gltype);
      return;
   }

   if (q->active_hw)
      vrend_do_end_query(q);

   list_delinit(&q->ctx_queries);
}

void vrend_get_query_result(struct vrend_context *ctx, uint32_t handle,
                            uint32_t wait)
{
   struct vrend_query *q;
   boolean ret;

   q = vrend_object_lookup(ctx->sub->object_hash, handle, VIRGL_OBJECT_QUERY);
   if (!q)
      return;

   ret = vrend_check_query(q);
   if (ret == FALSE)
      list_addtail(&q->waiting_queries, &vrend_state.waiting_query_list);
}

void vrend_render_condition(struct vrend_context *ctx,
                            uint32_t handle,
                            boolean condtion,
                            uint mode)
{
   struct vrend_query *q;
   GLenum glmode = 0;
   struct vrend_nontimer_hw_query *hwq, *last = NULL;

   if (handle == 0) {
      glEndConditionalRenderNV();
      return;
   }

   q = vrend_object_lookup(ctx->sub->object_hash, handle, VIRGL_OBJECT_QUERY);
   if (!q)
      return;

   switch (mode) {
   case PIPE_RENDER_COND_WAIT:
      glmode = GL_QUERY_WAIT;
      break;
   case PIPE_RENDER_COND_NO_WAIT:
      glmode = GL_QUERY_NO_WAIT;
      break;
   case PIPE_RENDER_COND_BY_REGION_WAIT:
      glmode = GL_QUERY_BY_REGION_WAIT;
      break;
   case PIPE_RENDER_COND_BY_REGION_NO_WAIT:
      glmode = GL_QUERY_BY_REGION_NO_WAIT;
      break;
   }

   LIST_FOR_EACH_ENTRY(hwq, &q->hw_queries, query_list)
      last = hwq;

   if (!last)
      return;
   glBeginConditionalRender(last->id, glmode);
   
}

int vrend_create_so_target(struct vrend_context *ctx,
                           uint32_t handle,
                           uint32_t res_handle,
                           uint32_t buffer_offset,
                           uint32_t buffer_size)
{
   struct vrend_so_target *target;
   struct vrend_resource *res;
   int ret_handle;
   res = vrend_renderer_ctx_res_lookup(ctx, res_handle);
   if (!res) {
      report_context_error(ctx, VIRGL_ERROR_CTX_ILLEGAL_RESOURCE, res_handle);
      return EINVAL;
   }

   target = CALLOC_STRUCT(vrend_so_target);
   if (!target)
      return ENOMEM;

   pipe_reference_init(&target->reference, 1);
   target->res_handle = res_handle;
   target->buffer_offset = buffer_offset;
   target->buffer_size = buffer_size;

   vrend_resource_reference(&target->buffer, res);

   ret_handle = vrend_renderer_object_insert(ctx, target, sizeof(*target), handle,
                                             VIRGL_OBJECT_STREAMOUT_TARGET);
   if (ret_handle) {
      FREE(target);
      return ENOMEM;
   }
   return 0;
}

static void vrender_get_glsl_version(int *glsl_version)
{
   int major_local, minor_local;
   const GLubyte *version_str;
   int c;
   int version;

   version_str = glGetString(GL_SHADING_LANGUAGE_VERSION);
   c = sscanf((const char *)version_str, "%i.%i",
              &major_local, &minor_local);
   assert(c == 2);

   version = (major_local * 100) + minor_local;
   if (glsl_version)
      *glsl_version = version;
}

void vrend_renderer_fill_caps(uint32_t set, uint32_t version,
                             union virgl_caps *caps)
{
   int i;
   GLint max;
   int gl_ver = epoxy_gl_version();
   memset(caps, 0, sizeof(*caps));

   if (set != 1 && set != 0) {
      caps->max_version = 0;
      return;
   }

   caps->max_version = 1;

   caps->v1.bset.occlusion_query = 1;
   if (gl_ver >= 30) {
      caps->v1.bset.indep_blend_enable = 1;
      caps->v1.bset.conditional_render = 1;
   } else {
      if (glewIsSupported("GL_EXT_draw_buffers2"))
         caps->v1.bset.indep_blend_enable = 1;
      if (glewIsSupported("GL_NV_conditional_render"))
         caps->v1.bset.conditional_render = 1;
   }

   if (use_core_profile) {
      caps->v1.bset.poly_stipple = 0;
      caps->v1.bset.color_clamping = 0;
   } else {
      caps->v1.bset.poly_stipple = 1;
      caps->v1.bset.color_clamping = 1;
   }
   if (gl_ver >= 31) {
      caps->v1.bset.instanceid = 1;
      glGetIntegerv(GL_MAX_VERTEX_UNIFORM_BLOCKS, &max);
      vrend_state.max_uniform_blocks = max;
      caps->v1.max_uniform_blocks = max + 1;
   } else {
      if (glewIsSupported("GL_ARB_draw_instanced"))
         caps->v1.bset.instanceid = 1;
   }

   if (vrend_state.have_nv_prim_restart || vrend_state.have_gl_prim_restart)
      caps->v1.bset.primitive_restart = 1;

   if (gl_ver >= 32) {
      caps->v1.bset.fragment_coord_conventions = 1;
      caps->v1.bset.depth_clip_disable = 1;
      caps->v1.bset.seamless_cube_map = 1;
   } else {
      if (glewIsSupported("GL_ARB_fragment_coord_conventions"))
         caps->v1.bset.fragment_coord_conventions = 1;
      if (glewIsSupported("GL_ARB_seamless_cube_map"))
         caps->v1.bset.seamless_cube_map = 1;
   }

   if (glewIsSupported("GL_AMD_seamless_cube_map_per_texture"))
      caps->v1.bset.seamless_cube_map_per_texture = 1;

   if (glewIsSupported("GL_ARB_texture_multisample")) {
       /* disable multisample until developed */
      caps->v1.bset.texture_multisample = 1;
   }
   if (gl_ver >= 40) {
      caps->v1.bset.indep_blend_func = 1;
      caps->v1.bset.cube_map_array = 1;
   } else {
      if (glewIsSupported("GL_ARB_draw_buffers_blend"))
         caps->v1.bset.indep_blend_func = 1;
      if (glewIsSupported("GL_ARB_texture_cube_map_array"))
         caps->v1.bset.cube_map_array = 1;
   }

   if (gl_ver >= 42) {
      caps->v1.bset.start_instance = 1;
   } else {
      if (glewIsSupported("GL_ARB_base_instance"))      
         caps->v1.bset.start_instance = 1;
   }         
   if (glewIsSupported("GL_ARB_shader_stencil_export"))
      caps->v1.bset.shader_stencil_export = 1;

   /* we only support up to GLSL 1.40 features now */
   caps->v1.glsl_level = 130;
   if (use_core_profile) {
      if (gl_ver == 31)
         caps->v1.glsl_level = 140;
      else if (gl_ver == 32)
         caps->v1.glsl_level = 150;
      else if (gl_ver >= 33)
         caps->v1.glsl_level = 330;
   }

   if (glewIsSupported("GL_EXT_texture_mirror_clamp"))
      caps->v1.bset.mirror_clamp = true;

   if (glewIsSupported("GL_EXT_texture_array")) {
      glGetIntegerv(GL_MAX_ARRAY_TEXTURE_LAYERS, &max);
      caps->v1.max_texture_array_layers = max;
   }
   glGetIntegerv(GL_MAX_TRANSFORM_FEEDBACK_BUFFERS, &max);
   caps->v1.max_streamout_buffers = max;
   if (glewIsSupported("GL_ARB_blend_func_extended")) {
      glGetIntegerv(GL_MAX_DUAL_SOURCE_DRAW_BUFFERS, &max);
      caps->v1.max_dual_source_render_targets = max;
   } else
      caps->v1.max_dual_source_render_targets = 0;

   glGetIntegerv(GL_MAX_DRAW_BUFFERS, &max);
   caps->v1.max_render_targets = max;

   glGetIntegerv(GL_MAX_SAMPLES, &max);
   caps->v1.max_samples = max;

   if (glewIsSupported("GL_ARB_texture_buffer_object")) {
      glGetIntegerv(GL_MAX_TEXTURE_BUFFER_SIZE, &max);
      caps->v1.max_tbo_size = max;
   }

   caps->v1.prim_mask = (1 << PIPE_PRIM_POINTS) | (1 << PIPE_PRIM_LINES) | (1 << PIPE_PRIM_LINE_STRIP) | (1 << PIPE_PRIM_LINE_LOOP) | (1 << PIPE_PRIM_TRIANGLES) | (1 << PIPE_PRIM_TRIANGLE_STRIP) | (1 << PIPE_PRIM_TRIANGLE_FAN);
   if (use_core_profile == 0) {
      caps->v1.prim_mask |= (1 << PIPE_PRIM_QUADS) | (1 << PIPE_PRIM_QUAD_STRIP) | (1 << PIPE_PRIM_POLYGON);
   }
   if (caps->v1.glsl_level >= 150)
      caps->v1.prim_mask |= (1 << PIPE_PRIM_LINES_ADJACENCY) |
         (1 << PIPE_PRIM_LINE_STRIP_ADJACENCY) |
         (1 << PIPE_PRIM_TRIANGLES_ADJACENCY) |
         (1 << PIPE_PRIM_TRIANGLE_STRIP_ADJACENCY);

   for (i = 0; i < VIRGL_FORMAT_MAX; i++) {
      uint32_t offset = i / 32;
      uint32_t index = i % 32;

      if (tex_conv_table[i].internalformat != 0) {
         if (vrend_format_can_sample(i)) {
            caps->v1.sampler.bitmask[offset] |= (1 << index);
            if (vrend_format_can_render(i))
               caps->v1.render.bitmask[offset] |= (1 << index);
         }
      }
   }

}

GLint64 vrend_renderer_get_timestamp(void)
{
   GLint64 v;
   glGetInteger64v(GL_TIMESTAMP, &v);
   return v;
}

void *vrend_renderer_get_cursor_contents(uint32_t res_handle, uint32_t *width, uint32_t *height)
{
   GLenum format, type;
   struct vrend_resource *res;
   int blsize;
   void *data, *data2;
   int size;
   int h;

   res = vrend_resource_lookup(res_handle, 0);
   if (!res)
      return NULL;

   if (res->base.width0 > 128 || res->base.height0 > 128)
      return NULL;

   if (res->target != GL_TEXTURE_2D)
      return NULL;

   *width = res->base.width0;
   *height = res->base.height0;
   format = tex_conv_table[res->base.format].glformat;
   type = tex_conv_table[res->base.format].gltype; 
   blsize = util_format_get_blocksize(res->base.format);
   size = util_format_get_nblocks(res->base.format, res->base.width0, res->base.height0) * blsize;
   data = malloc(size);
   data2 = malloc(size);

   if (!data || !data2)
      return NULL;

   glBindTexture(res->target, res->id);
   glGetnTexImageARB(res->target, 0, format, type, size, data);

   for (h = 0; h < res->base.height0; h++) {
      uint32_t doff = (res->base.height0 - h - 1) * res->base.width0 * blsize;
      uint32_t soff = h * res->base.width0 * blsize;

      memcpy(data2 + doff, data + soff, res->base.width0 * blsize);
   }
   free(data);
      
   return data2;
}

void vrend_renderer_force_ctx_0(void)
{
   struct vrend_context *ctx0 = vrend_lookup_renderer_ctx(0);
   vrend_state.current_ctx = NULL;
   vrend_state.current_hw_ctx = NULL;
   vrend_hw_switch_context(ctx0, TRUE);
   vrend_clicbs->make_current(0, ctx0->sub->gl_context);
}

void vrend_renderer_get_rect(int res_handle, struct iovec *iov, unsigned int num_iovs,
                            uint32_t offset, int x, int y, int width, int height)
{
   struct vrend_resource *res = vrend_resource_lookup(res_handle, 0);
   struct pipe_box box;
   int elsize;
   int stride;

   elsize = util_format_get_blocksize(res->base.format);
   box.x = x;
   box.y = y;
   box.z = 0;
   box.width = width;
   box.height = height;
   box.depth = 1;

   stride = util_format_get_nblocksx(res->base.format, res->base.width0) * elsize;
   vrend_renderer_transfer_send_iov(res->handle, 0,
                                   0, stride, 0, &box, offset, iov, num_iovs);
}
                                   
void vrend_renderer_attach_res_ctx(int ctx_id, int resource_id)
{
   struct vrend_context *ctx = vrend_lookup_renderer_ctx(ctx_id);
   struct vrend_resource *res = vrend_resource_lookup(resource_id, 0);

   vrend_object_insert_nofree(ctx->res_hash, res, sizeof(*res), resource_id, 1, false);
}

void vrend_renderer_detach_res_ctx(int ctx_id, int res_handle)
{
   struct vrend_context *ctx = vrend_lookup_renderer_ctx(ctx_id);
   struct vrend_resource *res = vrend_object_lookup(ctx->res_hash, res_handle, 1);

   if (!res)
      return;

   vrend_object_remove(ctx->res_hash, res_handle, 1);
}

static struct vrend_resource *vrend_renderer_ctx_res_lookup(struct vrend_context *ctx, int res_handle)
{
   struct vrend_resource *res = vrend_object_lookup(ctx->res_hash, res_handle, 1);

   return res;
}

int vrend_renderer_resource_get_info(int res_handle,
                                     struct vrend_renderer_resource_info *info)
{
   struct vrend_resource *res = vrend_resource_lookup(res_handle, 0);
   int elsize;

   if (!res)
      return -1;

   elsize = util_format_get_blocksize(res->base.format);

   info->handle = res_handle;
   info->tex_id = res->id;
   info->width = res->base.width0;
   info->height = res->base.height0;
   info->depth = res->base.depth0;
   info->format = res->base.format;
   info->flags = res->y_0_top ? VIRGL_RESOURCE_Y_0_TOP : 0;
   info->stride = util_format_get_nblocksx(res->base.format, u_minify(res->base.width0, 0)) * elsize;

   return 0;
}

void vrend_renderer_get_cap_set(uint32_t cap_set, uint32_t *max_ver,
                               uint32_t *max_size)
{ 
   if (cap_set != VREND_CAP_SET) {
      *max_ver = 0;
      *max_size = 0;
      return;
   }

   *max_ver = 1;
   *max_size = sizeof(union virgl_caps);
}

void vrend_renderer_create_sub_ctx(struct vrend_context *ctx, int sub_ctx_id)
{
   struct vrend_sub_context *sub;
   struct virgl_gl_ctx_param ctx_params;

   LIST_FOR_EACH_ENTRY(sub, &ctx->sub_ctxs, head) {
      if (sub->sub_ctx_id == sub_ctx_id) {
	 return;
      }
   }

   sub = CALLOC_STRUCT(vrend_sub_context);
   if (!sub)
      return;

   ctx_params.shared = (ctx->ctx_id == 0 && sub_ctx_id == 0) ? false : true;
   ctx_params.major_ver = renderer_gl_major;
   ctx_params.minor_ver = renderer_gl_minor;
   sub->gl_context = vrend_clicbs->create_gl_context(0, &ctx_params);
   vrend_clicbs->make_current(0, sub->gl_context);

   sub->sub_ctx_id = sub_ctx_id;

   glGenVertexArrays(1, &sub->vaoid);
   glGenFramebuffers(1, &sub->fb_id);
   glGenFramebuffers(2, sub->blit_fb_ids);

   list_inithead(&sub->programs);
   sub->object_hash = vrend_object_init_ctx_table();
   vrend_bind_va(sub->vaoid);

   ctx->sub = sub;
   list_add(&sub->head, &ctx->sub_ctxs);
   if (sub_ctx_id == 0)
	ctx->sub0 = sub;
}


void vrend_renderer_destroy_sub_ctx(struct vrend_context *ctx, int sub_ctx_id)
{
   struct vrend_sub_context *sub, *tofree = NULL;

   /* never destroy sub context id 0 */
   if (sub_ctx_id == 0)
      return;

   LIST_FOR_EACH_ENTRY(sub, &ctx->sub_ctxs, head) {
      if (sub->sub_ctx_id == sub_ctx_id) {
	 tofree = sub;
      }      
   }

   if (tofree) {
      if (ctx->sub == tofree) {
         ctx->sub = ctx->sub0;
	 vrend_clicbs->make_current(0, ctx->sub->gl_context);
      }
      vrend_destroy_sub_context(tofree);
   }
}

void vrend_renderer_set_sub_ctx(struct vrend_context *ctx, int sub_ctx_id)
{
   struct vrend_sub_context *sub;
   /* find the sub ctx */

   if (ctx->sub && ctx->sub->sub_ctx_id == sub_ctx_id)
      return;

   LIST_FOR_EACH_ENTRY(sub, &ctx->sub_ctxs, head) {
      if (sub->sub_ctx_id == sub_ctx_id) {
	 ctx->sub = sub;
	 vrend_clicbs->make_current(0, sub->gl_context);
	 break;
      }
   }
}	 

