#include <stdint.h>
#include <string.h>

#include "util/u_memory.h"
#include "util/u_math.h"
#include "pipe/p_state.h"
#include "graw_protocol.h"
#include "graw_encode.h"
#include "tgsi/tgsi_dump.h"

static unsigned uif(float f)
{
   union { float f; unsigned int ui; } myuif;
   myuif.f = f;
   return myuif.ui;
}

static int graw_encoder_write_cmd_dword(struct graw_context *ctx,
                                        uint32_t dword)
{
   int len = (dword >> 16);

   if ((ctx->cbuf->cdw + len + 1) > QXL_MAX_CMDBUF_DWORDS)
      ctx->base.flush(&ctx->base, NULL, 0);

   graw_encoder_write_dword(ctx->cbuf, dword);
   return 0;
}

static void graw_encoder_write_res(struct graw_context *ctx,
                                   struct graw_resource *res)
{
   struct qxl_winsys *qws = rempipe_screen(ctx->base.screen)->qws;

   if (res && res->hw_res)
      qws->emit_res(qws, ctx->cbuf, res->hw_res, TRUE);
   else {
      graw_encoder_write_dword(ctx->cbuf, 0);
   }
}

int graw_encode_bind_object(struct graw_context *ctx,
			    uint32_t handle, uint32_t object)
{
   graw_encoder_write_cmd_dword(ctx, GRAW_CMD0(GRAW_BIND_OBJECT, object, 1));
   graw_encoder_write_dword(ctx->cbuf, handle);
   return 0;
}

int graw_encode_delete_object(struct graw_context *ctx,
			      uint32_t handle, uint32_t object)
{
   graw_encoder_write_cmd_dword(ctx, GRAW_CMD0(GRAW_DESTROY_OBJECT, object, 1));
   graw_encoder_write_dword(ctx->cbuf, handle);
}

int graw_encode_blend_state(struct graw_context *ctx,
                            uint32_t handle,
                            const struct pipe_blend_state *blend_state)
{
   uint32_t tmp;
   int i;

   graw_encoder_write_cmd_dword(ctx, GRAW_CMD0(GRAW_CREATE_OBJECT, GRAW_OBJECT_BLEND, 2 + 1 + PIPE_MAX_COLOR_BUFS));
   graw_encoder_write_dword(ctx->cbuf, handle);

   tmp = (blend_state->independent_blend_enable << 0) |
      (blend_state->logicop_enable << 1) |
      (blend_state->dither << 2) |
      (blend_state->alpha_to_coverage << 3) |
      (blend_state->alpha_to_one << 4);

   graw_encoder_write_dword(ctx->cbuf, tmp);

   tmp = blend_state->logicop_func << 0;
   graw_encoder_write_dword(ctx->cbuf, tmp);

   for (i = 0; i < PIPE_MAX_COLOR_BUFS; i++) {
      tmp = ((blend_state->rt[i].blend_enable << 0) |
             (blend_state->rt[i].rgb_func << 1) |
             (blend_state->rt[i].rgb_src_factor << 4) |
             (blend_state->rt[i].rgb_dst_factor << 9) |
             (blend_state->rt[i].alpha_func << 14) |
             (blend_state->rt[i].alpha_src_factor << 17) |
             (blend_state->rt[i].alpha_dst_factor << 22) |
             (blend_state->rt[i].colormask << 27));
      graw_encoder_write_dword(ctx->cbuf, tmp);
   }
   return 0;
}

