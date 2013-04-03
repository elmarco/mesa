#include <stdint.h>
#include <unistd.h>
#include <stdio.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <signal.h>
#include <termios.h>
#include "pipe/p_state.h"
#include "graw_protocol.h"
#include "graw_pipe_winsys.h"
#include "graw_context.h"

int graw_fd;
uint32_t buf[255];

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

static int do_blocked_write(int fd, void *data, uint32_t bytes)
{
   int left = bytes;
   int sofar = 0;

   do {
      int ret;
      ret = write(fd, data + sofar, left);
      if (ret > 0){
         sofar += ret;
         left -= ret;
      }
      if (ret < 0)
         return ret;
   } while (left > 0);
   return sofar;
}

//#define USE_SERIAL 1

void graw_renderer_init()
{
   struct sockaddr_un address;
   int  socket_fd, nbytes;
   char buffer[256];
   int ret;
    struct sigaction newact; 

    memset(&newact, 0, sizeof(struct sigaction));
    newact.sa_handler = SIG_IGN;
   sigaction(SIGPIPE, &newact, NULL);

#ifndef USE_SERIAL
   socket_fd = socket(PF_UNIX, SOCK_STREAM, 0);
   if(socket_fd < 0)
   {
      printf("socket() failed\n");
      return 1;
   }

   /* start with a clean address structure */
   memset(&address, 0, sizeof(struct sockaddr_un));
   
   address.sun_family = AF_UNIX;
   snprintf(address.sun_path, PATH_MAX, "/tmp/.demo_socket");

   if(connect(socket_fd, 
              (struct sockaddr *) &address, 
              sizeof(struct sockaddr_un)) != 0)
   {
      printf("connect() failed\n");
      return 1;
   }
   graw_fd = socket_fd;
   buf[0] = GRAW_CMD0(GRAW_CREATE_RENDERER, 0, 0);
   ret = write(graw_fd, buf, 1 * sizeof(uint32_t));
#else
   {
      struct termios options;
      graw_fd = open("/dev/virtio-ports/com.redhat.3d.0", O_RDWR);
#if 0
      /* get the current options */
      tcgetattr(graw_fd, &options);
      options.c_lflag     &= ~(ICANON | ECHO | ECHOE | ISIG);
      options.c_iflag &= ~(ICRNL | IXON | IXOFF | IXANY);
      options.c_oflag &= ~OPOST;

      tcsetattr(graw_fd, TCSANOW, &options);
#endif
   }
   buf[0] = GRAW_CMD0(GRAW_CREATE_RENDERER, 0, 0);
   ret = write(graw_fd, buf, 1 * sizeof(uint32_t));
#endif
}

static uint32_t next_handle;
uint32_t graw_object_assign_handle(void)
{
   return next_handle++;
}

uint32_t graw_renderer_resource_create(enum pipe_texture_target target, uint32_t format, uint32_t bind, uint32_t width, uint32_t height, uint32_t depth)
{
   uint32_t handle;
   handle = graw_object_assign_handle();

   buf[0] = GRAW_CMD0(GRAW_CREATE_RESOURCE, 0, 7);
   buf[1] = handle;
   buf[2] = target;
   buf[3] = format;
   buf[4] = bind;
   buf[5] = width;
   buf[6] = height;
   buf[7] = depth;
   write(graw_fd, buf, 8 * sizeof(uint32_t));
   return handle;
}

void grend_flush_frontbuffer(uint32_t res_handle)
{
   buf[0] = GRAW_CMD0(GRAW_FLUSH_FRONTBUFFER, 0, 1);
   buf[1] = res_handle;
   write(graw_fd, buf, 2 * sizeof(uint32_t));
}

void graw_decode_block(uint32_t *block, int ndw)
{
   fprintf(stderr,"sending ndw %d\n", ndw);
   buf[0] = GRAW_CMD0(GRAW_SUBMIT_CMD, 0, ndw);
   write(graw_fd, buf, 1 * sizeof(uint32_t));
   do_blocked_write(graw_fd, block, ndw * sizeof(uint32_t));
}

void graw_transfer_block(uint32_t res_handle, int level,
                         const struct pipe_box *transfer_box,
                         const struct pipe_box *box,
                         void *data, int ndw)
{

   if (ndw > 65536-15) {
      struct pipe_box ntb;
      int h;
      int mdw;
      void *mdata;
      /* need to split */
      if (transfer_box->depth > 1) {
         fprintf(stderr,"gotta handle depth\n");
         exit(-1);
      }
      ntb = *transfer_box;
      mdw = ntb.width;
      ntb.y = 0;
      ntb.height = 1;
      mdata = data;
      for (h = 0; h < transfer_box->height; h++) {
         buf[0] = GRAW_CMD0(GRAW_TRANSFER_PUT, 0, mdw + 14);
         buf[1] = res_handle;
         buf[2] = box->x;
         buf[3] = box->y;
         buf[4] = box->z;
         buf[5] = box->width;
         buf[6] = box->height;
         buf[7] = box->depth;
         buf[8] = ntb.x;
         buf[9] = ntb.y;
         buf[10] = ntb.z;
         buf[11] = ntb.width;
         buf[12] = ntb.height;
         buf[13] = ntb.depth;
         buf[14] = level;
         write(graw_fd, buf, 15 * sizeof(uint32_t));
         do_blocked_write(graw_fd, mdata, mdw * sizeof(uint32_t));      
         mdata += mdw;
         ntb.y++;

      }

   } else {
      
      buf[0] = GRAW_CMD0(GRAW_TRANSFER_PUT, 0, ndw + 14);
      buf[1] = res_handle;
      buf[2] = box->x;
      buf[3] = box->y;
      buf[4] = box->z;
      buf[5] = box->width;
      buf[6] = box->height;
      buf[7] = box->depth;
      buf[8] = transfer_box->x;
      buf[9] = transfer_box->y;
      buf[10] = transfer_box->z;
      buf[11] = transfer_box->width;
      buf[12] = transfer_box->height;
      buf[13] = transfer_box->depth;
      buf[14] = level;
      write(graw_fd, buf, 15 * sizeof(uint32_t));
      do_blocked_write(graw_fd, data, ndw * sizeof(uint32_t));
   }
   
}

void graw_transfer_get_block(uint32_t res_handle, const struct pipe_box *box,
                             void *data, int ndw)
{
   int ret;

   buf[0] = GRAW_CMD0(GRAW_TRANSFER_GET, 0, 7);
   buf[1] = res_handle;
   buf[2] = box->x;
   buf[3] = box->y;
   buf[4] = box->z;
   buf[5] = box->width;
   buf[6] = box->height;
   buf[7] = box->depth;
   write(graw_fd, buf, 8 * sizeof(uint32_t));

   ret = do_blocked_read(graw_fd, data, ndw * 4);
}

