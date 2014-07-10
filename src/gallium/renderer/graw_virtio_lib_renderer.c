#include <stdio.h>
#include <time.h>

#include <epoxy/gl.h>

#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/eventfd.h>
#include "virtgpu_hw.h"
#include "pipe/p_state.h"
#include "util/u_format.h"
#include "util/u_math.h"
#include "graw_renderer.h"

#include "virglrenderer.h"
#include "virgl_egl.h"

static struct virgl_renderer_callbacks *rcbs;

static void *dev_cookie;
extern int localrender;
static int use_egl_context;
struct virgl_egl *egl_info;
static struct grend_if_cbs virgl_cbs;

void graw_transfer_write_return(void *data, uint32_t bytes, uint64_t offset,
                                struct virgl_iovec *iov, int num_iovs)
{
   graw_iov_from_buf(iov, num_iovs, offset, data, bytes);
}

void graw_transfer_write_tex_return(struct pipe_resource *res,
				    struct pipe_box *box,
                                    uint32_t level,
                                    uint32_t dst_stride,
                                    uint64_t offset,
                                    struct virgl_iovec *iov,
                                    int num_iovs,
				    void *myptr, int size, int invert)
{
   int elsize = util_format_get_blocksize(res->format);
   int h;
   uint32_t myoffset = offset;
   uint32_t stride = dst_stride ? dst_stride : util_format_get_nblocksx(res->format, u_minify(res->width0, level)) * elsize;
//   uint32_t stride = dst_stride ? dst_stride : util_format_get_nblocksx(res->format, box->width) * elsize;

   if (!invert && (stride == util_format_get_nblocksx(res->format, box->width) * elsize))
      graw_iov_from_buf(iov, num_iovs, offset, myptr, size);
   else if (invert) {
      for (h = box->height - 1; h >= 0; h--) {
         void *sptr = myptr + (h * elsize * box->width);
         graw_iov_from_buf(iov, num_iovs, myoffset, sptr, box->width * elsize);
         myoffset += stride;
      }
   } else {
      for (h = 0; h < box->height; h++) {
         void *sptr = myptr + (h * elsize * box->width);
         graw_iov_from_buf(iov, num_iovs, myoffset, sptr, box->width * elsize);
         myoffset += stride;
      }
   }
}

static void virgl_write_fence(uint32_t fence_id)
{
   rcbs->write_fence(dev_cookie, fence_id);   
}

static virgl_gl_context create_gl_context(int scanout_idx)
{
    if (use_egl_context)
        return virgl_egl_create_context(egl_info);
    return rcbs->create_gl_context(dev_cookie, scanout_idx);
}

static void destroy_gl_context(virgl_gl_context ctx)
{
    if (use_egl_context)
        return virgl_egl_destroy_context(egl_info, ctx);
    return rcbs->destroy_gl_context(dev_cookie, ctx);
}

static int make_current(int scanout_idx, virgl_gl_context ctx)
{
    if (use_egl_context)
        return virgl_egl_make_context_current(egl_info, ctx);
    return rcbs->make_current(dev_cookie, scanout_idx, ctx);
}

static void flush_scanout(int scanout_id, int x, int y, uint32_t width, uint32_t height)
{
   if (rcbs->rect_update)
      rcbs->rect_update(dev_cookie, scanout_id, x, y, width, height);
}

static void scanout_rect_info(int scanout_id, GLuint tex_id,
                              int x, int y, uint32_t width,
                              uint32_t height)
{
   if (rcbs->scanout_rect_info)
      rcbs->scanout_rect_info(dev_cookie, scanout_id, tex_id,
                              x, y, width, height);
}

static void scanout_resource_info(int scanout_id, GLuint tex_id, uint32_t flags,
                                  uint32_t stride, uint32_t width,
                                  uint32_t height, uint32_t format)
{
   if (rcbs->scanout_resource_info)
      rcbs->scanout_resource_info(dev_cookie, scanout_id, tex_id, flags,
                                  stride, width, height, virgl_egl_get_gbm_format(format));
}

static struct grend_if_cbs virgl_cbs = {
   virgl_write_fence,
   scanout_rect_info,
   scanout_resource_info,
   create_gl_context,
   destroy_gl_context,
   make_current,
   flush_scanout,
};

void *virgl_get_cursor_data(uint32_t resource_id, uint32_t *width, uint32_t *height)
{
   return graw_renderer_get_cursor_contents(resource_id, width, height);
}

void virgl_renderer_set_cursor_info(uint32_t cursor_handle, int x, int y)
{
   grend_set_cursor_info(cursor_handle, x, y);
}

void virgl_renderer_poll(void)
{
   graw_renderer_check_queries();
   graw_renderer_check_fences();
}

void virgl_renderer_get_rect(int idx, struct virgl_iovec *iov, unsigned int num_iovs,
                             uint32_t offset, int x, int y, int width, int height)
{
   graw_renderer_get_rect(idx, iov, num_iovs, offset, x, y, width, height);
   
}

int virgl_renderer_init(void *cookie, int flags, struct virgl_renderer_callbacks *cbs)
{
   dev_cookie = cookie;
   rcbs = cbs;
   localrender = 1;

   if (flags & VIRGL_RENDERER_USE_EGL) {
       egl_info = virgl_egl_init();
       if (!egl_info)
           return -1;
       use_egl_context = 1;
   }

   if (cbs->version != 1)
      return -1;
   graw_renderer_init(&virgl_cbs);
   return 0;
}

int virgl_renderer_get_fd_for_texture(uint32_t tex_id, int *fd)
{
    return virgl_egl_get_fd_for_texture(egl_info, tex_id, fd);

}
