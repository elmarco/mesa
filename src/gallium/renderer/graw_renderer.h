#ifndef GRAW_RENDERER_H
#define GRAW_RENDERER_H

#include "graw_protocol.h"
#include "graw_iov.h"
struct grend_context;

struct grend_resource;


void grend_create_vs(struct grend_context *ctx,
                     uint32_t handle,
                     const struct pipe_shader_state *vs);

void grend_create_fs(struct grend_context *ctx,
                     uint32_t handle,
                     const struct pipe_shader_state *vs);

void grend_bind_vs(struct grend_context *ctx,
                   uint32_t handle);

void grend_bind_fs(struct grend_context *ctx,
                   uint32_t handle);

void grend_clear(struct grend_context *ctx,
                 unsigned buffers,
                 const union pipe_color_union *color,
                 double depth, unsigned stencil);

void grend_draw_vbo(struct grend_context *ctx,
                    const struct pipe_draw_info *info);

void grend_set_framebuffer_state(struct grend_context *ctx,
                                 uint32_t nr_cbufs, uint32_t surf_handle[8],
   uint32_t zsurf_handle);

void grend_flush(struct grend_context *ctx);


void grend_flush_frontbuffer(uint32_t res_handle);
struct grend_context *grend_create_context(int id);
bool grend_destroy_context(struct grend_context *ctx);
void graw_renderer_context_create(uint32_t handle);
void graw_renderer_context_create_internal(uint32_t handle);
void graw_renderer_context_destroy(uint32_t handle);

void graw_renderer_resource_create(uint32_t handle, enum pipe_texture_target target, uint32_t format, uint32_t bind, uint32_t width, uint32_t height, uint32_t depth, uint32_t array_size, uint32_t last_level, uint32_t nr_samples);
void graw_renderer_resource_unref(uint32_t handle);

void grend_create_surface(struct grend_context *ctx,
                          uint32_t handle,
                          uint32_t res_handle, uint32_t format,
                          uint32_t val0, uint32_t val1);
void grend_create_sampler_view(struct grend_context *ctx,
                               uint32_t handle,
                               uint32_t res_handle, uint32_t format,
                               uint32_t val0, uint32_t val1, uint32_t swizzle_packed);

void grend_create_vertex_elements_state(struct grend_context *ctx,
                                        uint32_t handle,
                                        unsigned num_elements,
                                        const struct pipe_vertex_element *elements);
void grend_bind_vertex_elements_state(struct grend_context *ctx,
                                      uint32_t handle);

void grend_set_single_vbo(struct grend_context *ctx,
                         int index,
                         uint32_t stride,
                         uint32_t buffer_offset,
                         uint32_t res_handle);
void grend_set_num_vbo(struct grend_context *ctx,
                      int num_vbo);

void grend_transfer_inline_write(struct grend_context *ctx,
                                 uint32_t res_handle,
                                 unsigned level,
                                 unsigned usage,
                                 const struct pipe_box *box,
                                 const void *data,
                                 unsigned stride,
                                 unsigned layer_stride);

void grend_set_viewport_state(struct grend_context *ctx,
                              const struct pipe_viewport_state *state);
void grend_set_num_sampler_views(struct grend_context *ctx,
                                 uint32_t shader_type,
                                 int num_sampler_views);
void grend_set_single_sampler_view(struct grend_context *ctx,
                                   uint32_t shader_type,
                                   int index,
                                   uint32_t res_handle);

void grend_object_bind_blend(struct grend_context *ctx,
                             uint32_t handle);
void grend_object_bind_dsa(struct grend_context *ctx,
                             uint32_t handle);
void grend_object_bind_rasterizer(struct grend_context *ctx,
                                  uint32_t handle);

void grend_bind_sampler_states(struct grend_context *ctx,
                               uint32_t shader_type,
                               uint32_t num_states,
                               uint32_t *handles);
void grend_set_index_buffer(struct grend_context *ctx,
                            uint32_t res_handle,
                            uint32_t index_size,
                            uint32_t offset);

void graw_renderer_transfer_write_iov(uint32_t handle, int level,
                                      uint32_t src_stride,
                                      uint32_t flags,
                                      struct pipe_box *dst_box,
                                      uint64_t offset,
                                      struct graw_iovec *iovec,
                                      unsigned int iovec_cnt);

void graw_renderer_resource_copy_region(struct grend_context *ctx,
                                        uint32_t dst_handle, uint32_t dst_level,
                                        uint32_t dstx, uint32_t dsty, uint32_t dstz,
                                        uint32_t src_handle, uint32_t src_level,
                                        const struct pipe_box *src_box);

void graw_renderer_blit(struct grend_context *ctx,
                        uint32_t dst_handle, uint32_t src_handle,
                        const struct pipe_blit_info *info);

void graw_renderer_transfer_send_iov(uint32_t handle, uint32_t level, uint32_t flags,struct pipe_box *box, uint64_t offset, struct graw_iovec *iov, int iovec_cnt);
void grend_set_stencil_ref(struct grend_context *ctx, struct pipe_stencil_ref *ref);
void grend_set_blend_color(struct grend_context *ctx, struct pipe_blend_color *color);
void grend_set_scissor_state(struct grend_context *ctx, struct pipe_scissor_state *ss);

void grend_set_constants(struct grend_context *ctx,
                         uint32_t shader,
                         uint32_t index,
                         uint32_t num_constant,
                         float *data);

void graw_transfer_write_return(void *data, uint32_t bytes, uint64_t offset,
                                struct graw_iovec *iov, int iovec_cnt);

void graw_transfer_write_tex_return(struct pipe_resource *res,
				    struct pipe_box *box,
                                    uint32_t level,
                                    uint64_t offset,
                                    struct graw_iovec *iov,
                                    int num_iovs,
				    void *myptr, int size, int invert);

int graw_renderer_set_scanout(uint32_t res_handle,
                              struct pipe_box *box);

int graw_renderer_flush_buffer(uint32_t res_handle,
                               struct pipe_box *box);

void graw_renderer_fini(void);
void graw_reset_decode(void);

void graw_decode_block_iov(struct graw_iovec *iov, uint32_t niovs, uint64_t offset, int ndw);

int graw_renderer_create_fence(int client_fence_id);

void graw_write_fence(unsigned fence_id);
void graw_renderer_check_fences(void);

int swap_buffers(void);
void grend_hw_switch_context(struct grend_context *ctx);
void graw_renderer_object_insert(struct grend_context *ctx, void *data,
                                 uint32_t size, uint32_t handle, enum graw_object_type type);
void graw_renderer_object_destroy(struct grend_context *ctx, uint32_t handle);

void grend_create_query(struct grend_context *ctx, uint32_t handle,
                           uint32_t query_type);

#endif
