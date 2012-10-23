#include <stdlib.h>
#include <sys/select.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdint.h>
#include <stdio.h>
#include "pipe/p_state.h"
#include "graw_protocol.h"
#include "graw_pipe_winsys.h"

#include "graw_renderer_glut.h"
#include "graw_decode.h"
int graw_fd;

uint32_t cmdbuf[65536];
uint32_t decbuf[255];

static void process_cmd(void)
{
   int cmd;
   int ret;
   uint32_t ndw;
   ret = read(graw_fd, &cmd, 4);

   switch (cmd & 0xff) {
   case GRAW_CREATE_RENDERER:
      ret = read(graw_fd, &decbuf, 4 * sizeof(uint32_t));
      graw_renderer_glut_init(decbuf[0], decbuf[1], decbuf[2], decbuf[3]);
      graw_renderer_init(decbuf[0], decbuf[1], decbuf[2], decbuf[3]);

      break;
   case GRAW_CREATE_RESOURCE:
      ret = read(graw_fd, &decbuf, 5 * sizeof(uint32_t));

      graw_renderer_resource_create(decbuf[0], decbuf[1], decbuf[2], decbuf[3], decbuf[4]);
      break;
   case GRAW_FLUSH_FRONTBUFFER:
      ret = read(graw_fd, &decbuf, 1 * sizeof(uint32_t));
      grend_flush_frontbuffer(decbuf[0]);
      break;
   case GRAW_SUBMIT_CMD:
      ndw = cmd >> 16;
      ret = read(graw_fd, &cmdbuf, ndw * sizeof(uint32_t));
      graw_decode_block(cmdbuf, ndw);
      break;
   case GRAW_TRANSFER_PUT:
      ndw = cmd >> 16;
      ret = read(graw_fd, &cmdbuf, ndw * sizeof(uint32_t));
      graw_decode_transfer(cmdbuf, ndw);
      break;
   default:
      fprintf(stderr,"read unknown cmd %d\n", cmd);
      break;
   }
      
}

int main(void)
{
   fd_set rset, errset;
   int ret = 0;

   graw_fd = open(GRAW_PIPENAME, O_RDWR);
   if (graw_fd < 0)
      return -1;
   
   while (ret >= 0) { 
      FD_ZERO(&rset);
      FD_SET(graw_fd, &rset);
      
      ret = select(graw_fd+1, &rset, NULL, NULL, NULL);
      
      if (FD_ISSET(graw_fd, &rset))
	 process_cmd();
   }
}
  
