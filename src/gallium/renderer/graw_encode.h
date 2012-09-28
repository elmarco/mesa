
#ifndef GRAW_ENCODER_H
#define GRAW_ENCODER_H

struct graw_encoder_state {
   uint32_t *buf;
   uint32_t buf_total;
   uint32_t buf_offset;
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

#endif
