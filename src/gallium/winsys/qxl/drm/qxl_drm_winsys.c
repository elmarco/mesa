#define _FILE_OFFSET_BITS 64

#include "qxl_drm_winsys.h"
#include "qxl_drm_public.h"
#include "util/u_memory.h"
#include "state_tracker/drm_driver.h"

#include "os/os_mman.h"
#include <sys/ioctl.h>
#include <errno.h>
#include <xf86drm.h>
#include "virgl_drm.h"

struct qxl_bomgr {
   struct pb_manager base;
   struct qxl_drm_winsys *qws;

};

static INLINE struct qxl_bomgr *qxl_bomgr(struct pb_manager *mgr)
{
   return (struct qxl_bomgr *)mgr;
}

const struct pb_vtbl qxl_bo_vtbl;

static INLINE struct qxl_bo *qxl_bo(struct pb_buffer *bo)
{
   assert(bo->vtbl == &qxl_bo_vtbl);
   return (struct qxl_bo *)bo;
}

static struct qxl_bo *get_qxl_bo(struct pb_buffer *_buf)
{
    struct qxl_bo *bo = NULL;

    if (_buf->vtbl == &qxl_bo_vtbl) {
        bo = qxl_bo(_buf);
    } else {
        struct pb_buffer *base_buf;
        pb_size offset;
        pb_get_base_buffer(_buf, &base_buf, &offset);

        if (base_buf->vtbl == &qxl_bo_vtbl)
            bo = qxl_bo(base_buf);
    }

    return bo;
}

static void qxl_bo_get_base_buffer(struct pb_buffer *buf,
                                   struct pb_buffer **base_buf,
                                   unsigned *offset)
{
   *base_buf = buf;
   *offset = 0;
}

static enum pipe_error qxl_bo_validate(struct pb_buffer *_buf,
                                       struct pb_validate *vl,
                                       unsigned flags)
{
   return PIPE_OK;
}
                                       
static void qxl_bo_fence(struct pb_buffer *buf,
                         struct pipe_fence_handle *fence)
{

}

static void qxl_bo_destroy(struct pb_buffer *_buf)
{
   struct qxl_bo *bo = qxl_bo(_buf);
   struct qxl_bomgr *mgr = bo->mgr;
   struct drm_gem_close close_bo;

   if (bo->ptr)
      os_munmap(bo->ptr, bo->base.size);

   close_bo.handle = bo->handle;
   drmIoctl(bo->qws->fd, DRM_IOCTL_GEM_CLOSE, &close_bo);
      
   FREE(bo);
}

const struct pb_vtbl qxl_bo_vtbl = {
   qxl_bo_destroy,
   NULL,
   NULL,
   qxl_bo_validate,
   qxl_bo_fence,
   qxl_bo_get_base_buffer,
};

static struct pb_buffer *qxl_bomgr_create_bo(struct pb_manager *_mgr,
                                             pb_size size,
                                             const struct pb_desc *desc)
{
   struct qxl_bomgr *mgr = qxl_bomgr(_mgr);
   struct qxl_drm_winsys *qdws = mgr->qws;
   struct qxl_bo *bo;
   struct drm_virgl_alloc alloccmd;
   int ret;

   alloccmd.size = size;
   alloccmd.handle = 0;
   ret = drmIoctl(qdws->fd, DRM_IOCTL_VIRGL_ALLOC, &alloccmd);
   if (ret != 0)
      return NULL;

   bo = CALLOC_STRUCT(qxl_bo);

   pipe_reference_init(&bo->base.reference, 1);
   bo->base.alignment = desc->alignment;
   bo->base.usage = desc->usage;
   bo->base.size = size;
   bo->base.vtbl = &qxl_bo_vtbl;
   bo->handle = alloccmd.handle;
   bo->mgr = mgr;
   bo->qws = qdws;
   
   return &bo->base;
}

static void qxl_bomgr_flush(struct pb_manager *mgr)
{

}

