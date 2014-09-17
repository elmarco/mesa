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
#include "pipe/p_state.h"
#include "util/u_format.h"
#include "util/u_math.h"
#include "vrend_renderer.h"

#include "virglrenderer.h"
#include "virgl_egl.h"

static struct virgl_renderer_callbacks *rcbs;

static void *dev_cookie;
extern int localrender;
static int use_egl_context;
struct virgl_egl *egl_info;
static struct vrend_if_cbs virgl_cbs;

void vrend_transfer_write_return(void *data, uint32_t bytes, uint64_t offset,
                                struct iovec *iov, int num_iovs)
{
   vrend_iov_from_buf(iov, num_iovs, offset, data, bytes);
}

void vrend_transfer_write_tex_return(struct pipe_resource *res,
				    struct pipe_box *box,
                                    uint32_t level,
                                    uint32_t dst_stride,
                                    uint64_t offset,
                                    struct iovec *iov,
                                    int num_iovs,
				    void *myptr, int size, int invert)
{
   int elsize = util_format_get_blocksize(res->format);
   int h;
   uint32_t myoffset = offset;
   uint32_t stride = dst_stride ? dst_stride : util_format_get_nblocksx(res->format, u_minify(res->width0, level)) * elsize;
//   uint32_t stride = dst_stride ? dst_stride : util_format_get_nblocksx(res->format, box->width) * elsize;

   if (!invert && (stride == util_format_get_nblocksx(res->format, box->width) * elsize))
      vrend_iov_from_buf(iov, num_iovs, offset, myptr, size);
   else if (invert) {
      for (h = box->height - 1; h >= 0; h--) {
         void *sptr = myptr + (h * elsize * box->width);
         vrend_iov_from_buf(iov, num_iovs, myoffset, sptr, box->width * elsize);
         myoffset += stride;
      }
   } else {
      for (h = 0; h < box->height; h++) {
         void *sptr = myptr + (h * elsize * box->width);
         vrend_iov_from_buf(iov, num_iovs, myoffset, sptr, box->width * elsize);
         myoffset += stride;
      }
   }
}

static void virgl_write_fence(uint32_t fence_id)
{
   rcbs->write_fence(dev_cookie, fence_id);   
}

static virgl_gl_context create_gl_context(int scanout_idx, bool shared)
{
    if (use_egl_context)
        return virgl_egl_create_context(egl_info);
    return rcbs->create_gl_context(dev_cookie, scanout_idx, shared);
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

static struct vrend_if_cbs virgl_cbs = {
   virgl_write_fence,
   create_gl_context,
   destroy_gl_context,
   make_current,
};

void *virgl_get_cursor_data(uint32_t resource_id, uint32_t *width, uint32_t *height)
{
   return vrend_renderer_get_cursor_contents(resource_id, width, height);
}

void virgl_renderer_poll(void)
{
   virgl_renderer_force_ctx_0();
   vrend_renderer_check_queries();
   vrend_renderer_check_fences();
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
   vrend_renderer_init(&virgl_cbs);
   return 0;
}

int virgl_renderer_get_fd_for_texture(uint32_t tex_id, int *fd)
{
    return virgl_egl_get_fd_for_texture(egl_info, tex_id, fd);
}
