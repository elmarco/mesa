#ifndef VIRGL_DRM_WINSYS_H
#define VIRGL_DRM_WINSYS_H

#include <stdint.h>
#include "pipe/p_compiler.h"
#include "drm.h"

#include "os/os_thread.h"
#include "util/u_double_list.h"
#include "util/u_inlines.h"
#include "util/u_hash_table.h"

#include "pipe/p_screen.h"
#include "pipe/p_context.h"
#include "pipe/p_context.h"

#include "virgl_hw.h"
#include "virgl/virgl_winsys.h"


struct virgl_drm_winsys;

struct virgl_hw_res {
   struct pipe_reference reference;
   uint32_t res_handle;
   uint32_t bo_handle;
   uint32_t name;
   int num_cs_references;
   uint32_t size;
   void *ptr;
   uint32_t stride;

   struct list_head head;
   uint32_t format;
   uint32_t bind;
   boolean cacheable;
   int64_t start, end;
   boolean flinked;
   uint32_t flink;
};

struct virgl_drm_winsys
{
   struct virgl_winsys base;
   int fd;
   struct list_head delayed;
   int num_delayed;
   unsigned usecs;
   pipe_mutex mutex;

   struct util_hash_table *bo_handles;
   pipe_mutex bo_handles_mutex;
};

struct virgl_drm_cmd_buf {
   struct virgl_cmd_buf base;

   uint32_t buf[VIRGL_MAX_CMDBUF_DWORDS];

   unsigned nres;
   unsigned cres;
   struct virgl_hw_res **res_bo;
   struct virgl_winsys *ws;
   uint32_t *res_hlist;

   char                        is_handle_added[512];
   unsigned                    reloc_indices_hashlist[512];

};

static INLINE struct virgl_drm_winsys *
virgl_drm_winsys(struct virgl_winsys *iws)
{
   return (struct virgl_drm_winsys *)iws;
}

#endif