static boolean qxl_bomgr_is_buffer_busy(struct pb_manager *mgr,
                                        struct pb_buffer *_buf)
{
   struct qxl_bo *bo = get_qxl_bo(_buf);
   struct drm_virgl_3d_wait waitcmd;
   int ret;

   waitcmd.handle = bo->handle;
   waitcmd.flags = VIRGL_WAIT_NOWAIT;
 again:
   ret = drmIoctl(bo->qws->fd, DRM_IOCTL_VIRGL_WAIT, &waitcmd);
   return (ret != 0 ? TRUE : FALSE);
}

static void qxl_bomgr_destroy(struct pb_manager *_mgr)
{
   FREE(_mgr);
}

static struct pb_manager *qxl_bomgr_create(struct qxl_drm_winsys *qdws)
{
   struct qxl_bomgr *mgr;

   mgr = CALLOC_STRUCT(qxl_bomgr);
   if (!mgr)
      return NULL;

   mgr->base.destroy = qxl_bomgr_destroy;
   mgr->base.create_buffer = qxl_bomgr_create_bo;
   mgr->base.flush = qxl_bomgr_flush;
   mgr->base.is_buffer_busy = qxl_bomgr_is_buffer_busy;

   mgr->qws = qdws;
   return &mgr->base;
}

static void *qxl_bo_map(struct pb_buffer *_buf)
{
   struct drm_virgl_map mmap_arg;
   struct qxl_bo *bo = get_qxl_bo(_buf);
   void *ptr;

   if (bo->ptr)
      return bo->ptr;

   mmap_arg.handle = bo->handle;
   if (drmIoctl(bo->qws->fd, DRM_IOCTL_VIRGL_MAP, &mmap_arg))
      return NULL;

   ptr = os_mmap(0, bo->base.size, PROT_READ|PROT_WRITE, MAP_SHARED,
                 bo->qws->fd, mmap_arg.offset);
   if (ptr == MAP_FAILED)
      return NULL;

   bo->ptr = ptr;
   return ptr;
}

static void qxl_bo_wait(struct pb_buffer *_buf)
{
   struct qxl_bo *bo = get_qxl_bo(_buf);
   struct drm_virgl_3d_wait waitcmd;
   int ret;

   waitcmd.handle = bo->handle;
   waitcmd.flags = 0;
 again:
   ret = drmIoctl(bo->qws->fd, DRM_IOCTL_VIRGL_WAIT, &waitcmd);
   if (ret == -EAGAIN)
      goto again;
}

static unsigned int qxl_bo_get_handle(struct pb_buffer *_buf)
{
   struct qxl_bo *bo = get_qxl_bo(_buf);
   return bo->handle;
}

static struct pb_buffer *
qxl_winsys_bo_create(struct qxl_winsys *qws,
                     unsigned size,
                     unsigned alignment)
{
   struct qxl_drm_winsys *qdws = qxl_drm_winsys(qws);
   struct pb_desc desc;
   struct pb_buffer *buffer;

   memset(&desc, 0, sizeof(desc));
   desc.alignment = alignment;

   buffer = qdws->cman->create_buffer(qdws->cman, size, &desc);
   if (!buffer)
      return NULL;

   return (struct pb_buffer *)buffer;
}

static int
qxl_bo_transfer_put(struct pb_buffer *_buf,
                    struct qxl_hw_res *res,
                    const struct pipe_box *box,
                    uint32_t src_stride,
                    uint32_t buf_offset, uint32_t level)
{
   struct qxl_bo *bo = get_qxl_bo(_buf);
   struct drm_virgl_3d_transfer_put putcmd;
   int ret;

   putcmd.res_handle = res->res_handle;
   putcmd.bo_handle = bo->handle;
   putcmd.dst_box = *(struct drm_virgl_3d_box *)box;
   putcmd.src_stride = src_stride;
   putcmd.src_offset = buf_offset;
   putcmd.dst_level = level;

   ret = drmIoctl(bo->qws->fd, DRM_IOCTL_VIRGL_TRANSFER_PUT, &putcmd);
   return ret;
}

