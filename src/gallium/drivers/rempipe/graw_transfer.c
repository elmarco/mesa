#include "util/u_format.h"
#include "util/u_math.h"

#include "pipebuffer/pb_buffer.h"
#include "graw_context.h"

static void *graw_transfer_map(struct pipe_context *ctx,
                               struct pipe_resource *resource,
                               unsigned level,
                               unsigned usage,
                               const struct pipe_box *box,
                               struct pipe_transfer **transfer)
{
   struct graw_context *grctx = (struct graw_context *)ctx;
   struct rempipe_screen *rs = rempipe_screen(ctx->screen);
   struct graw_resource *grres = (struct graw_resource *)resource;
   enum pipe_format format = resource->format;
   struct graw_transfer *trans;
   void *ptr;
   boolean readback = TRUE;
   struct qxl_bo *bo;

   if ((!(usage & PIPE_TRANSFER_UNSYNCHRONIZED)) && rs->qws->res_is_referenced(rs->qws, grctx->cbuf, grres->hw_res))
      ctx->flush(ctx, NULL, 0);

   trans = util_slab_alloc(&grctx->texture_transfer_pool);
   if (trans == NULL)
      return NULL;

   trans->lmsize = box->width * box->height * box->depth * util_format_get_blocksize(resource->format);
   trans->lmsize = align(trans->lmsize, 4);

   if (grres->backing_bo) {
      trans->bo = NULL;
      pb_reference(&trans->bo, grres->backing_bo);
   } else {
      /* allocate a bo */
      trans->bo = rs->qws->bo_create(rs->qws, trans->lmsize, 0);
      if (!trans->bo) {
         util_slab_free(&grctx->texture_transfer_pool, trans);
         return NULL;
      }
   }

   ptr = rs->qws->bo_map(trans->bo);
   if (!ptr) {
      pb_reference(&trans->bo, NULL);
      util_slab_free(&grctx->texture_transfer_pool, trans);
      return NULL;
   }

   if (grres->clean)
      readback = FALSE;
   else if (usage & PIPE_TRANSFER_DISCARD_RANGE)
      readback = FALSE;
   else if ((usage & (PIPE_TRANSFER_WRITE | PIPE_TRANSFER_FLUSH_EXPLICIT)) ==
            (PIPE_TRANSFER_WRITE | PIPE_TRANSFER_FLUSH_EXPLICIT))
      readback = FALSE;

   if (readback == FALSE) {
      if ((usage & PIPE_TRANSFER_READ))
         memset(ptr, 0, trans->lmsize);
   } else {
      struct graw_resource *grres = (struct graw_resource *)resource;
      struct rempipe_screen *rs = rempipe_screen(ctx->screen);
      uint32_t offset = grres->backing_bo ? box->x : 0;

      rs->qws->transfer_get(trans->bo, grres->hw_res, box, offset, level);

      /* wait for data */
      rs->qws->bo_wait(trans->bo);
   }

   trans->base.resource = resource;
   trans->base.level = level;
   trans->base.usage = usage;
   trans->base.box = *box;
   trans->base.stride = box->width * util_format_get_blocksize(resource->format);
   trans->base.layer_stride = 0;
   trans->offset = grres->backing_bo ? box->x : 0;
   *transfer = &trans->base;

   return ptr + trans->offset;
}

static void graw_transfer_unmap(struct pipe_context *ctx,
                                 struct pipe_transfer *transfer)
{
   struct rempipe_screen *rs = rempipe_screen(ctx->screen);
   struct graw_context *grctx = (struct graw_context *)ctx;
   struct graw_transfer *trans = (struct graw_transfer *)transfer;
   struct graw_resource *grres = (struct graw_resource *)transfer->resource;

   if (trans->base.usage & PIPE_TRANSFER_WRITE) {

      if (!(transfer->usage & PIPE_TRANSFER_FLUSH_EXPLICIT)) {
         struct rempipe_screen *rs = rempipe_screen(ctx->screen);
         grres->clean = FALSE;
         grctx->num_transfers++;
         rs->qws->transfer_put(trans->bo, grres->hw_res,
                               &transfer->box, 0, trans->offset, transfer->level);

      }
      
   }

   pb_reference(&trans->bo, NULL);
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
   struct rempipe_screen *rs = rempipe_screen(ctx->screen);
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
   rs->qws->transfer_put(trans->bo, grres->hw_res,
                         &hw_box, 0, offset, transfer->level);

   grres->clean = FALSE;   
}

void graw_init_transfer_functions(struct graw_context *grctx)
{
   grctx->base.transfer_map = graw_transfer_map;
   grctx->base.transfer_unmap = graw_transfer_unmap;
   grctx->base.transfer_flush_region = graw_transfer_flush_region;
}
