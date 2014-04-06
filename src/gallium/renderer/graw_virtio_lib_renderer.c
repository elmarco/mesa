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

#define VIRTGPU_FILL_CMD(out) do {                                \
        size_t s;                                                       \
        s = graw_iov_to_buf(cmd_iov, cmd_iov_cnt, 0, &out, sizeof(out));     \
        if (s != sizeof(out)) {                                         \
           fprintf(stderr,                                              \
                          "%s: command size incorrect %zu vs %zu\n",    \
                          __func__, s, sizeof(out));                    \
            return;                                                     \
        }                                                               \
    } while(0)

static int graw_process_cmd(struct virtgpu_cmd_hdr *hdr, 
                            struct virgl_iovec *cmd_iov,
                            unsigned int cmd_num_iovs,
                            struct virgl_iovec *iov,
                            unsigned int niovs);

int virgl_renderer_process_vcmd(struct virtgpu_cmd_hdr *hdr, 
                                struct virgl_iovec *cmd_iov, unsigned int cmd_niovs,
                                struct virgl_iovec *iov, unsigned int niovs)
{
   int ret;
   ret = graw_process_cmd(hdr, cmd_iov, cmd_niovs, iov, niovs);
   graw_renderer_check_fences();
   return ret;
}

static void virgl_cmd_create_resource_2d(struct virgl_iovec *cmd_iov,
                                         unsigned int cmd_iov_cnt)
{
   struct virtgpu_resource_create_2d c2d;
   struct graw_renderer_resource_create_args args;

   VIRTGPU_FILL_CMD(c2d);

   args.handle = c2d.resource_id;
   args.target = 2;
   args.format = c2d.format;
   args.bind = (1 << 1);
   args.width = c2d.width;
   args.height = c2d.height;
   args.depth = 1;
   args.array_size = 1;
   args.last_level = 0;
   args.nr_samples = 0;
   args.flags = VIRGL_RESOURCE_Y_0_TOP;
   graw_renderer_resource_create(&args, NULL, 0);
}

static void virgl_cmd_create_resource_3d(struct virgl_iovec *cmd_iov,
                                         unsigned int cmd_iov_cnt)
{
   struct virtgpu_resource_create_3d c3d;
   struct graw_renderer_resource_create_args args;

   VIRTGPU_FILL_CMD(c3d);

   args.handle = c3d.resource_id;
   args.target = c3d.target;
   args.format = c3d.format;
   args.bind = c3d.bind;
   args.width = c3d.width;
   args.height = c3d.height;
   args.depth = c3d.depth;
   args.array_size = c3d.array_size;
   args.last_level = c3d.last_level;
   args.nr_samples = c3d.nr_samples;
   args.flags = c3d.flags;
   graw_renderer_resource_create(&args, NULL, 0);
}

static void virgl_cmd_resource_unref(struct virgl_iovec *cmd_iov,
                                     unsigned cmd_iov_cnt)
{
   struct virtgpu_resource_unref unref;
   
   VIRTGPU_FILL_CMD(unref);
   graw_renderer_resource_unref(unref.resource_id);
}

static void virgl_cmd_get_caps(struct virgl_iovec *cmd_iov,
                               unsigned cmd_iov_cnt,
                               struct virgl_iovec *iov,
                               unsigned niovs)
{
   struct virtgpu_cmd_get_cap gc;
   struct virtgpu_resp_caps caps;

   VIRTGPU_FILL_CMD(gc);

   caps.hdr.type = VIRTGPU_CMD_GET_CAPS;
   caps.hdr.flags = 0;
   graw_renderer_fill_caps(gc.cap_set,
                           gc.cap_set_version,
                           (union virgl_caps *)&caps.caps);

   graw_iov_from_buf(iov, niovs, 0, &caps, sizeof(union virtgpu_caps));

}

static void virgl_cmd_context_create(struct virgl_iovec *cmd_iov,
                                     unsigned int cmd_iov_cnt)
{
   struct virtgpu_ctx_create cc;
   VIRTGPU_FILL_CMD(cc);
   graw_renderer_context_create(cc.ctx_id, cc.nlen,
                                cc.debug_name); 
}

static void virgl_cmd_context_destroy(struct virgl_iovec *cmd_iov,
                                      unsigned int cmd_iov_cnt)
{
   struct virtgpu_ctx_destroy cd;
   VIRTGPU_FILL_CMD(cd);
   graw_renderer_context_destroy(cd.ctx_id);
}

