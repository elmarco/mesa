#ifndef QXL_DRM_WINSYS_H
#define QXL_DRM_WINSYS_H

#include "pipe/p_compiler.h"
#include "drm.h"

#include "pipe/p_screen.h"
#include "pipe/p_context.h"
#include "qxl/qxl_winsys.h"
#include "pipebuffer/pb_bufmgr.h"

struct qxl_bomgr;
struct qxl_drm_winsys;

struct qxl_bo {
   struct pb_buffer base;
   struct qxl_bomgr *mgr;
   struct qxl_drm_winsys *qws;
   uint32_t handle;
   void *ptr;
};

struct qxl_hw_res {
   struct pipe_reference reference;
   uint32_t res_handle;
   int num_cs_references;
   uint32_t do_del;
};
  
struct qxl_drm_winsys
{
   struct qxl_winsys base;
   int fd;
   struct pb_manager *kman;
   struct pb_manager *cman;
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

struct qxl_drm_cmd_buf {
   struct qxl_cmd_buf base;

   uint32_t buf[QXL_MAX_CMDBUF_DWORDS];
   
   unsigned nres;
   unsigned cres;
   struct qxl_hw_res **res_bo;
   struct qxl_winsys *ws;
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
