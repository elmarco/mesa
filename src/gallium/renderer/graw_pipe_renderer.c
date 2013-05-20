#include <stdlib.h>
#include <sys/select.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdint.h>
#include <stdio.h>
#include <signal.h>
#include <assert.h>
#include "pipe/p_state.h"
#include "graw_protocol.h"
#include "graw_pipe_winsys.h"

#include "graw_renderer_glut.h"
#include "graw_renderer_glx.h"
#include "graw_renderer.h"
#include "graw_decode.h"

#include <sys/socket.h>
#include <sys/un.h>

int graw_fd;

uint32_t cmdbuf[65536];
uint32_t decbuf[255];

static int do_blocked_read(int fd, void *data, uint32_t bytes)
{
   int left = bytes;
   int sofar = 0;

   do {
      int ret;

      ret = read(fd, data + sofar, left);
      if (ret > 0) {
         sofar += ret;
         left -= ret;
      }
      if (ret < 0)
         return ret;
   } while (left > 0);
   return sofar;
}

static int process_cmd(void)
{
   int cmd;
   int ret;
   uint32_t ndw;

 retry:
   ret = do_blocked_read(graw_fd, &cmd, 4);
   if (ret == 0)
       return -1;

   fprintf(stderr,"cmd is %d %d %d\n", cmd, cmd &0xff, ret);
   switch (cmd & 0xff) {
   case GRAW_CREATE_RENDERER:
      graw_renderer_init_glx(0);
//      graw_renderer_glut_init(decbuf[0], decbuf[1], decbuf[2], decbuf[3]);
      graw_renderer_init();

      break;
   case GRAW_CREATE_RESOURCE:
      ret = do_blocked_read(graw_fd, &decbuf, 7 * sizeof(uint32_t));

	fprintf(stderr,"resource create\n");
        graw_renderer_resource_create(decbuf[0], decbuf[1], decbuf[2], decbuf[3], decbuf[4], decbuf[5], decbuf[6], 0, 0, 0);
      break;
   case GRAW_FLUSH_FRONTBUFFER:
      ret = do_blocked_read(graw_fd, &decbuf, 1 * sizeof(uint32_t));
      grend_flush_frontbuffer(decbuf[0]);
      break;
   case GRAW_SUBMIT_CMD:
      ndw = cmd >> 16;
      fprintf(stderr,"submit cmd pre %d %d\n", ndw, ret);
      ret = do_blocked_read(graw_fd, &cmdbuf, ndw * sizeof(uint32_t));
      fprintf(stderr,"submit cmd %d %d\n", ndw, ret);
//      graw_decode_block(cmdbuf, ndw);
      break;
   case GRAW_TRANSFER_PUT:
      ndw = cmd >> 16;
      ret = do_blocked_read(graw_fd, &cmdbuf, ndw * sizeof(uint32_t));
//      graw_decode_transfer(cmdbuf, ndw);
      break;
   case GRAW_TRANSFER_GET:
      ndw = cmd >> 16;
      ret = do_blocked_read(graw_fd, &decbuf, 7 * sizeof(uint32_t));
//      graw_decode_get_transfer(decbuf, ndw);
      break;
   default:
      fprintf(stderr,"read unknown cmd %d\n", cmd);
      assert(0);
      break;
   }
   goto retry;
}

static void pipehandler(void)
{
  fprintf(stderr,"got sigpipe\n");
  graw_renderer_fini_glx();
}

static int socket_main(void)
{
    struct sockaddr_un address;
    int socket_fd;
    socklen_t address_length = 0;
    int ret = 0;
    fd_set rset, errset;
    struct sigaction newact; 

    socket_fd = socket(PF_UNIX, SOCK_STREAM, 0);
    if(socket_fd < 0) {
        printf("socket() failed\n");
        return 1;
    }

    unlink("/tmp/.demo_socket");

    /* start with a clean address structure */
    memset(&address, 0, sizeof(struct sockaddr_un));

    address.sun_family = AF_UNIX;
    snprintf(address.sun_path, PATH_MAX, "/tmp/.demo_socket");

    if(bind(socket_fd, 
            (struct sockaddr *) &address, 
            sizeof(struct sockaddr_un)) != 0)
    {
        printf("bind() failed\n");
        return 1;
    }

    if(listen(socket_fd, 5) != 0)
    {
        printf("listen() failed\n");
        return 1;
    }
 relisten:
    graw_fd = accept(socket_fd, 
           (struct sockaddr *) &address,
           &address_length);

    memset(&newact, 0, sizeof(struct sigaction));
    newact.sa_handler = SIG_IGN;
    sigaction(SIGPIPE, &newact, NULL);
    while (ret >= 0) { 
        FD_ZERO(&rset);
        FD_SET(graw_fd, &rset);
      
        ret = select(graw_fd+1, &rset, NULL, NULL, NULL);
      
        if (FD_ISSET(graw_fd, &rset)) {
            ret = process_cmd();
            if (ret == -1) {
                close(graw_fd);
                graw_fd = 0;
                graw_reset_decode();
                graw_renderer_fini();
                graw_renderer_fini_glx();
                ret = 0;
                goto relisten;
            }
        }
    }
    return 0;
}

int main(int argc, char **argv)
{
   socket_main();
}

void graw_transfer_write_return(void *data, uint32_t ndw, struct graw_iovec *iov, int iovec_cnt)
{
   uint32_t count = ndw;
   while (count) {
      int amt = count < 4096 ? count : 4096;

      write(graw_fd, data + (ndw - count), amt);
      count -= amt;
   }
}
void graw_transfer_write_tex_return(struct pipe_resource *res,
				    struct pipe_box *box,
                                    uint32_t level,
				    void *data, void *myptr)
{
  fprintf(stderr,"TODO\n");
}

void graw_write_fence(unsigned fence_id)
{

}
