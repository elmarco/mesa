#include <stdio.h>
#include <time.h>

#include <GL/glew.h>
#include <GL/gl.h>

#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/eventfd.h>
#include "send_scm.h"
#include "virgl_hw.h"
#include "pipe/p_state.h"
#include "util/u_format.h"
#include "util/u_math.h"
#include "graw_renderer.h"

#include "virglrenderer.h"
static struct graw_renderer_callbacks *rcbs;

static void *dev_cookie;
extern int localrender;

void graw_renderer_init(void);
static void graw_process_cmd(struct virgl_command *cmd, struct graw_iovec *iov,
                             unsigned int niovs);

void graw_lib_renderer_init(void *cookie, struct graw_renderer_callbacks *cbs)
{
   dev_cookie = cookie;
   rcbs = cbs;
   localrender = 1;
}

static void send_irq(uint32_t pd)
{
   rcbs->send_irq(dev_cookie, pd);
}

void graw_process_vcmd(void *cmd, struct graw_iovec *iov, unsigned int niovs)
{
   struct virgl_command *qcmd = cmd;

   graw_process_cmd(qcmd, iov, niovs);
   graw_renderer_check_fences();
}

static void graw_process_cmd(struct virgl_command *cmd, struct graw_iovec *iov,
                             unsigned int niovs)
{
   static int inited;

   switch (cmd->type) {
   case VIRGL_CMD_CREATE_CONTEXT:
      graw_renderer_context_create(cmd->u.ctx.handle, cmd->u.ctx_create.nlen,
         cmd->u.ctx_create.debug_name);
      break;
   case VIRGL_CMD_DESTROY_CONTEXT:
      graw_renderer_context_destroy(cmd->u.ctx.handle);
      break;
   case VIRGL_CMD_CREATE_RESOURCE: {
      struct graw_renderer_resource_create_args args;

      args.handle = cmd->u.res_create.handle;
      args.target = cmd->u.res_create.target;
      args.format = cmd->u.res_create.format;
      args.bind = cmd->u.res_create.bind;
      args.width = cmd->u.res_create.width;
      args.height = cmd->u.res_create.height;
      args.depth = cmd->u.res_create.depth;
      args.array_size = cmd->u.res_create.array_size;
      args.last_level = cmd->u.res_create.last_level;
      args.nr_samples = cmd->u.res_create.nr_samples;
         
//         fprintf(stderr,"got res create %d %d\n", cmd->u.res_create.width,
//                 cmd->u.res_create.height);
      graw_renderer_resource_create(&args);

      break;
   }
   case VIRGL_CMD_SUBMIT:
//         fprintf(stderr,"cmd submit %lx %d\n", cmd->u.cmd_submit.data, cmd->u.cmd_submit.size);
      
   {
      graw_decode_block_iov(iov, niovs, cmd->u.cmd_submit.ctx_id, cmd->u.cmd_submit.phy_addr, cmd->u.cmd_submit.size / 4);
   }
   
   break;
   case VIRGL_CMD_TRANSFER_GET:
//         fprintf(stderr,"got transfer get %d\n", cmd->u.transfer_get.res_handle);
      if (!niovs)
         return;
      grend_stop_current_queries();
      graw_renderer_transfer_send_iov(cmd->u.transfer_get.res_handle,
                                      cmd->u.transfer_get.ctx_id,
                                      cmd->u.transfer_get.level,
                                      cmd->u.transfer_get.stride,
                                      cmd->u.transfer_get.layer_stride,
                                      (struct pipe_box *)&cmd->u.transfer_get.box,
                                      cmd->u.transfer_get.data, iov,
                                      niovs);
      break;
   case VIRGL_CMD_TRANSFER_PUT:
      if (!niovs)
         return;
      grend_stop_current_queries();
      graw_renderer_transfer_write_iov(cmd->u.transfer_put.res_handle,
                                       cmd->u.transfer_put.ctx_id,
                                       cmd->u.transfer_put.level,
                                       cmd->u.transfer_put.stride,
                                       cmd->u.transfer_put.layer_stride,
                                   (struct pipe_box *)&cmd->u.transfer_put.box,
                                   cmd->u.transfer_put.data, iov,
                                   niovs);
      break;
      
   case VIRGL_CMD_SET_SCANOUT:
      grend_stop_current_queries();
      graw_renderer_set_scanout(cmd->u.set_scanout.res_handle,
                                cmd->u.set_scanout.ctx_id,
                                (struct pipe_box *)&cmd->u.set_scanout.box);
      (*rcbs->resize_front)(dev_cookie, cmd->u.set_scanout.box.w,
                            cmd->u.set_scanout.box.h);
      break;
   case VIRGL_CMD_FLUSH_BUFFER:
      grend_stop_current_queries();
      graw_renderer_flush_buffer(cmd->u.flush_buffer.res_handle,
                                 cmd->u.flush_buffer.ctx_id,
                                 (struct pipe_box *)&cmd->u.flush_buffer.box);
      break;
   case VIRGL_CMD_RESOURCE_UNREF:
      graw_renderer_resource_unref(cmd->u.res_unref.res_handle);
      break;
   case VIRGL_CMD_ATTACH_RES_CTX:
      /* TODO add security */
      break;
   case VIRGL_CMD_DETACH_RES_CTX:
      /* TODO add security */
      break;
   case VIRGL_CMD_GET_3D_CAPABILITIES:
      if (!niovs)
         return;
      graw_renderer_fill_caps(cmd->u.get_cap.cap_set,
                              cmd->u.get_cap.cap_set_version,
                              cmd->u.get_cap.offset, iov, niovs);
      break;
   case 0xdeadbeef:
      if (inited) {
         graw_renderer_fini();
         
      }
      graw_renderer_init();
      inited = 1;
      break;
   }
   
   if (cmd->flags & VIRGL_COMMAND_EMIT_FENCE)
      graw_renderer_create_fence(cmd->fence_id);

}

void graw_transfer_write_return(void *data, uint32_t bytes, uint64_t offset,
                                struct graw_iovec *iov, int num_iovs)
{
   graw_iov_from_buf(iov, num_iovs, offset, data, bytes);
}

void graw_transfer_write_tex_return(struct pipe_resource *res,
				    struct pipe_box *box,
                                    uint32_t level,
                                    uint32_t dst_stride,
                                    uint64_t offset,
                                    struct graw_iovec *iov,
                                    int num_iovs,
				    void *myptr, int size, int invert)
{
   int elsize = util_format_get_blocksize(res->format);
   int h;
   uint32_t myoffset = offset;
   uint32_t stride = dst_stride ? dst_stride : box->width * elsize;

   if (!invert && (!dst_stride || dst_stride == box->width * elsize))
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

void graw_write_fence(unsigned fence_id)
{
   rcbs->write_fence(dev_cookie, fence_id);   
   send_irq(0x20);
}

int swap_buffers(void)
{
    rcbs->swap_buffers(dev_cookie);  
    return 0;
}

void graw_renderer_set_cursor_info(uint32_t cursor_handle, int x, int y)
{
   grend_set_cursor_info(cursor_handle, x, y);
}

void graw_renderer_poll(void)
{
   graw_renderer_check_queries();
   graw_renderer_check_fences();
}
