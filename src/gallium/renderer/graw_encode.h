
#ifndef GRAW_ENCODER_H
#define GRAW_ENCODER_H

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
   state->buf[state->buf_offset++] = dword;
}

static inline void graw_encoder_write_qword(struct graw_encoder_state *state,
                                            uint32_t qword)
{
   memcpy(state->buf + state->buf_offset, &qword, sizeof(uint64_t));
   state->buf_offset += 2;
}

static inline void graw_encoder_write_block(struct graw_encoder_state *state,
					    uint8_t *ptr, uint32_t len)
{
   memcpy(state->buf + state->buf_offset, ptr, len);
   state->buf_offset += (len + 3) / 4;
}

extern struct graw_encoder_state *graw_encoder_init_queue(void);

extern int graw_encode_blend_state(struct graw_encoder_state *enc,
                                   uint32_t handle,
                                   struct pipe_blend_state *blend_state);
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


#endif
