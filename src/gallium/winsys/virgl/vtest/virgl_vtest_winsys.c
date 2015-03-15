#define _FILE_OFFSET_BITS 64

#include "virgl_vtest_winsys.h"
#include "virgl_vtest_public.h"
#include "util/u_memory.h"
#include "util/u_format.h"
#include "util/u_inlines.h"
#include "state_tracker/drm_driver.h"

static int
virgl_vtest_transfer_put(struct virgl_winsys *vws,
                      struct virgl_hw_res *res,
                      const struct pipe_box *box,
                      uint32_t stride, uint32_t layer_stride,
                      uint32_t buf_offset, uint32_t level)
{
   return 0;
}
static int
virgl_vtest_transfer_get(struct virgl_winsys *vws,
                      struct virgl_hw_res *res,
                      const struct pipe_box *box,
                      uint32_t stride, uint32_t layer_stride,
                      uint32_t buf_offset, uint32_t level)
{
   return 0;
}

static void virgl_hw_res_destroy(struct virgl_vtest_winsys *vtws,
                                 struct virgl_hw_res *res)
{
   virgl_vtest_send_resource_unref(vtws, res->res_handle);
   free(res->ptr);
   FREE(res);
}

static void virgl_vtest_resource_reference(struct virgl_vtest_winsys *vtws,
                                           struct virgl_hw_res **dres,
                                           struct virgl_hw_res *sres)
{
   struct virgl_hw_res *old = *dres;
   if (pipe_reference(&(*dres)->reference, &sres->reference)) {
      virgl_hw_res_destroy(vtws, old);
   }
   *dres = sres;
}
static struct virgl_hw_res *virgl_vtest_winsys_resource_create(
   struct virgl_winsys *vws,
   enum pipe_texture_target target,
   uint32_t format,
   uint32_t bind,
   uint32_t width,
   uint32_t height,
   uint32_t depth,
   uint32_t array_size,
   uint32_t last_level,
   uint32_t nr_samples,
   uint32_t size)
{
   struct virgl_vtest_winsys *vtws = virgl_vtest_winsys(vws);
   struct virgl_hw_res *res;
   static int handle = 1;
   res = CALLOC_STRUCT(virgl_hw_res);
   if (!res)
      return NULL;

   res->ptr = malloc(size);
   if (!res->ptr) {
      FREE(res);
      return NULL;
   }

   virgl_vtest_send_resource_create(vtws, handle, target, format, bind, width,
                                    height, depth, array_size, last_level,
                                    nr_samples);

   res->res_handle = handle++;
   pipe_reference_init(&res->reference, 1);
   return res;
}

static void virgl_vtest_winsys_resource_unref(struct virgl_winsys *vws,
                                              struct virgl_hw_res *hres)
{
   struct virgl_vtest_winsys *vtws = virgl_vtest_winsys(vws);
   virgl_vtest_resource_reference(vtws, &hres, NULL);
}

static struct virgl_cmd_buf *virgl_vtest_cmd_buf_create(struct virgl_winsys *qws)
{
   struct virgl_vtest_cmd_buf *cbuf;

   cbuf = CALLOC_STRUCT(virgl_vtest_cmd_buf);
   if (!cbuf)
      return NULL;

   cbuf->ws = qws;
   cbuf->base.buf = cbuf->buf;
   return &cbuf->base;
}

static void virgl_vtest_cmd_buf_destroy(struct virgl_cmd_buf *_cbuf)
{
   struct virgl_vtest_cmd_buf *cbuf = (struct virgl_vtest_cmd_buf *)_cbuf;

   FREE(cbuf);
}

static int virgl_vtest_winsys_submit_cmd(struct virgl_winsys *vws, struct virgl_cmd_buf *_cbuf)
{
   struct virgl_vtest_winsys *vtws = virgl_vtest_winsys(vws);
   struct virgl_vtest_cmd_buf *cbuf = (struct virgl_vtest_cmd_buf *)_cbuf;
   return virgl_vtest_submit_cmd(vtws, cbuf);
}

static void virgl_vtest_emit_res(struct virgl_winsys *vws, struct virgl_cmd_buf *_cbuf, struct virgl_hw_res *res, boolean write_buf)
{
   struct virgl_vtest_cmd_buf *cbuf = (struct virgl_vtest_cmd_buf *)_cbuf;
   if (write_buf)
      cbuf->base.buf[cbuf->base.cdw++] = res->res_handle;
}

static int virgl_vtest_get_caps(struct virgl_winsys *vws, struct virgl_drm_caps *caps)
{
   struct virgl_vtest_winsys *vtws = virgl_vtest_winsys(vws);
   return virgl_vtest_send_get_caps(vtws, caps);
}
static void
virgl_vtest_winsys_destroy(struct virgl_winsys *vws)
{
   struct virgl_vtest_winsys *vtws = virgl_vtest_winsys(vws);

   FREE(vtws);
}

struct virgl_winsys *
virgl_vtest_winsys_wrap(struct sw_winsys *sws)
{
   struct virgl_vtest_winsys *vws;

   vws = CALLOC_STRUCT(virgl_vtest_winsys);
   if (!vws)
      return NULL;

   virgl_vtest_connect(vws);
   vws->sws = sws;

   vws->base.destroy = virgl_vtest_winsys_destroy;

   vws->base.transfer_put = virgl_vtest_transfer_put;
   vws->base.transfer_get = virgl_vtest_transfer_get;
   
   vws->base.resource_create = virgl_vtest_winsys_resource_create;
   vws->base.resource_unref = virgl_vtest_winsys_resource_unref;

   vws->base.cmd_buf_create = virgl_vtest_cmd_buf_create;
   vws->base.cmd_buf_destroy = virgl_vtest_cmd_buf_destroy;
   vws->base.submit_cmd = virgl_vtest_winsys_submit_cmd;

   vws->base.emit_res = virgl_vtest_emit_res;
   vws->base.get_caps = virgl_vtest_get_caps;
   return &vws->base;
}