static void virgl_cmd_resource_flush(struct virgl_iovec *cmd_iov,
                                     unsigned cmd_iov_cnt)
{
   struct virtgpu_resource_flush rf;
   struct pipe_box box;

   VIRTGPU_FILL_CMD(rf);

   box.x = rf.x;
   box.y = rf.y;
   box.z = 0;
   box.width = rf.width;
   box.height = rf.height;
   box.depth = 1;
   graw_renderer_flush_buffer(rf.resource_id, 0, &box);

}

static void virgl_cmd_set_scanout(struct virgl_iovec *cmd_iov,
                                  unsigned cmd_iov_cnt)
{
   struct virtgpu_set_scanout ss;
   struct pipe_box box;
   VIRTGPU_FILL_CMD(ss);
   box.x = ss.x;
   box.y = ss.y;
   box.z = 0;
   box.width = ss.width;
   box.height = ss.height;
   box.depth = 1;
   graw_renderer_set_scanout(ss.resource_id, ss.scanout_id,
                             0, &box);
}

static unsigned virgl_cmd_submit_3d(struct virgl_iovec *cmd_iov,
                                    unsigned cmd_iov_cnt,
                                    struct virgl_iovec *iov,
                                    unsigned niovs)
{
   struct virtgpu_cmd_submit cs;
   VIRTGPU_FILL_CMD(cs);
   graw_decode_block_iov(iov, niovs, cs.ctx_id, cs.phy_addr, cs.size / 4);
   return cs.ctx_id;
}

static void virgl_cmd_transfer_to_host_2d(struct virgl_iovec *cmd_iov,
                                          unsigned cmd_iov_cnt)
{
   struct virtgpu_transfer_to_host_2d t2d;
   struct pipe_box box;

   VIRTGPU_FILL_CMD(t2d);

   box.x = t2d.x;
   box.y = t2d.y;
   box.z = 0;
   box.width = t2d.width;
   box.height = t2d.height;
   box.depth = 1;
   
   graw_renderer_transfer_write_iov(t2d.resource_id,
                                    0,
                                    0,
                                    0,
                                    0,
                                    (struct pipe_box *)&box,
                                    t2d.offset, NULL, 0);

}
                                
static unsigned virgl_cmd_transfer_to_host_3d(struct virgl_iovec *cmd_iov,
                                          unsigned cmd_iov_cnt)
{
   struct virtgpu_transfer_to_host_3d t3d;

   VIRTGPU_FILL_CMD(t3d);
   graw_renderer_transfer_write_iov(t3d.resource_id,
                                    t3d.ctx_id,
                                    t3d.level,
                                    t3d.stride,
                                    t3d.layer_stride,
                                    (struct pipe_box *)&t3d.box,
                                    t3d.data, NULL, 0);
   return t3d.ctx_id;
}

static unsigned virgl_cmd_transfer_from_host_3d(struct virgl_iovec *cmd_iov,
                                                unsigned cmd_iov_cnt)
{
   struct virtgpu_transfer_from_host_3d tf3d;
   VIRTGPU_FILL_CMD(tf3d);
   graw_renderer_transfer_send_iov(tf3d.resource_id,
                                   tf3d.ctx_id,
                                   tf3d.level,
                                   tf3d.stride,
                                   tf3d.layer_stride,
                                   (struct pipe_box *)&tf3d.box,
                                   tf3d.data, NULL, 0);
   return tf3d.ctx_id;
}

static void virgl_resource_attach_backing(struct virgl_iovec *cmd_iov,
                                          unsigned int cmd_iov_cnt,
                                          struct virgl_iovec *iov,
                                          unsigned int iov_cnt)
{
    struct virtgpu_resource_attach_backing att_rb;
    uint32_t gsize = graw_iov_size(iov, iov_cnt);
    struct virgl_iovec *res_iovs;
    int i;
    void *data;
    int ret;

    VIRTGPU_FILL_CMD(att_rb);

    res_iovs = malloc(att_rb.nr_entries * sizeof(struct virgl_iovec));
    if (!res_iovs)
	return;

    if (iov_cnt > 1) {
	data = malloc(gsize);
	graw_iov_to_buf(iov, iov_cnt, 0, data, gsize);
    } else
	data = iov[0].iov_base;

    for (i = 0; i < att_rb.nr_entries; i++) {
	struct virtgpu_mem_entry *ent = ((struct virtgpu_mem_entry *)data) + i;
	res_iovs[i].iov_len = ent->length;
        ret = rcbs->map_iov(&res_iovs[i], ent->addr);
        if (ret) {
           fprintf(stderr, "failed to attach backing %d\n", att_rb.resource_id);
           free(res_iovs);
           res_iovs = NULL;
           goto fail_free;
        }
    }

    ret = graw_renderer_resource_attach_iov(att_rb.resource_id, res_iovs,
                                            att_rb.nr_entries);
    goto out;
 fail_free:
    free(res_iovs);
 out:

    if (iov_cnt > 1)
       free(data);
}

