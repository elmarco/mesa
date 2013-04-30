#include <stdint.h>
#include <unistd.h>
#include <stdio.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/mman.h>
#include <sys/un.h>
#include <signal.h>
#include <termios.h>

#include "pipebuffer/pb_buffer.h"
#include "pipe/p_state.h"
#include "graw_protocol.h"
#include "graw_pipe_winsys.h"
#include "graw_context.h"
#include "rempipe.h"
#include "xf86drm.h"
#include "qxl_drm.h"

int global_hack_fd;


static uint32_t next_handle;
uint32_t graw_object_assign_handle(void)
{
   return next_handle++;
}

void grend_flush_frontbuffer(uint32_t res_handle)
{
}
void graw_renderer_init()
{
   return;
}

uint32_t graw_renderer_resource_create(enum pipe_texture_target target,
                                       uint32_t format,
                                       uint32_t bind,
                                       uint32_t width,
                                       uint32_t height,
                                       uint32_t depth,
                                       uint32_t array_size,
                                       uint32_t last_level,
                                       uint32_t nr_samples)
{
   struct drm_qxl_3d_resource_create createcmd;
   int ret;

   createcmd.target = target;
   createcmd.format = format;
   createcmd.bind = bind;
   createcmd.width = width;
   createcmd.height = height;
   createcmd.depth = depth;
   createcmd.array_size = array_size;
   createcmd.last_level = last_level;
   createcmd.nr_samples = nr_samples;
   createcmd.res_handle = 0;

   ret = drmIoctl(global_hack_fd, DRM_IOCTL_QXL_3D_RESOURCE_CREATE, &createcmd);
   if (ret == 0)
      return createcmd.res_handle;
   return 0;
}


void graw_decode_block(uint32_t *block, int ndw)
{
   struct drm_qxl_execbuffer eb;
   struct drm_qxl_command cmd;
   int ret;

   if (ndw == 0)
      return;

   cmd.command = (unsigned long)(void*)block;
   cmd.command_size = ndw * 4;
   cmd.relocs_num = 0;
   cmd.relocs = 0;
   cmd.type = 0;
    
   eb.flags = QXL_EXECBUFFER_3D;
   eb.commands_num = 1;
   eb.commands = (unsigned long)(void*)&cmd;

   ret = drmIoctl(global_hack_fd, DRM_IOCTL_QXL_EXECBUFFER, &eb);
   return;
}

void graw_transfer_block(struct rempipe_screen *rs,
                         uint32_t res_handle, int level,
                         const struct pipe_box *box,
                         uint32_t src_stride,
                         void *data, int ndw)
{
   int ret;
   int size;
   void *ptr;
   struct pb_buffer *buf;
   size = ndw * 4;

   buf = rs->qws->bo_create(rs->qws, size, 0);
   if (!buf) {
      fprintf(stderr,"failed to create bo1\n");
      return ;
   }

   ptr = rs->qws->bo_map(buf);
   if (!ptr) {
      fprintf(stderr,"failed to map bo1\n");
      return;
   }

   memcpy(ptr, data, ndw * 4);
  
   ret = rs->qws->transfer_put(buf, res_handle, box,
                               src_stride, level);
   pb_reference(&buf, NULL);
}


void graw_transfer_get_block(struct rempipe_screen *rs,
                             uint32_t res_handle, uint32_t level,
                             const struct pipe_box *box,
                             void *data, int ndw)
{

   int ret;
   int size;
   void *ptr;
   struct pb_buffer *buf;
   size = ndw * 4;

   buf = rs->qws->bo_create(rs->qws, size, 0);
   if (!buf) {
      fprintf(stderr,"failed to create bo1\n");
      return ;
   }

   ptr = rs->qws->bo_map(buf);
   if (!ptr) {
      fprintf(stderr,"failed to map bo1\n");
      return;
   }

   ret = rs->qws->transfer_get(buf, res_handle, box, level);

   rs->qws->bo_wait(buf);

   ptr = rs->qws->bo_map(buf);
   
   memcpy(data, ptr, ndw*4);

   pb_reference(&buf, NULL);
}

