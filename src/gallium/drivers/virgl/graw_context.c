
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
#include "util/u_helpers.h"
#include "util/u_slab.h"
#include "util/u_upload_mgr.h"
#include "util/u_blitter.h"
#include "tgsi/tgsi_text.h"

#include "pipebuffer/pb_buffer.h"
#include "state_tracker/graw.h"
#include "state_tracker/drm_driver.h"

#include "graw_encode.h"

#include "graw_context.h"

#include "virgl_resource.h"
#include "virgl.h"
#include "state_tracker/sw_winsys.h"
 struct pipe_screen encscreen;

static uint32_t next_handle;
uint32_t graw_object_assign_handle(void)
{
   return ++next_handle;
}

static void graw_buffer_flush(struct graw_context *grctx,
                              struct virgl_buffer *vbuf)
{
   struct virgl_screen *rs = virgl_screen(grctx->base.screen);

   assert(vbuf->on_list);

   vbuf->dirt_box.height = 1;
   vbuf->dirt_box.depth = 1;
   vbuf->dirt_box.y = 0;
   vbuf->dirt_box.z = 0;

   grctx->num_transfers++;
   rs->vws->transfer_put(rs->vws, vbuf->base.hw_res,
                         &vbuf->dirt_box, 0, 0, vbuf->dirt_box.x, 0);

   vbuf->dirt_box.x = vbuf->base.u.b.width0+1;
   vbuf->dirt_box.width = 0;
}

static struct pipe_surface *graw_create_surface(struct pipe_context *ctx,
                                                struct pipe_resource *resource,
                                                const struct pipe_surface *templ)
{
   struct graw_context *grctx = (struct graw_context *)ctx;
   struct graw_surface *surf;
   struct virgl_resource *res = virgl_resource(resource);
   uint32_t handle;

   surf = CALLOC_STRUCT(graw_surface);
   if (surf == NULL)
      return NULL;

   res->clean = FALSE;
   handle = graw_object_assign_handle();
   pipe_reference_init(&surf->base.reference, 1);
   pipe_resource_reference(&surf->base.texture, resource);
   surf->base.context = ctx;
   surf->base.format = templ->format;
   if (resource->target != PIPE_BUFFER) {
      surf->base.width = u_minify(resource->width0, templ->u.tex.level);
      surf->base.height = u_minify(resource->height0, templ->u.tex.level);
      surf->base.u.tex.level = templ->u.tex.level;
      surf->base.u.tex.first_layer = templ->u.tex.first_layer;
      surf->base.u.tex.last_layer = templ->u.tex.last_layer;
   } else {
      surf->base.width = templ->u.buf.last_element - templ->u.buf.first_element + 1;
      surf->base.height = resource->height0;
      surf->base.u.buf.first_element = templ->u.buf.first_element;
      surf->base.u.buf.last_element = templ->u.buf.last_element;
   }
   graw_encoder_create_surface(grctx, handle, res, &surf->base);
   surf->handle = handle;
   return &surf->base;
}

static void graw_surface_destroy(struct pipe_context *ctx,
                                 struct pipe_surface *psurf)
{
   struct graw_context *grctx = (struct graw_context *)ctx;
   struct graw_surface *surf = (struct graw_surface *)psurf;

   pipe_resource_reference(&surf->base.texture, NULL);
   graw_encode_delete_object(grctx, surf->handle, VIRGL_OBJECT_SURFACE);
   FREE(surf);
}

static void *graw_create_blend_state(struct pipe_context *ctx,
                                              const struct pipe_blend_state *blend_state)
{
   struct graw_context *grctx = (struct graw_context *)ctx;
   uint32_t handle; 
   handle = graw_object_assign_handle();

   graw_encode_blend_state(grctx, handle, blend_state);
   return (void *)(unsigned long)handle;

}

static void graw_bind_blend_state(struct pipe_context *ctx,
                                           void *blend_state)
{
   struct graw_context *grctx = (struct graw_context *)ctx;
   uint32_t handle = (unsigned long)blend_state;
   graw_encode_bind_object(grctx, handle, VIRGL_OBJECT_BLEND);
}

static void graw_delete_blend_state(struct pipe_context *ctx,
                                     void *blend_state)
{
   struct graw_context *grctx = (struct graw_context *)ctx;
   uint32_t handle = (unsigned long)blend_state;
   graw_encode_delete_object(grctx, handle, VIRGL_OBJECT_BLEND);
}