static int
qxl_bo_transfer_get(struct pb_buffer *_buf,
                    struct qxl_hw_res *res,
                    const struct pipe_box *box,
                    uint32_t buf_offset,
                    uint32_t level)
{
   struct qxl_bo *bo = get_qxl_bo(_buf);
   struct drm_virgl_3d_transfer_get getcmd;
   int ret;

   getcmd.res_handle = res->res_handle;
   getcmd.bo_handle = bo->handle;
   getcmd.level = level;
   getcmd.dst_offset = buf_offset;
   getcmd.box = *(struct drm_virgl_3d_box *)box;
   ret = drmIoctl(bo->qws->fd, DRM_IOCTL_VIRGL_TRANSFER_GET, &getcmd);
   return ret;
}

static void
qxl_drm_buffer_destroy(struct qxl_winsys *qws,
                       struct qxl_winsys_buffer *buffer)
{
   FREE(buffer);
}

static struct qxl_winsys_buffer *
qxl_drm_buffer_from_handle(struct qxl_winsys *qws,
                           struct winsys_handle *whandle,
                           uint32_t size,
                           int32_t *stride)
{
   struct qxl_drm_winsys *qdws = qxl_drm_winsys(qws);
   struct qxl_drm_buffer *buf = CALLOC_STRUCT(qxl_drm_buffer);
   struct drm_gem_open open_arg;
//   struct drm_qxl_bo_info info_arg;
   if (!buf)
      return NULL;

   memset(&open_arg, 0, sizeof(open_arg));
//   memset(&info_arg, 0, sizeof(info_arg));

   buf->magic = 0xDEADBEEF;
   buf->flinked = TRUE;
   buf->flink = whandle->handle;
   buf->size = size;
   open_arg.name = whandle->handle;
   if (drmIoctl(qdws->fd, DRM_IOCTL_GEM_OPEN, &open_arg)) {
      goto err;
   }

   buf->handle = open_arg.handle;
   buf->name = whandle->handle;
   

#if 0
   info_arg.handle = buf->handle;
   if (drmIoctl(qdws->fd, DRM_IOCTL_QXL_BO_INFO, &info_arg)) {
      goto err;
   }

   *stride = info_arg.stride;
#endif
   return (struct qxl_winsys_buffer *)buf;
 err:
   FREE(buf);
   return NULL;

}


static void
qxl_drm_winsys_destroy(struct qxl_winsys *qws)
{
   struct qxl_drm_winsys *qdws = qxl_drm_winsys(qws);

   qdws->cman->destroy(qdws->cman);
   qdws->kman->destroy(qdws->kman);

   FREE(qdws);
}

static void qxl_drm_resource_reference(struct qxl_drm_winsys *qdws,
                                       struct qxl_hw_res **dres,
                                       struct qxl_hw_res *sres)
{
   struct qxl_hw_res *old = *dres;
   struct drm_virgl_3d_resource_unref unrefcmd;
   if (pipe_reference(&(*dres)->reference, &sres->reference)) {
      unrefcmd.res_handle = old->res_handle;
      drmIoctl(qdws->fd, DRM_IOCTL_VIRGL_RESOURCE_UNREF, &unrefcmd);
      
      FREE(old);
   }
   *dres = sres;
}

