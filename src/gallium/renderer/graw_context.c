#include <GL/glew.h>
#include <GL/gl.h>
#include <GL/glut.h>

#include <stdio.h>
#include "pipe/p_shader_tokens.h"

#include "pipe/p_context.h"
#include "pipe/p_defines.h"
#include "pipe/p_screen.h"
#include "pipe/p_state.h"
#include "util/u_inlines.h"
#include "util/u_memory.h"
#include "util/u_transfer.h"
#include "tgsi/tgsi_text.h"

#include "state_tracker/graw.h"

#include "graw_protocol.h"

#include "graw_encode.h"
#include "graw_object.h"

#include "graw_renderer.h"
#include "graw_renderer_glut.h"
#include "graw_decode.h"
struct graw_screen;

struct graw_resource {
   struct pipe_resource base;
   uint32_t res_handle;
};

struct graw_buffer {
   struct graw_resource base;
};

struct graw_texture {
   struct graw_resource base;
};


struct graw_vertex_element {
   unsigned count;
   struct pipe_vertex_element elements[PIPE_MAX_ATTRIBS];
   GLuint vboids[PIPE_MAX_ATTRIBS];
};

struct graw_shader_state {
   uint id;
   unsigned type;
   char *glsl_prog;
};

struct graw_sampler_view {
   struct pipe_sampler_view base;
   uint32_t handle;
};
   
struct graw_context {
   struct pipe_context base;
   GLuint vaoid;

   struct graw_vertex_element *ve;
   int num_vbos;
   struct pipe_vertex_buffer vbo[PIPE_MAX_ATTRIBS];

   struct graw_shader_state *vs;
   struct graw_shader_state *fs;

   struct graw_encoder_state *eq;
};

static struct pipe_screen encscreen;

static struct pipe_surface *graw_create_surface(struct pipe_context *ctx,
                                                         struct pipe_resource *resource,
                                                         const struct pipe_surface *templat)
{
   struct graw_context *grctx = (struct graw_context *)ctx;
   struct graw_surface *surf;
   struct graw_texture *tex;
   struct graw_resource *res = (struct graw_resource *)resource;
   uint32_t handle;
   handle = graw_object_assign_handle();

   surf = calloc(1, sizeof(struct graw_surface));

   surf->base = *templat;
   surf->base.texture = NULL;
   pipe_resource_reference(&surf->base.texture, resource);
   surf->base.context = ctx;
   
   graw_encoder_create_surface(grctx->eq, handle, res->res_handle, templat);
   surf->handle = handle;
   return &surf->base;
}

static void *graw_create_blend_state(struct pipe_context *ctx,
                                              const struct pipe_blend_state *blend_state)
{
   struct graw_context *grctx = (struct graw_context *)ctx;
   struct pipe_blend_state *state = CALLOC_STRUCT(pipe_blend_state);
   uint32_t handle; 
   handle = graw_object_assign_handle();

   graw_encode_blend_state(grctx->eq, handle, blend_state);
   return (void *)(unsigned long)handle;

}

static void graw_bind_blend_state(struct pipe_context *ctx,
                                           void *blend_state)
{
   struct graw_context *grctx = (struct graw_context *)ctx;
   uint32_t handle = (unsigned long)blend_state;
   graw_encode_bind_object(grctx->eq, handle, GRAW_OBJECT_BLEND);
}

static void graw_delete_blend_state(struct pipe_context *ctx,
                                     void *blend_state)
{
   struct graw_context *grctx = (struct graw_context *)ctx;
   uint32_t handle = (unsigned long)blend_state;
   graw_encode_delete_object(grctx->eq, handle, GRAW_OBJECT_BLEND);
}

static void *graw_create_depth_stencil_alpha_state(struct pipe_context *ctx,
                                                            const struct pipe_depth_stencil_alpha_state *blend_state)
{
   return NULL;

}

static void graw_bind_depth_stencil_alpha_state(struct pipe_context *ctx,
                                                         void *blend_state)
{

}

static void *graw_create_rasterizer_state(struct pipe_context *ctx,
                                                   const struct pipe_rasterizer_state *rs_state)
{
   struct graw_context *grctx = (struct graw_context *)ctx;
   uint32_t handle;
   handle = graw_object_assign_handle();

   graw_encode_rasterizer_state(grctx->eq, handle, rs_state);
   return (void *)(unsigned long)handle;
}

