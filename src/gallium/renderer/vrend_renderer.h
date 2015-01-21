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

#ifndef VREND_RENDERER_H
#define VREND_RENDERER_H

#include "pipe/p_state.h"
#include "util/u_inlines.h"
#include "virgl_protocol.h"
#include "vrend_iov.h"
#include "virgl_hw.h"

typedef void *virgl_gl_context;
typedef void *virgl_gl_drawable;

struct virgl_gl_ctx_param {
   bool shared;
   int major_ver;
   int minor_ver;
};

extern int vrend_dump_shaders;
struct vrend_context;

struct vrend_resource {
   struct pipe_resource base;
   GLuint id;
   GLenum target;
   /* fb id if we need to readback this resource */
   GLuint readback_fb_id;
   GLuint readback_fb_level;
   GLuint readback_fb_z;
   void *ptr;
   GLuint handle;

   struct iovec *iov;
   uint32_t num_iovs;
   boolean y_0_top;

   boolean scannedout;
   GLuint tbo_tex_id;/* tbos have two ids to track */
};

/* assume every format is sampler friendly */
#define VREND_BIND_SAMPLER (1 << 0)
#define VREND_BIND_RENDER (1 << 1)
#define VREND_BIND_DEPTHSTENCIL (1 << 2)

#define VREND_BIND_NEED_SWIZZLE (1 << 28)

struct vrend_format_table {
   enum virgl_formats format;
   GLenum internalformat;
   GLenum glformat;
   GLenum gltype;
   uint32_t bindings;
   int flags;
   uint8_t swizzle[4];
};

struct vrend_if_cbs {
   void (*write_fence)(unsigned fence_id);

   virgl_gl_context (*create_gl_context)(int scanout, struct virgl_gl_ctx_param *params);
   void (*destroy_gl_context)(virgl_gl_context ctx);
   int (*make_current)(int scanout, virgl_gl_context ctx);
};
void vrend_renderer_init(struct vrend_if_cbs *cbs);

void vrend_insert_format(struct vrend_format_table *entry, uint32_t bindings);
void vrend_insert_format_swizzle(int override_format, struct vrend_format_table *entry, uint32_t bindings, uint8_t swizzle[4]);
void vrend_create_vs(struct vrend_context *ctx,
                     uint32_t handle,
                     const struct pipe_shader_state *vs);

void vrend_create_gs(struct vrend_context *ctx,
                     uint32_t handle,
                     const struct pipe_shader_state *gs);

void vrend_create_fs(struct vrend_context *ctx,
                     uint32_t handle,
                     const struct pipe_shader_state *vs);

void vrend_bind_vs(struct vrend_context *ctx,
                   uint32_t handle);

void vrend_bind_gs(struct vrend_context *ctx,
                   uint32_t handle);

void vrend_bind_fs(struct vrend_context *ctx,
                   uint32_t handle);

void vrend_bind_vs_so(struct vrend_context *ctx,
                      uint32_t handle);
void vrend_clear(struct vrend_context *ctx,
                 unsigned buffers,
                 const union pipe_color_union *color,
                 double depth, unsigned stencil);

void vrend_draw_vbo(struct vrend_context *ctx,
                    const struct pipe_draw_info *info);

void vrend_set_framebuffer_state(struct vrend_context *ctx,
                                 uint32_t nr_cbufs, uint32_t surf_handle[8],
   uint32_t zsurf_handle);

void vrend_flush(struct vrend_context *ctx);


void vrend_flush_frontbuffer(uint32_t res_handle);
struct vrend_context *vrend_create_context(int id, uint32_t nlen, const char *debug_name);
bool vrend_destroy_context(struct vrend_context *ctx);
void vrend_renderer_context_create(uint32_t handle, uint32_t nlen, const char *name);
void vrend_renderer_context_create_internal(uint32_t handle, uint32_t nlen, const char *name);
void vrend_renderer_context_destroy(uint32_t handle);

struct vrend_renderer_resource_create_args {
   uint32_t handle;
   enum pipe_texture_target target;
   uint32_t format;
   uint32_t bind;
   uint32_t width;
   uint32_t height;
   uint32_t depth;
   uint32_t array_size;
   uint32_t last_level;
   uint32_t nr_samples;
   uint32_t flags;
};
     
void vrend_renderer_resource_create(struct vrend_renderer_resource_create_args *args, struct iovec *iov, uint32_t num_iovs);

