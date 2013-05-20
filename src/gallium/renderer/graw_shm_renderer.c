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
#include "graw_renderer_glx.h"

#define USE_SOCKET 1

#define MAPPING_SIZE (64*1024*1024)
int fd;

int vm_efd;
void *mapping;
int inited;

extern int localrender;
struct qxl_3d_ram *ramp;

static int send_irq(int fd, uint32_t pd)
{
   int ret;
   uint64_t x = 1;
   //   fprintf(stderr,"notify 3d %x\n", pd);

   ramp->pad |= pd;

   ret = write(fd, &x, sizeof(uint64_t));
   if (ret != 8)
      fprintf(stderr,"vm efd write failed %d %d\n", ret, errno);
   return 0;
}

static int create_listening_socket(char * path) {

    struct sockaddr_un local;
    int len, conn_socket;

    if ((conn_socket = socket(AF_UNIX, SOCK_STREAM, 0)) == -1) {
        perror("socket");
        exit(1);
    }

    umask(0);

    local.sun_family = AF_UNIX;
    strcpy(local.sun_path, path);
    unlink(local.sun_path);
    len = strlen(local.sun_path) + sizeof(local.sun_family);
    if (bind(conn_socket, (struct sockaddr *)&local, len) == -1) {
        perror("bind");
        exit(1);
    }
    umask(022);

    if (listen(conn_socket, 5) == -1) {
        perror("listen");
        exit(1);
    }
    return conn_socket;
}

void graw_renderer_init(void);
int main(int argc, char **argv)
{
   int count = 0;
   int conn_socket, maxfd;
   fd_set readset;
   int vm_sock, vm_efd2;
   struct graw_iovec iov;

   if (argc == 2 && !strcmp(argv[1], "-render"))
      localrender = 1;
   fd = shm_open("dave", O_CREAT|O_RDWR, S_IRWXU);
   if (fd == -1)
      return -1;

   if (ftruncate(fd, MAPPING_SIZE) != 0) {
      fprintf(stderr,"couldn't truncate\n");
      exit(-1);
   }

   conn_socket = create_listening_socket("/tmp/shmemsock");
   maxfd = conn_socket;

   /* we only care about one opener in this code */
   for (;;) {
      int ret;
      FD_ZERO(&readset);
      FD_SET(conn_socket, &readset);
      ret = select(maxfd+1, &readset, NULL, NULL, NULL);
      if (ret == -1) {
         perror("select()");
         exit(-1);
      }

      printf("got connection.\n");
      {
         struct sockaddr_un remote;
         socklen_t t = sizeof(remote);
         int new_posn = 1;
         vm_sock = accept(conn_socket, (struct sockaddr *)&remote, &t);
         if (vm_sock == -1) {
            perror("accept");
            exit(1);
         }

         vm_efd = eventfd(0, 0);
         vm_efd2 = eventfd(0, 0);
         sendPosition(vm_sock, new_posn);
         sendUpdate(vm_sock, -1, sizeof(long), fd);

         sendUpdate(vm_sock, 1, sizeof(long), vm_efd);
         sendUpdate(vm_sock, 2, sizeof(long), vm_efd2);
         break;
      }
   }

   mapping = mmap(0, MAPPING_SIZE, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
   if (!mapping)
      return -1;

   ramp = mapping;

   iov.iov_base = mapping;
   iov.iov_len = 0;

   SPICE_RING_INIT(&ramp->cmd_3d_ring);
   ramp->version = 1;
   {
      QXL3DCommand *cmd;
      int notify;
   restart:
      process_x_event();
      count = 0;
      while (SPICE_RING_IS_EMPTY(&ramp->cmd_3d_ring)) {
         struct timespec req = {0, 50000000};

         if (count < 20)
            req.tv_nsec = 50000*count;
         else if (count < 40)
            req.tv_nsec = 500000*count;
         else
            req.tv_nsec = 50000000;
         count++;
         graw_renderer_check_fences();
         nanosleep(&req, NULL);

      }
      
      graw_renderer_check_fences();
      cmd = SPICE_RING_CONS_ITEM(&ramp->cmd_3d_ring);
    
//      fprintf(stderr, "got cmd %x\n", cmd->type);

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
            graw_decode_block_iov(&iov, 1, cmd->u.cmd_submit.phy_addr, cmd->u.cmd_submit.size / 4);
         }

         break;
      case QXL_3D_TRANSFER_GET:
//         fprintf(stderr,"got transfer get %d\n", cmd->u.transfer_get.res_handle);
	 graw_renderer_transfer_send_iov(cmd->u.transfer_get.res_handle,
                                     cmd->u.transfer_get.level,
				     (struct pipe_box *)&cmd->u.transfer_get.box,
                                     cmd->u.transfer_get.phy_addr,
                                         &iov, 1);
	 break;
      case QXL_3D_TRANSFER_PUT:
	 graw_renderer_transfer_write_iov(cmd->u.transfer_put.res_handle,
				      cmd->u.transfer_put.dst_level,
				      cmd->u.transfer_put.src_stride,
				      (struct pipe_box *)&cmd->u.transfer_put.dst_box,
                                      cmd->u.transfer_put.phy_addr,
                                          &iov, 1);
	 break;

      case QXL_3D_SET_SCANOUT:
         graw_renderer_set_scanout(cmd->u.set_scanout.res_handle,
                                   (struct pipe_box *)&cmd->u.set_scanout.box);
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
            graw_renderer_fini_glx();

         }
         ramp->last_fence = 0;
         graw_renderer_init_glx(localrender);
         graw_renderer_init();
         inited = 1;
         break;
      }

      if (cmd->flags & QXL_3D_COMMAND_EMIT_FENCE)
         graw_renderer_create_fence(cmd->fence_id);

      SPICE_RING_POP(&ramp->cmd_3d_ring, notify);

      if (notify) {
         send_irq(vm_efd, 1);
      }
      goto restart;
   }
   
}

void graw_transfer_write_return(void *data, uint32_t ndw, struct graw_iovec *iov, int iovec_cnt)
{
   if (iovec_cnt == 1)
      memcpy(iov[0].iov_base, data, ndw);
   else
      fprintf(stderr,"iovec cnt is > 0\n");
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
   send_irq(vm_efd, 4);
}
