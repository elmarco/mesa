
#include "qxl_drm_winsys.h"
#include "qxl_drm_public.h"
#include "util/u_memory.h"
#include "state_tracker/drm_driver.h"

#include <sys/ioctl.h>
#include <xf86drm.h>
#include <sys/mman.h>
#include "qxl_drm.h"

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
   struct drm_qxl_bo_info info_arg;
   if (!buf)
      return NULL;

   memset(&open_arg, 0, sizeof(open_arg));
   memset(&info_arg, 0, sizeof(info_arg));

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
   
   info_arg.handle = buf->handle;
   if (drmIoctl(qdws->fd, DRM_IOCTL_QXL_BO_INFO, &info_arg)) {
      goto err;
   }

   *stride = info_arg.stride;
   return (struct qxl_winsys_buffer *)buf;
 err:
   FREE(buf);
   return NULL;

}

static void *
qxl_drm_buffer_map(struct qxl_winsys *qws,
                   struct qxl_winsys_buffer *buffer,
                   boolean write)
{
   struct qxl_drm_winsys *qdws = qxl_drm_winsys(qws);
   struct qxl_drm_buffer *qbuf = qxl_drm_buffer(buffer);
   struct drm_qxl_map qxl_map;
   struct drm_qxl_update_area qxl_ua;
   void *map;


   if (qbuf->ptr)
      return qbuf->ptr;

   memset(&qxl_map, 0, sizeof(qxl_map));
   qxl_map.handle = qbuf->handle;

   if (drmIoctl(qdws->fd, DRM_IOCTL_QXL_MAP, &qxl_map)) {
      return NULL;
   }

    map = mmap(0, qbuf->size, PROT_READ | PROT_WRITE, MAP_SHARED, qdws->fd,
               qxl_map.offset);
    if (map == MAP_FAILED) {
        return NULL;
    }
    qbuf->ptr = map;
    return map;
}

static void
qxl_drm_buffer_unmap(struct qxl_winsys *qws,
                     struct qxl_winsys_buffer *buffer)
{

}

static void
qxl_drm_winsys_destroy(struct qxl_winsys *qws)
{
   struct qxl_drm_winsys *qdws = qxl_drm_winsys(qws);

   FREE(qdws);

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
   qdws->base.destroy = qxl_drm_winsys_destroy;

   qdws->base.buffer_from_handle = qxl_drm_buffer_from_handle;
   qdws->base.buffer_destroy = qxl_drm_buffer_destroy;
   qdws->base.buffer_map = qxl_drm_buffer_map;
   qdws->base.buffer_unmap = qxl_drm_buffer_unmap;
   return &qdws->base;
}