int graw_encode_dsa_state(struct graw_context *ctx,
                          uint32_t handle,
                          const struct pipe_depth_stencil_alpha_state *dsa_state)
{
   uint32_t tmp;
   int i;
   graw_encoder_write_cmd_dword(ctx, GRAW_CMD0(GRAW_CREATE_OBJECT, GRAW_OBJECT_DSA, 1 + 4));
   graw_encoder_write_dword(ctx->cbuf, handle);

   tmp = dsa_state->depth.enabled |
      dsa_state->depth.writemask << 1 |
      dsa_state->depth.func << 2 |
      dsa_state->alpha.enabled << 8 |
      dsa_state->alpha.func << 9;
   graw_encoder_write_dword(ctx->cbuf, tmp);

   for (i = 0; i < 2; i++) {
      tmp = dsa_state->stencil[i].enabled |
         dsa_state->stencil[i].func << 1 |
         dsa_state->stencil[i].fail_op << 4 |
         dsa_state->stencil[i].zpass_op << 7 |
         dsa_state->stencil[i].zfail_op << 10 |
         dsa_state->stencil[i].valuemask << 13 |
         dsa_state->stencil[i].writemask << 21;
      graw_encoder_write_dword(ctx->cbuf, tmp);
   }
      
   graw_encoder_write_dword(ctx->cbuf, uif(dsa_state->alpha.ref_value));
   return 0;
}
int graw_encode_rasterizer_state(struct graw_context *ctx,
                                  uint32_t handle,
                                  const struct pipe_rasterizer_state *state)
{
   uint32_t tmp;

   graw_encoder_write_cmd_dword(ctx, GRAW_CMD0(GRAW_CREATE_OBJECT, GRAW_OBJECT_RASTERIZER, 1 + 4));
   graw_encoder_write_dword(ctx->cbuf, handle);

   tmp = (state->flatshade << 0) |
      (state->depth_clip << 1) |
      (state->gl_rasterization_rules << 2) |
      (state->rasterizer_discard << 3) |
      (state->flatshade_first << 4) |
      (state->light_twoside << 5) |
      (state->sprite_coord_mode << 6) |
      (state->point_quad_rasterization << 7) |
      (state->cull_face << 8) |
      (state->fill_front << 10) |
      (state->fill_back << 12) |
      (state->scissor << 14) |
      (state->front_ccw << 15);
   
   graw_encoder_write_dword(ctx->cbuf, tmp);
   graw_encoder_write_dword(ctx->cbuf, uif(state->point_size));
   graw_encoder_write_dword(ctx->cbuf, state->sprite_coord_enable);
   graw_encoder_write_dword(ctx->cbuf, 0);
}

int graw_encode_shader_state(struct graw_context *ctx,
                             uint32_t handle,
			     uint32_t type,
                             const struct pipe_shader_state *shader)
{
   static char str[65536];
   uint32_t len;

   memset(str, 0, 65536);
   tgsi_dump_str(shader->tokens, 0, str, sizeof(str));

   len = strlen(str) + 1;
   graw_encoder_write_cmd_dword(ctx, GRAW_CMD0(GRAW_CREATE_OBJECT, type, ((len + 3)/ 4) + 1));
   graw_encoder_write_dword(ctx->cbuf, handle);
   graw_encoder_write_block(ctx->cbuf, str, len);
   return 0;
}

int graw_encode_clear(struct graw_context *ctx,
                      unsigned buffers,
                      const union pipe_color_union *color,
                      double depth, unsigned stencil)
{
   int i;
   
   graw_encoder_write_cmd_dword(ctx, GRAW_CMD0(GRAW_CLEAR, 0, 8));
                            
   graw_encoder_write_dword(ctx->cbuf, buffers);
   for (i = 0; i < 4; i++)
      graw_encoder_write_dword(ctx->cbuf, color->ui[i]);
   graw_encoder_write_qword(ctx->cbuf, *(uint64_t *)&depth);
   graw_encoder_write_dword(ctx->cbuf, stencil);
}

int graw_encoder_set_framebuffer_state(struct graw_context *ctx,
				       const struct pipe_framebuffer_state *state)
{
   struct graw_surface *zsurf = (struct graw_surface *)state->zsbuf;
   int i;

   graw_encoder_write_cmd_dword(ctx, GRAW_CMD0(GRAW_SET_FRAMEBUFFER_STATE, 0, 2 + state->nr_cbufs));
   graw_encoder_write_dword(ctx->cbuf, state->nr_cbufs);
   graw_encoder_write_dword(ctx->cbuf, zsurf ? zsurf->handle : 0);
   for (i = 0; i < state->nr_cbufs; i++) {
      struct graw_surface *surf = (struct graw_surface *)state->cbufs[i];
      graw_encoder_write_dword(ctx->cbuf, surf->handle);
   }
   return 0;
}

