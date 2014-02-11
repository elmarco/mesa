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
   void (*write_fence)(void *cookie, uint32_t fence);

   void (*map_iov)(struct graw_iovec *iov, uint64_t addr);
   void (*unmap_iov)(struct graw_iovec *iov);
   
   /* interact with GL implementation */
   virgl_gl_context (*create_gl_context)(void *cookie, int scanout_idx);
   void (*destroy_gl_context)(void *cookie, virgl_gl_context ctx);
   int (*make_current)(void *cookie, int scanout_idx, virgl_gl_context ctx);

   /* */
   void (*notify_state)(void *cookie, int idx, int x, int y, uint32_t width, uint32_t height);

   void (*rect_update)(void *cookie, int idx, int x, int y, int width, int height);
   void (*scanout_info)(void *cookie, int idx, uint32_t tex_id, uint32_t flags,
                        int x, int y,
                        uint32_t width, uint32_t height);
};

GREND_EXPORT void graw_lib_renderer_init(void *cookie, struct graw_renderer_callbacks *cb);


/* virtio-gpu compatible interface */

GREND_EXPORT void virgl_renderer_init(void *cookie, struct graw_renderer_callbacks *cb);
GREND_EXPORT void virgl_renderer_poll(void); /* force fences */

GREND_EXPORT int virgl_renderer_process_vcmd(void *cmd, struct graw_iovec *iov, unsigned int num_iovs);

GREND_EXPORT void virgl_renderer_set_cursor_info(uint32_t cursor_handle, int x, int y);

/* we need to give qemu the cursor resource contents */
GREND_EXPORT void *virgl_get_cursor_data(uint32_t resource_id, uint32_t *width, uint32_t *height);

GREND_EXPORT void virgl_renderer_get_rect(int idx, struct graw_iovec *iov, unsigned int num_iovs,
                                          uint32_t offset, int x, int y, int width, int height);

#endif
