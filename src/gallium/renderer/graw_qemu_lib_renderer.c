#include <stdio.h>
#include <time.h>

#include <GL/glew.h>
#include <GL/gl.h>
#include <GL/glx.h>

#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/eventfd.h>
#include "send_scm.h"
#include "virgl_hw.h"
#include "pipe/p_state.h"
#include "util/u_format.h"
#include "util/u_math.h"
#include "graw_renderer.h"

#include "virglrenderer.h"

static Display *Dpy;
static GLXDrawable Draw;
static GLXContext ctx0;
static XVisualInfo *myvisual;
static void setup_glx_crap(void);

static struct graw_renderer_callbacks *rcbs;

static void *dev_cookie;
extern int localrender;

static struct grend_if_cbs graw_cbs;

static void graw_process_cmd(struct virgl_command *cmd, struct graw_iovec *iov,
                             unsigned int niovs);

void graw_lib_renderer_init(void *cookie, struct graw_renderer_callbacks *cbs)
{
   dev_cookie = cookie;
   rcbs = cbs;
   localrender = 1;
}

static void send_irq(uint32_t pd)
{
   rcbs->send_irq(dev_cookie, pd);
}

void graw_process_vcmd(void *cmd, struct graw_iovec *iov, unsigned int niovs)
{
   struct virgl_command *qcmd = cmd;

   graw_process_cmd(qcmd, iov, niovs);
   graw_renderer_check_fences();
}

static void virgl_cmd_create_resource(struct virgl_command *cmd, struct graw_iovec *iov, unsigned int niovs)
{
   struct graw_renderer_resource_create_args args;
   struct graw_iovec *res_iovs;
   uint32_t gsize = graw_iov_size(iov, niovs);
   void *data;
   int i;

   res_iovs = malloc(cmd->u.res_create.nr_sg_entries * sizeof(struct graw_iovec));

   if (niovs > 1) {
      data = malloc(gsize);
      graw_iov_to_buf(iov, niovs, 0, data, gsize);
   } else
      data = (iov[0].iov_base);
   
   for (i = 0; i < cmd->u.res_create.nr_sg_entries; i++) {
      struct virgl_iov_entry *ent = ((struct virgl_iov_entry *)data) + i;

      res_iovs[i].iov_len = ent->length;
      rcbs->map_iov(&res_iovs[i], ent->addr);
   }

   args.handle = cmd->u.res_create.handle;
   args.target = cmd->u.res_create.target;
   args.format = cmd->u.res_create.format;
   args.bind = cmd->u.res_create.bind;
   args.width = cmd->u.res_create.width;
   args.height = cmd->u.res_create.height;
   args.depth = cmd->u.res_create.depth;
   args.array_size = cmd->u.res_create.array_size;
   args.last_level = cmd->u.res_create.last_level;
   args.nr_samples = cmd->u.res_create.nr_samples;
   args.flags = cmd->u.res_create.flags;
   graw_renderer_resource_create(&args, res_iovs, cmd->u.res_create.nr_sg_entries);

   if (niovs > 1)
      free(data);
}

static void graw_process_cmd(struct virgl_command *cmd, struct graw_iovec *iov,
                             unsigned int niovs)
{
   static int inited;
   int fence_ctx_id = 0;
   switch (cmd->type) {
   case VIRGL_CMD_CREATE_CONTEXT:
      graw_renderer_context_create(cmd->u.ctx.handle, cmd->u.ctx_create.nlen,
         cmd->u.ctx_create.debug_name);
      break;
   case VIRGL_CMD_DESTROY_CONTEXT:
      graw_renderer_context_destroy(cmd->u.ctx.handle);
      break;
   case VIRGL_CMD_CREATE_RESOURCE:
      virgl_cmd_create_resource(cmd, iov, niovs);
      break;
   case VIRGL_CMD_SUBMIT:
//         fprintf(stderr,"cmd submit %lx %d\n", cmd->u.cmd_submit.data, cmd->u.cmd_submit.size);
      
   {
      graw_decode_block_iov(iov, niovs, cmd->u.cmd_submit.ctx_id, cmd->u.cmd_submit.phy_addr, cmd->u.cmd_submit.size / 4);
      fence_ctx_id = cmd->u.cmd_submit.ctx_id;
   }
   
   break;
   case VIRGL_CMD_TRANSFER_GET:
//         fprintf(stderr,"got transfer get %d\n", cmd->u.transfer_get.res_handle);
      graw_renderer_transfer_send_iov(cmd->u.transfer_get.res_handle,
                                      cmd->u.transfer_get.ctx_id,
                                      cmd->u.transfer_get.level,
                                      cmd->u.transfer_get.stride,
                                      cmd->u.transfer_get.layer_stride,
                                      (struct pipe_box *)&cmd->u.transfer_get.box,
                                      cmd->u.transfer_get.data, iov,
                                      niovs);
      fence_ctx_id = cmd->u.transfer_get.ctx_id;
      break;
   case VIRGL_CMD_TRANSFER_PUT:
      graw_renderer_transfer_write_iov(cmd->u.transfer_put.res_handle,
                                       cmd->u.transfer_put.ctx_id,
                                       cmd->u.transfer_put.level,
                                       cmd->u.transfer_put.stride,
                                       cmd->u.transfer_put.layer_stride,
                                   (struct pipe_box *)&cmd->u.transfer_put.box,
                                   cmd->u.transfer_put.data, iov,
                                   niovs);
      fence_ctx_id = cmd->u.transfer_put.ctx_id;
      break;
      
   case VIRGL_CMD_SET_SCANOUT:
      graw_renderer_set_scanout(cmd->u.set_scanout.res_handle,
                                cmd->u.set_scanout.ctx_id,
                                (struct pipe_box *)&cmd->u.set_scanout.box);
      (*rcbs->resize_front)(dev_cookie, cmd->u.set_scanout.box.w,
                            cmd->u.set_scanout.box.h);
      break;
   case VIRGL_CMD_FLUSH_BUFFER:
      graw_renderer_flush_buffer(cmd->u.flush_buffer.res_handle,
                                 cmd->u.flush_buffer.ctx_id,
                                 (struct pipe_box *)&cmd->u.flush_buffer.box);
      break;
   case VIRGL_CMD_RESOURCE_UNREF:
      graw_renderer_resource_unref(cmd->u.res_unref.res_handle);
      break;
   case VIRGL_CMD_ATTACH_RES_CTX:
      /* TODO add security */
      break;
   case VIRGL_CMD_DETACH_RES_CTX:
      /* TODO add security */
      break;
   case VIRGL_CMD_GET_3D_CAPABILITIES:
      if (!niovs)
         return;
      graw_renderer_fill_caps(cmd->u.get_cap.cap_set,
                              cmd->u.get_cap.cap_set_version,
                              cmd->u.get_cap.offset, iov, niovs);
      break;
   case VIRGL_CMD_TIMESTAMP_GET: {
      GLint64 ts;
      ts = graw_renderer_get_timestamp();
      break;
   }
   case 0xdeadbeef:
      if (inited) {
         graw_renderer_fini();
         
      }
      setup_glx_crap();
      graw_renderer_init(&graw_cbs);
      inited = 1;
      break;
   }
   
   if (cmd->flags & VIRGL_COMMAND_EMIT_FENCE)
      graw_renderer_create_fence(cmd->fence_id, fence_ctx_id);

}

