#include <stdio.h>
#include <time.h>

#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/eventfd.h>
#include "send_scm.h"
#include "qxl_3d_dev.h"
#include "pipe/p_state.h"
#include "util/u_format.h"
#include "util/u_math.h"
#include "graw_renderer.h"

#include "graw_renderer_lib_if.h"
static struct graw_renderer_callbacks *rcbs;

static volatile struct qxl_3d_ram *ramp;
static void *mapping;
static void *dev_cookie;
extern int localrender;

void graw_renderer_init(void);
static void graw_process_cmd(QXL3DCommand *cmd);

void graw_renderer_set_vram_params(void *ptr, uint32_t size)
{
   ramp = mapping = ptr;
   
   SPICE_RING_INIT(&ramp->cmd_3d_ring);
   ramp->version = 1;
   localrender = 1;
}

void graw_lib_renderer_init(void *cookie, struct graw_renderer_callbacks *cbs)
{
   dev_cookie = cookie;
   rcbs = cbs;
}

static int send_irq(uint32_t pd)
{
   int ret;
   uint64_t x = 1;
   //   fprintf(stderr,"notify 3d %x\n", pd);

   ramp->pad |= pd;

   rcbs->send_irq(dev_cookie, 0);

   return 0;
}

void graw_renderer_ping(void)
{
   QXL3DCommand *cmd;
   int notify;

 retry:
   if (SPICE_RING_IS_EMPTY(&ramp->cmd_3d_ring))
      return;

   graw_renderer_check_fences();
   cmd = SPICE_RING_CONS_ITEM(&ramp->cmd_3d_ring);
    
   graw_process_cmd(cmd);
//      fprintf(stderr, "got cmd %x\n", cmd->type);

   SPICE_RING_POP(&ramp->cmd_3d_ring, notify);

   if (notify) {
      send_irq(1);
   }
   
   goto retry;
}

void graw_process_vcmd(void *cmd)
{
   QXL3DCommand *qcmd = cmd;
   graw_process_cmd(qcmd);
}

static void graw_process_cmd(QXL3DCommand *cmd)
{
   static int inited;

   switch (cmd->type) {
   case QXL_3D_CMD_CREATE_RESOURCE:
//         fprintf(stderr,"got res create %d %d\n", cmd->u.res_create.width,
//                 cmd->u.res_create.height);
      graw_renderer_resource_create(cmd->u.res_create.handle,
                                    cmd->u.res_create.target,
                                    cmd->u.res_create.format,
                                    cmd->u.res_create.bind,
                                    cmd->u.res_create.width,
                                    cmd->u.res_create.height,
                                    cmd->u.res_create.depth,
                                    cmd->u.res_create.array_size,
                                    cmd->u.res_create.last_level,
                                    cmd->u.res_create.nr_samples);
      break;
   case QXL_3D_CMD_SUBMIT:
//         fprintf(stderr,"cmd submit %lx %d\n", cmd->u.cmd_submit.phy_addr, cmd->u.cmd_submit.size);
      
   {
      uint32_t *cmdmap = mapping + cmd->u.cmd_submit.phy_addr;
//            fprintf(stderr,"%08x %08x\n", cmdmap[0], cmdmap[1]);
      graw_decode_block(cmdmap, cmd->u.cmd_submit.size / 4);
   }
   
   break;
   case QXL_3D_TRANSFER_GET:
//         fprintf(stderr,"got transfer get %d\n", cmd->u.transfer_get.res_handle);
      graw_renderer_transfer_send(cmd->u.transfer_get.res_handle,
                                  cmd->u.transfer_get.level,
                                  (struct pipe_box *)&cmd->u.transfer_get.box,
                                  mapping + cmd->u.transfer_get.phy_addr);
      break;
   case QXL_3D_TRANSFER_PUT:
      graw_renderer_transfer_write(cmd->u.transfer_put.res_handle,
                                   cmd->u.transfer_put.dst_level,
                                   cmd->u.transfer_put.src_stride,
                                   (struct pipe_box *)&cmd->u.transfer_put.dst_box,
                                   mapping + cmd->u.transfer_put.phy_addr);
      break;
      
   case QXL_3D_SET_SCANOUT:
      graw_renderer_set_scanout(cmd->u.set_scanout.res_handle,
                                (struct pipe_box *)&cmd->u.set_scanout.box);
      (*rcbs->resize_front)(dev_cookie, cmd->u.set_scanout.box.w,
                            cmd->u.set_scanout.box.h);
      break;
   case QXL_3D_FLUSH_BUFFER:
      graw_renderer_flush_buffer(cmd->u.flush_buffer.res_handle,
                                 (struct pipe_box *)&cmd->u.flush_buffer.box);
      break;
   case QXL_3D_RESOURCE_UNREF:
      graw_renderer_resource_unref(cmd->u.res_unref.res_handle);
      break;
   case 0xdeadbeef:
      if (inited) {
         graw_renderer_fini();
         
      }
      ramp->last_fence = 0;
      graw_renderer_init();
      inited = 1;
      break;
   }
   
   if (cmd->flags & QXL_3D_COMMAND_EMIT_FENCE)
      graw_renderer_create_fence(cmd->fence_id);

}

void graw_transfer_write_return(void *data, uint32_t ndw, void *myptr)
{
  memcpy(myptr, data, ndw);
}

void graw_transfer_write_tex_return(struct pipe_resource *res,
				    struct pipe_box *box,
                                    uint32_t level,
				    void *data, void *myptr)
{
   int h;
   int elsize = util_format_get_blocksize(res->format);
   void *dptr = myptr;
   int w = u_minify(res->width0, level);
   int resh = u_minify(res->height0, level);

   for (h = resh - box->y - 1; h >= resh - box->y - box->height; h--) {
      void *sptr = data + (h * elsize * w) + box->x * elsize;
      memcpy(dptr, sptr, box->width * elsize);
      dptr += box->width * elsize;
   }

}

void graw_write_fence(unsigned fence_id)
{
   ramp->last_fence = fence_id;
   send_irq(4);
}

int swap_buffers(void)
{
    rcbs->swap_buffers(dev_cookie);  
    return 0;
}
