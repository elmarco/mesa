/* library interface from QEMU to virglrenderer */

#ifndef VIRGLRENDERER_H
#define VIRGLRENDERER_H

#include <stdint.h>

struct graw_iovec;

#define GREND_EXPORT  __attribute__((visibility("default")))
GREND_EXPORT void graw_renderer_poll(void); /* force fences */

GREND_EXPORT void graw_process_vcmd(void *cmd, struct graw_iovec *iov, unsigned int num_iovs);

GREND_EXPORT void graw_renderer_set_cursor_info(uint32_t cursor_handle, int x, int y);


typedef void *virgl_gl_context;

struct graw_renderer_callbacks {
   /* send IRQ to guest */
   void (*send_irq)(void *cookie, uint32_t irq);
   int (*swap_buffers)(void *cookie);
   void (*resize_window)(void *cookie, int idx, int width, int height);
   void (*write_fence)(void *cookie, uint32_t fence);

   void (*map_iov)(struct graw_iovec *iov, uint64_t addr);
   void (*unmap_iov)(struct graw_iovec *iov);
   
   /* interact with GL implementation */
   virgl_gl_context (*create_gl_context)(void *cookie);
   void (*destroy_gl_context)(void *cookie, virgl_gl_context ctx);
   int (*make_current)(void *cookie, virgl_gl_context ctx);
   virgl_gl_context (*get_current_context)(void *cookie);
};

GREND_EXPORT void graw_lib_renderer_init(void *cookie, struct graw_renderer_callbacks *cb);


/* virtio-gpu compatible interface */

GREND_EXPORT void virgl_renderer_init(void *cookie, struct graw_renderer_callbacks *cb);
GREND_EXPORT void virgl_renderer_poll(void); /* force fences */

GREND_EXPORT int virgl_renderer_process_vcmd(void *cmd, struct graw_iovec *iov, unsigned int num_iovs);

GREND_EXPORT void virgl_renderer_set_cursor_info(uint32_t cursor_handle, int x, int y);
#endif
