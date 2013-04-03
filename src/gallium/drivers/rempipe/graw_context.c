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
#include "util/u_format.h"
#include "util/u_transfer.h"
#include "util/u_slab.h"
#include "tgsi/tgsi_text.h"

#include "state_tracker/graw.h"

#include "graw_protocol.h"

#include "graw_encode.h"

#include "graw_context.h"

#include "rempipe.h"
#include "state_tracker/sw_winsys.h"
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

   struct sw_displaytarget *dt; 
   uint32_t stride;

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

   struct graw_sampler_view fs_views[16];
   struct graw_sampler_view vs_views[16];

   struct pipe_framebuffer_state framebuffer;

   struct util_slab_mempool texture_transfer_pool;
};

struct graw_transfer {
   struct pipe_transfer base;
   void *localmem;
   uint32_t lmsize;
   uint32_t offset;
};

static struct pipe_screen encscreen;

static struct pipe_surface *graw_create_surface(struct pipe_context *ctx,
                                                struct pipe_resource *resource,
                                                const struct pipe_surface *templ)
{
   struct graw_context *grctx = (struct graw_context *)ctx;
   struct graw_surface *surf;
   struct graw_texture *tex;
   struct graw_resource *res = (struct graw_resource *)resource;
   uint32_t handle;

   surf = calloc(1, sizeof(struct graw_surface));
   if (surf == NULL)
      return NULL;

   handle = graw_object_assign_handle();
   pipe_reference_init(&surf->base.reference, 1);
   surf->base.format = templ->format;
   surf->base.u = templ->u;
   surf->base.texture = NULL;
   pipe_resource_reference(&surf->base.texture, resource);
   surf->base.context = ctx;
   
   graw_encoder_create_surface(grctx->eq, handle, res->res_handle, templ);
   surf->handle = handle;
   surf->base.width = surf->base.texture->width0;
   surf->base.height = surf->base.texture->height0;

   return &surf->base;
}

static void graw_surface_destroy(struct pipe_context *ctx,
                                 struct pipe_surface *surf)
{
   struct graw_context *grctx = (struct graw_context *)ctx;
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
   struct graw_context *grctx = (struct graw_context *)ctx;
   uint32_t handle; 
   handle = graw_object_assign_handle();

   graw_encode_dsa_state(grctx->eq, handle, blend_state);
   return (void *)(unsigned long)handle;
}

static void graw_bind_depth_stencil_alpha_state(struct pipe_context *ctx,
                                                void *blend_state)
{
   struct graw_context *grctx = (struct graw_context *)ctx;
   uint32_t handle = (unsigned long)blend_state;
   graw_encode_bind_object(grctx->eq, handle, GRAW_OBJECT_DSA);
}

