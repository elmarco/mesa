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
                               src_stride, 0, level);
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

