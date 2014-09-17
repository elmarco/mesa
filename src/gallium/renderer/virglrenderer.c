#include <stdio.h>
#include <time.h>

#include <epoxy/gl.h>

#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/eventfd.h>
#include "pipe/p_state.h"
#include "util/u_format.h"
#include "util/u_math.h"
#include "vrend_renderer.h"

#include "virglrenderer.h"
#include "virgl_egl.h"
/* new API - just wrap internal API for now */

int virgl_renderer_resource_create(struct virgl_renderer_resource_create_args *args, struct iovec *iov, uint32_t num_iovs)
{
   vrend_renderer_resource_create((struct vrend_renderer_resource_create_args *)args, iov, num_iovs);
   return 0;
}

void virgl_renderer_resource_unref(uint32_t res_handle)
{
   vrend_renderer_resource_unref(res_handle);
}

void virgl_renderer_fill_caps(uint32_t set, uint32_t version,
                              union virgl_caps *caps)
{
   vrend_renderer_fill_caps(set, version, caps);
}

int virgl_renderer_context_create(uint32_t handle, uint32_t nlen, const char *name)
{
   vrend_renderer_context_create(handle, nlen, name);
   return 0;
}

void virgl_renderer_context_destroy(uint32_t handle)
{
   vrend_renderer_context_destroy(handle);
}

void virgl_renderer_submit_cmd(void *buffer,
                               int ctx_id,
                               int ndw)
{
   vrend_decode_block(ctx_id, buffer, ndw);
}

void virgl_renderer_transfer_write_iov(uint32_t handle, 
                                       uint32_t ctx_id,
                                       int level,
                                       uint32_t stride,
                                       uint32_t layer_stride,
                                       struct virgl_box *box,
                                       uint64_t offset,
                                       struct iovec *iovec,
                                       unsigned int iovec_cnt)
{
   vrend_renderer_transfer_write_iov(handle, ctx_id, level,
                                    stride, layer_stride, (struct pipe_box *)box,
                                    offset, iovec, iovec_cnt);
}

void virgl_renderer_transfer_read_iov(uint32_t handle, uint32_t ctx_id,
                                     uint32_t level, uint32_t stride,
                                     uint32_t layer_stride,
                                     struct virgl_box *box,
                                     uint64_t offset, struct iovec *iov,
                                     int iovec_cnt)
{
   vrend_renderer_transfer_send_iov(handle, ctx_id, level, stride,
                                   layer_stride, (struct pipe_box *)box,
                                   offset, iov, iovec_cnt);
}

void virgl_renderer_check_fences()
{
   vrend_renderer_check_fences();
}

int virgl_renderer_resource_attach_iov(int res_handle, struct iovec *iov,
                                      int num_iovs)
{
   return vrend_renderer_resource_attach_iov(res_handle, iov, num_iovs);
}

void virgl_renderer_resource_detach_iov(int res_handle, struct iovec **iov_p, int *num_iovs_p)
{
   return vrend_renderer_resource_detach_iov(res_handle, iov_p, num_iovs_p);
}

int virgl_renderer_create_fence(int client_fence_id, uint32_t ctx_id)
{
   return vrend_renderer_create_fence(client_fence_id, ctx_id);
}

void virgl_renderer_force_ctx_0(void)
{
   vrend_renderer_force_ctx_0();
}

void virgl_renderer_ctx_attach_resource(int ctx_id, int res_handle)
{
   vrend_renderer_attach_res_ctx(ctx_id, res_handle);
}

void virgl_renderer_ctx_detach_resource(int ctx_id, int res_handle)
{
   vrend_renderer_detach_res_ctx(ctx_id, res_handle);
}

int virgl_renderer_resource_get_info(int res_handle,
                                     struct virgl_renderer_resource_info *info)
{
   int ret;
   ret = vrend_renderer_resource_get_info(res_handle, (struct vrend_renderer_resource_info *)info);
   info->format = virgl_egl_get_gbm_format(info->format);
   return ret; 
}

void virgl_renderer_get_cap_set(uint32_t cap_set, uint32_t *max_ver,
                                uint32_t *max_size)
{
   vrend_renderer_get_cap_set(cap_set, max_ver, max_size);
}

void virgl_renderer_get_rect(int resource_id, struct iovec *iov, unsigned int num_iovs,
                                uint32_t offset, int x, int y, int width, int height)
{
   vrend_renderer_get_rect(resource_id, iov, num_iovs, offset, x, y, width, height);
}
