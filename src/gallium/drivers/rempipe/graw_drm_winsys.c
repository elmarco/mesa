#include <stdint.h>
#include <unistd.h>
#include <stdio.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/mman.h>
#include <sys/un.h>
#include <signal.h>
#include <termios.h>
#include "pipe/p_state.h"
#include "graw_protocol.h"
#include "graw_pipe_winsys.h"
#include "graw_context.h"

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
                                       uint32_t depth)
{
   struct drm_qxl_3d_resource_create createcmd;
   int ret;

   createcmd.target = target;
   createcmd.format = format;
   createcmd.bind = bind;
   createcmd.width = width;
   createcmd.height = height;
   createcmd.depth = depth;
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

static int qxl_3d_alloc(int fd, int size, uint32_t *handle)
{
  int ret;
  struct drm_qxl_3d_alloc alloccmd;

  alloccmd.size = size;
  alloccmd.handle = 0;

  ret = drmIoctl(fd, DRM_IOCTL_QXL_3D_ALLOC, &alloccmd);
  if (ret == 0)
    *handle = alloccmd.handle;
  return ret;
}

static int qxl_3d_transfer_put(int fd, uint32_t res_handle, uint32_t bo_handle,
			struct drm_qxl_3d_box *box,
			struct drm_qxl_3d_box *transfer_box,
			uint32_t level)
{
  struct drm_qxl_3d_transfer_put putcmd;
  int ret;

  putcmd.res_handle = res_handle;
  putcmd.bo_handle = bo_handle;
  putcmd.box = *box;
  putcmd.transfer_box = *transfer_box;
  putcmd.level = level;

  ret = drmIoctl(fd, DRM_IOCTL_QXL_3D_TRANSFER_PUT, &putcmd);
  return ret;
}

static int qxl_3d_transfer_get(int fd, uint32_t res_handle, uint32_t bo_handle,
			struct drm_qxl_3d_box *box)
{
  struct drm_qxl_3d_transfer_get getcmd;
  int ret;
  
  getcmd.res_handle = res_handle;
  getcmd.bo_handle = bo_handle;
  getcmd.box = *box;

  ret = drmIoctl(fd, DRM_IOCTL_QXL_3D_TRANSFER_GET, &getcmd);
  return ret;
}
static void *gem_mmap(int fd, uint32_t handle, int size, int prot)
{
	struct drm_qxl_map mmap_arg;
	void *ptr;

	mmap_arg.handle = handle;
	if (drmIoctl(fd, DRM_IOCTL_QXL_MAP, &mmap_arg))
		return NULL;

	ptr = mmap64(0, size, prot, MAP_SHARED, fd, mmap_arg.offset);
	if (ptr == MAP_FAILED)
		ptr = NULL;

	return ptr;
}

static int qxl_3d_wait(int fd, int handle)
{
  struct drm_qxl_3d_wait waitcmd;
  int ret;

  waitcmd.handle = handle;

  ret = drmIoctl(fd, DRM_IOCTL_QXL_3D_WAIT, &waitcmd);
  return ret;
}


void graw_transfer_block(uint32_t res_handle, int level,
                         const struct pipe_box *transfer_box,
                         const struct pipe_box *box,
                         void *data, int ndw)
{
   int ret;
   uint32_t bo_handle;
   int fd = global_hack_fd;
   int size;
   void *ptr;
   size = ndw * 4;


   ret = qxl_3d_alloc(fd, size, &bo_handle);
   if (ret) {
      fprintf(stderr,"failed to create bo1 %d\n", ret);
      return ;
   }
   
   ptr = gem_mmap(fd, bo_handle, size, PROT_READ|PROT_WRITE);
   if (!ptr) {
   }

   memcpy(ptr, data, ndw*4);

   ret = qxl_3d_transfer_put(fd, res_handle, bo_handle, box,
                             transfer_box, level);


}


void graw_transfer_get_block(uint32_t res_handle, const struct pipe_box *box,
                             void *data, int ndw)
{

   int ret;
   uint32_t bo_handle;
   int fd = global_hack_fd;
   int size;
   void *ptr;
   size = ndw * 4;

   ret = qxl_3d_alloc(fd, size, &bo_handle);
   if (ret) {
      fprintf(stderr,"failed to create bo1 %d\n", ret);
      return;
   }
   ret = qxl_3d_transfer_get(fd, res_handle, bo_handle, box);
  

   ret = qxl_3d_wait(fd, bo_handle);
   
   ptr = gem_mmap(fd, bo_handle, size, PROT_READ|PROT_WRITE);

   memcpy(data, ptr, ndw*4);
}