static void graw_bind_rasterizer_state(struct pipe_context *ctx,
                                                void *rs_state)
{
   struct graw_context *grctx = (struct graw_context *)ctx;
   uint32_t handle = (unsigned long)rs_state;

   graw_encode_bind_object(grctx->eq, handle, GRAW_OBJECT_RASTERIZER);
}

static void graw_set_framebuffer_state(struct pipe_context *ctx,
                                                const struct pipe_framebuffer_state *state)
{
   struct graw_context *grctx = (struct graw_context *)ctx;
   graw_encoder_set_framebuffer_state(grctx->eq, state);
}

static void graw_set_viewport_state(struct pipe_context *ctx,
                                     const struct pipe_viewport_state *state)
{
   struct graw_context *grctx = (struct graw_context *)ctx;
   graw_encoder_set_viewport_state(grctx->eq, state);
}

static void *graw_create_vertex_elements_state(struct pipe_context *ctx,
                                                        unsigned num_elements,
                                                        const struct pipe_vertex_element *elements)
{
   struct graw_context *grctx = (struct graw_context *)ctx;
   uint32_t handle = graw_object_assign_handle();
   graw_encoder_create_vertex_elements(grctx->eq, handle,
                                       num_elements, elements);
   return (void*)(unsigned long)handle;

}

static void graw_bind_vertex_elements_state(struct pipe_context *ctx,
                                                     void *ve)
{
   struct graw_context *grctx = (struct graw_context *)ctx;
   uint32_t handle = (unsigned long)ve;
   graw_encode_bind_object(grctx->eq, handle, GRAW_OBJECT_VERTEX_ELEMENTS);
}

static void graw_set_vertex_buffers(struct pipe_context *ctx,
                                             unsigned num_buffers,
                                             const struct pipe_vertex_buffer *buffers)
{
   struct graw_context *grctx = (struct graw_context *)ctx;
   uint32_t res_handles[PIPE_MAX_ATTRIBS];
   struct graw_resource *res;
   int i;

   for (i = 0; i < num_buffers; i++) {
      res = (struct graw_resource *)buffers[i].buffer;
      res_handles[i] = res->res_handle;
   }
   graw_encoder_set_vertex_buffers(grctx->eq, num_buffers, buffers, res_handles);
}

static void graw_set_index_buffer(struct pipe_context *ctx,
                                  const struct pipe_index_buffer *buf)
{
   struct graw_context *grctx = (struct graw_context *)ctx;
   struct graw_resource *res;

   res = (struct graw_resource *)buf->buffer;
   graw_encoder_set_index_buffer(grctx->eq, buf, res->res_handle);
}

static void graw_transfer_inline_write(struct pipe_context *ctx,
                                                struct pipe_resource *res,
                                                unsigned level,
                                                unsigned usage,
                                                const struct pipe_box *box,
                                                const void *data,
                                                unsigned stride,
                                                unsigned layer_stride)
{
   struct graw_context *grctx = (struct graw_context *)ctx;
   struct graw_resource *grres = (struct graw_resource *)res;
   void *ptr;

   graw_encoder_inline_write(grctx->eq, grres->res_handle, level, usage,
                             box, data, stride, layer_stride);
}

static void *graw_create_vs_state(struct pipe_context *ctx,
                                   const struct pipe_shader_state *shader)
{
   struct graw_context *grctx = (struct graw_context *)ctx;
   uint32_t handle;
   int ret;

   handle = graw_object_assign_handle();

   /* encode VS state */
   ret = graw_encode_shader_state(grctx->eq, handle,
                                  GRAW_OBJECT_VS, shader);
   if (ret)
      return NULL;

   return (void *)(unsigned long)handle;
}

static void *graw_create_fs_state(struct pipe_context *ctx,
                                   const struct pipe_shader_state *shader)
{
   struct graw_context *grctx = (struct graw_context *)ctx;
   uint32_t handle;
   int ret;
   handle = graw_object_assign_handle();

   /* encode VS state */
   ret = graw_encode_shader_state(grctx->eq, handle,
                                  GRAW_OBJECT_FS, shader);
   if (ret)
      return NULL;

   return (void *)(unsigned long)handle;
}

