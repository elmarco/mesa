#include <stdint.h>
#include <unistd.h>
#include <stdio.h>
#include <fcntl.h>
#include "pipe/p_state.h"
#include "graw_protocol.h"
#include "graw_pipe_winsys.h"
#include "graw_context.h"

int graw_fd;
int graw_cli_fd;
uint32_t buf[255];

void graw_renderer_init(int x, int y, int width, int height)
{
   graw_fd = open(GRAW_PIPENAME, O_RDWR);
   graw_cli_fd = open(GRAW_CLI_PIPENAME, O_RDWR);

   buf[0] = GRAW_CMD0(GRAW_CREATE_RENDERER, 0, 4);
   buf[1] = x;
   buf[2] = y;
   buf[3] = width;
   buf[4] = height;
   
   write(graw_fd, buf, 5 * sizeof(uint32_t));

}  

static uint32_t next_handle;
uint32_t graw_object_assign_handle(void)
{
   return next_handle++;
}

void graw_renderer_resource_create(uint32_t handle, enum pipe_texture_target target, uint32_t format, uint32_t bind, uint32_t width, uint32_t height)
{
   buf[0] = GRAW_CMD0(GRAW_CREATE_RESOURCE, 0, 6);
   buf[1] = handle;
   buf[2] = target;
   buf[3] = format;
   buf[4] = bind;
   buf[5] = width;
   buf[6] = height;
   write(graw_fd, buf, 7 * sizeof(uint32_t));
}

void grend_flush_frontbuffer(uint32_t res_handle)
{
   buf[0] = GRAW_CMD0(GRAW_FLUSH_FRONTBUFFER, 0, 1);
   buf[1] = res_handle;
   write(graw_fd, buf, 2 * sizeof(uint32_t));

   /* we need to block to get back the contents of the front buffer? */
}

void graw_decode_block(uint32_t *block, int ndw)
{
   fprintf(stderr,"sending ndw %d\n", ndw);
   buf[0] = GRAW_CMD0(GRAW_SUBMIT_CMD, 0, ndw);
   write(graw_fd, buf, 1 * sizeof(uint32_t));
   write(graw_fd, block, ndw * sizeof(uint32_t));
}

void graw_transfer_block(uint32_t res_handle, const struct pipe_box *box,
                         void *data, int ndw)
{
   buf[0] = GRAW_CMD0(GRAW_TRANSFER_PUT, 0, ndw + 7);
   buf[1] = res_handle;
   buf[2] = box->x;
   buf[3] = box->y;
   buf[4] = box->z;
   buf[5] = box->width;
   buf[6] = box->height;
   buf[7] = box->depth;
   write(graw_fd, buf, 8 * sizeof(uint32_t));
   write(graw_fd, data, ndw * sizeof(uint32_t));
   
}

void graw_transfer_get_block(uint32_t res_handle, const struct pipe_box *box,
                             void *data, int ndw)
{
   int left = ndw * 4;
   int sofar = 0;
   buf[0] = GRAW_CMD0(GRAW_TRANSFER_GET, 0, 7);
   buf[1] = res_handle;
   buf[2] = box->x;
   buf[3] = box->y;
   buf[4] = box->z;
   buf[5] = box->width;
   buf[6] = box->height;
   buf[7] = box->depth;
   write(graw_fd, buf, 8 * sizeof(uint32_t));

   fprintf(stderr,"left is %d\n", left);
   /* blocking read time */
   do {
      int ret;

      ret = read(graw_cli_fd, data + sofar, left);
      if (ret > 0) {
         sofar += ret;
         left -= ret;
      }
   } while (left > 0);
   
}

