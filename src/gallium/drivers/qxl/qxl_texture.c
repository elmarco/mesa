#include "qxl_screen.h"
#include "qxl_context.h"
#include "qxl_winsys.h"
#include "util/u_memory.h"
#include "util/u_format.h"
#include "util/u_inlines.h"

static void
qxl_texture_destroy(struct pipe_screen *screen,
                    struct pipe_resource *pt)
{
   struct qxl_texture *tex = qxl_texture(pt);
   struct qxl_winsys *qws = qxl_screen(screen)->qws;

   if (tex->buffer)
      qws->buffer_destroy(qws, tex->buffer);
   FREE(tex);
}

static void *
qxl_texture_transfer_map(struct pipe_context *pipe,
                         struct pipe_resource *resource,
                         unsigned level,
                         unsigned usage,
                         const struct pipe_box *box,
                         struct pipe_transfer **ptransfer)
{
   struct qxl_context *qxl = qxl_context(pipe);
   struct qxl_texture *tex = qxl_texture(resource);
   struct qxl_transfer *transfer = util_slab_alloc(&qxl->texture_transfer_pool);
   struct qxl_winsys *qws = qxl_screen(pipe->screen)->qws;
   enum pipe_format format = resource->format;
   void *map;
   if (transfer == NULL)
      return NULL;

   transfer->b.resource = resource;
   transfer->b.level = level;
   transfer->b.usage = usage;
   transfer->b.box = *box;
   transfer->b.stride = tex->stride;

   *ptransfer = &transfer->b;
   
   map = qws->buffer_map(qws, tex->buffer,
                         (transfer->b.usage & PIPE_TRANSFER_WRITE) ? TRUE : FALSE);
   if (map == NULL) {
      util_slab_free(&qxl->texture_transfer_pool, transfer);
      return NULL;
   }

   return map +
      box->y / util_format_get_blockheight(format) * transfer->b.stride +
      box->x / util_format_get_blockwidth(format) * util_format_get_blocksize(format);
}

static void
qxl_texture_transfer_unmap(struct pipe_context *pipe,
                           struct pipe_transfer *transfer)
{
   struct qxl_context *qxl = qxl_context(pipe);
   struct qxl_transfer *qtransfer = (struct qxl_transfer *)transfer;
   struct qxl_texture *tex = qxl_texture(transfer->resource);
   struct qxl_winsys *qws = qxl_screen(pipe->screen)->qws;

   
   qws->buffer_unmap(qws, tex->buffer);

   util_slab_free(&qxl->texture_transfer_pool, qtransfer);
}

struct u_resource_vtbl qxl_texture_vtbl =
{
   NULL,
   qxl_texture_destroy,
   qxl_texture_transfer_map,
   u_default_transfer_flush_region,
   qxl_texture_transfer_unmap,
   u_default_transfer_inline_write,
};

static struct pipe_resource *qxl_texture_create(struct pipe_screen *screen,
                                                const struct pipe_resource *template)
{
   struct qxl_screen *qs = qxl_screen(screen);
   struct qxl_winsys *qws = qs->qws;
   struct qxl_texture *tex = CALLOC_STRUCT(qxl_texture);

   if (!tex)
      return NULL;

   tex->b.b = *template;
   tex->b.vtbl = &qxl_texture_vtbl;
   pipe_reference_init(&tex->b.b.reference, 1);
   tex->b.b.screen = screen;
   
   return &tex->b.b;
}
                                                
static struct pipe_resource *qxl_resource_from_handle(struct pipe_screen *screen,
                                                      const struct pipe_resource *template,
                                                      struct winsys_handle *whandle)
{
   struct qxl_screen *qs = qxl_screen(screen);
   struct qxl_winsys *qws = qs->qws;
   struct qxl_winsys_buffer *buffer;
   int32_t stride;
   struct qxl_texture *tex;
   int size = template->width0 * 4 * template->height0;

   buffer = qws->buffer_from_handle(qws, whandle, size, &stride);

   if ((template->target != PIPE_TEXTURE_2D &&
       template->target != PIPE_TEXTURE_RECT) ||
       template->last_level != 0 ||
       template->depth0 != 1) {
      return NULL;
   }

   tex = CALLOC_STRUCT(qxl_texture);
   if (!tex)
      return NULL;

   tex->b.b = *template;
   tex->b.vtbl = &qxl_texture_vtbl;
   tex->b.b.screen = screen;
   pipe_reference_init(&tex->b.b.reference, 1);

   tex->stride = abs(stride);
   tex->buffer = buffer;
   return &tex->b.b;

}

void
qxl_screen_texture_functions_init(struct qxl_screen *qs)
{
   qs->base.resource_create = qxl_texture_create;
   qs->base.resource_from_handle = qxl_resource_from_handle;
   qs->base.resource_get_handle = u_resource_get_handle_vtbl;
   qs->base.resource_destroy = u_resource_destroy_vtbl;
}
