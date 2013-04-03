#ifndef QXL_DRM_WINSYS_H
#define QXL_DRM_WINSYS_H

#include "pipe/p_compiler.h"
#include "drm.h"

#include "pipe/p_screen.h"
#include "pipe/p_context.h"
#include "qxl/qxl_winsys.h"

struct qxl_drm_winsys
{
   struct qxl_winsys base;
   int fd;
};

struct qxl_drm_buffer {
   unsigned magic;
   void *ptr;
   boolean flinked;
   unsigned flink;

   uint32_t handle;
   uint32_t name;
   uint32_t size;
};

static INLINE struct qxl_drm_buffer *
qxl_drm_buffer(struct qxl_winsys_buffer *buffer)
{
   return (struct qxl_drm_buffer *)buffer;
}

static INLINE struct qxl_drm_winsys *
qxl_drm_winsys(struct qxl_winsys *iws)
{
   return (struct qxl_drm_winsys *)iws;
}

#endif