static void graw_bind_vs_state(struct pipe_context *ctx,
                                        void *vss)
{
   uint32_t handle = (unsigned long)vss;
   struct graw_context *grctx = (struct graw_context *)ctx;

   graw_encode_bind_object(grctx->eq, handle, GRAW_OBJECT_VS);
}


static void graw_bind_fs_state(struct pipe_context *ctx,
                                        void *vss)
{
   uint32_t handle = (unsigned long)vss;
   struct graw_context *grctx = (struct graw_context *)ctx;

   graw_encode_bind_object(grctx->eq, handle, GRAW_OBJECT_FS);
}

static void graw_clear(struct pipe_context *ctx,
                                unsigned buffers,
                                const union pipe_color_union *color,
                                double depth, unsigned stencil)
{
   struct graw_context *grctx = (struct graw_context *)ctx;

   graw_encode_clear(grctx->eq, buffers, color, depth, stencil);
}

static void graw_draw_vbo(struct pipe_context *ctx,
                                   const struct pipe_draw_info *info)
{
   struct graw_context *grctx = (struct graw_context *)ctx;

   graw_encoder_draw_vbo(grctx->eq, info);
}


static void graw_flush_eq(struct graw_encoder_state *eq, void *closure)
{
   struct graw_context *gr_ctx = closure;

   /* send the buffer to the remote side for decoding - for now jdi */
   graw_decode_block(eq->buf, eq->buf_offset);
   eq->buf_offset = 0;
}

static void graw_flush(struct pipe_context *ctx,
                                struct pipe_fence_handle **fence)
{
   struct graw_context *grctx = (struct graw_context *)ctx;

   graw_flush_eq(grctx->eq, grctx);
}

static struct pipe_sampler_view *graw_create_sampler_view(struct pipe_context *ctx,
                                      struct pipe_resource *texture,
                                      const struct pipe_sampler_view *state)
{
   struct graw_context *grctx = (struct graw_context *)ctx;
   struct graw_sampler_view *grview = CALLOC_STRUCT(graw_sampler_view);
   uint32_t handle;
   int ret;
   struct graw_resource *res;

   if (state == NULL)
      return NULL;

   res = (struct graw_resource *)texture;
//   handle = graw_object_assign_handle();
//  graw_encode_sampler_view(grctx->eq, handle, res->res_handle, state);

   grview->base = *state;
   grview->base.texture = NULL;
   pipe_reference(NULL, &texture->reference);
   grview->base.texture = texture;
//   grview->handle = handle;
   return &grview->base;
}

static void graw_set_fragment_sampler_views(struct pipe_context *ctx,	
					unsigned num_views,
					struct pipe_sampler_view **views)
{
   struct graw_context *grctx = (struct graw_context *)ctx;
   uint32_t handles[32];
   int i;
   for (i = 0; i < num_views; i++) {
      struct graw_sampler_view *grview = (struct graw_sampler_view *)views[i];
      struct graw_resource *grres = (struct graw_resource *)grview->base.texture;
      handles[i] = grres->res_handle;
   }
   graw_encode_set_fragment_sampler_views(grctx->eq, num_views, handles);
}

static void *graw_create_sampler_state(struct pipe_context *ctx,
					const struct pipe_sampler_state *state)
{
   struct graw_context *grctx = (struct graw_context *)ctx;
   uint32_t handle;
   int ret;
   handle = graw_object_assign_handle();

   graw_encode_sampler_state(grctx->eq, handle, state);
   return (void *)(unsigned long)handle;
}

static void graw_bind_fragment_sampler_states(struct pipe_context *ctx,
						unsigned num_samplers,
						void **samplers)
{
   struct graw_context *grctx = (struct graw_context *)ctx;
   uint32_t handles[32];
   int i;
   for (i = 0; i < num_samplers; i++) {
      handles[i] = (unsigned long)(samplers[i]);
   }
   graw_encode_bind_fragment_sampler_states(grctx->eq, num_samplers, handles);
}

