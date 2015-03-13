#define _FILE_OFFSET_BITS 64

#include "virgl_vtest_winsys.h"
#include "virgl_vtest_public.h"
#include <sys/socket.h>
#include <errno.h>
#include <stdio.h>
#include <netinet/in.h>
#include <sys/un.h>
#include <unistd.h>

/* connect to remote socket */
#define VTEST_SOCKET_NAME "/tmp/.virgl_test"

int virgl_vtest_connect(struct virgl_vtest_winsys *vws)
{
   struct sockaddr_un un;
   int sock, ret;

   sock = socket(PF_UNIX, SOCK_STREAM, 0);
   if (sock < 0)
      return -1;

   memset(&un, 0, sizeof(un));
   un.sun_family = AF_UNIX;
   snprintf(un.sun_path, sizeof(un.sun_path), "%s", VTEST_SOCKET_NAME);

   do {
      ret = 0;
      if (connect(sock, (struct sockaddr *)&un, sizeof(un)) < 0) {
         ret = -errno;
      }
   } while (ret == -EINTR);

   vws->sock_fd = sock;
   return 0;
}

int virgl_vtest_send_get_caps(struct virgl_vtest_winsys *vws,
                              struct virgl_drm_caps *caps)
{
   uint32_t get_caps_buf[2];
   uint32_t resp_buf[2];

   int ret;
   get_caps_buf[0] = 1;
   get_caps_buf[1] = VCMD_GET_CAPS;

   write(vws->sock_fd, &get_caps_buf, sizeof(get_caps_buf));

   ret = read(vws->sock_fd, resp_buf, 2 * sizeof(uint32_t));

   fprintf(stderr, "ret is %d: %d %d\n", ret, resp_buf[0], resp_buf[1]);

   ret = read(vws->sock_fd, &caps->caps, sizeof(union virgl_caps));

   return 0;
}

int virgl_vtest_send_resource_create(struct virgl_vtest_winsys *vws,
                                     enum pipe_texture_target target,
                                     uint32_t format,
                                     uint32_t bind,
                                     uint32_t width,
                                     uint32_t height,
                                     uint32_t depth,
                                     uint32_t array_size,
                                     uint32_t last_level,
                                     uint32_t nr_samples)
{
   uint32_t res_create_buf[12];
   int handle = 1;

   res_create_buf[0] = 11;
   res_create_buf[1] = VCMD_RESOURCE_CREATE;

   res_create_buf[2] = handle;
   res_create_buf[3] = target;
   res_create_buf[4] = format;
   res_create_buf[5] = bind;
   res_create_buf[6] = width;
   res_create_buf[7] = height;
   res_create_buf[8] = depth;
   res_create_buf[9] = array_size;
   res_create_buf[10] = last_level;
   res_create_buf[11] = nr_samples;

   write(vws->sock_fd, &res_create_buf, sizeof(res_create_buf));
   return 0;
}