int graw_encoder_set_viewport_state(struct graw_context *ctx,
				    const struct pipe_viewport_state *state)
{
   int i;
   graw_encoder_write_cmd_dword(ctx, GRAW_CMD0(GRAW_SET_VIEWPORT_STATE, 0, 8));
   for (i = 0; i < 4; i++)
      graw_encoder_write_dword(ctx->cbuf, uif(state->scale[i]));
   for (i = 0; i < 4; i++)
      graw_encoder_write_dword(ctx->cbuf, uif(state->translate[i]));
   return 0;
}

int graw_encoder_create_vertex_elements(struct graw_context *ctx,
                                        uint32_t handle,
                                        unsigned num_elements,
                                        const struct pipe_vertex_element *element)
{
   int i;
   graw_encoder_write_cmd_dword(ctx, GRAW_CMD0(GRAW_CREATE_OBJECT, GRAW_OBJECT_VERTEX_ELEMENTS, (4 * num_elements) + 1));
   graw_encoder_write_dword(ctx->cbuf, handle);
   for (i = 0; i < num_elements; i++) {
      graw_encoder_write_dword(ctx->cbuf, element[i].src_offset);
      graw_encoder_write_dword(ctx->cbuf, element[i].instance_divisor);
      graw_encoder_write_dword(ctx->cbuf, element[i].vertex_buffer_index);
      graw_encoder_write_dword(ctx->cbuf, element[i].src_format);
   }
   return 0;
}

int graw_encoder_set_vertex_buffers(struct graw_context *ctx,
                                    unsigned num_buffers,
                                    const struct pipe_vertex_buffer *buffers)
{
   int i;
   graw_encoder_write_cmd_dword(ctx, GRAW_CMD0(GRAW_SET_VERTEX_BUFFERS, 0, (3 * num_buffers)));
   for (i = 0; i < num_buffers; i++) {
      struct graw_resource *res = (struct graw_resource *)buffers[i].buffer;
      graw_encoder_write_dword(ctx->cbuf, buffers[i].stride);
      graw_encoder_write_dword(ctx->cbuf, buffers[i].buffer_offset);
      graw_encoder_write_res(ctx, res);
   }
}

int graw_encoder_set_index_buffer(struct graw_context *ctx,
                                  const struct pipe_index_buffer *ib)
{
   int length = 1 + (ib ? 2 : 0);
   struct graw_resource *res = NULL;
   if (ib)
      res = (struct graw_resource *)ib->buffer;

   graw_encoder_write_cmd_dword(ctx, GRAW_CMD0(GRAW_SET_INDEX_BUFFER, 0, length));
   graw_encoder_write_res(ctx, res);
   if (ib) {
      graw_encoder_write_dword(ctx->cbuf, ib->index_size);
      graw_encoder_write_dword(ctx->cbuf, ib->offset);
   }
}

int graw_encoder_draw_vbo(struct graw_context *ctx,
			  const struct pipe_draw_info *info)
{
   graw_encoder_write_cmd_dword(ctx, GRAW_CMD0(GRAW_DRAW_VBO, 0, 5));
   graw_encoder_write_dword(ctx->cbuf, info->start);
   graw_encoder_write_dword(ctx->cbuf, info->count);
   graw_encoder_write_dword(ctx->cbuf, info->mode);
   graw_encoder_write_dword(ctx->cbuf, info->indexed);
   graw_encoder_write_dword(ctx->cbuf, info->instance_count);
   return 0;
}

int graw_encoder_create_surface(struct graw_context *ctx,
				uint32_t handle,
				struct graw_resource *res,
				const struct pipe_surface *templat)
{
   graw_encoder_write_cmd_dword(ctx, GRAW_CMD0(GRAW_CREATE_OBJECT, GRAW_SURFACE, 5));
   graw_encoder_write_dword(ctx->cbuf, handle);
   graw_encoder_write_res(ctx, res);
   graw_encoder_write_dword(ctx->cbuf, templat->format);
   if (templat->texture->target == PIPE_BUFFER) {
      graw_encoder_write_dword(ctx->cbuf, templat->u.buf.first_element);
      graw_encoder_write_dword(ctx->cbuf, templat->u.buf.last_element);

   } else {
      graw_encoder_write_dword(ctx->cbuf, templat->u.tex.level);
      graw_encoder_write_dword(ctx->cbuf, templat->u.tex.first_layer | (templat->u.tex.last_layer << 16));
   }
   return 0;
}