static struct pipe_context *graw_context_create(struct pipe_screen *pscreen,
                                                         void *priv)
{
   struct graw_context *gr_ctx;

   gr_ctx = CALLOC_STRUCT(graw_context);

   gr_ctx->eq = graw_encoder_init_queue();
   if (!gr_ctx->eq) {
      free(gr_ctx);
      return NULL;
   }

   gr_ctx->eq->flush = graw_flush_eq;
   gr_ctx->eq->closure = gr_ctx;

   gr_ctx->base.create_surface = graw_create_surface;
   gr_ctx->base.set_framebuffer_state = graw_set_framebuffer_state;
   gr_ctx->base.create_blend_state = graw_create_blend_state;
   gr_ctx->base.bind_blend_state = graw_bind_blend_state;
   gr_ctx->base.delete_blend_state = graw_delete_blend_state;
   gr_ctx->base.create_depth_stencil_alpha_state = graw_create_depth_stencil_alpha_state;
   gr_ctx->base.bind_depth_stencil_alpha_state = graw_bind_depth_stencil_alpha_state;
   gr_ctx->base.create_rasterizer_state = graw_create_rasterizer_state;
   gr_ctx->base.bind_rasterizer_state = graw_bind_rasterizer_state;
   gr_ctx->base.set_viewport_state = graw_set_viewport_state;
   gr_ctx->base.create_vertex_elements_state = graw_create_vertex_elements_state;
   gr_ctx->base.bind_vertex_elements_state = graw_bind_vertex_elements_state;
   gr_ctx->base.set_vertex_buffers = graw_set_vertex_buffers;
   gr_ctx->base.set_index_buffer = graw_set_index_buffer;
   gr_ctx->base.transfer_inline_write = graw_transfer_inline_write;
   gr_ctx->base.create_fs_state = graw_create_fs_state;
   gr_ctx->base.create_vs_state = graw_create_vs_state;
   gr_ctx->base.bind_vs_state = graw_bind_vs_state;
   gr_ctx->base.bind_fs_state = graw_bind_fs_state;
   gr_ctx->base.clear = graw_clear;
   gr_ctx->base.draw_vbo = graw_draw_vbo;
   gr_ctx->base.flush = graw_flush;
   gr_ctx->base.screen = pscreen;
   gr_ctx->base.create_sampler_view = graw_create_sampler_view;
   gr_ctx->base.set_fragment_sampler_views = graw_set_fragment_sampler_views;
   gr_ctx->base.create_sampler_state = graw_create_sampler_state;
   gr_ctx->base.bind_fragment_sampler_states = graw_bind_fragment_sampler_states;
   return &gr_ctx->base;
}

static void graw_flush_frontbuffer(struct pipe_screen *screen,
                                       struct pipe_resource *res,
                                       unsigned level, unsigned layer,
                                       void *winsys_drawable_handle)
{
   struct graw_resource *gres = (struct graw_resource *)res;

   grend_flush_frontbuffer(gres->res_handle);
}

static struct pipe_resource *graw_resource_create(struct pipe_screen *pscreen,
                                                           const struct pipe_resource *template)
{
   struct graw_buffer *buf;
   struct graw_texture *tex;
   uint32_t handle;
   handle = graw_object_assign_handle();

   if (template->target == PIPE_BUFFER) {
      buf = CALLOC_STRUCT(graw_buffer);
      buf->base.base = *template;
      buf->base.base.screen = pscreen;
      pipe_reference_init(&buf->base.base.reference, 1);
      graw_renderer_resource_create(handle, template->target, template->format, template->bind, 0, 0);
      buf->base.res_handle = handle;
      return &buf->base.base;
   } else {
      tex = CALLOC_STRUCT(graw_texture);
      tex->base.base = *template;
      tex->base.base.screen = pscreen;
      pipe_reference_init(&tex->base.base.reference, 1);
      graw_renderer_resource_create(handle, template->target, template->format, template->bind, template->width0, template->height0);
      tex->base.res_handle = handle;
      return &tex->base.base;
   }
}

struct pipe_screen *
graw_create_window_and_screen( int x,
                               int y,
                               unsigned width,
                               unsigned height,
                               enum pipe_format format,
                               void **handle)
{

   *handle = 5;
   graw_renderer_glut_init(x, y, width, height);
   graw_renderer_init(x, y, width, height);

   encscreen.context_create = graw_context_create;
   encscreen.resource_create = graw_resource_create;
   encscreen.flush_frontbuffer = graw_flush_frontbuffer;
   return &encscreen;
}

void graw_transfer_write_return(void *data, uint32_t ndw)
{

}
