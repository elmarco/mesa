#ifndef GRAW_CONTEXT_H
#define GRAW_CONTEXT_H

#include "pipe/p_state.h"
#include "pipe/p_context.h"
#include "virgl_protocol.h"

#include "virgl.h"
#include "util/u_slab.h"
#include "util/u_double_list.h"
#include "indices/u_primconvert.h"

struct virgl_resource;
struct virgl_buffer;

struct graw_vertex_element {
   unsigned count;
   struct pipe_vertex_element elements[PIPE_MAX_ATTRIBS];
   uint32_t vboids[PIPE_MAX_ATTRIBS];
};

struct graw_shader_state {
   uint id;
   unsigned type;
   char *glsl_prog;
};

struct graw_sampler_view {
   struct pipe_sampler_view base;
   uint32_t handle;
};

struct graw_vs_state {
   uint32_t shader_handle;
   uint32_t so_handle;
};

struct graw_so_target {
   struct pipe_stream_output_target base;
   uint32_t handle;
};

struct graw_textures_info {
   struct graw_sampler_view *views[16];
   uint32_t enabled_mask;
};
   
struct graw_context {
   struct pipe_context base;
   struct virgl_cmd_buf *cbuf;
   uint32_t vaoid;

   struct graw_vertex_element *ve;
   int num_vbos;
   struct pipe_vertex_buffer vbo[PIPE_MAX_ATTRIBS];

   struct graw_shader_state *vs;
   struct graw_shader_state *fs;

   struct graw_encoder_state *eq;

   struct graw_textures_info samplers[PIPE_SHADER_TYPES];

   struct pipe_framebuffer_state framebuffer;

   struct util_slab_mempool texture_transfer_pool;

   struct pipe_index_buffer	index_buffer;
   struct u_upload_mgr		*uploader;
    struct blitter_context* blitter;

   struct pipe_vertex_buffer vertex_buffer[PIPE_MAX_ATTRIBS];
   unsigned num_vertex_buffers;
   boolean vertex_array_dirty;

   int num_transfers;
   int num_draws;
   struct list_head to_flush_bufs;

   struct primconvert_context *primconvert;
};

struct pipe_context *graw_context_create(struct pipe_screen *pscreen,
                                         void *priv);

void graw_init_blit_functions(struct graw_context *grctx);
void graw_init_query_functions(struct graw_context *grctx);
void graw_init_so_functions(struct graw_context *grctx);

void graw_transfer_inline_write(struct pipe_context *ctx,
                                struct pipe_resource *res,
                                unsigned level,
                                unsigned usage,
                                const struct pipe_box *box,
                                const void *data,
                                unsigned stride,
                                unsigned layer_stride);
#endif
