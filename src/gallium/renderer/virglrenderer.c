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
#include "virtgpu_hw.h"
#include "pipe/p_state.h"
#include "util/u_format.h"
#include "util/u_math.h"
#include "graw_renderer.h"

#include "virglrenderer.h"

/* new API - just wrap internal API for now */

void virgl_renderer_resource_create(struct virgl_renderer_resource_create_args *args, struct virgl_iovec *iov, uint32_t num_iovs)
{
   graw_renderer_resource_create((struct graw_renderer_resource_create_args *)args, iov, num_iovs);
}

void virgl_renderer_resource_unref(uint32_t res_handle)
{
   graw_renderer_resource_unref(res_handle);
}

void virgl_renderer_fill_caps(uint32_t set, uint32_t version,
                              union virgl_caps *caps)
{
   graw_renderer_fill_caps(set, version, caps);
}

void virgl_renderer_context_create(uint32_t handle, uint32_t nlen, const char *name)
{
   graw_renderer_context_create(handle, nlen, name);
}

void virgl_renderer_context_destroy(uint32_t handle)
{
   graw_renderer_context_destroy(handle);
}

void virgl_renderer_flush_buffer(uint32_t res_id, uint32_t ctx_id,
                                 struct virgl_box *box)
{
   graw_renderer_flush_buffer(res_id, ctx_id, (struct pipe_box *)box);
}

void virgl_renderer_set_scanout(uint32_t res_handle, uint32_t scanout_id, uint32_t ctx_id,
                                struct virgl_box *box)
{
   graw_renderer_set_scanout(res_handle, scanout_id, ctx_id, (struct pipe_box *)box);
}

void virgl_renderer_submit_cmd(struct virgl_iovec *iov,
                               int niovs,
                               int ctx_id, uint64_t offset,
                               int ndw)
{
   graw_decode_block_iov(iov, niovs, ctx_id, offset, ndw);
}

void virgl_renderer_transfer_write_iov(uint32_t handle, 
                                       uint32_t ctx_id,
                                       int level,
                                       uint32_t stride,
                                       uint32_t layer_stride,
                                       struct virgl_box *box,
                                       uint64_t offset,
                                       struct virgl_iovec *iovec,
                                       unsigned int iovec_cnt)
{
   graw_renderer_transfer_write_iov(handle, ctx_id, level,
                                    stride, layer_stride, (struct pipe_box *)box,
                                    offset, iovec, iovec_cnt);
}

void virgl_renderer_transfer_read_iov(uint32_t handle, uint32_t ctx_id,
                                     uint32_t level, uint32_t stride,
                                     uint32_t layer_stride,
                                     struct virgl_box *box,
                                     uint64_t offset, struct virgl_iovec *iov,
                                     int iovec_cnt)
{
   graw_renderer_transfer_send_iov(handle, ctx_id, level, stride,
                                   layer_stride, (struct pipe_box *)box,
                                   offset, iov, iovec_cnt);
}

void virgl_renderer_check_fences()
{
   graw_renderer_check_fences();
}

int virgl_renderer_resource_attach_iov(int res_handle, struct virgl_iovec *iov,
                                      int num_iovs)
{
   return graw_renderer_resource_attach_iov(res_handle, iov, num_iovs);
}

void virgl_renderer_resource_invalid_iov(int res_handle)
{
   return graw_renderer_resource_invalid_iov(res_handle);
}

int virgl_renderer_create_fence(int client_fence_id, uint32_t ctx_id)
{
   return graw_renderer_create_fence(client_fence_id, ctx_id);
}

void virgl_renderer_force_ctx_0(void)
{
   graw_renderer_force_ctx_0();
}

void virgl_renderer_ctx_attach_resource(int ctx_id, int res_handle)
{
   graw_renderer_attach_res_ctx(ctx_id, res_handle);
}

void virgl_renderer_ctx_detach_resource(int ctx_id, int res_handle)
{
   graw_renderer_detach_res_ctx(ctx_id, res_handle);
}