void graw_transfer_write_return(void *data, uint32_t bytes, uint64_t offset,
                                struct graw_iovec *iov, int num_iovs)
{
   graw_iov_from_buf(iov, num_iovs, offset, data, bytes);
}

void graw_transfer_write_tex_return(struct pipe_resource *res,
				    struct pipe_box *box,
                                    uint32_t level,
                                    uint32_t dst_stride,
                                    uint64_t offset,
                                    struct graw_iovec *iov,
                                    int num_iovs,
				    void *myptr, int size, int invert)
{
   int elsize = util_format_get_blocksize(res->format);
   int h;
   uint32_t myoffset = offset;
   uint32_t stride = dst_stride ? dst_stride : util_format_get_nblocksx(res->format, box->width) * elsize;

   if (!invert && (!dst_stride || dst_stride == util_format_get_nblocksx(res->format, box->width) * elsize))
      graw_iov_from_buf(iov, num_iovs, offset, myptr, size);
   else if (invert) {
      for (h = box->height - 1; h >= 0; h--) {
         void *sptr = myptr + (h * elsize * box->width);
         graw_iov_from_buf(iov, num_iovs, myoffset, sptr, box->width * elsize);
         myoffset += stride;
      }
   } else {
      for (h = 0; h < box->height; h++) {
         void *sptr = myptr + (h * elsize * box->width);
         graw_iov_from_buf(iov, num_iovs, myoffset, sptr, box->width * elsize);
         myoffset += stride;
      }
   }
}

static void graw_write_fence(unsigned fence_id)
{
   rcbs->write_fence(dev_cookie, fence_id);   
   send_irq(0x20);
}

static int swap_buffers(void)
{
    rcbs->swap_buffers(dev_cookie);  
    return 0;
}

static virgl_gl_context create_gl_context(void)
{
   GLXContext *glxctx = glXCreateContext(Dpy, myvisual, ctx0, TRUE);
   return (virgl_gl_context)glxctx;
}

static void destroy_gl_context(virgl_gl_context ctx)
{
   glXDestroyContext(Dpy, (GLXContext)ctx);
}

static int make_current(virgl_gl_context ctx)
{
   glXMakeCurrent(Dpy, Draw, (GLXContext)ctx);
}

static struct grend_if_cbs graw_cbs = {
   graw_write_fence,
   swap_buffers,
};

void graw_renderer_set_cursor_info(uint32_t cursor_handle, int x, int y)
{
   grend_set_cursor_info(cursor_handle, x, y);
}

void graw_renderer_poll(void)
{
   graw_renderer_check_queries();
   graw_renderer_check_fences();
}

static void setup_glx_crap(void)
{
   int attrib[] = { GLX_RGBA,
		    GLX_RED_SIZE, 1,
		    GLX_GREEN_SIZE, 1,
		    GLX_BLUE_SIZE, 1,
		    GLX_DOUBLEBUFFER,
		    None };
   int scrnum;

   XVisualInfo *VisInfo;
   Dpy = glXGetCurrentDisplay();
   Draw = glXGetCurrentDrawable();

   VisInfo = glXChooseVisual(Dpy, DefaultScreen(Dpy), attrib);

   myvisual = VisInfo;
   ctx0 = glXGetCurrentContext();

   {
      const GLubyte *name = glGetString(GL_VERSION);
      fprintf(stderr,"name is %s\n", name);
   }
}
