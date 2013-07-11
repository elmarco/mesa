/* library interface from virgl to graw renderer */

#ifndef GRAW_RENDERER_LIB_IF_H
#define GRAW_RENDERER_LIB_IF_H

#define GREND_EXPORT  __attribute__((visibility("default")))
GREND_EXPORT void graw_renderer_poll(void); /* force fences */

GREND_EXPORT void graw_process_vcmd(void *cmd, struct graw_iovec *iov, unsigned int num_iovs);

GREND_EXPORT void graw_renderer_set_cursor_info(uint32_t cursor_handle, int x, int y);

struct graw_renderer_callbacks {
   /* send IRQ to guest */
   void (*send_irq)(void *cookie, uint32_t irq);
   void (*swap_buffers)(void *cookie);
   void (*resize_front)(void *cookie, int width, int height);
   void (*write_fence)(void *cookie, uint32_t fence);
};

GREND_EXPORT void graw_lib_renderer_init(void *cookie, struct graw_renderer_callbacks *cb);
#endif
