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
   uint32_t get_caps_buf[VTEST_HDR_SIZE];
   uint32_t resp_buf[VTEST_HDR_SIZE];

   int ret;
   get_caps_buf[VTEST_CMD_LEN] = 0;
   get_caps_buf[VTEST_CMD_ID] = VCMD_GET_CAPS;

   write(vws->sock_fd, &get_caps_buf, sizeof(get_caps_buf));

   ret = read(vws->sock_fd, resp_buf, sizeof(resp_buf));

   fprintf(stderr, "ret is %d: %d %d\n", ret, resp_buf[0], resp_buf[1]);

   ret = read(vws->sock_fd, &caps->caps, sizeof(union virgl_caps));

   return 0;
}

int virgl_vtest_send_resource_create(struct virgl_vtest_winsys *vws,
                                     uint32_t handle,
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
   uint32_t res_create_buf[VCMD_RES_CREATE_SIZE], vtest_hdr[VTEST_HDR_SIZE];

   vtest_hdr[VTEST_CMD_LEN] = VCMD_RES_CREATE_SIZE;
   vtest_hdr[VTEST_CMD_ID] = VCMD_RESOURCE_CREATE;

   res_create_buf[VCMD_RES_CREATE_RES_HANDLE] = handle;
   res_create_buf[VCMD_RES_CREATE_TARGET] = target;
   res_create_buf[VCMD_RES_CREATE_FORMAT] = format;
   res_create_buf[VCMD_RES_CREATE_BIND] = bind;
   res_create_buf[VCMD_RES_CREATE_WIDTH] = width;
   res_create_buf[VCMD_RES_CREATE_HEIGHT] = height;
   res_create_buf[VCMD_RES_CREATE_DEPTH] = depth;
   res_create_buf[VCMD_RES_CREATE_ARRAY_SIZE] = array_size;
   res_create_buf[VCMD_RES_CREATE_LAST_LEVEL] = last_level;
   res_create_buf[VCMD_RES_CREATE_NR_SAMPLES] = nr_samples;

   write(vws->sock_fd, &vtest_hdr, sizeof(vtest_hdr));
   write(vws->sock_fd, &res_create_buf, sizeof(res_create_buf));

   return 0;
}

int virgl_vtest_submit_cmd(struct virgl_vtest_winsys *vws,
                           struct virgl_vtest_cmd_buf *cbuf)
{
   uint32_t vtest_hdr[VTEST_HDR_SIZE];

   vtest_hdr[VTEST_CMD_LEN] = cbuf->base.cdw;
   vtest_hdr[VTEST_CMD_ID] = VCMD_SUBMIT_CMD;

   write(vws->sock_fd, &vtest_hdr, sizeof(vtest_hdr));
   write(vws->sock_fd, cbuf->buf, cbuf->base.cdw * 4);
   return 0;
}

int virgl_vtest_send_resource_unref(struct virgl_vtest_winsys *vws,
                                    uint32_t handle)
{
   uint32_t vtest_hdr[VTEST_HDR_SIZE];
   uint32_t cmd[1];
   vtest_hdr[VTEST_CMD_LEN] = 1;
   vtest_hdr[VTEST_CMD_ID] = VCMD_RESOURCE_UNREF;

   cmd[0] = handle;
   write(vws->sock_fd, &vtest_hdr, sizeof(vtest_hdr));
   write(vws->sock_fd, &cmd, sizeof(cmd));
   return 0;
}