static void graw_delete_depth_stencil_alpha_state(struct pipe_context *ctx,
                                                  void *dsa_state)
{
   struct graw_context *grctx = (struct graw_context *)ctx;
   uint32_t handle = (unsigned long)dsa_state;
   graw_encode_delete_object(grctx->eq, handle, GRAW_OBJECT_DSA);
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

static void graw_delete_rasterizer_state(struct pipe_context *ctx,
                                         void *rs_state)
{
   struct graw_context *grctx = (struct graw_context *)ctx;
   uint32_t handle = (unsigned long)rs_state;
   graw_encode_delete_object(grctx->eq, handle, GRAW_OBJECT_RASTERIZER);
}

static void graw_set_framebuffer_state(struct pipe_context *ctx,
                                                const struct pipe_framebuffer_state *state)
{
   struct graw_context *grctx = (struct graw_context *)ctx;

   grctx->framebuffer = *state;
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
                                    unsigned start_slot,
                                    unsigned num_buffers,
                                    const struct pipe_vertex_buffer *buffers)
{
   struct graw_context *grctx = (struct graw_context *)ctx;
   uint32_t res_handles[PIPE_MAX_ATTRIBS];
   struct graw_resource *res;
   int i;

   for (i = 0; i < num_buffers; i++) {
      res = (struct graw_resource *)buffers[i].buffer;
      if (res)
         res_handles[i] = res->res_handle;
   }
   graw_encoder_set_vertex_buffers(grctx->eq, num_buffers, buffers, res_handles);
}

static void graw_set_stencil_ref(struct pipe_context *ctx,
                                 const struct pipe_stencil_ref *ref)
{

}

static void graw_set_index_buffer(struct pipe_context *ctx,
                                  const struct pipe_index_buffer *buf)
{
   struct graw_context *grctx = (struct graw_context *)ctx;
   struct graw_resource *res;
   uint32_t handle = 0;
   if (buf) {
      res = (struct graw_resource *)buf->buffer;
      handle = res->res_handle;
   }
   graw_encoder_set_index_buffer(grctx->eq, buf, handle);
}

static void graw_set_constant_buffer(struct pipe_context *ctx,
                                     uint shader, uint index,
                                     struct pipe_constant_buffer *buf)
{
   struct graw_context *grctx = (struct graw_context *)ctx;

   if (buf) {
      if (!buf->user_buffer){
         fprintf(stderr,"CONST BUFFER FAIL\n");
         return;
      }
      
      graw_encoder_write_constant_buffer(grctx->eq, shader, index, buf->buffer_size / 4, buf->user_buffer);
   } else
      graw_encoder_write_constant_buffer(grctx->eq, shader, index, 0, NULL);
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

static void
graw_delete_fs_state(struct pipe_context *ctx,
                     void *fs)
{


}

static void
graw_delete_vs_state(struct pipe_context *ctx,
                     void *vs)
{

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
                       struct pipe_fence_handle **fence,
                       enum pipe_flush_flags flags)
{
   struct graw_context *grctx = (struct graw_context *)ctx;

   graw_flush_eq(grctx->eq, grctx);

   if (flags == PIPE_FLUSH_END_OF_FRAME) {
      graw_flush_frontbuffer(ctx->screen,
                             grctx->framebuffer.cbufs[0]->texture,
                             0, 0, NULL);
   }
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
   grview->base.reference.count = 1;

   grview->base.texture = NULL;
   grview->base.context = ctx;
   pipe_resource_reference(&grview->base.texture, texture);
//   grview->handle = handle;
   return &grview->base;
}

static void graw_set_vertex_sampler_views(struct pipe_context *ctx,	
					unsigned num_views,
					struct pipe_sampler_view **views)
{
   struct graw_context *grctx = (struct graw_context *)ctx;
   uint32_t handles[32] = {0};
   int i;

   for (i = 0; i < num_views; i++) {
      struct graw_sampler_view *grview = (struct graw_sampler_view *)views[i];

      if (views[i]) {
         struct graw_resource *grres = (struct graw_resource *)grview->base.texture;
         handles[i] = grres->res_handle;
      }
      pipe_sampler_view_reference((struct pipe_sampler_view **)&grctx->fs_views[i], views[i]);
   }
   graw_encode_set_vertex_sampler_views(grctx->eq, num_views, handles);
}

static void graw_set_fragment_sampler_views(struct pipe_context *ctx,	
					unsigned num_views,
					struct pipe_sampler_view **views)
{
   struct graw_context *grctx = (struct graw_context *)ctx;
   uint32_t handles[32] = {0};
   int i;

   for (i = 0; i < num_views; i++) {
      struct graw_sampler_view *grview = (struct graw_sampler_view *)views[i];

      if (views[i]) {
         struct graw_resource *grres = (struct graw_resource *)grview->base.texture;
         handles[i] = grres->res_handle;
      }
      pipe_sampler_view_reference((struct pipe_sampler_view **)&grctx->fs_views[i], views[i]);
   }
   graw_encode_set_fragment_sampler_views(grctx->eq, num_views, handles);
}

static void graw_destroy_sampler_view(struct pipe_context *ctx,
                                 struct pipe_sampler_view *view)
{
   pipe_resource_reference(&view->texture, NULL);
   FREE(view);
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

static void graw_bind_vertex_sampler_states(struct pipe_context *ctx,
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

static void graw_set_polygon_stipple(struct pipe_context *ctx,
                                     const struct pipe_poly_stipple *ps)
{

}

static void graw_set_scissor_state(struct pipe_context *ctx,
                                   const struct pipe_scissor_state *ss)
{

}

static void graw_set_sample_mask(struct pipe_context *ctx,
                                 unsigned sample_mask)
{

}

static void *graw_transfer_map(struct pipe_context *ctx,
                               struct pipe_resource *resource,
                               unsigned level,
                               unsigned usage,
                               const struct pipe_box *box,
                               struct pipe_transfer **transfer)
{
   struct graw_context *grctx = (struct graw_context *)ctx;
   enum pipe_format format = resource->format;
   struct graw_transfer *trans;

   trans = util_slab_alloc(&grctx->texture_transfer_pool);
   if (trans == NULL)
      return NULL;

   trans->lmsize = box->width * box->height * box->depth * util_format_get_blocksize(resource->format);
   trans->localmem = malloc(trans->lmsize);
   if (1) {//usage & PIPE_TRANSFER_READ) {
      struct graw_resource *grres = (struct graw_resource *)resource;
      fprintf(stderr, "TODO READ\n");
      graw_flush(ctx, NULL, 0);
      graw_transfer_get_block(grres->res_handle, box, trans->localmem, trans->lmsize / 4);
   }

   trans->base.resource = resource;
   trans->base.level = level;
   trans->base.usage = usage;
   trans->base.box = *box;
   trans->base.stride = box->width * util_format_get_blocksize(resource->format);
   trans->base.layer_stride = 0;

//   trans->offset = box->y / util_format_get_blockheight(format) * trans->base.stride +
//      box->x / util_format_get_blockwidth(format) * util_format_get_blocksize(format);

   *transfer = &trans->base;

   return trans->localmem;
}

static void graw_transfer_unmap(struct pipe_context *ctx,
                                 struct pipe_transfer *transfer)
{
   struct graw_context *grctx = (struct graw_context *)ctx;
   struct graw_transfer *trans = (struct graw_transfer *)transfer;
   struct graw_resource *grres = (struct graw_resource *)transfer->resource;
   if (trans->base.usage & PIPE_TRANSFER_WRITE) {
      graw_transfer_block(grres->res_handle, transfer->level, &transfer->box, &transfer->box, trans->localmem + trans->offset, trans->lmsize / 4);
      
   }

   FREE(trans->localmem);

   util_slab_free(&grctx->texture_transfer_pool, trans);
}

static void graw_transfer_flush_region(struct pipe_context *ctx,
                                       struct pipe_transfer *transfer,
                                       const struct pipe_box *box)
{
   struct graw_context *grctx = (struct graw_context *)ctx;
   struct graw_resource *grres = (struct graw_resource *)transfer->resource;
   struct graw_transfer *trans = (struct graw_transfer *)transfer;
   uint32_t offset;
   uint32_t size;

   graw_transfer_block(grres->res_handle, transfer->level, &transfer->box, box, trans->localmem + trans->offset, box->width);
   
}
                                       
static void
graw_context_destroy( struct pipe_context *ctx )
{
   struct graw_context *grctx = (struct graw_context *)ctx;

   FREE(grctx);
}

struct pipe_context *graw_context_create(struct pipe_screen *pscreen,
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

   gr_ctx->base.destroy = graw_context_destroy;
   gr_ctx->base.create_surface = graw_create_surface;
   gr_ctx->base.surface_destroy = graw_surface_destroy;
   gr_ctx->base.set_framebuffer_state = graw_set_framebuffer_state;
   gr_ctx->base.create_blend_state = graw_create_blend_state;
   gr_ctx->base.bind_blend_state = graw_bind_blend_state;
   gr_ctx->base.delete_blend_state = graw_delete_blend_state;
   gr_ctx->base.create_depth_stencil_alpha_state = graw_create_depth_stencil_alpha_state;
   gr_ctx->base.bind_depth_stencil_alpha_state = graw_bind_depth_stencil_alpha_state;
   gr_ctx->base.delete_depth_stencil_alpha_state = graw_delete_depth_stencil_alpha_state;
   gr_ctx->base.create_rasterizer_state = graw_create_rasterizer_state;
   gr_ctx->base.bind_rasterizer_state = graw_bind_rasterizer_state;
   gr_ctx->base.delete_rasterizer_state = graw_delete_rasterizer_state;

   gr_ctx->base.set_viewport_state = graw_set_viewport_state;
   gr_ctx->base.create_vertex_elements_state = graw_create_vertex_elements_state;
   gr_ctx->base.bind_vertex_elements_state = graw_bind_vertex_elements_state;
   gr_ctx->base.set_vertex_buffers = graw_set_vertex_buffers;
   gr_ctx->base.set_index_buffer = graw_set_index_buffer;
   gr_ctx->base.set_constant_buffer = graw_set_constant_buffer;
   gr_ctx->base.transfer_inline_write = graw_transfer_inline_write;
   gr_ctx->base.create_fs_state = graw_create_fs_state;
   gr_ctx->base.create_vs_state = graw_create_vs_state;
   gr_ctx->base.bind_vs_state = graw_bind_vs_state;
   gr_ctx->base.bind_fs_state = graw_bind_fs_state;
   gr_ctx->base.delete_vs_state = graw_delete_vs_state;
   gr_ctx->base.delete_fs_state = graw_delete_fs_state;
   
   gr_ctx->base.clear = graw_clear;
   gr_ctx->base.draw_vbo = graw_draw_vbo;
   gr_ctx->base.flush = graw_flush;
   gr_ctx->base.screen = pscreen;
   gr_ctx->base.create_sampler_view = graw_create_sampler_view;
   gr_ctx->base.sampler_view_destroy = graw_destroy_sampler_view;
   gr_ctx->base.set_fragment_sampler_views = graw_set_fragment_sampler_views;
   gr_ctx->base.set_vertex_sampler_views = graw_set_vertex_sampler_views;

   gr_ctx->base.create_sampler_state = graw_create_sampler_state;
   gr_ctx->base.bind_fragment_sampler_states = graw_bind_fragment_sampler_states;
   gr_ctx->base.bind_vertex_sampler_states = graw_bind_vertex_sampler_states;

   gr_ctx->base.set_polygon_stipple = graw_set_polygon_stipple;
   gr_ctx->base.set_scissor_state = graw_set_scissor_state;
   gr_ctx->base.set_sample_mask = graw_set_sample_mask;
   gr_ctx->base.set_stencil_ref = graw_set_stencil_ref;

   gr_ctx->base.transfer_map = graw_transfer_map;
   gr_ctx->base.transfer_unmap = graw_transfer_unmap;
   gr_ctx->base.transfer_flush_region = graw_transfer_flush_region;


   util_slab_create(&gr_ctx->texture_transfer_pool, sizeof(struct graw_transfer),
                    16, UTIL_SLAB_SINGLETHREADED);
   return &gr_ctx->base;
}

void graw_flush_frontbuffer(struct pipe_screen *screen,
                            struct pipe_resource *res,
                            unsigned level, unsigned layer,
                            void *winsys_drawable_handle)
{
   struct sw_winsys *winsys = rempipe_screen(screen)->winsys;
   struct graw_texture *gres = (struct graw_texture *)res;
   void *map;
   struct pipe_box box;
   int size = res->width0 * res->height0 * util_format_get_blocksize(res->format);
   void *alloced = malloc(size);
   int i;

   grend_flush_frontbuffer(gres->base.res_handle);
   if (!alloced) {
      fprintf(stderr,"Failed to malloc\n");

   }
   map = winsys->displaytarget_map(winsys, gres->dt, 0);

   box.x = box.y = box.z = 0;
   box.width = res->width0;
   box.height = res->height0;
   box.depth = 1;
   graw_transfer_get_block(gres->base.res_handle, &box, alloced, size / 4);

   for (i = 0; i < res->height0; i++) {
      int offsrc = res->width0 * util_format_get_blocksize(res->format) * (res->height0 - i - 1);
      int offdst = gres->stride * i;
      memcpy(map + offdst, alloced + offsrc, res->width0 * util_format_get_blocksize(res->format));
   }
   free(alloced);
   winsys->displaytarget_unmap(winsys, gres->dt);

//   winsys->displaytarget_display(winsys, gres->dt, winsys_drawable_handle);

}

struct pipe_resource *graw_resource_create(struct pipe_screen *pscreen,
                                           const struct pipe_resource *template)
{
   struct graw_buffer *buf;
   struct graw_texture *tex;
   uint32_t handle;

   if (template->target == PIPE_BUFFER) {
      buf = CALLOC_STRUCT(graw_buffer);
      buf->base.base = *template;
      buf->base.base.screen = pscreen;
      pipe_reference_init(&buf->base.base.reference, 1);
      handle = graw_renderer_resource_create(template->target, template->format, template->bind, template->width0, 1, 1);
      buf->base.res_handle = handle;
      return &buf->base.base;
   } else {
      tex = CALLOC_STRUCT(graw_texture);
      tex->base.base = *template;
      tex->base.base.screen = pscreen;
      pipe_reference_init(&tex->base.base.reference, 1);
      handle = graw_renderer_resource_create(template->target, template->format, template->bind, template->width0, template->height0, template->depth0);
      tex->base.res_handle = handle;

      if (template->bind & (PIPE_BIND_DISPLAY_TARGET |
                            PIPE_BIND_SCANOUT |
                            PIPE_BIND_SHARED))
      {
         struct sw_winsys *winsys = rempipe_screen(pscreen)->winsys;
         tex->dt = winsys->displaytarget_create(winsys,
                                                tex->base.base.bind,
                                                tex->base.base.format,
                                                tex->base.base.width0, 
                                                tex->base.base.height0,
                                                16,
                                                &tex->stride );
      }
      

      return &tex->base.base;
   }
}


struct pipe_resource *
rempipe_resource_from_handle(struct pipe_screen *screen,
                             const struct pipe_resource *template,
                             struct winsys_handle *whandle)
{
   struct sw_winsys *winsys = rempipe_screen(screen)->winsys;
   struct graw_texture *rpr = CALLOC_STRUCT(graw_texture);
   uint32_t handle;

   rpr->base.base = *template;
   pipe_reference_init(&rpr->base.base.reference, 1);
   rpr->base.base.screen = screen;      

   handle = graw_renderer_resource_create(template->target, template->format, template->bind, template->width0, template->height0, template->depth0);
   rpr->base.res_handle = handle;
   rpr->dt = winsys->displaytarget_from_handle(winsys,
                                               template,
                                               whandle,
                                               &rpr->stride);
   return &rpr->base.base;
}   

void
graw_resource_destroy(struct pipe_screen *pscreen,
                      struct pipe_resource *pt)
{


}