void vrend_renderer_resource_unref(uint32_t handle);

void vrend_create_surface(struct vrend_context *ctx,
                          uint32_t handle,
                          uint32_t res_handle, uint32_t format,
                          uint32_t val0, uint32_t val1);
void vrend_create_sampler_view(struct vrend_context *ctx,
                               uint32_t handle,
                               uint32_t res_handle, uint32_t format,
                               uint32_t val0, uint32_t val1, uint32_t swizzle_packed);

void vrend_create_so_target(struct vrend_context *ctx,
                            uint32_t handle,
                            uint32_t res_handle,
                            uint32_t buffer_offset,
                            uint32_t buffer_size);
void vrend_set_streamout_targets(struct vrend_context *ctx,
                                 uint32_t append_bitmask,
                                 uint32_t num_targets,
                                 uint32_t *handles);

void vrend_create_vertex_elements_state(struct vrend_context *ctx,
                                        uint32_t handle,
                                        unsigned num_elements,
                                        const struct pipe_vertex_element *elements);
void vrend_bind_vertex_elements_state(struct vrend_context *ctx,
                                      uint32_t handle);

void vrend_set_single_vbo(struct vrend_context *ctx,
                         int index,
                         uint32_t stride,
                         uint32_t buffer_offset,
                         uint32_t res_handle);
void vrend_set_num_vbo(struct vrend_context *ctx,
                      int num_vbo);

void vrend_transfer_inline_write(struct vrend_context *ctx,
                                 uint32_t res_handle,
                                 unsigned level,
                                 unsigned usage,
                                 const struct pipe_box *box,
                                 const void *data,
                                 unsigned stride,
                                 unsigned layer_stride);

void vrend_set_viewport_state(struct vrend_context *ctx,
                              const struct pipe_viewport_state *state);
void vrend_set_num_sampler_views(struct vrend_context *ctx,
                                 uint32_t shader_type,
                                 uint32_t start_slot,
                                 int num_sampler_views);
void vrend_set_single_sampler_view(struct vrend_context *ctx,
                                   uint32_t shader_type,
                                   int index,
                                   uint32_t res_handle);

void vrend_object_bind_blend(struct vrend_context *ctx,
                             uint32_t handle);
void vrend_object_bind_dsa(struct vrend_context *ctx,
                             uint32_t handle);
void vrend_object_bind_rasterizer(struct vrend_context *ctx,
                                  uint32_t handle);

void vrend_bind_sampler_states(struct vrend_context *ctx,
                               uint32_t shader_type,
                               uint32_t start_slot,
                               uint32_t num_states,
                               uint32_t *handles);
void vrend_set_index_buffer(struct vrend_context *ctx,
                            uint32_t res_handle,
                            uint32_t index_size,
                            uint32_t offset);

void vrend_renderer_transfer_write_iov(uint32_t handle, 
                                      uint32_t ctx_id,
                                      int level,
                                      uint32_t stride,
                                      uint32_t layer_stride,
                                      struct pipe_box *box,
                                      uint64_t offset,
                                      struct iovec *iovec,
                                      unsigned int iovec_cnt);

void vrend_renderer_resource_copy_region(struct vrend_context *ctx,
                                        uint32_t dst_handle, uint32_t dst_level,
                                        uint32_t dstx, uint32_t dsty, uint32_t dstz,
                                        uint32_t src_handle, uint32_t src_level,
                                        const struct pipe_box *src_box);

void vrend_renderer_blit(struct vrend_context *ctx,
                        uint32_t dst_handle, uint32_t src_handle,
                        const struct pipe_blit_info *info);

void vrend_renderer_transfer_send_iov(uint32_t handle, uint32_t ctx_id,
                                     uint32_t level, uint32_t stride,
                                     uint32_t layer_stride,
                                     struct pipe_box *box,
                                     uint64_t offset, struct iovec *iov,
                                     int iovec_cnt);
void vrend_set_stencil_ref(struct vrend_context *ctx, struct pipe_stencil_ref *ref);
void vrend_set_blend_color(struct vrend_context *ctx, struct pipe_blend_color *color);
void vrend_set_scissor_state(struct vrend_context *ctx, struct pipe_scissor_state *ss);

void vrend_set_polygon_stipple(struct vrend_context *ctx, struct pipe_poly_stipple *ps);

