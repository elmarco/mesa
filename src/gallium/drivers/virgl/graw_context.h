#ifndef GRAW_CONTEXT_H
#define GRAW_CONTEXT_H

#include "pipe/p_state.h"
#include "pipe/p_context.h"
#include "graw_protocol.h"

#include "virgl.h"
#include "util/u_slab.h"
#include "util/u_double_list.h"

struct graw_screen;

struct graw_resource {
   struct pipe_resource base;
   struct virgl_hw_res *hw_res;
   boolean clean;
   struct pb_buffer *backing_bo; /* for vbo uploads at least */
};

struct graw_buffer {
   struct graw_resource base;
   struct list_head flush_list;
   boolean on_list;
   /* for backed buffers */
   struct pipe_box dirt_box;
};

struct graw_texture {
   struct graw_resource base;
};


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

   struct graw_sampler_view fs_views[16];
   struct graw_sampler_view vs_views[16];

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
};

struct graw_transfer {
   struct pipe_transfer base;
   struct pb_buffer *bo;
   uint32_t lmsize;
   uint32_t offset;
};

struct pipe_context *graw_context_create(struct pipe_screen *pscreen,
                                         void *priv);

struct pipe_resource *graw_resource_create(struct pipe_screen *pscreen,
                                           const struct pipe_resource *template);

void
graw_resource_destroy(struct pipe_screen *pscreen,
                      struct pipe_resource *pt);

void graw_flush_frontbuffer(struct pipe_screen *screen,
                            struct pipe_resource *res,
                            unsigned level, unsigned layer,
                            void *winsys_drawable_handle);

void grend_flush_frontbuffer(uint32_t res_handle);

struct pipe_resource *
virgl_resource_from_handle(struct pipe_screen *screen,
                             const struct pipe_resource *templat,
                             struct winsys_handle *whandle);

void graw_init_blit_functions(struct graw_context *grctx);

void graw_init_transfer_functions(struct graw_context *grctx);
void graw_init_query_functions(struct graw_context *grctx);
#endif
