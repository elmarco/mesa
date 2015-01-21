
#include "util/u_memory.h"
#include "graw_context.h"
#include "virgl_resource.h"


static void virgl_buffer_destroy(struct pipe_screen *screen,
                                 struct pipe_resource *buf)
{
    struct virgl_screen *vs = virgl_screen(screen);
    struct virgl_buffer *vbuf = virgl_buffer(buf);

    vs->vws->resource_unref(vs->vws, vbuf->base.hw_res);
    FREE(vbuf);
}

static void *virgl_buffer_transfer_map(struct pipe_context *ctx,
                                       struct pipe_resource *resource,
                                       unsigned level,
                                       unsigned usage,
                                       const struct pipe_box *box,
                                       struct pipe_transfer **transfer)
{
   struct graw_context *grctx = (struct graw_context *)ctx;
   struct virgl_screen *vs = virgl_screen(ctx->screen);
   struct virgl_buffer *vbuf = virgl_buffer(resource);
   struct virgl_transfer *trans;
   void *ptr;
   boolean readback = TRUE;
   uint32_t offset;

   if ((!(usage & PIPE_TRANSFER_UNSYNCHRONIZED)) && vs->vws->res_is_referenced(vs->vws, grctx->cbuf, vbuf->base.hw_res))
      ctx->flush(ctx, NULL, 0);
   else if ((usage & PIPE_TRANSFER_READ) && (vbuf->on_list == TRUE))
      ctx->flush(ctx, NULL, 0);

   trans = util_slab_alloc(&grctx->texture_transfer_pool);
   if (trans == NULL)
      return NULL;

   trans->base.resource = resource;
   trans->base.level = level;
   trans->base.usage = usage;
   trans->base.box = *box;
   trans->base.stride = 0;
   trans->base.layer_stride = 0;

   offset = box->x;

   ptr = vs->vws->resource_map(vs->vws, vbuf->base.hw_res);
   if (!ptr) {
      return NULL;
   }

   if (vbuf->base.clean)
      readback = FALSE;
   else if (usage & PIPE_TRANSFER_DISCARD_RANGE)
      readback = FALSE;
   else if ((usage & (PIPE_TRANSFER_WRITE | PIPE_TRANSFER_FLUSH_EXPLICIT)) ==
            (PIPE_TRANSFER_WRITE | PIPE_TRANSFER_FLUSH_EXPLICIT))
      readback = FALSE;

   if (readback)
   {
      vs->vws->transfer_get(vs->vws, vbuf->base.hw_res, box, trans->base.stride, trans->base.layer_stride, offset, level);

      /* wait for data */
      vs->vws->resource_wait(vs->vws, vbuf->base.hw_res);
   }

   if (!(usage & PIPE_TRANSFER_UNSYNCHRONIZED))
      vs->vws->resource_wait(vs->vws, vbuf->base.hw_res);
   trans->offset = offset;
   *transfer = &trans->base;

   return ptr + trans->offset;
}

static void virgl_buffer_transfer_unmap(struct pipe_context *ctx,
                                        struct pipe_transfer *transfer)
{
   struct graw_context *grctx = (struct graw_context *)ctx;
   struct virgl_transfer *trans = (struct virgl_transfer *)transfer;
   struct virgl_buffer *vbuf = virgl_buffer(transfer->resource);

   if (trans->base.usage & PIPE_TRANSFER_WRITE) {
      if (!(transfer->usage & PIPE_TRANSFER_FLUSH_EXPLICIT)) {
         struct virgl_screen *vs = virgl_screen(ctx->screen);
         vbuf->base.clean = FALSE;
         grctx->num_transfers++;
         vs->vws->transfer_put(vs->vws, vbuf->base.hw_res,
                               &transfer->box, trans->base.stride, trans->base.layer_stride, trans->offset, transfer->level);

      }
      
   }

   util_slab_free(&grctx->texture_transfer_pool, trans);
}

static void virgl_buffer_transfer_flush_region(struct pipe_context *ctx,
                                               struct pipe_transfer *transfer,
                                               const struct pipe_box *box)
{
   struct graw_context *grctx = (struct graw_context *)ctx;
   struct virgl_buffer *vbuf = virgl_buffer(transfer->resource);
   uint32_t start, end, cstart, cend;
   
   if (!vbuf->on_list) {
       struct pipe_resource *res = NULL;

       list_addtail(&vbuf->flush_list, &grctx->to_flush_bufs);
       vbuf->on_list = TRUE;
       pipe_resource_reference(&res, &vbuf->base.u.b);
   }

   start = transfer->box.x + box->x;
   end = start + box->width;
   cstart = vbuf->dirt_box.x;
   cend = vbuf->dirt_box.x + vbuf->dirt_box.width;

   if (start < cstart)
       cstart = start;
   if (end > cend || vbuf->dirt_box.width == 0)
       cend = end;
   
   vbuf->dirt_box.x = cstart;
   vbuf->dirt_box.width = cend - cstart;

   vbuf->base.clean = FALSE;
}

static const struct u_resource_vtbl virgl_buffer_vtbl =
{
	u_default_resource_get_handle,		/* get_handle */
	virgl_buffer_destroy,			/* resource_destroy */
	virgl_buffer_transfer_map,		/* transfer_map */
	virgl_buffer_transfer_flush_region,	/* transfer_flush_region */
	virgl_buffer_transfer_unmap,		/* transfer_unmap */
	graw_transfer_inline_write      /* transfer_inline_write */
};

struct pipe_resource *virgl_buffer_create(struct virgl_screen *vs,
                                          const struct pipe_resource *template)
{
   struct virgl_buffer *buf;
   uint32_t size;

   buf = CALLOC_STRUCT(virgl_buffer);
   buf->base.clean = TRUE;
   buf->base.u.b = *template;
   buf->base.u.b.screen = &vs->base;
   buf->base.u.vtbl = &virgl_buffer_vtbl;
   pipe_reference_init(&buf->base.u.b.reference, 1);

   size = template->width0;
   buf->dirt_box.x = template->width0 + 1;
   buf->base.hw_res = vs->vws->resource_create(vs->vws, template->target, template->format, template->bind, template->width0, 1, 1, 1, 0, 0, size);
   
   return &buf->base.u.b;
}
