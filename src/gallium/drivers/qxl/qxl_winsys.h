#ifndef QXL_WINSYS_H
#define QXL_WINSYS_H

#include "pipe/p_compiler.h"

struct qxl_winsys_buffer;

struct qxl_winsys {
   unsigned pci_id;

   struct qxl_winsys_buffer *
   (*buffer_from_handle)(struct qxl_winsys *qws,
                         struct winsys_handle *whandle,
                         uint32_t size,
                         int32_t *stride);
   
   void *(*buffer_map)(struct qxl_winsys *qws,
                       struct qxl_winsys_buffer *buffer,
                       boolean write);

   void (*buffer_unmap)(struct qxl_winsys *qws,
                        struct qxl_winsys_buffer *buffer);

   void (*buffer_destroy)(struct qxl_winsys *qws,
                          struct qxl_winsys_buffer *buffer);

   void (*destroy)(struct qxl_winsys *qws);

   /* 3d fns */
   struct pb_buffer *(*bo_create)(struct qxl_winsys *qws,
                                  unsigned size,
                                  unsigned alignment);

   boolean (*bo_is_busy)(struct pb_buffer *buf);
   void (*bo_wait)(struct pb_buffer *buf);
   void *(*bo_map)(struct pb_buffer *buf);
   unsigned int (*bo_get_handle)(struct pb_buffer *buf);

   int (*transfer_put)(struct pb_buffer *buf,
                       uint32_t res_handle, const struct pipe_box *box,
                       uint32_t src_stride, uint32_t level);

   int (*transfer_get)(struct pb_buffer *buf,
                       uint32_t res_handle, const struct pipe_box *box,
                       uint32_t level);
                       
};

#endif
