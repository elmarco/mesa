#ifndef GRAW_CONTEXT_H
#define GRAW_CONTEXT_H

#include "pipe/p_state.h"
#include "pipe/p_context.h"
#include "graw_protocol.h"

#include "util/u_slab.h"

struct graw_screen;

struct graw_resource {
   struct pipe_resource base;
   uint32_t res_handle;
   boolean clean;
};

struct graw_buffer {
   struct graw_resource base;
};

struct graw_texture {
   struct graw_resource base;

   struct sw_displaytarget *dt; 
   uint32_t stride;

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
};

struct graw_transfer {
   struct pipe_transfer base;
   void *localmem;
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

void graw_transfer_block(uint32_t res_handle,
                         int level,
                         const struct pipe_box *transfer_box,
                         const struct pipe_box *box,
                         void *data, int ndw);
void graw_transfer_get_block(uint32_t res_handle, uint32_t level,
                             const struct pipe_box *box,
                             void *data, int ndw);
void grend_flush_frontbuffer(uint32_t res_handle);

uint32_t graw_renderer_resource_create(enum pipe_texture_target target, uint32_t format, uint32_t bind, uint32_t width, uint32_t height, uint32_t depth, uint32_t array_size, uint32_t last_level, uint32_t nr_samples);

struct pipe_resource *
rempipe_resource_from_handle(struct pipe_screen *screen,
                             const struct pipe_resource *templat,
                             struct winsys_handle *whandle);

void graw_init_blit_functions(struct graw_context *grctx);
#endif