static void virgl_resource_inval_backing_iov(struct virgl_iovec *iov, uint32_t iov_cnt)
{
   int i;
   for (i = 0; i < iov_cnt; i++) {
      rcbs->unmap_iov(&iov[i]);
   }
   free(iov);
}

static void virgl_resource_inval_backing(struct virgl_iovec *cmd_iov, uint32_t cmd_iov_cnt)
{
   struct virtgpu_resource_inval_backing inval_rb;
   VIRTGPU_FILL_CMD(inval_rb);
   graw_renderer_resource_invalid_iov(inval_rb.resource_id);
}

static int graw_process_cmd(struct virtgpu_cmd_hdr *hdr, struct virgl_iovec *cmd_iov,
                            unsigned int cmd_niovs,
                            struct virgl_iovec *iov,
                            unsigned int niovs)
{
   int fence_ctx_id = 0;

   graw_renderer_force_ctx_0();
   switch (hdr->type) {
   case VIRTGPU_CMD_CTX_CREATE:
      virgl_cmd_context_create(cmd_iov, cmd_niovs);
      break;
   case VIRTGPU_CMD_CTX_DESTROY:
      virgl_cmd_context_destroy(cmd_iov, cmd_niovs);
      break;
   case VIRTGPU_CMD_RESOURCE_CREATE_2D:
      virgl_cmd_create_resource_2d(cmd_iov, cmd_niovs);
      break;
   case VIRTGPU_CMD_RESOURCE_CREATE_3D:
      virgl_cmd_create_resource_3d(cmd_iov, cmd_niovs);
      break;
   case VIRTGPU_CMD_SUBMIT_3D:
      fence_ctx_id = virgl_cmd_submit_3d(cmd_iov, cmd_niovs, iov, niovs);
      break;
   case VIRTGPU_CMD_TRANSFER_TO_HOST_2D:
      virgl_cmd_transfer_to_host_2d(cmd_iov, cmd_niovs);
      break;
   case VIRTGPU_CMD_TRANSFER_TO_HOST_3D:
      fence_ctx_id = virgl_cmd_transfer_to_host_3d(cmd_iov, cmd_niovs);
      break;
   case VIRTGPU_CMD_TRANSFER_FROM_HOST_3D:
      fence_ctx_id = virgl_cmd_transfer_from_host_3d(cmd_iov, cmd_niovs);
      break;
    case VIRTGPU_CMD_RESOURCE_ATTACH_BACKING:
      virgl_resource_attach_backing(cmd_iov, cmd_niovs, iov, niovs);
      break;
   case VIRTGPU_CMD_RESOURCE_INVAL_BACKING:
      virgl_resource_inval_backing(cmd_iov, cmd_niovs);
      break;
   case VIRTGPU_CMD_SET_SCANOUT:
      virgl_cmd_set_scanout(cmd_iov, cmd_niovs);
      break;
   case VIRTGPU_CMD_RESOURCE_FLUSH:
      virgl_cmd_resource_flush(cmd_iov, cmd_niovs);
      break;
   case VIRTGPU_CMD_RESOURCE_UNREF:
      virgl_cmd_resource_unref(cmd_iov, cmd_niovs);
      break;
   case VIRTGPU_CMD_CTX_ATTACH_RESOURCE:
      /* TODO add security */
      break;
   case VIRTGPU_CMD_CTX_DETACH_RESOURCE:
      /* TODO add security */
      break;
   case VIRTGPU_CMD_GET_CAPS:
      if (!niovs)
         return;
      virgl_cmd_get_caps(cmd_iov, cmd_niovs, iov, niovs);
      break;
   case VIRTGPU_CMD_GET_DISPLAY_INFO:
      return -1;
   }
   
   if (hdr->flags & VIRGL_COMMAND_EMIT_FENCE)
      graw_renderer_create_fence(hdr->fence, fence_ctx_id);
   return 0;
}

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
   virgl_resource_inval_backing_iov,
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
