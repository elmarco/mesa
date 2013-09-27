#include "util/u_memory.h"
#include "util/u_format.h"
#include "virgl.h"
#include "virgl_resource.h"
#include "graw_context.h"

static unsigned
vrend_get_tex_image_offset(const struct virgl_texture *res,
                           unsigned level, unsigned layer)
{
   const struct pipe_resource *pres = &res->base.u.b;
   const unsigned hgt = u_minify(pres->height0, level);
   const unsigned nblocksy = util_format_get_nblocksy(pres->format, hgt);
   unsigned offset = res->level_offset[level];

   if (pres->target == PIPE_TEXTURE_CUBE ||
       pres->target == PIPE_TEXTURE_CUBE_ARRAY ||
       pres->target == PIPE_TEXTURE_3D ||
       pres->target == PIPE_TEXTURE_2D_ARRAY) {
      offset += layer * nblocksy * res->stride[level];
   }
   else if (pres->target == PIPE_TEXTURE_1D_ARRAY) {
      offset += layer * res->stride[level];
   }
   else {
      assert(layer == 0);
   }

   return offset;
}

static void *virgl_texture_transfer_map(struct pipe_context *ctx,
                                        struct pipe_resource *resource,
                                        unsigned level,
                                        unsigned usage,
                                        const struct pipe_box *box,
                                        struct pipe_transfer **transfer)
{
   struct graw_context *grctx = (struct graw_context *)ctx;
   struct virgl_screen *vs = virgl_screen(ctx->screen);
   struct virgl_texture *vtex = (struct virgl_texture *)resource;
   enum pipe_format format = resource->format;
   struct virgl_transfer *trans;
   void *ptr;
   boolean readback = TRUE;
   uint32_t offset;
   const unsigned h = u_minify(vtex->base.u.b.height0, level);
   const unsigned nblocksy = util_format_get_nblocksy(format, h);

   if ((!(usage & PIPE_TRANSFER_UNSYNCHRONIZED)) && vs->vws->res_is_referenced(vs->vws, grctx->cbuf, vtex->base.hw_res))
      ctx->flush(ctx, NULL, 0);

   trans = util_slab_alloc(&grctx->texture_transfer_pool);
   if (trans == NULL)
      return NULL;

   trans->base.resource = resource;
   trans->base.level = level;
   trans->base.usage = usage;
   trans->base.box = *box;
   trans->base.stride = vtex->stride[level];
   trans->base.layer_stride = trans->base.stride * nblocksy;

   offset = vrend_get_tex_image_offset(vtex, level, box->z);

   offset += box->y / util_format_get_blockheight(format) * trans->base.stride +
      box->x / util_format_get_blockwidth(format) * util_format_get_blocksize(format);

   ptr = vs->vws->resource_map(vs->vws, vtex->base.hw_res);
   if (!ptr) {
      return NULL;
   }

   if (vtex->base.clean)
      readback = FALSE;
   else if (usage & PIPE_TRANSFER_DISCARD_RANGE)
      readback = FALSE;
   else if ((usage & (PIPE_TRANSFER_WRITE | PIPE_TRANSFER_FLUSH_EXPLICIT)) ==
            (PIPE_TRANSFER_WRITE | PIPE_TRANSFER_FLUSH_EXPLICIT))
      readback = FALSE;

   if (readback)
   {
      vs->vws->transfer_get(vs->vws, vtex->base.hw_res, box, trans->base.stride, trans->base.layer_stride, offset, level);

      /* wait for data */
      vs->vws->resource_wait(vs->vws, vtex->base.hw_res);
   }

   if (!(usage & PIPE_TRANSFER_UNSYNCHRONIZED))
      vs->vws->resource_wait(vs->vws, vtex->base.hw_res);
   trans->offset = offset;
   *transfer = &trans->base;

   return ptr + trans->offset;
}

static void virgl_texture_transfer_unmap(struct pipe_context *ctx,
                                         struct pipe_transfer *transfer)
{
   struct graw_context *grctx = (struct graw_context *)ctx;
   struct virgl_transfer *trans = (struct virgl_transfer *)transfer;
   struct virgl_texture *vtex = (struct virgl_texture *)transfer->resource;

