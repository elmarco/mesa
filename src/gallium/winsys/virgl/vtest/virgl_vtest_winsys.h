#ifndef VIRGL_DRM_WINSYS_H
#define VIRGL_DRM_WINSYS_H

#include <stdint.h>
#include "pipe/p_compiler.h"
#include "pipe/p_defines.h"
#include "pipe/p_state.h"
#include "../drm/virgl_hw.h"
#include "virgl/virgl_winsys.h"

#include "vtest_protocol.h"
struct virgl_vtest_winsys {
   struct virgl_winsys base;

   struct sw_winsys *sws;

   /* fd to remote renderer */
   int sock_fd;
};

struct virgl_hw_res {
   struct pipe_reference reference;
   uint32_t res_handle;

   void *ptr;
   int size;
};

struct virgl_vtest_cmd_buf {
   struct virgl_cmd_buf base;
   uint32_t buf[VIRGL_MAX_CMDBUF_DWORDS];
   unsigned nres;
   unsigned cres;
   struct virgl_winsys *ws;
};

static INLINE struct virgl_vtest_winsys *
virgl_vtest_winsys(struct virgl_winsys *iws)
{
   return (struct virgl_vtest_winsys *)iws;
}

int virgl_vtest_connect(struct virgl_vtest_winsys *vws);
int virgl_vtest_send_get_caps(struct virgl_vtest_winsys *vws,
                              struct virgl_drm_caps *caps);
int virgl_vtest_send_resource_create(struct virgl_vtest_winsys *vws,
                                     enum pipe_texture_target target,
                                     uint32_t format,
                                     uint32_t bind,
                                     uint32_t width,
                                     uint32_t height,
                                     uint32_t depth,
                                     uint32_t array_size,
                                     uint32_t last_level,
                                     uint32_t nr_samples);
int virgl_vtest_submit_cmd(struct virgl_vtest_winsys *vtws,
                           struct virgl_vtest_cmd_buf *cbuf);
#endif
