#include <stdint.h>
#include <string.h>

#include "util/u_memory.h"
#include "pipe/p_state.h"
#include "graw_protocol.h"
#include "graw_encode.h"
#include "graw_object.h"
#include "tgsi/tgsi_dump.h"

static unsigned uif(float f)
{
   union { float f; unsigned int ui; } myuif;
   myuif.f = f;
   return myuif.ui;
}

int graw_encode_bind_object(struct graw_encoder_state *enc,
			    uint32_t handle, uint32_t object)
{
   graw_encoder_write_dword(enc, GRAW_CMD0(GRAW_BIND_OBJECT, object, 1));
   graw_encoder_write_dword(enc, handle);
}

int graw_encode_delete_object(struct graw_encoder_state *enc,
			      uint32_t handle, uint32_t object)
{
   graw_encoder_write_dword(enc, GRAW_CMD0(GRAW_DESTROY_OBJECT, object, 1));
   graw_encoder_write_dword(enc, handle);
}

int graw_encode_blend_state(struct graw_encoder_state *enc,
                            uint32_t handle,
                            struct pipe_blend_state *blend_state)
{
   uint32_t tmp;
   int i;

   graw_encoder_write_dword(enc, GRAW_CMD0(GRAW_CREATE_OBJECT, GRAW_OBJECT_BLEND, 2 + 1));
   graw_encoder_write_dword(enc, handle);

   tmp = (blend_state->independent_blend_enable << 0) |
      (blend_state->logicop_enable << 1) |
      (blend_state->dither << 3) |
      (blend_state->alpha_to_coverage << 4) |
      (blend_state->alpha_to_one << 5);

   graw_encoder_write_dword(enc, tmp);

   tmp = blend_state->logicop_func << 0;
   graw_encoder_write_dword(enc, tmp);

   for (i = 0; i < PIPE_MAX_COLOR_BUFS; i++) {

   }
   return 0;
}

int graw_encode_shader_state(struct graw_encoder_state *enc,
                             uint32_t handle,
			     uint32_t type,
                             const struct pipe_shader_state *shader)
{
   uint32_t tmp;
   static char str[8192];
   uint32_t len;
   tgsi_dump_str(shader->tokens, 0, str, sizeof(str));

   len = strlen(str);
   graw_encoder_write_dword(enc, GRAW_CMD0(GRAW_CREATE_OBJECT, type, (len / 4) + 1));
   graw_encoder_write_dword(enc, handle);
   graw_encoder_write_block(enc, str, len);
   return 0;
}

int graw_encode_clear(struct graw_encoder_state *enc,
                      unsigned buffers,
                      const union pipe_color_union *color,
                      double depth, unsigned stencil)
{
   int i;
   
   graw_encoder_write_dword(enc, GRAW_CMD0(GRAW_CLEAR, 0, 8));
                            
   graw_encoder_write_dword(enc, buffers);
   for (i = 0; i < 4; i++)
      graw_encoder_write_dword(enc, color->ui[i]);
   graw_encoder_write_qword(enc, *(uint64_t *)&depth);
   graw_encoder_write_dword(enc, stencil);
}

int graw_encoder_set_framebuffer_state(struct graw_encoder_state *enc,
				       const struct pipe_framebuffer_state *state)
{
   struct graw_surface *surf = state->cbufs[0];
   graw_encoder_write_dword(enc, GRAW_CMD0(GRAW_SET_FRAMEBUFFER_STATE, 0, 2));
   graw_encoder_write_dword(enc, state->nr_cbufs);
   graw_encoder_write_dword(enc, surf->handle);
   return 0;
}

int graw_encoder_set_viewport_state(struct graw_encoder_state *enc,
				    const struct pipe_viewport_state *state)
{
   int i;
   graw_encoder_write_dword(enc, GRAW_CMD0(GRAW_SET_VIEWPORT_STATE, 0, 8));
   for (i = 0; i < 4; i++)
      graw_encoder_write_dword(enc, uif(state->scale[i]));
   for (i = 0; i < 4; i++)
      graw_encoder_write_dword(enc, uif(state->translate[i]));
   return 0;
}

int graw_encoder_draw_vbo(struct graw_encoder_state *enc,
			  const struct pipe_draw_info *info)
{
   graw_encoder_write_dword(enc, GRAW_CMD0(GRAW_DRAW_VBO, 0, 0));
}

int graw_encoder_create_surface(struct graw_encoder_state *enc,
				uint32_t handle,
				uint32_t res_handle,
				const struct pipe_surface *templat)
{
   graw_encoder_write_dword(enc, GRAW_CMD0(GRAW_CREATE_OBJECT, GRAW_SURFACE, 6));
   graw_encoder_write_dword(enc, handle);
   graw_encoder_write_dword(enc, res_handle);
   graw_encoder_write_dword(enc, templat->width);
   graw_encoder_write_dword(enc, templat->height);
   graw_encoder_write_dword(enc, templat->usage);
   graw_encoder_write_dword(enc, templat->format);
}

#define EQ_BUF_SIZE (16*1024)

struct graw_encoder_state *graw_encoder_init_queue(void)
{
   struct graw_encoder_state *eq;

   eq = CALLOC_STRUCT(graw_encoder_state);
   if (!eq)
      return NULL;

   eq->buf = malloc(EQ_BUF_SIZE);
   if (!eq->buf){
      free(eq);
      return NULL;
   }
   eq->buf_total = EQ_BUF_SIZE;
   eq->buf_offset = 0;
   return eq;
}