static void *graw_create_depth_stencil_alpha_state(struct pipe_context *ctx,
                                                   const struct pipe_depth_stencil_alpha_state *blend_state)
{
   struct graw_context *grctx = (struct graw_context *)ctx;
   uint32_t handle; 
   handle = graw_object_assign_handle();

   graw_encode_dsa_state(grctx, handle, blend_state);
   return (void *)(unsigned long)handle;
}

static void graw_bind_depth_stencil_alpha_state(struct pipe_context *ctx,
                                                void *blend_state)
{
   struct graw_context *grctx = (struct graw_context *)ctx;
   uint32_t handle = (unsigned long)blend_state;
   graw_encode_bind_object(grctx, handle, VIRGL_OBJECT_DSA);
}

static void graw_delete_depth_stencil_alpha_state(struct pipe_context *ctx,
                                                  void *dsa_state)
{
   struct graw_context *grctx = (struct graw_context *)ctx;
   uint32_t handle = (unsigned long)dsa_state;
   graw_encode_delete_object(grctx, handle, VIRGL_OBJECT_DSA);
}

static void *graw_create_rasterizer_state(struct pipe_context *ctx,
                                                   const struct pipe_rasterizer_state *rs_state)
{
   struct graw_context *grctx = (struct graw_context *)ctx;
   uint32_t handle;
   handle = graw_object_assign_handle();

   graw_encode_rasterizer_state(grctx, handle, rs_state);
   return (void *)(unsigned long)handle;
}

static void graw_bind_rasterizer_state(struct pipe_context *ctx,
                                                void *rs_state)
{
   struct graw_context *grctx = (struct graw_context *)ctx;
   uint32_t handle = (unsigned long)rs_state;

   graw_encode_bind_object(grctx, handle, VIRGL_OBJECT_RASTERIZER);
}

static void graw_delete_rasterizer_state(struct pipe_context *ctx,
                                         void *rs_state)
{
   struct graw_context *grctx = (struct graw_context *)ctx;
   uint32_t handle = (unsigned long)rs_state;
   graw_encode_delete_object(grctx, handle, VIRGL_OBJECT_RASTERIZER);
}

static void graw_set_framebuffer_state(struct pipe_context *ctx,
                                                const struct pipe_framebuffer_state *state)
{
   struct graw_context *grctx = (struct graw_context *)ctx;

   grctx->framebuffer = *state;
   graw_encoder_set_framebuffer_state(grctx, state);
}

static void graw_set_viewport_states(struct pipe_context *ctx,
                                     unsigned start_slot,
                                     unsigned num_viewports,
                                     const struct pipe_viewport_state *state)
{
   struct graw_context *grctx = (struct graw_context *)ctx;
   graw_encoder_set_viewport_state(grctx, state);
}

static void *graw_create_vertex_elements_state(struct pipe_context *ctx,
                                                        unsigned num_elements,
                                                        const struct pipe_vertex_element *elements)
{
   struct graw_context *grctx = (struct graw_context *)ctx;
   uint32_t handle = graw_object_assign_handle();
   graw_encoder_create_vertex_elements(grctx, handle,
                                       num_elements, elements);
   return (void*)(unsigned long)handle;

}

static void graw_delete_vertex_elements_state(struct pipe_context *ctx,
                                              void *ve)
{
   struct graw_context *grctx = (struct graw_context *)ctx;
   uint32_t handle = (unsigned long)ve;

   graw_encode_delete_object(grctx, handle, VIRGL_OBJECT_VERTEX_ELEMENTS);
}

static void graw_bind_vertex_elements_state(struct pipe_context *ctx,
                                                     void *ve)
{
   struct graw_context *grctx = (struct graw_context *)ctx;
   uint32_t handle = (unsigned long)ve;
   graw_encode_bind_object(grctx, handle, VIRGL_OBJECT_VERTEX_ELEMENTS);
}

