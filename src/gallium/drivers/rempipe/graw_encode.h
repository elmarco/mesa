
#ifndef GRAW_ENCODER_H
#define GRAW_ENCODER_H

#include <stdio.h>
struct graw_surface {
   struct pipe_surface base;
   uint32_t handle;
};

struct graw_encoder_state {
   uint32_t *buf;
   uint32_t buf_total;
   uint32_t buf_offset;

   /* for testing purposes */
   uint32_t buf_read_offset;

   void (*flush)(struct graw_encoder_state *state, void *closure);
   void *closure;
};

static inline void graw_encoder_write_dword(struct graw_encoder_state *state,
                                            uint32_t dword)
{
   fprintf(stderr,"[%d] 0x%x\n", state->buf_offset, dword);
   state->buf[state->buf_offset++] = dword;
}

static inline void graw_encoder_write_qword(struct graw_encoder_state *state,
                                            uint64_t qword)
{
   memcpy(state->buf + state->buf_offset, &qword, sizeof(uint64_t));
   state->buf_offset += 2;
}

static inline void graw_encoder_write_block(struct graw_encoder_state *state,
					    uint8_t *ptr, uint32_t len)
{
   int x;
   memcpy(state->buf + state->buf_offset, ptr, len);
   x = (len % 4);
   fprintf(stderr, "[%d] block %d x is %d\n", state->buf_offset, len, x);
   if (x) {
      uint8_t *mp = state->buf + state->buf_offset;
      mp += len;
      memset(mp, 0, x);
   }
   state->buf_offset += (len + 3) / 4;
}

extern struct graw_encoder_state *graw_encoder_init_queue(void);

extern int graw_encode_blend_state(struct graw_encoder_state *enc,
                                   uint32_t handle,
                                   const struct pipe_blend_state *blend_state);
extern int graw_encode_rasterizer_state(struct graw_encoder_state *enc,
                                         uint32_t handle,
                                         const struct pipe_rasterizer_state *state);

extern int graw_encode_shader_state(struct graw_encoder_state *enc,
                                    uint32_t handle,
                                    uint32_t type,
                                    const struct pipe_shader_state *shader);

int graw_encode_clear(struct graw_encoder_state *graw,
                      unsigned buffers,
                      const union pipe_color_union *color,
                      double depth, unsigned stencil);

int graw_encode_bind_object(struct graw_encoder_state *enc,
			    uint32_t handle, uint32_t object);
int graw_encode_delete_object(struct graw_encoder_state *enc,
                              uint32_t handle, uint32_t object);

int graw_encoder_set_framebuffer_state(struct graw_encoder_state *enc,
				       const struct pipe_framebuffer_state *state);
int graw_encoder_set_viewport_state(struct graw_encoder_state *enc,
				    const struct pipe_viewport_state *state);

int graw_encoder_draw_vbo(struct graw_encoder_state *enc,
			  const struct pipe_draw_info *info);


int graw_encoder_create_surface(struct graw_encoder_state *enc,
                                uint32_t handle,
				uint32_t res_handle,
				const struct pipe_surface *templat);

int graw_encoder_flush_frontbuffer(struct graw_encoder_state *enc,
                                   uint32_t res_handle);

int graw_encoder_create_vertex_elements(struct graw_encoder_state *enc,
                                        uint32_t handle,
                                        unsigned num_elements,
                                        const struct pipe_vertex_element *element);

int graw_encoder_set_vertex_buffers(struct graw_encoder_state *enc,
                                    unsigned num_buffers,
                                    const struct pipe_vertex_buffer *buffers,
                                    uint32_t *res_handles);


int graw_encoder_inline_write(struct graw_encoder_state *enc,
                              uint32_t res_handle,
                              unsigned level, unsigned usage,
                              const struct pipe_box *box,
                              void *data, unsigned stride,
                              unsigned layer_stride);
int graw_encode_sampler_state(struct graw_encoder_state *enc,
                              uint32_t handle,
                              const struct pipe_sampler_state *state);
int graw_encode_sampler_view(struct graw_encoder_state *enc,
                             uint32_t handle,
                             uint32_t res_handle,
                             const struct pipe_sampler_view *state);

int graw_encode_set_fragment_sampler_views(struct graw_encoder_state *enc,
                                           uint32_t num_handles,
                                           uint32_t *handles);
int graw_encode_set_vertex_sampler_views(struct graw_encoder_state *enc,
                                           uint32_t num_handles,
                                           uint32_t *handles);
int graw_encode_bind_fragment_sampler_states(struct graw_encoder_state *enc,
                                             uint32_t num_handles,
                                             uint32_t *handles);

int graw_encoder_set_index_buffer(struct graw_encoder_state *enc,
                                  const struct pipe_index_buffer *ib,
                                  uint32_t res_handle);
uint32_t graw_object_assign_handle(void);

int graw_encoder_write_constant_buffer(struct graw_encoder_state *enc,
                                       uint32_t shader,
                                       uint32_t index,
                                       uint32_t size,
                                       void *data);
int graw_encode_dsa_state(struct graw_encoder_state *enc,
                          uint32_t handle,
                          const struct pipe_depth_stencil_alpha_state *dsa_state);

int graw_encoder_set_stencil_ref(struct graw_encoder_state *enc,
                                 const struct pipe_stencil_ref *ref);

int graw_encoder_set_blend_color(struct graw_encoder_state *enc,
                                 const struct pipe_blend_color *color);

int graw_encoder_set_scissor_state(struct graw_encoder_state *enc,
                                   const struct pipe_scissor_state *ss);
int graw_encode_resource_copy_region(struct graw_encoder_state *enc,
                                     uint32_t dst_res_handle,
                                     unsigned dst_level,
                                     unsigned dstx, unsigned dsty, unsigned dstz,
                                     uint32_t src_res_handle,
                                     unsigned src_level,
                                     const struct pipe_box *src_box);

int graw_encode_blit(struct graw_encoder_state *enc,
                     uint32_t dst_handle, uint32_t src_handle,
                     const struct pipe_blit_info *blit);
#endif