void vrend_set_clip_state(struct vrend_context *ctx, struct pipe_clip_state *ucp);
void vrend_set_sample_mask(struct vrend_context *ctx, unsigned sample_mask);

void vrend_set_constants(struct vrend_context *ctx,
                         uint32_t shader,
                         uint32_t index,
                         uint32_t num_constant,
                         float *data);

void vrend_transfer_write_return(void *data, uint32_t bytes, uint64_t offset,
                                struct iovec *iov, int iovec_cnt);

void vrend_transfer_write_tex_return(struct pipe_resource *res,
				    struct pipe_box *box,
                                    uint32_t level,
                                    uint32_t dst_stride,
                                    uint64_t offset,
                                    struct iovec *iov,
                                    int num_iovs,
				    void *myptr, int size, int invert);

void vrend_renderer_fini(void);
void vrend_reset_decode(void);

void vrend_decode_block(uint32_t ctx_id, uint32_t *block, int ndw);
struct vrend_context *vrend_lookup_renderer_ctx(uint32_t ctx_id);

int vrend_renderer_create_fence(int client_fence_id, uint32_t ctx_id);

void vrend_renderer_check_fences(void);
void vrend_renderer_check_queries(void);
void vrend_stop_current_queries(void);

boolean vrend_hw_switch_context(struct vrend_context *ctx, boolean now);
void vrend_renderer_object_insert(struct vrend_context *ctx, void *data,
                                 uint32_t size, uint32_t handle, enum virgl_object_type type);
void vrend_renderer_object_destroy(struct vrend_context *ctx, uint32_t handle);

void vrend_create_query(struct vrend_context *ctx, uint32_t handle,
                        uint32_t query_type, uint32_t res_handle,
                        uint32_t offset);

void vrend_begin_query(struct vrend_context *ctx, uint32_t handle);
void vrend_end_query(struct vrend_context *ctx, uint32_t handle);
void vrend_get_query_result(struct vrend_context *ctx, uint32_t handle,
                            uint32_t wait);
void vrend_render_condition(struct vrend_context *ctx,
                            uint32_t handle,
                            boolean condtion,
                            uint mode);
void *vrend_renderer_get_cursor_contents(uint32_t res_handle, uint32_t *width, uint32_t *height);
void vrend_use_program(GLuint program_id);
void vrend_blend_enable(GLboolean blend_enable);
void vrend_depth_test_enable(GLboolean depth_test_enable);
void vrend_bind_va(GLuint vaoid);
int vrend_renderer_flush_buffer_res(struct vrend_resource *res,
                                   struct pipe_box *box);

void vrend_renderer_fill_caps(uint32_t set, uint32_t version,
                             union virgl_caps *caps);

GLint64 vrend_renderer_get_timestamp(void);
/* formats */
void vrend_build_format_list(void);

int vrend_renderer_resource_attach_iov(int res_handle, struct iovec *iov,
                                       int num_iovs);
void vrend_renderer_resource_detach_iov(int res_handle,
					struct iovec **iov_p,
					int *num_iovs_p);
void vrend_renderer_resource_destroy(struct vrend_resource *res);

static INLINE void
vrend_resource_reference(struct vrend_resource **ptr, struct vrend_resource *tex)
{
   struct vrend_resource *old_tex = *ptr;

   if (pipe_reference(&(*ptr)->base.reference, &tex->base.reference))
      vrend_renderer_resource_destroy(old_tex);
   *ptr = tex;
}

void vrend_renderer_force_ctx_0(void);

void vrend_renderer_get_rect(int resource_id, struct iovec *iov, unsigned int num_iovs,
                            uint32_t offset, int x, int y, int width, int height);
void vrend_renderer_attach_res_ctx(int ctx_id, int resource_id);
void vrend_renderer_detach_res_ctx(int ctx_id, int resource_id);


struct vrend_renderer_resource_info {
   uint32_t handle;
   uint32_t format;
   uint32_t width;
   uint32_t height;
   uint32_t depth;
   uint32_t flags;
   uint32_t tex_id;
   uint32_t stride;
};

int vrend_renderer_resource_get_info(int res_handle,
                                     struct vrend_renderer_resource_info *info);

#define VREND_CAP_SET 1

void vrend_renderer_get_cap_set(uint32_t cap_set, uint32_t *max_ver,
                               uint32_t *max_size);
#endif
