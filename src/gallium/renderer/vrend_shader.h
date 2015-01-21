/**************************************************************************
 *
 * Copyright (C) 2014 Red Hat Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 **************************************************************************/

#ifndef VREND_SHADER_H
#define VREND_SHADER_H

#include "pipe/p_state.h"

/* need to store patching info for interpolation */
struct vrend_interp_info {
   int semantic_name;
   int semantic_index;
   int interpolate;
};

struct vrend_shader_info {
   uint32_t samplers_used_mask;
   int num_consts;
   int num_inputs;
   int num_interps;
   int num_outputs;
   int num_ubos;
   int num_ucp;
   int glsl_ver;
   uint32_t shadow_samp_mask;
   struct pipe_stream_output_info so_info;

   struct vrend_interp_info *interpinfo;
   int gs_out_prim;
};

struct vrend_shader_key {
   uint32_t coord_replace;
   boolean invert_fs_origin;
   boolean pstipple_tex;
   boolean add_alpha_test;
   boolean color_two_side;
   uint8_t alpha_test;
   uint8_t clip_plane_enable;
   float alpha_ref_val;
   boolean gs_present;
};
  
struct vrend_shader_cfg {
   int glsl_version;
   bool use_core_profile;
};

boolean vrend_patch_vertex_shader_interpolants(char *program,
                                               struct vrend_shader_info *vs_info,
                                               struct vrend_shader_info *fs_info,
   bool is_gs);

char *vrend_convert_shader(struct vrend_shader_cfg *cfg,
                           const struct tgsi_token *tokens,
                           struct vrend_shader_key *key,
                           struct vrend_shader_info *sinfo);
const char *vrend_shader_samplertypeconv(int sampler_type, int *is_shad);
#endif