int graw_encoder_inline_write(struct graw_context *ctx,
                              struct graw_resource *res,
                              unsigned level, unsigned usage,
                              const struct pipe_box *box,
                              void *data, unsigned stride,
                              unsigned layer_stride)
{
   uint32_t size = (stride ? stride : box->width) * box->height;
   uint32_t length;

   size = align(size, 4);

   length = 11 + size / 4;
   graw_encoder_write_cmd_dword(ctx, GRAW_CMD0(GRAW_RESOURCE_INLINE_WRITE, 0, length));
   graw_encoder_write_res(ctx, res);
   graw_encoder_write_dword(ctx->cbuf, level);
   graw_encoder_write_dword(ctx->cbuf, usage);
   graw_encoder_write_dword(ctx->cbuf, stride);
   graw_encoder_write_dword(ctx->cbuf, layer_stride);
   graw_encoder_write_dword(ctx->cbuf, box->x);
   graw_encoder_write_dword(ctx->cbuf, box->y);
   graw_encoder_write_dword(ctx->cbuf, box->z);
   graw_encoder_write_dword(ctx->cbuf, box->width);
   graw_encoder_write_dword(ctx->cbuf, box->height);
   graw_encoder_write_dword(ctx->cbuf, box->depth);

   graw_encoder_write_block(ctx->cbuf, data, size);

}

int graw_encoder_flush_frontbuffer(struct graw_context *ctx,
                                   struct graw_resource *res)
{
//   graw_encoder_write_dword(ctx->cbuf, GRAW_CMD0(GRAW_FLUSH_FRONTUBFFER, 0, 1));
//   graw_encoder_write_dword(ctx->cbuf, res_handle);
   return 0;
}

int graw_encode_sampler_state(struct graw_context *ctx,
                              uint32_t handle,
                              const struct pipe_sampler_state *state)
{
   uint32_t tmp;

   graw_encoder_write_cmd_dword(ctx, GRAW_CMD0(GRAW_CREATE_OBJECT, GRAW_OBJECT_SAMPLER_STATE , 5));
   graw_encoder_write_dword(ctx->cbuf, handle);

   tmp = state->wrap_s |
      state->wrap_t << 3 |
      state->wrap_r << 6 |
      state->min_img_filter << 9 |
      state->min_mip_filter << 11 |
      state->mag_img_filter << 13 |
      state->compare_mode << 15 |
      state->compare_func << 16;

   graw_encoder_write_dword(ctx->cbuf, tmp);
   graw_encoder_write_dword(ctx->cbuf, uif(state->lod_bias));
   graw_encoder_write_dword(ctx->cbuf, uif(state->min_lod));
   graw_encoder_write_dword(ctx->cbuf, uif(state->max_lod));
}


int graw_encode_sampler_view(struct graw_context *ctx,
                             uint32_t handle,
                             struct graw_resource *res,
                             const struct pipe_sampler_view *state)
{
   graw_encoder_write_cmd_dword(ctx, GRAW_CMD0(GRAW_CREATE_OBJECT, GRAW_OBJECT_SAMPLER_VIEW, 6));
   graw_encoder_write_dword(ctx->cbuf, handle);
   graw_encoder_write_res(ctx, res);
   graw_encoder_write_dword(ctx->cbuf, state->format);
   if (res->base.target == PIPE_BUFFER) {
      graw_encoder_write_dword(ctx->cbuf, state->u.buf.first_element);
      graw_encoder_write_dword(ctx->cbuf, state->u.buf.last_element);
   } else {
      graw_encoder_write_dword(ctx->cbuf, state->u.tex.first_layer | state->u.tex.last_layer << 16);
      graw_encoder_write_dword(ctx->cbuf, state->u.tex.first_level | state->u.tex.last_level << 8);
   }
   graw_encoder_write_dword(ctx->cbuf, state->swizzle_r | (state->swizzle_g << 3) | (state->swizzle_b << 6) | (state->swizzle_a << 9));
}

