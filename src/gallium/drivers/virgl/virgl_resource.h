#ifndef VIRGL_RESOURCE_H
#define VIRGL_RESOURCE_H

#include "util/u_inlines.h"
#include "util/u_range.h"
#include "util/u_double_list.h"
#include "util/u_transfer.h"

#define VR_MAX_TEXTURE_2D_LEVELS 15

struct virgl_screen;

struct virgl_resource {
   struct u_resource u;
   struct virgl_hw_res *hw_res;
   boolean clean;
};

struct virgl_buffer {
   struct virgl_resource base;

   struct list_head flush_list;
   boolean on_list;

   /* for backed buffers */
   struct pipe_box dirt_box;
};

struct virgl_texture {
   struct virgl_resource base;

   unsigned long level_offset[VR_MAX_TEXTURE_2D_LEVELS];
   unsigned stride[VR_MAX_TEXTURE_2D_LEVELS];
};

struct virgl_transfer {
   struct pipe_transfer base;
   uint32_t offset;
   struct virgl_resource *resolve_tmp;
};

void virgl_resource_destroy(struct pipe_screen *screen,
                            struct pipe_resource *resource);

void virgl_init_screen_resource_functions(struct pipe_screen *screen);

void virgl_init_context_resource_functions(struct pipe_context *ctx);

struct pipe_resource *virgl_texture_create(struct virgl_screen *vs,
                                           const struct pipe_resource *templ);

struct pipe_resource *virgl_texture_from_handle(struct virgl_screen *vs,
                                                const struct pipe_resource *templ,
                                                struct winsys_handle *whandle);

static INLINE struct virgl_resource *virgl_resource(struct pipe_resource *r)
{
   return (struct virgl_resource *)r;
}

static INLINE struct virgl_buffer *virgl_buffer(struct pipe_resource *r)
{
   return (struct virgl_buffer *)r;
}

struct pipe_resource *virgl_buffer_create(struct virgl_screen *vs,
                                          const struct pipe_resource *templ);

#endif