static void graw_set_vertex_buffers(struct pipe_context *ctx,
                                    unsigned start_slot,
                                    unsigned num_buffers,
                                    const struct pipe_vertex_buffer *buffers)
{
   struct graw_context *grctx = (struct graw_context *)ctx;

   util_set_vertex_buffers_count(grctx->vertex_buffer,
                                 &grctx->num_vertex_buffers,
                                 buffers, start_slot, num_buffers);

   grctx->vertex_array_dirty = TRUE;
}

static void graw_hw_set_vertex_buffers(struct pipe_context *ctx)
{
   struct graw_context *grctx = (struct graw_context *)ctx;
 
   if (grctx->vertex_array_dirty) {
      graw_encoder_set_vertex_buffers(grctx, grctx->num_vertex_buffers, grctx->vertex_buffer);
   }
}

static void graw_set_stencil_ref(struct pipe_context *ctx,
                                 const struct pipe_stencil_ref *ref)
{
   struct graw_context *grctx = (struct graw_context *)ctx;
   graw_encoder_set_stencil_ref(grctx, ref);
}

static void graw_set_blend_color(struct pipe_context *ctx,
                                 const struct pipe_blend_color *color)
{
   struct graw_context *grctx = (struct graw_context *)ctx;
   graw_encoder_set_blend_color(grctx, color);
}

static void graw_set_index_buffer(struct pipe_context *ctx,
                                  const struct pipe_index_buffer *ib)
{
        struct graw_context *grctx = (struct graw_context *)ctx;

        if (ib) {
                pipe_resource_reference(&grctx->index_buffer.buffer, ib->buffer);
                memcpy(&grctx->index_buffer, ib, sizeof(*ib));
        } else {
                pipe_resource_reference(&grctx->index_buffer.buffer, NULL);
        }
        
}

static void graw_hw_set_index_buffer(struct pipe_context *ctx,
                                     struct pipe_index_buffer *ib)
{
        struct graw_context *grctx = (struct graw_context *)ctx;
        graw_encoder_set_index_buffer(grctx, ib);

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
      
      graw_encoder_write_constant_buffer(grctx, shader, index, buf->buffer_size / 4, buf->user_buffer);
   } else
      graw_encoder_write_constant_buffer(grctx, shader, index, 0, NULL);
}

