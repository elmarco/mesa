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
};

#endif
