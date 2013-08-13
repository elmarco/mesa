#ifndef GRAW_SHADER_H
#define GRAW_SHADER_H

#include "pipe/p_state.h"
char *tgsi_convert(const struct tgsi_token *tokens,
                   int flags, int *num_samplers, int *num_consts, int *num_inputs, const struct pipe_stream_output_info *so_info);

#endif
