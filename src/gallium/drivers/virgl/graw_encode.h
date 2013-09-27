
#ifndef GRAW_ENCODER_H
#define GRAW_ENCODER_H

#include <stdio.h>

#include "graw_context.h"
struct graw_surface {
   struct pipe_surface base;
   uint32_t handle;
};

static inline void graw_encoder_write_dword(struct virgl_cmd_buf *state,
                                            uint32_t dword)
{
   state->buf[state->cdw++] = dword;
}

static inline void graw_encoder_write_qword(struct virgl_cmd_buf *state,
                                            uint64_t qword)
{
   memcpy(state->buf + state->cdw, &qword, sizeof(uint64_t));
   state->cdw += 2;
}

static inline void graw_encoder_write_block(struct virgl_cmd_buf *state,
					    const uint8_t *ptr, uint32_t len)
{
   int x;
   memcpy(state->buf + state->cdw, ptr, len);
   x = (len % 4);
//   fprintf(stderr, "[%d] block %d x is %d\n", state->cdw, len, x);
   if (x) {
      uint8_t *mp = (uint8_t *)(state->buf + state->cdw);
      mp += len;
      memset(mp, 0, x);
   }
   state->cdw += (len + 3) / 4;
}

extern int graw_encode_blend_state(struct graw_context *ctx,
                                   uint32_t handle,
                                   const struct pipe_blend_state *blend_state);
extern int graw_encode_rasterizer_state(struct graw_context *ctx,
                                         uint32_t handle,
                                         const struct pipe_rasterizer_state *state);

extern int graw_encode_shader_state(struct graw_context *ctx,
                                    uint32_t handle,
                                    uint32_t type,
                                    const struct pipe_shader_state *shader);

int graw_encode_stream_output_info(struct graw_context *ctx,
                                   uint32_t handle,
                                   uint32_t type,
                                   const struct pipe_shader_state *shader);

int graw_encoder_set_so_targets(struct graw_context *ctx,
                                unsigned num_targets,
                                struct pipe_stream_output_target **targets,
                                unsigned append_bitmask);

int graw_encoder_create_so_target(struct graw_context *ctx,
                                  uint32_t handle,
                                  struct virgl_resource *res,
                                  unsigned buffer_offset,
                                  unsigned buffer_size);

int graw_encode_clear(struct graw_context *ctx,
                      unsigned buffers,
                      const union pipe_color_union *color,
                      double depth, unsigned stencil);

int graw_encode_bind_object(struct graw_context *ctx,
			    uint32_t handle, uint32_t object);
int graw_encode_delete_object(struct graw_context *ctx,
                              uint32_t handle, uint32_t object);

int graw_encoder_set_framebuffer_state(struct graw_context *ctx,
				       const struct pipe_framebuffer_state *state);
int graw_encoder_set_viewport_state(struct graw_context *ctx,
				    const struct pipe_viewport_state *state);

int graw_encoder_draw_vbo(struct graw_context *ctx,
			  const struct pipe_draw_info *info);


int graw_encoder_create_surface(struct graw_context *ctx,
                                uint32_t handle,
                                struct virgl_resource *res,
				const struct pipe_surface *templat);

int graw_encoder_flush_frontbuffer(struct graw_context *ctx,
                                   struct virgl_resource *res);

int graw_encoder_create_vertex_elements(struct graw_context *ctx,
                                        uint32_t handle,
                                        unsigned num_elements,
                                        const struct pipe_vertex_element *element);

int graw_encoder_set_vertex_buffers(struct graw_context *ctx,
                                    unsigned num_buffers,
                                    const struct pipe_vertex_buffer *buffers);


int graw_encoder_inline_write(struct graw_context *ctx,
                              struct virgl_resource *res,
                              unsigned level, unsigned usage,
                              const struct pipe_box *box,
                              const void *data, unsigned stride,
                              unsigned layer_stride);
int graw_encode_sampler_state(struct graw_context *ctx,
                              uint32_t handle,
                              const struct pipe_sampler_state *state);
int graw_encode_sampler_view(struct graw_context *ctx,
                             uint32_t handle,
                             struct virgl_resource *res,
                             const struct pipe_sampler_view *state);

int graw_encode_set_sampler_views(struct graw_context *ctx,
                                  uint32_t shader_type,
                                  uint32_t num_views,
                                  struct graw_sampler_view **views);

int graw_encode_bind_sampler_states(struct graw_context *ctx,
                                    uint32_t shader_type,
                                    uint32_t num_handles,
                                    uint32_t *handles);

int graw_encoder_set_index_buffer(struct graw_context *ctx,
                                  const struct pipe_index_buffer *ib);
                                  
uint32_t graw_object_assign_handle(void);

int graw_encoder_write_constant_buffer(struct graw_context *ctx,
                                       uint32_t shader,
                                       uint32_t index,
                                       uint32_t size,
                                       const void *data);
int graw_encode_dsa_state(struct graw_context *ctx,
                          uint32_t handle,
                          const struct pipe_depth_stencil_alpha_state *dsa_state);

int graw_encoder_set_stencil_ref(struct graw_context *ctx,
                                 const struct pipe_stencil_ref *ref);

int graw_encoder_set_blend_color(struct graw_context *ctx,
                                 const struct pipe_blend_color *color);

int graw_encoder_set_scissor_state(struct graw_context *ctx,
                                   const struct pipe_scissor_state *ss);

void graw_encoder_set_polygon_stipple(struct graw_context *ctx,
                                      const struct pipe_poly_stipple *ps);

void graw_encoder_set_sample_mask(struct graw_context *ctx,
                                  unsigned sample_mask);

void graw_encoder_set_clip_state(struct graw_context *ctx,
                                 const struct pipe_clip_state *clip);

int graw_encode_resource_copy_region(struct graw_context *ctx,
                                     struct virgl_resource *dst_res,
                                     unsigned dst_level,
                                     unsigned dstx, unsigned dsty, unsigned dstz,
                                     struct virgl_resource *src_res,
                                     unsigned src_level,
                                     const struct pipe_box *src_box);

int graw_encode_blit(struct graw_context *ctx,
                     struct virgl_resource *dst_res,
                     struct virgl_resource *src_res,
                     const struct pipe_blit_info *blit);

int graw_encoder_create_query(struct graw_context *ctx,
                              uint32_t handle,
                              uint query_type,
                              struct virgl_resource *res,
                              uint32_t offset);

int graw_encoder_begin_query(struct graw_context *ctx,
                             uint32_t handle);
int graw_encoder_end_query(struct graw_context *ctx,
                           uint32_t handle);
int graw_encoder_get_query_result(struct graw_context *ctx,
                                  uint32_t handle, boolean wait);

int graw_encoder_set_query_state(struct graw_context *ctx,
                                 boolean query_enabled);
#endif