static struct qxl_hw_res *qxl_drm_winsys_resource_create(struct qxl_winsys *qws,
                                               enum pipe_texture_target target,
                                               uint32_t format,
                                               uint32_t bind,
                                               uint32_t width,
                                               uint32_t height,
                                               uint32_t depth,
                                               uint32_t array_size,
                                               uint32_t last_level,
                                               uint32_t nr_samples)
{
   struct qxl_drm_winsys *qdws = qxl_drm_winsys(qws);
   struct drm_virgl_3d_resource_create createcmd;
   int ret;
   struct qxl_hw_res *res;

   res = CALLOC_STRUCT(qxl_hw_res);
   if (!res)
      return NULL;

   createcmd.target = target;
   createcmd.format = format;
   createcmd.bind = bind;
   createcmd.width = width;
   createcmd.height = height;
   createcmd.depth = depth;
   createcmd.array_size = array_size;
   createcmd.last_level = last_level;
   createcmd.nr_samples = nr_samples;
   createcmd.res_handle = 0;

   ret = drmIoctl(qdws->fd, DRM_IOCTL_VIRGL_RESOURCE_CREATE, &createcmd);
   if (ret != 0) {
      FREE(res);
      return NULL;
   }

   res->res_handle = createcmd.res_handle;
   pipe_reference_init(&res->reference, 1);
   res->num_cs_references = 0;
   return res;
}

static struct qxl_hw_res *qxl_drm_winsys_resource_create_handle(struct qxl_winsys *qws,
                                                                struct winsys_handle *whandle)
{
   struct qxl_drm_winsys *qdws = qxl_drm_winsys(qws);
   struct qxl_hw_res *res;

   res = CALLOC_STRUCT(qxl_hw_res);
   if (!res)
      return NULL;

   res->res_handle = whandle->handle;
   pipe_reference_init(&res->reference, 1);
   res->num_cs_references = 0;
   return res;  
}

static void qxl_drm_winsys_resource_unref(struct qxl_winsys *qws,
                                          struct qxl_hw_res *hres)
{
   struct qxl_drm_winsys *qdws = qxl_drm_winsys(qws);

   qxl_drm_resource_reference(qdws, &hres, NULL);
}

static struct qxl_cmd_buf *qxl_drm_cmd_buf_create(struct qxl_winsys *qws)
{
   struct qxl_drm_winsys *qdws = qxl_drm_winsys(qws);
   struct qxl_drm_cmd_buf *cbuf;

   cbuf = CALLOC_STRUCT(qxl_drm_cmd_buf);
   if (!cbuf)
      return NULL;

   cbuf->ws = qws;

   cbuf->nres = 512;
   cbuf->res_bo = (struct qxl_hw_res **)
      CALLOC(1, cbuf->nres * sizeof(struct qxl_hw_buf*));
   if (!cbuf->res_bo) {
      return FALSE;
   }

   cbuf->base.buf = cbuf->buf;
   return &cbuf->base;
}

static void qxl_drm_cmd_buf_destroy(struct qxl_cmd_buf *_cbuf)
{
   struct qxl_drm_cmd_buf *cbuf = (struct qxl_drm_cmd_buf *)_cbuf;

   FREE(cbuf->res_bo);
   FREE(cbuf);

}

static boolean qxl_drm_lookup_res(struct qxl_drm_cmd_buf *cbuf, struct qxl_hw_res *res)
{
   int i;

   for (i = 0; i < cbuf->cres; i++)
      if (cbuf->res_bo[i] == res)
         return TRUE;
   return FALSE;
}

static void qxl_drm_add_res(struct qxl_drm_winsys *qdws,
                            struct qxl_drm_cmd_buf *cbuf, struct qxl_hw_res *res)
{
   if (cbuf->cres > cbuf->nres)
      assert(0);

   cbuf->res_bo[cbuf->cres] = NULL;
   qxl_drm_resource_reference(qdws, &cbuf->res_bo[cbuf->cres], res);
   p_atomic_inc(&res->num_cs_references);
   cbuf->cres++;
}

static void qxl_drm_release_all_res(struct qxl_drm_winsys *qdws,
                                    struct qxl_drm_cmd_buf *cbuf)
{
   int i;

   for (i = 0; i < cbuf->cres; i++) {
      p_atomic_dec(&cbuf->res_bo[i]->num_cs_references);
      qxl_drm_resource_reference(qdws, &cbuf->res_bo[i], NULL);
   }
   cbuf->cres = 0;
}

