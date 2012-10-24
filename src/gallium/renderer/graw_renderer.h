#ifndef GRAW_RENDERER_H
#define GRAW_RENDERER_H

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
                                 uint32_t nr_cbufs, uint32_t surf_handle,
   uint32_t zsurf_handle);

void grend_flush(struct grend_context *ctx);


void grend_flush_frontbuffer(uint32_t res_handle);
struct grend_context *grend_create_context(void);

void graw_renderer_resource_create(uint32_t handle, enum pipe_texture_target target, uint32_t format, uint32_t bind, uint32_t width, uint32_t height);

void grend_create_surface(struct grend_context *ctx,
                          uint32_t handle,
                          uint32_t res_handle);

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
void grend_set_num_fs_sampler_views(struct grend_context *ctx,
                                    int num_fs_sampler_views);
void grend_set_single_fs_sampler_view(struct grend_context *ctx,
                                      int index,
                                      uint32_t res_handle);

void grend_object_bind_blend(struct grend_context *ctx,
                             uint32_t handle);
void grend_object_bind_dsa(struct grend_context *ctx,
                             uint32_t handle);
void grend_object_bind_rasterizer(struct grend_context *ctx,
                                  uint32_t handle);

void grend_object_bind_sampler_states(struct grend_context *ctx,
                                      uint32_t num_states,
                                      uint32_t *handles);
void grend_set_index_buffer(struct grend_context *ctx,
                            uint32_t res_handle,
                            uint32_t index_size,
                            uint32_t offset);

void graw_renderer_transfer_write(uint32_t handle, struct pipe_box *transfer_box,
                                  struct pipe_box *box,
                                  void *data);

void graw_renderer_transfer_send(uint32_t handle, struct pipe_box *box);


void grend_set_constants(struct grend_context *ctx,
                         uint32_t shader,
                         uint32_t index,
                         uint32_t num_constant,
                         float *data);

void graw_transfer_write_return(void *data, uint32_t ndw);
#endif
