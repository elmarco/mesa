#include "util/u_format.h"
#include "util/u_math.h"

#include "pipebuffer/pb_buffer.h"
#include "graw_context.h"

static unsigned
vrend_get_tex_image_offset(const struct graw_resource *res,
                           unsigned level, unsigned layer)
{
   const unsigned hgt = u_minify(res->base.height0, level);
   const unsigned nblocksy = util_format_get_nblocksy(res->base.format, hgt);
   unsigned offset = res->level_offset[level];

   if (res->base.target == PIPE_TEXTURE_CUBE ||
       res->base.target == PIPE_TEXTURE_CUBE_ARRAY ||
       res->base.target == PIPE_TEXTURE_3D ||
       res->base.target == PIPE_TEXTURE_2D_ARRAY) {
      offset += layer * nblocksy * res->stride[level];
   }
   else if (res->base.target == PIPE_TEXTURE_1D_ARRAY) {
      offset += layer * res->stride[level];
   }
   else {
      assert(layer == 0);
   }

   return offset;
}

static void *graw_transfer_map(struct pipe_context *ctx,
                               struct pipe_resource *resource,
                               unsigned level,
                               unsigned usage,
                               const struct pipe_box *box,
                               struct pipe_transfer **transfer)
{
   struct graw_context *grctx = (struct graw_context *)ctx;
   struct virgl_screen *vs = virgl_screen(ctx->screen);
   struct graw_resource *grres = (struct graw_resource *)resource;
   enum pipe_format format = resource->format;
   struct graw_transfer *trans;
   void *ptr;
   boolean readback = TRUE;
   uint32_t offset;
   const unsigned h = u_minify(grres->base.height0, level);
   const unsigned nblocksy = util_format_get_nblocksy(format, h);

   if ((!(usage & PIPE_TRANSFER_UNSYNCHRONIZED)) && vs->vws->res_is_referenced(vs->vws, grctx->cbuf, grres->hw_res))
      ctx->flush(ctx, NULL, 0);

   trans = util_slab_alloc(&grctx->texture_transfer_pool);
   if (trans == NULL)
      return NULL;

   trans->base.resource = resource;
   trans->base.level = level;
   trans->base.usage = usage;
   trans->base.box = *box;
   trans->base.stride = grres->stride[level];
   trans->base.layer_stride = trans->base.stride * nblocksy;

   offset = vrend_get_tex_image_offset(grres, level, box->z);

   offset += box->y / util_format_get_blockheight(format) * trans->base.stride +
      box->x / util_format_get_blockwidth(format) * util_format_get_blocksize(format);

   ptr = vs->vws->resource_map(vs->vws, grres->hw_res);
   if (!ptr) {
      return NULL;
   }

   if (grres->clean)
      readback = FALSE;
   else if (usage & PIPE_TRANSFER_DISCARD_RANGE)
      readback = FALSE;
   else if ((usage & (PIPE_TRANSFER_WRITE | PIPE_TRANSFER_FLUSH_EXPLICIT)) ==
            (PIPE_TRANSFER_WRITE | PIPE_TRANSFER_FLUSH_EXPLICIT))
      readback = FALSE;

   if (readback)
   {
      vs->vws->transfer_get(vs->vws, grres->hw_res, box, trans->base.stride, trans->base.layer_stride, offset, level);

      /* wait for data */
      vs->vws->resource_wait(vs->vws, grres->hw_res);
   }

   trans->offset = offset;
   *transfer = &trans->base;

   return ptr + trans->offset;
}

static void graw_transfer_unmap(struct pipe_context *ctx,
                                 struct pipe_transfer *transfer)
{
   struct virgl_screen *vs = virgl_screen(ctx->screen);
   struct graw_context *grctx = (struct graw_context *)ctx;
   struct graw_transfer *trans = (struct graw_transfer *)transfer;
   struct graw_resource *grres = (struct graw_resource *)transfer->resource;

   if (trans->base.usage & PIPE_TRANSFER_WRITE) {

      if (!(transfer->usage & PIPE_TRANSFER_FLUSH_EXPLICIT)) {
         struct virgl_screen *vs = virgl_screen(ctx->screen);
         grres->clean = FALSE;
         grctx->num_transfers++;
         vs->vws->transfer_put(vs->vws, grres->hw_res,
                               &transfer->box, trans->base.stride, trans->base.layer_stride, trans->offset, transfer->level);

      }
      
   }

   util_slab_free(&grctx->texture_transfer_pool, trans);
}

static void graw_transfer_flush_region(struct pipe_context *ctx,
                                       struct pipe_transfer *transfer,
                                       const struct pipe_box *box)
{
   struct graw_context *grctx = (struct graw_context *)ctx;
   struct graw_resource *grres = (struct graw_resource *)transfer->resource;
   struct graw_transfer *trans = (struct graw_transfer *)transfer;
   uint32_t offset;
   uint32_t size;
   struct pipe_box hw_box;
   struct virgl_screen *vs = virgl_screen(ctx->screen);

   if (1 /*grres->backing_bo*/) {
      struct graw_buffer *buf = (struct graw_buffer *)grres;
      uint32_t start, end, cstart, cend;
      if (!buf->on_list) {
         struct pipe_resource *res = NULL;

         list_addtail(&buf->flush_list, &grctx->to_flush_bufs);
         buf->on_list = TRUE;
         pipe_resource_reference(&res, &buf->base.base);

      }

      start = transfer->box.x + box->x;
      end = start + box->width;
      cstart = buf->dirt_box.x;
      cend = buf->dirt_box.x + buf->dirt_box.width;

      if (start < cstart)
         cstart = start;
      if (end > cend || buf->dirt_box.width == 0)
         cend = end;

      buf->dirt_box.x = cstart;
      buf->dirt_box.width = cend - cstart;

      grres->clean = FALSE;
      return;
   }

   offset = trans->offset;
   if (box->x || box->y)
      fprintf(stderr, "box->x is %d box->y is %d\n", box->x, box->y);
   offset += box->x;

   hw_box.x = transfer->box.x + box->x;
   hw_box.y = transfer->box.y + box->y;
   hw_box.z = transfer->box.z + box->z;
   hw_box.width = box->width;
   hw_box.height = box->height;
   hw_box.depth = box->depth;

   grctx->num_transfers++;
   vs->vws->transfer_put(vs->vws, grres->hw_res,
                         &hw_box, trans->base.stride, 0, offset, transfer->level);

   grres->clean = FALSE;   
}

void graw_init_transfer_functions(struct graw_context *grctx)
{
   grctx->base.transfer_map = graw_transfer_map;
   grctx->base.transfer_unmap = graw_transfer_unmap;
   grctx->base.transfer_flush_region = graw_transfer_flush_region;
}