int graw_encode_set_sampler_views(struct graw_context *ctx,
                                  uint32_t shader_type,
                                  uint32_t num_handles,
                                  uint32_t *handles)
{
   int i;
   graw_encoder_write_cmd_dword(ctx, GRAW_CMD0(GRAW_SET_SAMPLER_VIEWS, 0, num_handles + 1));
   graw_encoder_write_dword(ctx->cbuf, shader_type);
   for (i = 0; i < num_handles; i++)
      graw_encoder_write_dword(ctx->cbuf, handles[i]);
   return 0;
}

int graw_encode_bind_sampler_states(struct graw_context *ctx,
                                    uint32_t shader_type,
                                    uint32_t num_handles,
                                    uint32_t *handles)
{
   int i;
   graw_encoder_write_cmd_dword(ctx, GRAW_CMD0(GRAW_BIND_SAMPLER_STATES, 0, num_handles + 1));
   graw_encoder_write_dword(ctx->cbuf, shader_type);
   for (i = 0; i < num_handles; i++)
      graw_encoder_write_dword(ctx->cbuf, handles[i]);
   return 0;
}

int graw_encoder_write_constant_buffer(struct graw_context *ctx,
                                       uint32_t shader,
                                       uint32_t index,
                                       uint32_t size,
                                       void *data)
{
   graw_encoder_write_cmd_dword(ctx, GRAW_CMD0(GRAW_SET_CONSTANT_BUFFER, 0, size + 2));
   graw_encoder_write_dword(ctx->cbuf, shader);
   graw_encoder_write_dword(ctx->cbuf, index);
   if (data)
      graw_encoder_write_block(ctx->cbuf, data, size * 4);

}

int graw_encoder_set_stencil_ref(struct graw_context *ctx,
                                 const struct pipe_stencil_ref *ref)
{
   graw_encoder_write_cmd_dword(ctx, GRAW_CMD0(GRAW_SET_STENCIL_REF, 0, 1));
   graw_encoder_write_dword(ctx->cbuf, (ref->ref_value[0] | (ref->ref_value[1] << 8)));
}

int graw_encoder_set_blend_color(struct graw_context *ctx,
                                 const struct pipe_blend_color *color)
{
   int i;
   graw_encoder_write_cmd_dword(ctx, GRAW_CMD0(GRAW_SET_BLEND_COLOR, 0, 4));
   for (i = 0; i < 4; i++)
      graw_encoder_write_dword(ctx->cbuf, uif(color->color[i]));
}

int graw_encoder_set_scissor_state(struct graw_context *ctx,
                                   const struct pipe_scissor_state *ss)
{
   graw_encoder_write_cmd_dword(ctx, GRAW_CMD0(GRAW_SET_SCISSOR_STATE, 0, 2));
   graw_encoder_write_dword(ctx->cbuf, (ss->minx | ss->miny << 16));
   graw_encoder_write_dword(ctx->cbuf, (ss->maxx | ss->maxy << 16));
}

int graw_encode_resource_copy_region(struct graw_context *ctx,
                                     struct graw_resource *dst_res,
                                     unsigned dst_level,
                                     unsigned dstx, unsigned dsty, unsigned dstz,
                                     struct graw_resource *src_res,
                                     unsigned src_level,
                                     const struct pipe_box *src_box)
{
   graw_encoder_write_cmd_dword(ctx, GRAW_CMD0(GRAW_RESOURCE_COPY_REGION, 0, 13));
   graw_encoder_write_res(ctx, dst_res);
   graw_encoder_write_dword(ctx->cbuf, dst_level);
   graw_encoder_write_dword(ctx->cbuf, dstx);
   graw_encoder_write_dword(ctx->cbuf, dsty);
   graw_encoder_write_dword(ctx->cbuf, dstz);
   graw_encoder_write_res(ctx, src_res);
   graw_encoder_write_dword(ctx->cbuf, src_level);
   graw_encoder_write_dword(ctx->cbuf, src_box->x);
   graw_encoder_write_dword(ctx->cbuf, src_box->y);
   graw_encoder_write_dword(ctx->cbuf, src_box->z);
   graw_encoder_write_dword(ctx->cbuf, src_box->width);
   graw_encoder_write_dword(ctx->cbuf, src_box->height);
   graw_encoder_write_dword(ctx->cbuf, src_box->depth);
   return 0;
}

