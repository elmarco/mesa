#define _FILE_OFFSET_BITS 64

#include "virgl_drm_winsys.h"
#include "virgl_drm_public.h"
#include "util/u_memory.h"
#include "util/u_format.h"
#include "state_tracker/drm_driver.h"

#include "os/os_mman.h"
#include <sys/ioctl.h>
#include <errno.h>
#include <xf86drm.h>
#include "virgl_drm.h"

struct virgl_bomgr {
   struct pb_manager base;
   struct virgl_drm_winsys *qws;

};

static INLINE struct virgl_bomgr *virgl_bomgr(struct pb_manager *mgr)
{
   return (struct virgl_bomgr *)mgr;
}

const struct pb_vtbl virgl_bo_vtbl;

static INLINE struct virgl_bo *virgl_bo(struct pb_buffer *bo)
{
   assert(bo->vtbl == &virgl_bo_vtbl);
   return (struct virgl_bo *)bo;
}

static struct virgl_bo *get_virgl_bo(struct pb_buffer *_buf)
{
    struct virgl_bo *bo = NULL;

    if (_buf->vtbl == &virgl_bo_vtbl) {
        bo = virgl_bo(_buf);
    } else {
        struct pb_buffer *base_buf;
        pb_size offset;
        pb_get_base_buffer(_buf, &base_buf, &offset);

        if (base_buf->vtbl == &virgl_bo_vtbl)
            bo = virgl_bo(base_buf);
    }

    return bo;
}

static void virgl_bo_get_base_buffer(struct pb_buffer *buf,
                                   struct pb_buffer **base_buf,
                                   unsigned *offset)
{
   *base_buf = buf;
   *offset = 0;
}

static enum pipe_error virgl_bo_validate(struct pb_buffer *_buf,
                                       struct pb_validate *vl,
                                       unsigned flags)
{
   return PIPE_OK;
}
                                       
static void virgl_bo_fence(struct pb_buffer *buf,
                         struct pipe_fence_handle *fence)
{

}

static void virgl_bo_destroy(struct pb_buffer *_buf)
{
   struct virgl_bo *bo = virgl_bo(_buf);
   struct virgl_bomgr *mgr = bo->mgr;
   struct drm_gem_close close_bo;

   if (bo->ptr)
      os_munmap(bo->ptr, bo->base.size);

   close_bo.handle = bo->handle;
   drmIoctl(bo->qws->fd, DRM_IOCTL_GEM_CLOSE, &close_bo);
      
   FREE(bo);
}

const struct pb_vtbl virgl_bo_vtbl = {
   virgl_bo_destroy,
   NULL,
   NULL,
   virgl_bo_validate,
   virgl_bo_fence,
   virgl_bo_get_base_buffer,
};

static struct pb_buffer *virgl_bomgr_create_bo(struct pb_manager *_mgr,
                                             pb_size size,
                                             const struct pb_desc *desc)
{
   struct virgl_bomgr *mgr = virgl_bomgr(_mgr);
   struct virgl_drm_winsys *qdws = mgr->qws;
   struct virgl_bo *bo;
   struct drm_virgl_alloc alloccmd;
   int ret;

   alloccmd.size = size;
   alloccmd.handle = 0;
   ret = drmIoctl(qdws->fd, DRM_IOCTL_VIRGL_ALLOC, &alloccmd);
   if (ret != 0)
      return NULL;

   bo = CALLOC_STRUCT(virgl_bo);

   pipe_reference_init(&bo->base.reference, 1);
   bo->base.alignment = desc->alignment;
   bo->base.usage = desc->usage;
   bo->base.size = size;
   bo->base.vtbl = &virgl_bo_vtbl;
   bo->handle = alloccmd.handle;
   bo->mgr = mgr;
   bo->qws = qdws;
   
   return &bo->base;
}

static void virgl_bomgr_flush(struct pb_manager *mgr)
{

}

static boolean virgl_bomgr_is_buffer_busy(struct pb_manager *mgr,
                                        struct pb_buffer *_buf)
{
   struct virgl_bo *bo = get_virgl_bo(_buf);
   struct drm_virgl_3d_wait waitcmd;
   int ret;

