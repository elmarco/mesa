
#include "graw_encode.h"

int graw_encode_blend_state(struct graw_encoder_state *enc,
                            struct pipe_blend_state *blend_state)
{
   uint32_t tmp;

   tmp = (blend_state->independent_blend_enable << 0) |
      (blend_state->logicop_enable << 1) |
      (blend_state->dither << 3) |
      (blend_state->alpha_to_coverage << 4) |
      (blend_state->alpha_to_one << 5);

   graw_encode_write_dword(enc, tmp);

   tmp = blend_state->logicop_fund << 0;
   graw_encode_write_dword(enc, tmp);

   for (i = 0; i < PIPE_MAX_COLOR_BUFS; i++);

}
