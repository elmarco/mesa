#ifndef VIRGL_DRM_WINSYS_H
#define VIRGL_DRM_WINSYS_H

#include "pipe/p_compiler.h"
#include "drm.h"

#include "pipe/p_screen.h"
#include "pipe/p_context.h"
#include "virgl/virgl_winsys.h"
#include "pipebuffer/pb_bufmgr.h"

struct virgl_bomgr;
struct virgl_drm_winsys;

struct virgl_bo {
   struct pb_buffer base;
   struct virgl_bomgr *mgr;
   struct virgl_drm_winsys *qws;
   uint32_t handle;
   void *ptr;
};

struct virgl_hw_res {
   struct pipe_reference reference;
   uint32_t res_handle;
   int num_cs_references;
   uint32_t do_del;
};
  
struct virgl_drm_winsys
{
   struct virgl_winsys base;
   int fd;
   struct pb_manager *kman;
   struct pb_manager *cman;
};

struct virgl_drm_buffer {
   unsigned magic;
   void *ptr;
   boolean flinked;
   unsigned flink;

   uint32_t handle;
   uint32_t name;
   uint32_t size;
};

struct virgl_drm_cmd_buf {
   struct virgl_cmd_buf base;

   uint32_t buf[VIRGL_MAX_CMDBUF_DWORDS];
   
   unsigned nres;
   unsigned cres;
   struct virgl_hw_res **res_bo;
   struct virgl_winsys *ws;
};

static INLINE struct virgl_drm_buffer *
virgl_drm_buffer(struct virgl_winsys_buffer *buffer)
{
   return (struct virgl_drm_buffer *)buffer;
}

static INLINE struct virgl_drm_winsys *
virgl_drm_winsys(struct virgl_winsys *iws)
{
   return (struct virgl_drm_winsys *)iws;
}

#endif