   waitcmd.handle = bo->handle;
   waitcmd.flags = VIRGL_WAIT_NOWAIT;
 again:
   ret = drmIoctl(bo->qws->fd, DRM_IOCTL_VIRGL_WAIT, &waitcmd);
   return (ret != 0 ? TRUE : FALSE);
}

static void virgl_bomgr_destroy(struct pb_manager *_mgr)
{
   FREE(_mgr);
}

static struct pb_manager *virgl_bomgr_create(struct virgl_drm_winsys *qdws)
{
   struct virgl_bomgr *mgr;

   mgr = CALLOC_STRUCT(virgl_bomgr);
   if (!mgr)
      return NULL;

   mgr->base.destroy = virgl_bomgr_destroy;
   mgr->base.create_buffer = virgl_bomgr_create_bo;
   mgr->base.flush = virgl_bomgr_flush;
   mgr->base.is_buffer_busy = virgl_bomgr_is_buffer_busy;

   mgr->qws = qdws;
   return &mgr->base;
}

static void *virgl_bo_map(struct pb_buffer *_buf)
{
   struct drm_virgl_map mmap_arg;
   struct virgl_bo *bo = get_virgl_bo(_buf);
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

static void virgl_bo_wait(struct pb_buffer *_buf)
{
   struct virgl_bo *bo = get_virgl_bo(_buf);
   struct drm_virgl_3d_wait waitcmd;
   int ret;

   waitcmd.handle = bo->handle;
   waitcmd.flags = 0;
 again:
   ret = drmIoctl(bo->qws->fd, DRM_IOCTL_VIRGL_WAIT, &waitcmd);
   if (ret == -EAGAIN)
      goto again;
}

static unsigned int virgl_bo_get_handle(struct pb_buffer *_buf)
{
   struct virgl_bo *bo = get_virgl_bo(_buf);
   return bo->handle;
}

static struct pb_buffer *
virgl_winsys_bo_create(struct virgl_winsys *qws,
                     unsigned size,
                     unsigned alignment)
{
   struct virgl_drm_winsys *qdws = virgl_drm_winsys(qws);
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
virgl_bo_transfer_put(struct virgl_winsys *vws,
                      struct virgl_hw_res *res,
                      const struct pipe_box *box,
                      uint32_t stride, uint32_t layer_stride,
                      uint32_t buf_offset, uint32_t level)
{
   struct virgl_drm_winsys *vdws = virgl_drm_winsys(vws);
   struct drm_virgl_3d_transfer_put putcmd;
   int ret;

   putcmd.bo_handle = res->bo_handle;
   putcmd.box = *(struct drm_virgl_3d_box *)box;
   putcmd.offset = buf_offset;
   putcmd.level = level;
   putcmd.stride = stride;
   putcmd.layer_stride = stride;
   ret = drmIoctl(vdws->fd, DRM_IOCTL_VIRGL_TRANSFER_PUT, &putcmd);
   return ret;
}

static int
virgl_bo_transfer_get(struct virgl_winsys *vws,
                      struct virgl_hw_res *res,
                      const struct pipe_box *box,
                      uint32_t stride, uint32_t layer_stride,
                      uint32_t buf_offset, uint32_t level)
{
   struct virgl_drm_winsys *vdws = virgl_drm_winsys(vws);
   struct drm_virgl_3d_transfer_get getcmd;
   int ret;

   getcmd.bo_handle = res->bo_handle;
   getcmd.level = level;
   getcmd.offset = buf_offset;
   getcmd.stride = stride;
   getcmd.layer_stride = layer_stride;
   getcmd.box = *(struct drm_virgl_3d_box *)box;
   ret = drmIoctl(vdws->fd, DRM_IOCTL_VIRGL_TRANSFER_GET, &getcmd);
   return ret;
}

static void
virgl_drm_winsys_destroy(struct virgl_winsys *qws)
{
   struct virgl_drm_winsys *qdws = virgl_drm_winsys(qws);

   qdws->cman->destroy(qdws->cman);
   qdws->kman->destroy(qdws->kman);

   FREE(qdws);
}

static void virgl_drm_resource_reference(struct virgl_drm_winsys *qdws,
                                       struct virgl_hw_res **dres,
                                       struct virgl_hw_res *sres)
{
   struct virgl_hw_res *old = *dres;
   if (pipe_reference(&(*dres)->reference, &sres->reference)) {
      struct drm_gem_close args;
      if (old->do_del) {
         // refe
      }
      if (old->ptr)
         os_munmap(old->ptr, old->size);
      args.handle = old->bo_handle;
      drmIoctl(qdws->fd, DRM_IOCTL_GEM_CLOSE, &args);

      FREE(old);
   }
   *dres = sres;
}

static struct virgl_hw_res *virgl_drm_winsys_resource_create(struct virgl_winsys *qws,
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
   struct virgl_drm_winsys *qdws = virgl_drm_winsys(qws);
   struct drm_virgl_resource_create createcmd;
   int ret;
   struct virgl_hw_res *res;
   uint32_t stride = width * util_format_get_blocksize(format);
   uint32_t size = stride * height * depth * array_size * (last_level + 1) * (nr_samples ? nr_samples : 1);
   
   res = CALLOC_STRUCT(virgl_hw_res);
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
   createcmd.stride = stride;
   createcmd.size = size;

   ret = drmIoctl(qdws->fd, DRM_IOCTL_VIRGL_RESOURCE_CREATE, &createcmd);
   if (ret != 0) {
      FREE(res);
      return NULL;
   }

   res->do_del = 1;
   res->res_handle = createcmd.res_handle;
   res->bo_handle = createcmd.bo_handle;
   res->size = size;
   res->stride = stride;
   pipe_reference_init(&res->reference, 1);
   res->num_cs_references = 0;
   return res;
}

static struct virgl_hw_res *virgl_drm_winsys_resource_create_handle(struct virgl_winsys *qws,
                                                                struct winsys_handle *whandle)
{
   struct virgl_drm_winsys *qdws = virgl_drm_winsys(qws);
   struct drm_gem_open open_arg = {};
   struct drm_virgl_resource_info info_arg = {};
   struct virgl_hw_res *res;

   memset(&open_arg, 0, sizeof(open_arg));
   res = CALLOC_STRUCT(virgl_hw_res);
   if (!res)
      return NULL;

   open_arg.name = whandle->handle;
   if (drmIoctl(qdws->fd, DRM_IOCTL_GEM_OPEN, &open_arg)) {
        FREE(res);
        goto fail;
   }
   res->do_del = 0;
   res->bo_handle = open_arg.handle;
   res->name = whandle->handle;
   memset(&info_arg, 0, sizeof(info_arg));
   info_arg.bo_handle = res->bo_handle;

   if (drmIoctl(qdws->fd, DRM_IOCTL_VIRGL_RESOURCE_INFO, &info_arg)) {
      /* close */
      FREE(res);
      goto fail;
   }

   res->res_handle = info_arg.res_handle;
   res->size = info_arg.size;
   res->stride = info_arg.stride;
   pipe_reference_init(&res->reference, 1);
   res->num_cs_references = 0;
   return res;  

 fail:
   return NULL;
}

static void virgl_drm_winsys_resource_unref(struct virgl_winsys *qws,
                                          struct virgl_hw_res *hres)
{
   struct virgl_drm_winsys *qdws = virgl_drm_winsys(qws);

   virgl_drm_resource_reference(qdws, &hres, NULL);
}

static void *virgl_drm_resource_map(struct virgl_winsys *qws, struct virgl_hw_res *res)
{
   struct virgl_drm_winsys *qdws = virgl_drm_winsys(qws);
   struct drm_virgl_map mmap_arg;
   void *ptr;

   if (res->ptr)
      return res->ptr;

   mmap_arg.handle = res->bo_handle;
   if (drmIoctl(qdws->fd, DRM_IOCTL_VIRGL_MAP, &mmap_arg))
      return NULL;

   ptr = os_mmap(0, res->size, PROT_READ|PROT_WRITE, MAP_SHARED,
                 qdws->fd, mmap_arg.offset);
   if (ptr == MAP_FAILED)
      return NULL;

   res->ptr = ptr;
   return ptr;

}

static void virgl_drm_resource_wait(struct virgl_winsys *qws, struct virgl_hw_res *res)
{
   struct virgl_drm_winsys *qdws = virgl_drm_winsys(qws);
   struct drm_virgl_3d_wait waitcmd;
   int ret;

   waitcmd.handle = res->bo_handle;
   waitcmd.flags = 0;
 again:
   ret = drmIoctl(qdws->fd, DRM_IOCTL_VIRGL_WAIT, &waitcmd);
   if (ret == -EAGAIN)
      goto again;
}

static struct virgl_cmd_buf *virgl_drm_cmd_buf_create(struct virgl_winsys *qws)
{
   struct virgl_drm_winsys *qdws = virgl_drm_winsys(qws);
   struct virgl_drm_cmd_buf *cbuf;

   cbuf = CALLOC_STRUCT(virgl_drm_cmd_buf);
   if (!cbuf)
      return NULL;

   cbuf->ws = qws;

   cbuf->nres = 512;
   cbuf->res_bo = (struct virgl_hw_res **)
      CALLOC(1, cbuf->nres * sizeof(struct virgl_hw_buf*));
   if (!cbuf->res_bo) {
      return FALSE;
   }

   cbuf->base.buf = cbuf->buf;
   return &cbuf->base;
}

static void virgl_drm_cmd_buf_destroy(struct virgl_cmd_buf *_cbuf)
{
   struct virgl_drm_cmd_buf *cbuf = (struct virgl_drm_cmd_buf *)_cbuf;

   FREE(cbuf->res_bo);
   FREE(cbuf);

}

static boolean virgl_drm_lookup_res(struct virgl_drm_cmd_buf *cbuf, struct virgl_hw_res *res)
{
   int i;

   for (i = 0; i < cbuf->cres; i++)
      if (cbuf->res_bo[i] == res)
         return TRUE;
   return FALSE;
}

static void virgl_drm_add_res(struct virgl_drm_winsys *qdws,
                            struct virgl_drm_cmd_buf *cbuf, struct virgl_hw_res *res)
{
   if (cbuf->cres > cbuf->nres)
      assert(0);

   cbuf->res_bo[cbuf->cres] = NULL;
   virgl_drm_resource_reference(qdws, &cbuf->res_bo[cbuf->cres], res);
   p_atomic_inc(&res->num_cs_references);
   cbuf->cres++;
}

static void virgl_drm_release_all_res(struct virgl_drm_winsys *qdws,
                                    struct virgl_drm_cmd_buf *cbuf)
{
   int i;

   for (i = 0; i < cbuf->cres; i++) {
      p_atomic_dec(&cbuf->res_bo[i]->num_cs_references);
      virgl_drm_resource_reference(qdws, &cbuf->res_bo[i], NULL);
   }
   cbuf->cres = 0;
}

static void virgl_drm_emit_res(struct virgl_winsys *qws,
                             struct virgl_cmd_buf *_cbuf, struct virgl_hw_res *res, boolean write_buf)
{
   struct virgl_drm_winsys *qdws = virgl_drm_winsys(qws);
   struct virgl_drm_cmd_buf *cbuf = (struct virgl_drm_cmd_buf *)_cbuf;
   int i;
   boolean already_in_list = virgl_drm_lookup_res(cbuf, res);
   
   if (write_buf)
      cbuf->base.buf[cbuf->base.cdw++] = res->res_handle;

   if (!already_in_list)
      virgl_drm_add_res(qdws, cbuf, res);
}

static boolean virgl_drm_res_is_ref(struct virgl_winsys *qws,
                               struct virgl_cmd_buf *_cbuf,
                               struct virgl_hw_res *res)
{
   struct virgl_drm_winsys *qdws = virgl_drm_winsys(qws);
   struct virgl_drm_cmd_buf *cbuf = (struct virgl_drm_cmd_buf *)_cbuf;
   
   if (!res->num_cs_references)
      return FALSE;

   return TRUE;
}

static int virgl_drm_winsys_submit_cmd(struct virgl_winsys *qws, struct virgl_cmd_buf *_cbuf)
{
   struct virgl_drm_winsys *qdws = virgl_drm_winsys(qws);
   struct virgl_drm_cmd_buf *cbuf = (struct virgl_drm_cmd_buf *)_cbuf;
   struct drm_virgl_execbuffer eb;
   int ret;

   if (cbuf->base.cdw == 0)
      return 0;

   eb.command = (unsigned long)(void*)cbuf->buf;
   eb.size = cbuf->base.cdw * 4;

   ret = drmIoctl(qdws->fd, DRM_IOCTL_VIRGL_EXECBUFFER, &eb);

   cbuf->base.cdw = 0;

   virgl_drm_release_all_res(qdws, cbuf);
   return ret;
}

static int virgl_drm_get_caps(struct virgl_winsys *vws, struct virgl_drm_caps *caps)
{
   struct virgl_drm_winsys *vdws = virgl_drm_winsys(vws);
   struct drm_virgl_get_caps args;
   struct drm_virgl_map mmap_arg;
   struct drm_gem_close close_bo;
   void *ptr;
   int ret;

   memset(&args, 0, sizeof(args));

   ret = drmIoctl(vdws->fd, DRM_IOCTL_VIRGL_GET_CAPS, &args);
   if (ret)
      return ret;

   mmap_arg.handle = args.handle;
   if (drmIoctl(vdws->fd, DRM_IOCTL_VIRGL_MAP, &mmap_arg))
      return -EINVAL;

   ptr = os_mmap(0, sizeof(union virgl_caps), PROT_READ|PROT_WRITE, MAP_SHARED,
                 vdws->fd, mmap_arg.offset);
   if (ptr == MAP_FAILED)
      return -EINVAL;

   memcpy(&caps->caps, ptr, sizeof(union virgl_caps));
          
   os_munmap(ptr, sizeof(union virgl_caps));

   close_bo.handle = args.handle;
   drmIoctl(vdws->fd, DRM_IOCTL_GEM_CLOSE, &close_bo);
   return 0;
}

struct virgl_winsys *
virgl_drm_winsys_create(int drmFD)
{
   struct virgl_drm_winsys *qdws;
   unsigned int deviceID;

   qdws = CALLOC_STRUCT(virgl_drm_winsys);
   if (!qdws)
      return NULL;

   qdws->fd = drmFD;

   qdws->kman = virgl_bomgr_create(qdws);
   if (!qdws->kman)
      goto fail;
   qdws->cman = pb_cache_manager_create(qdws->kman, 1000000);
   if (!qdws->cman)
      goto fail;

   qdws->base.destroy = virgl_drm_winsys_destroy;

   qdws->base.bo_create = virgl_winsys_bo_create;
   qdws->base.bo_map = virgl_bo_map;
   qdws->base.bo_wait = virgl_bo_wait;
   qdws->base.bo_get_handle = virgl_bo_get_handle;
   qdws->base.transfer_put = virgl_bo_transfer_put;
   qdws->base.transfer_get = virgl_bo_transfer_get;
   qdws->base.resource_create = virgl_drm_winsys_resource_create;
   qdws->base.resource_unref = virgl_drm_winsys_resource_unref;
   qdws->base.resource_create_from_handle = virgl_drm_winsys_resource_create_handle;
   qdws->base.resource_map = virgl_drm_resource_map;
   qdws->base.resource_wait = virgl_drm_resource_wait;
   qdws->base.cmd_buf_create = virgl_drm_cmd_buf_create;
   qdws->base.cmd_buf_destroy = virgl_drm_cmd_buf_destroy;
   qdws->base.submit_cmd = virgl_drm_winsys_submit_cmd;
   qdws->base.emit_res = virgl_drm_emit_res;
   qdws->base.res_is_referenced = virgl_drm_res_is_ref;
   
   qdws->base.get_caps = virgl_drm_get_caps;
   return &qdws->base;

 fail:
   FREE(qdws);
   return NULL;
}