int graw_encode_blit(struct graw_context *ctx,
                     struct graw_resource *dst_res,
                     struct graw_resource *src_res,
                     const struct pipe_blit_info *blit)
{
   graw_encoder_write_cmd_dword(ctx, GRAW_CMD0(GRAW_BLIT, 0, 23));
   graw_encoder_write_dword(ctx->cbuf, blit->mask);
   graw_encoder_write_dword(ctx->cbuf, blit->filter);
   graw_encoder_write_dword(ctx->cbuf, blit->scissor_enable);
   graw_encoder_write_dword(ctx->cbuf, (blit->scissor.minx | blit->scissor.miny << 16));
   graw_encoder_write_dword(ctx->cbuf, (blit->scissor.maxx | blit->scissor.maxy << 16));

   graw_encoder_write_res(ctx, dst_res);
   graw_encoder_write_dword(ctx->cbuf, blit->dst.level);
   graw_encoder_write_dword(ctx->cbuf, blit->dst.format);
   graw_encoder_write_dword(ctx->cbuf, blit->dst.box.x);
   graw_encoder_write_dword(ctx->cbuf, blit->dst.box.y);
   graw_encoder_write_dword(ctx->cbuf, blit->dst.box.z);
   graw_encoder_write_dword(ctx->cbuf, blit->dst.box.width);
   graw_encoder_write_dword(ctx->cbuf, blit->dst.box.height);
   graw_encoder_write_dword(ctx->cbuf, blit->dst.box.depth);

   graw_encoder_write_res(ctx, src_res);
   graw_encoder_write_dword(ctx->cbuf, blit->src.level);
   graw_encoder_write_dword(ctx->cbuf, blit->src.format);
   graw_encoder_write_dword(ctx->cbuf, blit->src.box.x);
   graw_encoder_write_dword(ctx->cbuf, blit->src.box.y);
   graw_encoder_write_dword(ctx->cbuf, blit->src.box.z);
   graw_encoder_write_dword(ctx->cbuf, blit->src.box.width);
   graw_encoder_write_dword(ctx->cbuf, blit->src.box.height);
   graw_encoder_write_dword(ctx->cbuf, blit->src.box.depth);
   return 0;
}

int graw_encoder_create_query(struct graw_context *ctx,
                              uint32_t handle,
                              uint query_type)
{
   graw_encoder_write_cmd_dword(ctx, GRAW_CMD0(GRAW_CREATE_OBJECT, GRAW_QUERY, 2));
   graw_encoder_write_dword(ctx->cbuf, handle);
   graw_encoder_write_dword(ctx->cbuf, query_type);
   return 0;
}

int graw_encoder_begin_query(struct graw_context *ctx,
                             uint32_t handle)
{
   graw_encoder_write_cmd_dword(ctx, GRAW_CMD0(GRAW_BEGIN_QUERY, 0, 1));
   graw_encoder_write_dword(ctx->cbuf, handle);   
}

int graw_encoder_end_query(struct graw_context *ctx,
                           uint32_t handle)
{
   graw_encoder_write_cmd_dword(ctx, GRAW_CMD0(GRAW_END_QUERY, 0, 1));
   graw_encoder_write_dword(ctx->cbuf, handle);   
}

int graw_encoder_get_query_result(struct graw_context *ctx,
                                  uint32_t handle, boolean wait)
{
   graw_encoder_write_cmd_dword(ctx, GRAW_CMD0(GRAW_GET_QUERY_RESULT, 0, 2));
   graw_encoder_write_dword(ctx->cbuf, handle);
   graw_encoder_write_dword(ctx->cbuf, wait ? 1 : 0);
}
