#ifndef VIRGL_WINSYS_H
#define VIRGL_WINSYS_H

#include "pipe/p_compiler.h"

struct winsys_handle;
struct virgl_hw_res;

#define VIRGL_MAX_CMDBUF_DWORDS (16*1024)

struct virgl_drm_caps {
   union virgl_caps caps;
};

struct virgl_cmd_buf {
   unsigned cdw;
   uint32_t *buf;
};

struct virgl_winsys {
   unsigned pci_id;

   void (*destroy)(struct virgl_winsys *vws);

   int (*transfer_put)(struct virgl_winsys *vws,
                       struct virgl_hw_res *res,
                       const struct pipe_box *box,
                       uint32_t stride, uint32_t layer_stride,
                       uint32_t buf_offset, uint32_t level);

   int (*transfer_get)(struct virgl_winsys *vws,
                       struct virgl_hw_res *res,
                       const struct pipe_box *box,
                       uint32_t stride, uint32_t layer_stride,
                       uint32_t buf_offset, uint32_t level);

   struct virgl_hw_res *(*resource_create)(struct virgl_winsys *vws,
                               enum pipe_texture_target target,
                               uint32_t format, uint32_t bind,
                               uint32_t width, uint32_t height,
                               uint32_t depth, uint32_t array_size,
                               uint32_t last_level, uint32_t nr_samples,
                               uint32_t size);

   void (*resource_unref)(struct virgl_winsys *vws, struct virgl_hw_res *res);

   void *(*resource_map)(struct virgl_winsys *vws, struct virgl_hw_res *res);
   void (*resource_wait)(struct virgl_winsys *vws, struct virgl_hw_res *res);

   struct virgl_hw_res *(*resource_create_from_handle)(struct virgl_winsys *vws,
                                                       struct winsys_handle *whandle);
   boolean (*resource_get_handle)(struct virgl_winsys *vws,
                                  struct virgl_hw_res *res,
                                  uint32_t stride,
                                  struct winsys_handle *whandle);

   struct virgl_cmd_buf *(*cmd_buf_create)(struct virgl_winsys *ws);
   void (*cmd_buf_destroy)(struct virgl_cmd_buf *buf);

   void (*emit_res)(struct virgl_winsys *vws, struct virgl_cmd_buf *buf, struct virgl_hw_res *res, boolean write_buffer);
   int (*submit_cmd)(struct virgl_winsys *vws, struct virgl_cmd_buf *buf);

   boolean (*res_is_referenced)(struct virgl_winsys *vws,
                                struct virgl_cmd_buf *buf,
                                struct virgl_hw_res *res);

   int (*get_caps)(struct virgl_winsys *vws, struct virgl_drm_caps *caps);

   /* fence */
   struct pipe_fence_handle *(*cs_create_fence)(struct virgl_winsys *vws);
   bool (*fence_wait)(struct virgl_winsys *vws,
                      struct pipe_fence_handle *fence,
                      uint64_t timeout);

   void (*fence_reference)(struct virgl_winsys *vws,
                           struct pipe_fence_handle **dst,
                           struct pipe_fence_handle *src);

   /* for sw paths */
   void (*flush_frontbuffer)(struct virgl_winsys *vws,
                             struct virgl_hw_res *res,
                             unsigned level, unsigned layer,
                             void *winsys_drawable_handle,
                             struct pipe_box *sub_box);
};


#endif