void graw_transfer_inline_write(struct pipe_context *ctx,
                                struct pipe_resource *res,
                                unsigned level,
                                unsigned usage,
                                const struct pipe_box *box,
                                const void *data,
                                unsigned stride,
                                unsigned layer_stride)
{
   struct graw_context *grctx = (struct graw_context *)ctx;
   struct virgl_resource *grres = (struct virgl_resource *)res;

   grres->clean = FALSE;
   graw_encoder_inline_write(grctx, grres, level, usage,
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
   ret = graw_encode_shader_state(grctx, handle,
                                  VIRGL_OBJECT_VS, shader);
   if (ret) {
      return NULL;
   }

   return (void *)(unsigned long)handle;
}


static void *graw_create_gs_state(struct pipe_context *ctx,
                                   const struct pipe_shader_state *shader)
{
   struct graw_context *grctx = (struct graw_context *)ctx;
   uint32_t handle;
   int ret;

   handle = graw_object_assign_handle();

   /* encode GS state */
   ret = graw_encode_shader_state(grctx, handle,
                                  VIRGL_OBJECT_GS, shader);
   if (ret) {
      return NULL;
   }

   return (void *)(unsigned long)handle;
}

static void *graw_create_fs_state(struct pipe_context *ctx,
                                   const struct pipe_shader_state *shader)
{
   struct graw_context *grctx = (struct graw_context *)ctx;
   uint32_t handle;
   int ret;
   handle = graw_object_assign_handle();

   /* encode FS state */
   ret = graw_encode_shader_state(grctx, handle,
                                  VIRGL_OBJECT_FS, shader);
   if (ret)
      return NULL;

   return (void *)(unsigned long)handle;
}

static void
graw_delete_fs_state(struct pipe_context *ctx,
                     void *fs)
{
   uint32_t handle = (unsigned long)fs;
   struct graw_context *grctx = (struct graw_context *)ctx;

   graw_encode_delete_object(grctx, handle, VIRGL_OBJECT_FS);
}

static void
graw_delete_gs_state(struct pipe_context *ctx,
                     void *gs)
{
   uint32_t handle = (unsigned long)gs;
   struct graw_context *grctx = (struct graw_context *)ctx;

   graw_encode_delete_object(grctx, handle, VIRGL_OBJECT_GS);
}

static void
graw_delete_vs_state(struct pipe_context *ctx,
                     void *vs)
{
   uint32_t handle = (unsigned long)vs;
   struct graw_context *grctx = (struct graw_context *)ctx;

   graw_encode_delete_object(grctx, handle, VIRGL_OBJECT_VS);
}

static void graw_bind_vs_state(struct pipe_context *ctx,
                                        void *vss)
{
   uint32_t handle = (unsigned long)vss;
   struct graw_context *grctx = (struct graw_context *)ctx;

   graw_encode_bind_object(grctx, handle, VIRGL_OBJECT_VS);
}

static void graw_bind_gs_state(struct pipe_context *ctx,
                               void *vss)
{
   uint32_t handle = (unsigned long)vss;
   struct graw_context *grctx = (struct graw_context *)ctx;

   graw_encode_bind_object(grctx, handle, VIRGL_OBJECT_GS);
}


static void graw_bind_fs_state(struct pipe_context *ctx,
                                        void *vss)
{
   uint32_t handle = (unsigned long)vss;
   struct graw_context *grctx = (struct graw_context *)ctx;

   graw_encode_bind_object(grctx, handle, VIRGL_OBJECT_FS);
}

static void graw_clear(struct pipe_context *ctx,
                                unsigned buffers,
                                const union pipe_color_union *color,
                                double depth, unsigned stencil)
{
   struct graw_context *grctx = (struct graw_context *)ctx;

   graw_encode_clear(grctx, buffers, color, depth, stencil);
}

static void graw_draw_vbo(struct pipe_context *ctx,
                                   const struct pipe_draw_info *dinfo)
{
   struct graw_context *grctx = (struct graw_context *)ctx;
   struct virgl_screen *rs = virgl_screen(ctx->screen);
   struct pipe_index_buffer ib = {};
   struct pipe_draw_info info = *dinfo;

   if (!(rs->caps.caps.v1.prim_mask & (1 << dinfo->mode))) {
      util_primconvert_save_index_buffer(grctx->primconvert, &grctx->index_buffer);
//      util_primconvert_save_rasterizer_state(grctx->primconvert, grctx->rasterizer);
      util_primconvert_draw_vbo(grctx->primconvert, dinfo);
      return;
   }
   if (info.indexed) {
           pipe_resource_reference(&ib.buffer, grctx->index_buffer.buffer);
           ib.user_buffer = grctx->index_buffer.user_buffer;
           ib.index_size = grctx->index_buffer.index_size;
           ib.offset = grctx->index_buffer.offset + info.start * ib.index_size;

           if (ib.user_buffer) {
                   u_upload_data(grctx->uploader, 0, info.count * ib.index_size,
                                 ib.user_buffer, &ib.offset, &ib.buffer);
                   ib.user_buffer = NULL;
           }
   } //else
//           graw_hw_set_index_buffer(ctx, NULL);
   u_upload_unmap(grctx->uploader);

   grctx->num_draws++;
   graw_hw_set_vertex_buffers(ctx);
   if (info.indexed)
           graw_hw_set_index_buffer(ctx, &ib);

   graw_encoder_draw_vbo(grctx, &info);
}


static void graw_flush_eq(struct graw_context *ctx, void *closure)
{
   struct virgl_screen *rs = virgl_screen(ctx->base.screen);
   int i;
   /* send the buffer to the remote side for decoding - for now jdi */
   
//   fprintf(stderr,"flush transfers: %d draws %d\n", ctx->num_transfers, ctx->num_draws);
   ctx->num_transfers = ctx->num_draws = 0;
   rs->vws->submit_cmd(rs->vws, ctx->cbuf);

   /* add back current framebuffer resources to reference list? */
   {
      struct virgl_winsys *vws = virgl_screen(ctx->base.screen)->vws;
      struct pipe_surface *surf;
      struct virgl_resource *res;
      surf = ctx->framebuffer.zsbuf;
      if (surf) {
         res = (struct virgl_resource *)surf->texture;
         vws->emit_res(vws, ctx->cbuf, res->hw_res, FALSE);
      }
      for (i = 0; i < ctx->framebuffer.nr_cbufs; i++) {
         surf = ctx->framebuffer.cbufs[i];
         if (surf) {
            res = (struct virgl_resource *)surf->texture;
            vws->emit_res(vws, ctx->cbuf, res->hw_res, FALSE);
         }
      }
   }
}

static void graw_flush_from_st(struct pipe_context *ctx,
                               struct pipe_fence_handle **fence,
                               enum pipe_flush_flags flags)
{
   struct graw_context *grctx = (struct graw_context *)ctx;
   struct virgl_screen *rs = virgl_screen(ctx->screen);
   struct virgl_buffer *buf, *tmp;

   if (fence)
      *fence = rs->vws->cs_create_fence(rs->vws);

   LIST_FOR_EACH_ENTRY_SAFE(buf, tmp, &grctx->to_flush_bufs, flush_list) {
      struct pipe_resource *res = &buf->base.u.b;
      graw_buffer_flush(grctx, buf);
      list_del(&buf->flush_list);
      buf->on_list = FALSE;
      pipe_resource_reference(&res, NULL);   

   }
   graw_flush_eq(grctx, grctx);
}

static struct pipe_sampler_view *graw_create_sampler_view(struct pipe_context *ctx,
                                      struct pipe_resource *texture,
                                      const struct pipe_sampler_view *state)
{
   struct graw_context *grctx = (struct graw_context *)ctx;
   struct graw_sampler_view *grview = CALLOC_STRUCT(graw_sampler_view);
   uint32_t handle;
   struct virgl_resource *res;

   if (state == NULL)
      return NULL;

   res = (struct virgl_resource *)texture;
   handle = graw_object_assign_handle();
   graw_encode_sampler_view(grctx, handle, res, state);

   grview->base = *state;
   grview->base.reference.count = 1;

   grview->base.texture = NULL;
   grview->base.context = ctx;
   pipe_resource_reference(&grview->base.texture, texture);
   grview->handle = handle;
   return &grview->base;
}

static void graw_set_sampler_views(struct pipe_context *ctx,
                                   unsigned shader_type,
                                   unsigned start_slot,
                                   unsigned num_views,
                                   struct pipe_sampler_view **views)
{
   struct graw_context *grctx = (struct graw_context *)ctx;
   int i;
   uint32_t disable_mask = ~((1ull << num_views) - 1);
   struct graw_textures_info *tinfo = &grctx->samplers[shader_type];
   uint32_t new_mask = 0;
   uint32_t remaining_mask;

   remaining_mask = tinfo->enabled_mask & disable_mask;

   while (remaining_mask) {
      i = u_bit_scan(&remaining_mask);
      assert(tinfo->views[i]);
      
      pipe_sampler_view_reference((struct pipe_sampler_view **)&tinfo->views[i], NULL);
   }

   for (i = 0; i < num_views; i++) {
      struct graw_sampler_view *grview = (struct graw_sampler_view *)views[i];

      if (views[i] == (struct pipe_sampler_view *)tinfo->views[i])
         continue;

      if (grview) {
         new_mask |= 1 << i;
         pipe_sampler_view_reference((struct pipe_sampler_view **)&tinfo->views[i], views[i]);
      } else {
         pipe_sampler_view_reference((struct pipe_sampler_view **)&tinfo->views[i], NULL);
         disable_mask |= 1 << i;
      }
   }

   tinfo->enabled_mask &= ~disable_mask;
   tinfo->enabled_mask |= new_mask;
   graw_encode_set_sampler_views(grctx, shader_type, start_slot, num_views, tinfo->views);
}

static void graw_destroy_sampler_view(struct pipe_context *ctx,
                                 struct pipe_sampler_view *view)
{
   struct graw_context *grctx = (struct graw_context *)ctx;
   struct graw_sampler_view *grview = (struct graw_sampler_view *)view;

   graw_encode_delete_object(grctx, grview->handle, VIRGL_OBJECT_SAMPLER_VIEW);
   pipe_resource_reference(&view->texture, NULL);
   FREE(view);
}

static void *graw_create_sampler_state(struct pipe_context *ctx,
					const struct pipe_sampler_state *state)
{
   struct graw_context *grctx = (struct graw_context *)ctx;
   uint32_t handle;

   handle = graw_object_assign_handle();

   graw_encode_sampler_state(grctx, handle, state);
   return (void *)(unsigned long)handle;
}

static void graw_delete_sampler_state(struct pipe_context *ctx,
                                      void *ss)
{
   struct graw_context *grctx = (struct graw_context *)ctx;
   uint32_t handle = (unsigned long)ss;

   graw_encode_delete_object(grctx, handle, VIRGL_OBJECT_SAMPLER_STATE);
}

static void graw_bind_sampler_states(struct pipe_context *ctx,
                                     unsigned shader, unsigned start_slot,
                                     unsigned num_samplers,
                                     void **samplers)
{
   struct graw_context *grctx = (struct graw_context *)ctx;
   uint32_t handles[32];
   int i;
   for (i = 0; i < num_samplers; i++) {
      handles[i] = (unsigned long)(samplers[i]);
   }
   graw_encode_bind_sampler_states(grctx, shader, start_slot, num_samplers, handles);
}

static void graw_set_polygon_stipple(struct pipe_context *ctx,
                                     const struct pipe_poly_stipple *ps)
{
   struct graw_context *grctx = (struct graw_context *)ctx;
   graw_encoder_set_polygon_stipple(grctx, ps);
}

static void graw_set_scissor_states(struct pipe_context *ctx,
                                    unsigned start_slot,
                                    unsigned num_scissor,
                                   const struct pipe_scissor_state *ss)
{
   struct graw_context *grctx = (struct graw_context *)ctx;
   graw_encoder_set_scissor_state(grctx, ss);
}

static void graw_set_sample_mask(struct pipe_context *ctx,
                                 unsigned sample_mask)
{
   struct graw_context *grctx = (struct graw_context *)ctx;
   graw_encoder_set_sample_mask(grctx, sample_mask);
}

static void graw_set_clip_state(struct pipe_context *ctx,
                                const struct pipe_clip_state *clip)
{
   struct graw_context *grctx = (struct graw_context *)ctx;
   graw_encoder_set_clip_state(grctx, clip);
}

static void graw_resource_copy_region(struct pipe_context *ctx,
                                      struct pipe_resource *dst,
                                      unsigned dst_level,
                                      unsigned dstx, unsigned dsty, unsigned dstz,
                                      struct pipe_resource *src,
                                      unsigned src_level,
                                      const struct pipe_box *src_box)
{
   struct graw_context *grctx = (struct graw_context *)ctx;
   struct virgl_resource *dres = (struct virgl_resource *)dst;
   struct virgl_resource *sres = (struct virgl_resource *)src;

   dres->clean = FALSE;
   graw_encode_resource_copy_region(grctx, dres,
                                    dst_level, dstx, dsty, dstz,
                                    sres, src_level,
                                    src_box);
}

static void
graw_flush_resource(struct pipe_context *pipe,
                    struct pipe_resource *resource)
{
}


static void graw_blit(struct pipe_context *ctx,
                      const struct pipe_blit_info *blit)
{
   struct graw_context *grctx = (struct graw_context *)ctx;
   struct virgl_resource *dres = (struct virgl_resource *)blit->dst.resource;
   struct virgl_resource *sres = (struct virgl_resource *)blit->src.resource;

   dres->clean = FALSE;
   graw_encode_blit(grctx, dres, sres,
                    blit);
}

                                       
static void
graw_context_destroy( struct pipe_context *ctx )
{
   struct graw_context *grctx = (struct graw_context *)ctx;
   struct virgl_screen *rs = virgl_screen(ctx->screen);
   
   rs->vws->cmd_buf_destroy(grctx->cbuf);
   if (grctx->blitter)
      util_blitter_destroy(grctx->blitter);
   if (grctx->uploader)
      u_upload_destroy(grctx->uploader);
   util_primconvert_destroy(grctx->primconvert);
   
   util_slab_destroy(&grctx->texture_transfer_pool);
   FREE(grctx);
}

struct pipe_context *graw_context_create(struct pipe_screen *pscreen,
                                                         void *priv)
{
   struct graw_context *grctx;
   struct virgl_screen *rs = virgl_screen(pscreen);
   grctx = CALLOC_STRUCT(graw_context);

   grctx->cbuf = rs->vws->cmd_buf_create(rs->vws);
   if (!grctx->cbuf) {
      FREE(grctx);
      return NULL;
   }

   grctx->base.destroy = graw_context_destroy;
   grctx->base.create_surface = graw_create_surface;
   grctx->base.surface_destroy = graw_surface_destroy;
   grctx->base.set_framebuffer_state = graw_set_framebuffer_state;
   grctx->base.create_blend_state = graw_create_blend_state;
   grctx->base.bind_blend_state = graw_bind_blend_state;
   grctx->base.delete_blend_state = graw_delete_blend_state;
   grctx->base.create_depth_stencil_alpha_state = graw_create_depth_stencil_alpha_state;
   grctx->base.bind_depth_stencil_alpha_state = graw_bind_depth_stencil_alpha_state;
   grctx->base.delete_depth_stencil_alpha_state = graw_delete_depth_stencil_alpha_state;
   grctx->base.create_rasterizer_state = graw_create_rasterizer_state;
   grctx->base.bind_rasterizer_state = graw_bind_rasterizer_state;
   grctx->base.delete_rasterizer_state = graw_delete_rasterizer_state;

   grctx->base.set_viewport_states = graw_set_viewport_states;
   grctx->base.create_vertex_elements_state = graw_create_vertex_elements_state;
   grctx->base.bind_vertex_elements_state = graw_bind_vertex_elements_state;
   grctx->base.delete_vertex_elements_state = graw_delete_vertex_elements_state;
   grctx->base.set_vertex_buffers = graw_set_vertex_buffers;
   grctx->base.set_index_buffer = graw_set_index_buffer;
   grctx->base.set_constant_buffer = graw_set_constant_buffer;
   grctx->base.transfer_inline_write = graw_transfer_inline_write;

   grctx->base.create_vs_state = graw_create_vs_state;
   grctx->base.create_gs_state = graw_create_gs_state;
   grctx->base.create_fs_state = graw_create_fs_state;

   grctx->base.bind_vs_state = graw_bind_vs_state;
   grctx->base.bind_gs_state = graw_bind_gs_state;
   grctx->base.bind_fs_state = graw_bind_fs_state;

   grctx->base.delete_vs_state = graw_delete_vs_state;
   grctx->base.delete_gs_state = graw_delete_gs_state;
   grctx->base.delete_fs_state = graw_delete_fs_state;
   
   grctx->base.clear = graw_clear;
   grctx->base.draw_vbo = graw_draw_vbo;
   grctx->base.flush = graw_flush_from_st;
   grctx->base.screen = pscreen;
   grctx->base.create_sampler_view = graw_create_sampler_view;
   grctx->base.sampler_view_destroy = graw_destroy_sampler_view;
   grctx->base.set_sampler_views = graw_set_sampler_views;

   grctx->base.create_sampler_state = graw_create_sampler_state;
   grctx->base.delete_sampler_state = graw_delete_sampler_state;
   grctx->base.bind_sampler_states = graw_bind_sampler_states;

   grctx->base.set_polygon_stipple = graw_set_polygon_stipple;
   grctx->base.set_scissor_states = graw_set_scissor_states;
   grctx->base.set_sample_mask = graw_set_sample_mask;
   grctx->base.set_stencil_ref = graw_set_stencil_ref;
   grctx->base.set_clip_state = graw_set_clip_state;

   grctx->base.set_blend_color = graw_set_blend_color;

   grctx->base.resource_copy_region = graw_resource_copy_region;
   grctx->base.flush_resource = graw_flush_resource;
   grctx->base.blit =  graw_blit;

   virgl_init_context_resource_functions(&grctx->base);
   graw_init_query_functions(grctx);
   graw_init_so_functions(grctx);

   list_inithead(&grctx->to_flush_bufs);
   util_slab_create(&grctx->texture_transfer_pool, sizeof(struct virgl_transfer),
                    16, UTIL_SLAB_SINGLETHREADED);

   grctx->primconvert = util_primconvert_create(&grctx->base, rs->caps.caps.v1.prim_mask);
   grctx->uploader = u_upload_create(&grctx->base, 1024 * 1024, 256,
                                     PIPE_BIND_INDEX_BUFFER);
   if (!grctx->uploader)
           goto fail;

   grctx->blitter = NULL;//util_blitter_create(&grctx->base);
//   if (grctx->blitter == NULL)
//      goto fail;

   return &grctx->base;
fail:
   return NULL;
}
