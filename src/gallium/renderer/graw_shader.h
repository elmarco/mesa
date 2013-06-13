#ifndef GRAW_SHADER_H
#define GRAW_SHADER_H

char *tgsi_convert(const struct tgsi_token *tokens,
                   int flags, int *num_samplers, int *num_consts, int *num_inputs);

#endif