   if (trans->base.usage & PIPE_TRANSFER_WRITE) {
      if (!(transfer->usage & PIPE_TRANSFER_FLUSH_EXPLICIT)) {
         struct virgl_screen *vs = virgl_screen(ctx->screen);
         vtex->base.clean = FALSE;
         grctx->num_transfers++;
         vs->vws->transfer_put(vs->vws, vtex->base.hw_res,
                               &transfer->box, trans->base.stride, trans->base.layer_stride, trans->offset, transfer->level);

      }
      
   }

   util_slab_free(&grctx->texture_transfer_pool, trans);
}


static boolean
vrend_resource_layout(struct virgl_texture *res,
                      uint32_t *total_size)
{
   struct pipe_resource *pt = &res->base.u.b;
   unsigned level;
   unsigned width = pt->width0;
   unsigned height = pt->height0;
   unsigned depth = pt->depth0;
   unsigned buffer_size = 0;

   for (level = 0; level <= pt->last_level; level++) {
      unsigned slices;

      if (pt->target == PIPE_TEXTURE_CUBE)
         slices = 6;
      else if (pt->target == PIPE_TEXTURE_3D)
         slices = depth;
      else
         slices = pt->array_size;

      res->stride[level] = util_format_get_stride(pt->format, width);
      res->level_offset[level] = buffer_size;

      buffer_size += (util_format_get_nblocksy(pt->format, height) *
                      slices * res->stride[level]);

      width = u_minify(width, 1);
      height = u_minify(height, 1);
      depth = u_minify(depth, 1);
   }

   if (pt->nr_samples <= 1)
      *total_size = buffer_size;
   else /* don't create guest backing store for MSAA */
      *total_size = 0;
   return TRUE;
}



static boolean virgl_texture_get_handle(struct pipe_screen *screen,
                                         struct pipe_resource *ptex,
                                         struct winsys_handle *whandle)
{
   struct virgl_screen *vs = virgl_screen(screen);
   struct virgl_texture *vtex = (struct virgl_texture *)ptex;

   return vs->vws->resource_get_handle(vs->vws, vtex->base.hw_res, vtex->stride[0], whandle);
}

static void virgl_texture_destroy(struct pipe_screen *screen,
                                  struct pipe_resource *res)
{
    struct virgl_screen *vs = virgl_screen(screen);
    struct virgl_texture *vtex = (struct virgl_texture *)res;
    vs->vws->resource_unref(vs->vws, vtex->base.hw_res);
    FREE(vtex);
}

static const struct u_resource_vtbl virgl_texture_vtbl =
{
	virgl_texture_get_handle,	/* get_handle */
	virgl_texture_destroy,		/* resource_destroy */
	virgl_texture_transfer_map,	/* transfer_map */
	NULL,				/* transfer_flush_region */
	virgl_texture_transfer_unmap,	/* transfer_unmap */
	NULL				/* transfer_inline_write */
};

struct pipe_resource *
virgl_texture_from_handle(struct virgl_screen *vs,
                          const struct pipe_resource *template,
                          struct winsys_handle *whandle)
{
   struct virgl_texture *tex;
   uint32_t handle;
   uint32_t size;

   tex = CALLOC_STRUCT(virgl_texture);
   tex->base.u.b = *template;
   tex->base.u.b.screen = &vs->base;
   pipe_reference_init(&tex->base.u.b.reference, 1);
   tex->base.u.vtbl = &virgl_texture_vtbl;
   vrend_resource_layout(tex, &size);

   tex->base.hw_res = vs->vws->resource_create_from_handle(vs->vws, whandle);
   return &tex->base.u.b;
}  

struct pipe_resource *virgl_texture_create(struct virgl_screen *vs,
                                           const struct pipe_resource *template)
{
   struct virgl_texture *tex;
   uint32_t handle;
   uint32_t size;

   tex = CALLOC_STRUCT(virgl_texture);
   tex->base.clean = TRUE;
   tex->base.u.b = *template;
   tex->base.u.b.screen = &vs->base;
   pipe_reference_init(&tex->base.u.b.reference, 1);
   tex->base.u.vtbl = &virgl_texture_vtbl;
   vrend_resource_layout(tex, &size);
   
   tex->base.hw_res = vs->vws->resource_create(vs->vws, template->target, template->format, template->bind, template->width0, template->height0, template->depth0, template->array_size, template->last_level, template->nr_samples, size);
   assert(tex->base.hw_res);
   return &tex->base.u.b;
}