static void qxl_drm_emit_res(struct qxl_winsys *qws,
                             struct qxl_cmd_buf *_cbuf, struct qxl_hw_res *res)
{
   struct qxl_drm_winsys *qdws = qxl_drm_winsys(qws);
   struct qxl_drm_cmd_buf *cbuf = (struct qxl_drm_cmd_buf *)_cbuf;
   int i;
   boolean already_in_list = qxl_drm_lookup_res(cbuf, res);
   
   cbuf->base.buf[cbuf->base.cdw++] = res->res_handle;

   if (!already_in_list)
      qxl_drm_add_res(qdws, cbuf, res);
}

static boolean qxl_drm_res_is_ref(struct qxl_winsys *qws,
                               struct qxl_cmd_buf *_cbuf,
                               struct qxl_hw_res *res)
{
   struct qxl_drm_winsys *qdws = qxl_drm_winsys(qws);
   struct qxl_drm_cmd_buf *cbuf = (struct qxl_drm_cmd_buf *)_cbuf;
   
   if (!res->num_cs_references)
      return FALSE;

   return TRUE;
}

static int qxl_drm_winsys_submit_cmd(struct qxl_winsys *qws, struct qxl_cmd_buf *_cbuf)
{
   struct qxl_drm_winsys *qdws = qxl_drm_winsys(qws);
   struct qxl_drm_cmd_buf *cbuf = (struct qxl_drm_cmd_buf *)_cbuf;
   struct drm_virgl_execbuffer eb;
   int ret;

   if (cbuf->base.cdw == 0)
      return 0;

   eb.command = (unsigned long)(void*)cbuf->buf;
   eb.size = cbuf->base.cdw * 4;

   ret = drmIoctl(qdws->fd, DRM_IOCTL_VIRGL_EXECBUFFER, &eb);

   cbuf->base.cdw = 0;

   qxl_drm_release_all_res(qdws, cbuf);
   return ret;
}

struct qxl_winsys *
qxl_drm_winsys_create(int drmFD)
{
   struct qxl_drm_winsys *qdws;
   unsigned int deviceID;

   qdws = CALLOC_STRUCT(qxl_drm_winsys);
   if (!qdws)
      return NULL;

   qdws->fd = drmFD;

   qdws->kman = qxl_bomgr_create(qdws);
   if (!qdws->kman)
      goto fail;
   qdws->cman = pb_cache_manager_create(qdws->kman, 1000000);
   if (!qdws->cman)
      goto fail;

   qdws->base.destroy = qxl_drm_winsys_destroy;

   qdws->base.buffer_from_handle = qxl_drm_buffer_from_handle;
   qdws->base.buffer_destroy = qxl_drm_buffer_destroy;
   
   qdws->base.bo_create = qxl_winsys_bo_create;
   qdws->base.bo_map = qxl_bo_map;
   qdws->base.bo_wait = qxl_bo_wait;
   qdws->base.bo_get_handle = qxl_bo_get_handle;
   qdws->base.transfer_put = qxl_bo_transfer_put;
   qdws->base.transfer_get = qxl_bo_transfer_get;
   qdws->base.resource_create = qxl_drm_winsys_resource_create;
   qdws->base.resource_unref = qxl_drm_winsys_resource_unref;
   qdws->base.resource_create_from_handle = qxl_drm_winsys_resource_create_handle;
   qdws->base.cmd_buf_create = qxl_drm_cmd_buf_create;
   qdws->base.cmd_buf_destroy = qxl_drm_cmd_buf_destroy;
   qdws->base.submit_cmd = qxl_drm_winsys_submit_cmd;
   qdws->base.emit_res = qxl_drm_emit_res;
   qdws->base.res_is_referenced = qxl_drm_res_is_ref;
   return &qdws->base;

 fail:
   FREE(qdws);
   return NULL;
}
