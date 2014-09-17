#include <stdint.h>
#include <string.h>

#include "util/u_memory.h"
#include "util/u_math.h"
#include "pipe/p_state.h"
#include "virgl_encode.h"
#include "virgl_resource.h"
#include "tgsi/tgsi_dump.h"
#include "tgsi/tgsi_parse.h"

static unsigned uif(float f)
{
   union { float f; unsigned int ui; } myuif;
   myuif.f = f;
   return myuif.ui;
}

static int virgl_encoder_write_cmd_dword(struct virgl_context *ctx,
                                        uint32_t dword)
{
   int len = (dword >> 16);

   if ((ctx->cbuf->cdw + len + 1) > VIRGL_MAX_CMDBUF_DWORDS)
      ctx->base.flush(&ctx->base, NULL, 0);

   virgl_encoder_write_dword(ctx->cbuf, dword);
   return 0;
}

static void virgl_encoder_write_res(struct virgl_context *ctx,
                                   struct virgl_resource *res)
{
   struct virgl_winsys *vws = virgl_screen(ctx->base.screen)->vws;

   if (res && res->hw_res)
      vws->emit_res(vws, ctx->cbuf, res->hw_res, TRUE);
   else {
      virgl_encoder_write_dword(ctx->cbuf, 0);
   }
}

static void virgl_encoder_attach_res(struct virgl_context *ctx,
                                    struct virgl_resource *res)
{
   struct virgl_winsys *vws = virgl_screen(ctx->base.screen)->vws;

   if (res && res->hw_res)
      vws->emit_res(vws, ctx->cbuf, res->hw_res, FALSE);
}

int virgl_encode_bind_object(struct virgl_context *ctx,
			    uint32_t handle, uint32_t object)
{
   virgl_encoder_write_cmd_dword(ctx, VIRGL_CMD0(VIRGL_CCMD_BIND_OBJECT, object, 1));
   virgl_encoder_write_dword(ctx->cbuf, handle);
   return 0;
}

int virgl_encode_delete_object(struct virgl_context *ctx,
			      uint32_t handle, uint32_t object)
{
   virgl_encoder_write_cmd_dword(ctx, VIRGL_CMD0(VIRGL_CCMD_DESTROY_OBJECT, object, 1));
   virgl_encoder_write_dword(ctx->cbuf, handle);
   return 0;
}

int virgl_encode_blend_state(struct virgl_context *ctx,
                            uint32_t handle,
                            const struct pipe_blend_state *blend_state)
{
   uint32_t tmp;
   int i;

   virgl_encoder_write_cmd_dword(ctx, VIRGL_CMD0(VIRGL_CCMD_CREATE_OBJECT, VIRGL_OBJECT_BLEND, 2 + 1 + PIPE_MAX_COLOR_BUFS));
   virgl_encoder_write_dword(ctx->cbuf, handle);

   tmp = (blend_state->independent_blend_enable << 0) |
      (blend_state->logicop_enable << 1) |
      (blend_state->dither << 2) |
      (blend_state->alpha_to_coverage << 3) |
      (blend_state->alpha_to_one << 4);

   virgl_encoder_write_dword(ctx->cbuf, tmp);

   tmp = blend_state->logicop_func << 0;
   virgl_encoder_write_dword(ctx->cbuf, tmp);

   for (i = 0; i < PIPE_MAX_COLOR_BUFS; i++) {
      tmp = ((blend_state->rt[i].blend_enable << 0) |
             (blend_state->rt[i].rgb_func << 1) |
             (blend_state->rt[i].rgb_src_factor << 4) |
             (blend_state->rt[i].rgb_dst_factor << 9) |
             (blend_state->rt[i].alpha_func << 14) |
             (blend_state->rt[i].alpha_src_factor << 17) |
             (blend_state->rt[i].alpha_dst_factor << 22) |
             (blend_state->rt[i].colormask << 27));
      virgl_encoder_write_dword(ctx->cbuf, tmp);
   }
   return 0;
}

int virgl_encode_dsa_state(struct virgl_context *ctx,
                          uint32_t handle,
                          const struct pipe_depth_stencil_alpha_state *dsa_state)
{
   uint32_t tmp;
   int i;
   virgl_encoder_write_cmd_dword(ctx, VIRGL_CMD0(VIRGL_CCMD_CREATE_OBJECT, VIRGL_OBJECT_DSA, 1 + 4));
   virgl_encoder_write_dword(ctx->cbuf, handle);

   tmp = dsa_state->depth.enabled |
      dsa_state->depth.writemask << 1 |
      dsa_state->depth.func << 2 |
      dsa_state->alpha.enabled << 8 |
      dsa_state->alpha.func << 9;
   virgl_encoder_write_dword(ctx->cbuf, tmp);

   for (i = 0; i < 2; i++) {
      tmp = dsa_state->stencil[i].enabled |
         dsa_state->stencil[i].func << 1 |
         dsa_state->stencil[i].fail_op << 4 |
         dsa_state->stencil[i].zpass_op << 7 |
         dsa_state->stencil[i].zfail_op << 10 |
         dsa_state->stencil[i].valuemask << 13 |
         dsa_state->stencil[i].writemask << 21;
      virgl_encoder_write_dword(ctx->cbuf, tmp);
   }
      
   virgl_encoder_write_dword(ctx->cbuf, uif(dsa_state->alpha.ref_value));
   return 0;
}
int virgl_encode_rasterizer_state(struct virgl_context *ctx,
                                  uint32_t handle,
                                  const struct pipe_rasterizer_state *state)
{
   uint32_t tmp;

   virgl_encoder_write_cmd_dword(ctx, VIRGL_CMD0(VIRGL_CCMD_CREATE_OBJECT, VIRGL_OBJECT_RASTERIZER, 1 + 8));
   virgl_encoder_write_dword(ctx->cbuf, handle);

   tmp = (state->flatshade << 0) |
      (state->depth_clip << 1) |
      (state->clip_halfz << 2) |
      (state->rasterizer_discard << 3) |
      (state->flatshade_first << 4) |
      (state->light_twoside << 5) |
      (state->sprite_coord_mode << 6) |
      (state->point_quad_rasterization << 7) |
      (state->cull_face << 8) |
      (state->fill_front << 10) |
      (state->fill_back << 12) |
      (state->scissor << 14) |
      (state->front_ccw << 15) |
      (state->clamp_vertex_color << 16) |
      (state->clamp_fragment_color << 17) |
      (state->offset_line << 18) |
      (state->offset_point << 19) |
      (state->offset_tri << 20) |
      (state->poly_smooth << 21) |
      (state->poly_stipple_enable << 22) |
      (state->point_smooth << 23) |
      (state->point_size_per_vertex << 24) |
      (state->multisample << 25) |
      (state->line_smooth << 26) |
      (state->line_stipple_enable << 27) |
      (state->line_last_pixel << 28) |
      (state->half_pixel_center << 29) |
      (state->bottom_edge_rule << 30);
   
   virgl_encoder_write_dword(ctx->cbuf, tmp);
   virgl_encoder_write_dword(ctx->cbuf, uif(state->point_size));
   virgl_encoder_write_dword(ctx->cbuf, state->sprite_coord_enable);
   virgl_encoder_write_dword(ctx->cbuf, state->line_stipple_pattern | (state->line_stipple_factor << 16) | (state->clip_plane_enable << 24));
   virgl_encoder_write_dword(ctx->cbuf, uif(state->line_width));
   virgl_encoder_write_dword(ctx->cbuf, uif(state->offset_units));
   virgl_encoder_write_dword(ctx->cbuf, uif(state->offset_scale));
   virgl_encoder_write_dword(ctx->cbuf, uif(state->offset_clamp));
   return 0;
}

int virgl_encode_shader_state(struct virgl_context *ctx,
                             uint32_t handle,
			     uint32_t type,
                             const struct pipe_shader_state *shader)
{
   static char str[65536];
   uint32_t shader_len, len;
   int i;
   uint32_t tmp;
   int num_tokens = tgsi_num_tokens(shader->tokens);

   memset(str, 0, 65536);
   tgsi_dump_str(shader->tokens, TGSI_DUMP_FLOAT_AS_HEX, str, sizeof(str));

   shader_len = strlen(str) + 1;
   len = ((shader_len + 3) / 4) + 3 + (shader->stream_output.num_outputs ? shader->stream_output.num_outputs + 4 : 0);

   virgl_encoder_write_cmd_dword(ctx, VIRGL_CMD0(VIRGL_CCMD_CREATE_OBJECT, type, len));
   virgl_encoder_write_dword(ctx->cbuf, handle);
   virgl_encoder_write_dword(ctx->cbuf, num_tokens);
   virgl_encoder_write_dword(ctx->cbuf, shader->stream_output.num_outputs);
   if (shader->stream_output.num_outputs) {
      for (i = 0; i < 4; i++)
         virgl_encoder_write_dword(ctx->cbuf, shader->stream_output.stride[i]);

      for (i = 0; i < shader->stream_output.num_outputs; i++) {
         tmp = shader->stream_output.output[i].register_index | 
            (unsigned)shader->stream_output.output[i].start_component << 8 | 
            (unsigned)shader->stream_output.output[i].num_components << 10 | 
            (unsigned)shader->stream_output.output[i].output_buffer << 13 | 
            (unsigned)shader->stream_output.output[i].dst_offset << 16;
         virgl_encoder_write_dword(ctx->cbuf, tmp);
      }
   }
   virgl_encoder_write_block(ctx->cbuf, (uint8_t *)str, shader_len);
   return 0;
}


int virgl_encode_clear(struct virgl_context *ctx,
                      unsigned buffers,
                      const union pipe_color_union *color,
                      double depth, unsigned stencil)
{
   int i;
   
   virgl_encoder_write_cmd_dword(ctx, VIRGL_CMD0(VIRGL_CCMD_CLEAR, 0, 8));
                            
   virgl_encoder_write_dword(ctx->cbuf, buffers);
   for (i = 0; i < 4; i++)
      virgl_encoder_write_dword(ctx->cbuf, color->ui[i]);
   virgl_encoder_write_qword(ctx->cbuf, *(uint64_t *)&depth);
   virgl_encoder_write_dword(ctx->cbuf, stencil);
   return 0;
}

int virgl_encoder_set_framebuffer_state(struct virgl_context *ctx,
				       const struct pipe_framebuffer_state *state)
{
   struct virgl_surface *zsurf = (struct virgl_surface *)state->zsbuf;
   int i;

   virgl_encoder_write_cmd_dword(ctx, VIRGL_CMD0(VIRGL_CCMD_SET_FRAMEBUFFER_STATE, 0, 2 + state->nr_cbufs));
   virgl_encoder_write_dword(ctx->cbuf, state->nr_cbufs);
   virgl_encoder_write_dword(ctx->cbuf, zsurf ? zsurf->handle : 0);
   for (i = 0; i < state->nr_cbufs; i++) {
      struct virgl_surface *surf = (struct virgl_surface *)state->cbufs[i];
      virgl_encoder_write_dword(ctx->cbuf, surf ? surf->handle : 0);
      if (surf)
         virgl_encoder_attach_res(ctx, (struct virgl_resource *)surf->base.texture);
   }

   if (zsurf)
      virgl_encoder_attach_res(ctx, (struct virgl_resource *)zsurf->base.texture);

   return 0;
}

int virgl_encoder_set_viewport_state(struct virgl_context *ctx,
				    const struct pipe_viewport_state *state)
{
   int i;
   virgl_encoder_write_cmd_dword(ctx, VIRGL_CMD0(VIRGL_CCMD_SET_VIEWPORT_STATE, 0, 8));
   for (i = 0; i < 4; i++)
      virgl_encoder_write_dword(ctx->cbuf, uif(state->scale[i]));
   for (i = 0; i < 4; i++)
      virgl_encoder_write_dword(ctx->cbuf, uif(state->translate[i]));
   return 0;
}

int virgl_encoder_create_vertex_elements(struct virgl_context *ctx,
                                        uint32_t handle,
                                        unsigned num_elements,
                                        const struct pipe_vertex_element *element)
{
   int i;
   virgl_encoder_write_cmd_dword(ctx, VIRGL_CMD0(VIRGL_CCMD_CREATE_OBJECT, VIRGL_OBJECT_VERTEX_ELEMENTS, (4 * num_elements) + 1));
   virgl_encoder_write_dword(ctx->cbuf, handle);
   for (i = 0; i < num_elements; i++) {
      virgl_encoder_write_dword(ctx->cbuf, element[i].src_offset);
      virgl_encoder_write_dword(ctx->cbuf, element[i].instance_divisor);
      virgl_encoder_write_dword(ctx->cbuf, element[i].vertex_buffer_index);
      virgl_encoder_write_dword(ctx->cbuf, element[i].src_format);
   }
   return 0;
}

int virgl_encoder_set_vertex_buffers(struct virgl_context *ctx,
                                    unsigned num_buffers,
                                    const struct pipe_vertex_buffer *buffers)
{
   int i;
   virgl_encoder_write_cmd_dword(ctx, VIRGL_CMD0(VIRGL_CCMD_SET_VERTEX_BUFFERS, 0, (3 * num_buffers)));
   for (i = 0; i < num_buffers; i++) {
      struct virgl_resource *res = (struct virgl_resource *)buffers[i].buffer;
      virgl_encoder_write_dword(ctx->cbuf, buffers[i].stride);
      virgl_encoder_write_dword(ctx->cbuf, buffers[i].buffer_offset);
      virgl_encoder_write_res(ctx, res);
   }
   return 0;
}

int virgl_encoder_set_index_buffer(struct virgl_context *ctx,
                                  const struct pipe_index_buffer *ib)
{
   int length = 1 + (ib ? 2 : 0);
   struct virgl_resource *res = NULL;
   if (ib)
      res = (struct virgl_resource *)ib->buffer;

   virgl_encoder_write_cmd_dword(ctx, VIRGL_CMD0(VIRGL_CCMD_SET_INDEX_BUFFER, 0, length));
   virgl_encoder_write_res(ctx, res);
   if (ib) {
      virgl_encoder_write_dword(ctx->cbuf, ib->index_size);
      virgl_encoder_write_dword(ctx->cbuf, ib->offset);
   }
   return 0;
}

int virgl_encoder_draw_vbo(struct virgl_context *ctx,
			  const struct pipe_draw_info *info)
{
   virgl_encoder_write_cmd_dword(ctx, VIRGL_CMD0(VIRGL_CCMD_DRAW_VBO, 0, 11));
   virgl_encoder_write_dword(ctx->cbuf, info->start);
   virgl_encoder_write_dword(ctx->cbuf, info->count);
   virgl_encoder_write_dword(ctx->cbuf, info->mode);
   virgl_encoder_write_dword(ctx->cbuf, info->indexed);
   virgl_encoder_write_dword(ctx->cbuf, info->instance_count);
   virgl_encoder_write_dword(ctx->cbuf, info->index_bias);
   virgl_encoder_write_dword(ctx->cbuf, info->start_instance);
   virgl_encoder_write_dword(ctx->cbuf, info->primitive_restart);
   virgl_encoder_write_dword(ctx->cbuf, info->restart_index);
   virgl_encoder_write_dword(ctx->cbuf, info->min_index);
   virgl_encoder_write_dword(ctx->cbuf, info->max_index);
   
   return 0;
}

int virgl_encoder_create_surface(struct virgl_context *ctx,
				uint32_t handle,
				struct virgl_resource *res,
				const struct pipe_surface *templat)
{
   virgl_encoder_write_cmd_dword(ctx, VIRGL_CMD0(VIRGL_CCMD_CREATE_OBJECT, VIRGL_OBJECT_SURFACE, 5));
   virgl_encoder_write_dword(ctx->cbuf, handle);
   virgl_encoder_write_res(ctx, res);
   virgl_encoder_write_dword(ctx->cbuf, templat->format);
   if (templat->texture->target == PIPE_BUFFER) {
      virgl_encoder_write_dword(ctx->cbuf, templat->u.buf.first_element);
      virgl_encoder_write_dword(ctx->cbuf, templat->u.buf.last_element);

   } else {
      virgl_encoder_write_dword(ctx->cbuf, templat->u.tex.level);
      virgl_encoder_write_dword(ctx->cbuf, templat->u.tex.first_layer | (templat->u.tex.last_layer << 16));
   }
   return 0;
}

int virgl_encoder_create_so_target(struct virgl_context *ctx,
                                  uint32_t handle,
                                  struct virgl_resource *res,
                                  unsigned buffer_offset,
                                  unsigned buffer_size)
{
   virgl_encoder_write_cmd_dword(ctx, VIRGL_CMD0(VIRGL_CCMD_CREATE_OBJECT, VIRGL_OBJECT_STREAMOUT_TARGET, 4));
   virgl_encoder_write_dword(ctx->cbuf, handle);
   virgl_encoder_write_res(ctx, res);
   virgl_encoder_write_dword(ctx->cbuf, buffer_offset);
   virgl_encoder_write_dword(ctx->cbuf, buffer_size);
   return 0;
}

static void virgl_encoder_iw_emit_header_1d(struct virgl_context *ctx,
                                           struct virgl_resource *res,
                                           unsigned level, unsigned usage,
                                           const struct pipe_box *box,
                                           unsigned stride, unsigned layer_stride)
{
   virgl_encoder_write_res(ctx, res);
   virgl_encoder_write_dword(ctx->cbuf, level);
   virgl_encoder_write_dword(ctx->cbuf, usage);
   virgl_encoder_write_dword(ctx->cbuf, stride);
   virgl_encoder_write_dword(ctx->cbuf, layer_stride);
   virgl_encoder_write_dword(ctx->cbuf, box->x);
   virgl_encoder_write_dword(ctx->cbuf, box->y);
   virgl_encoder_write_dword(ctx->cbuf, box->z);
   virgl_encoder_write_dword(ctx->cbuf, box->width);
   virgl_encoder_write_dword(ctx->cbuf, box->height);
   virgl_encoder_write_dword(ctx->cbuf, box->depth);
}

int virgl_encoder_inline_write(struct virgl_context *ctx,
                              struct virgl_resource *res,
                              unsigned level, unsigned usage,
                              const struct pipe_box *box,
                              const void *data, unsigned stride,
                              unsigned layer_stride)
{
   uint32_t size = (stride ? stride : box->width) * box->height;
   uint32_t length, thispass, left_bytes;
   struct pipe_box mybox = *box;

   length = 11 + (size + 3) / 4;
   
   if ((ctx->cbuf->cdw + length + 1) > VIRGL_MAX_CMDBUF_DWORDS) {
      if (box->height > 1 || box->depth > 1) {
         debug_printf("inline transfer failed due to multi dimensions and too large\n");
         assert(0);
      }
   }

   left_bytes = size;
   while (left_bytes) {
      if (ctx->cbuf->cdw + 12 > VIRGL_MAX_CMDBUF_DWORDS)
         ctx->base.flush(&ctx->base, NULL, 0);

      thispass = (VIRGL_MAX_CMDBUF_DWORDS - ctx->cbuf->cdw - 12) * 4;

      length = MIN2(thispass, left_bytes);

      mybox.width = length;
      virgl_encoder_write_cmd_dword(ctx, VIRGL_CMD0(VIRGL_CCMD_RESOURCE_INLINE_WRITE, 0, ((length + 3) / 4) + 11));
      virgl_encoder_iw_emit_header_1d(ctx, res, level, usage, &mybox, stride, layer_stride);
      virgl_encoder_write_block(ctx->cbuf, data, length);
      left_bytes -= length;
      mybox.x += length;
      data += length;
   }
   return 0;
}

int virgl_encoder_flush_frontbuffer(struct virgl_context *ctx,
                                   struct virgl_resource *res)
{
//   virgl_encoder_write_dword(ctx->cbuf, VIRGL_CMD0(VIRGL_CCMD_FLUSH_FRONTUBFFER, 0, 1));
//   virgl_encoder_write_dword(ctx->cbuf, res_handle);
   return 0;
}

int virgl_encode_sampler_state(struct virgl_context *ctx,
                              uint32_t handle,
                              const struct pipe_sampler_state *state)
{
   uint32_t tmp;
   int i;
   virgl_encoder_write_cmd_dword(ctx, VIRGL_CMD0(VIRGL_CCMD_CREATE_OBJECT, VIRGL_OBJECT_SAMPLER_STATE , 9));
   virgl_encoder_write_dword(ctx->cbuf, handle);

   tmp = state->wrap_s |
      state->wrap_t << 3 |
      state->wrap_r << 6 |
      state->min_img_filter << 9 |
      state->min_mip_filter << 11 |
      state->mag_img_filter << 13 |
      state->compare_mode << 15 |
      state->compare_func << 16;

   virgl_encoder_write_dword(ctx->cbuf, tmp);
   virgl_encoder_write_dword(ctx->cbuf, uif(state->lod_bias));
   virgl_encoder_write_dword(ctx->cbuf, uif(state->min_lod));
   virgl_encoder_write_dword(ctx->cbuf, uif(state->max_lod));
   for (i = 0; i <  4; i++)
      virgl_encoder_write_dword(ctx->cbuf, state->border_color.ui[i]);
   return 0;
}


int virgl_encode_sampler_view(struct virgl_context *ctx,
                             uint32_t handle,
                             struct virgl_resource *res,
                             const struct pipe_sampler_view *state)
{
   virgl_encoder_write_cmd_dword(ctx, VIRGL_CMD0(VIRGL_CCMD_CREATE_OBJECT, VIRGL_OBJECT_SAMPLER_VIEW, 6));
   virgl_encoder_write_dword(ctx->cbuf, handle);
   virgl_encoder_write_res(ctx, res);
   virgl_encoder_write_dword(ctx->cbuf, state->format);
   if (res->u.b.target == PIPE_BUFFER) {
      virgl_encoder_write_dword(ctx->cbuf, state->u.buf.first_element);
      virgl_encoder_write_dword(ctx->cbuf, state->u.buf.last_element);
   } else {
      virgl_encoder_write_dword(ctx->cbuf, state->u.tex.first_layer | state->u.tex.last_layer << 16);
      virgl_encoder_write_dword(ctx->cbuf, state->u.tex.first_level | state->u.tex.last_level << 8);
   }
   virgl_encoder_write_dword(ctx->cbuf, state->swizzle_r | (state->swizzle_g << 3) | (state->swizzle_b << 6) | (state->swizzle_a << 9));
   return 0;
}

int virgl_encode_set_sampler_views(struct virgl_context *ctx,
                                  uint32_t shader_type,
                                  uint32_t start_slot,
                                  uint32_t num_views,
                                  struct virgl_sampler_view **views)
{
   int i;
   virgl_encoder_write_cmd_dword(ctx, VIRGL_CMD0(VIRGL_CCMD_SET_SAMPLER_VIEWS, 0, num_views + 2));
   virgl_encoder_write_dword(ctx->cbuf, shader_type);
   virgl_encoder_write_dword(ctx->cbuf, start_slot);
   for (i = 0; i < num_views; i++) {
      uint32_t handle = views[i] ? views[i]->handle : 0;
      virgl_encoder_write_dword(ctx->cbuf, handle);

      if (views[i])
         virgl_encoder_attach_res(ctx, (struct virgl_resource *)views[i]->base.texture);
   }
   return 0;
}

int virgl_encode_bind_sampler_states(struct virgl_context *ctx,
                                    uint32_t shader_type,
                                    uint32_t start_slot,
                                    uint32_t num_handles,
                                    uint32_t *handles)
{
   int i;
   virgl_encoder_write_cmd_dword(ctx, VIRGL_CMD0(VIRGL_CCMD_BIND_SAMPLER_STATES, 0, num_handles + 2));
   virgl_encoder_write_dword(ctx->cbuf, shader_type);
   virgl_encoder_write_dword(ctx->cbuf, start_slot);
   for (i = 0; i < num_handles; i++)
      virgl_encoder_write_dword(ctx->cbuf, handles[i]);
   return 0;
}

int virgl_encoder_write_constant_buffer(struct virgl_context *ctx,
                                       uint32_t shader,
                                       uint32_t index,
                                       uint32_t size,
                                       const void *data)
{
   virgl_encoder_write_cmd_dword(ctx, VIRGL_CMD0(VIRGL_CCMD_SET_CONSTANT_BUFFER, 0, size + 2));
   virgl_encoder_write_dword(ctx->cbuf, shader);
   virgl_encoder_write_dword(ctx->cbuf, index);
   if (data)
      virgl_encoder_write_block(ctx->cbuf, data, size * 4);
   return 0;
}

int virgl_encoder_set_stencil_ref(struct virgl_context *ctx,
                                 const struct pipe_stencil_ref *ref)
{
   virgl_encoder_write_cmd_dword(ctx, VIRGL_CMD0(VIRGL_CCMD_SET_STENCIL_REF, 0, 1));
   virgl_encoder_write_dword(ctx->cbuf, (ref->ref_value[0] | (ref->ref_value[1] << 8)));
   return 0;
}

int virgl_encoder_set_blend_color(struct virgl_context *ctx,
                                 const struct pipe_blend_color *color)
{
   int i;
   virgl_encoder_write_cmd_dword(ctx, VIRGL_CMD0(VIRGL_CCMD_SET_BLEND_COLOR, 0, 4));
   for (i = 0; i < 4; i++)
      virgl_encoder_write_dword(ctx->cbuf, uif(color->color[i]));
   return 0;
}

int virgl_encoder_set_scissor_state(struct virgl_context *ctx,
                                   const struct pipe_scissor_state *ss)
{
   virgl_encoder_write_cmd_dword(ctx, VIRGL_CMD0(VIRGL_CCMD_SET_SCISSOR_STATE, 0, 2));
   virgl_encoder_write_dword(ctx->cbuf, (ss->minx | ss->miny << 16));
   virgl_encoder_write_dword(ctx->cbuf, (ss->maxx | ss->maxy << 16));
   return 0;
}

void virgl_encoder_set_polygon_stipple(struct virgl_context *ctx,
                                      const struct pipe_poly_stipple *ps)
{
   int i;
   virgl_encoder_write_cmd_dword(ctx, VIRGL_CMD0(VIRGL_CCMD_SET_POLYGON_STIPPLE, 0, 32));
   for (i = 0; i < 32; i++) {
      virgl_encoder_write_dword(ctx->cbuf, ps->stipple[i]);
   }
}

void virgl_encoder_set_sample_mask(struct virgl_context *ctx,
                                  unsigned sample_mask)
{
   virgl_encoder_write_cmd_dword(ctx, VIRGL_CMD0(VIRGL_CCMD_SET_SAMPLE_MASK, 0, 1));
   virgl_encoder_write_dword(ctx->cbuf, sample_mask);
}

void virgl_encoder_set_clip_state(struct virgl_context *ctx,
                                 const struct pipe_clip_state *clip)
{
   int i, j;
   virgl_encoder_write_cmd_dword(ctx, VIRGL_CMD0(VIRGL_CCMD_SET_CLIP_STATE, 0, 32));
   for (i = 0; i < PIPE_MAX_CLIP_PLANES; i++) {
      for (j = 0; j < 4; j++) {
         virgl_encoder_write_dword(ctx->cbuf, uif(clip->ucp[i][j]));
      }
   }   
}

int virgl_encode_resource_copy_region(struct virgl_context *ctx,
                                     struct virgl_resource *dst_res,
                                     unsigned dst_level,
                                     unsigned dstx, unsigned dsty, unsigned dstz,
                                     struct virgl_resource *src_res,
                                     unsigned src_level,
                                     const struct pipe_box *src_box)
{
   virgl_encoder_write_cmd_dword(ctx, VIRGL_CMD0(VIRGL_CCMD_RESOURCE_COPY_REGION, 0, 13));
   virgl_encoder_write_res(ctx, dst_res);
   virgl_encoder_write_dword(ctx->cbuf, dst_level);
   virgl_encoder_write_dword(ctx->cbuf, dstx);
   virgl_encoder_write_dword(ctx->cbuf, dsty);
   virgl_encoder_write_dword(ctx->cbuf, dstz);
   virgl_encoder_write_res(ctx, src_res);
   virgl_encoder_write_dword(ctx->cbuf, src_level);
   virgl_encoder_write_dword(ctx->cbuf, src_box->x);
   virgl_encoder_write_dword(ctx->cbuf, src_box->y);
   virgl_encoder_write_dword(ctx->cbuf, src_box->z);
   virgl_encoder_write_dword(ctx->cbuf, src_box->width);
   virgl_encoder_write_dword(ctx->cbuf, src_box->height);
   virgl_encoder_write_dword(ctx->cbuf, src_box->depth);
   return 0;
}

int virgl_encode_blit(struct virgl_context *ctx,
                     struct virgl_resource *dst_res,
                     struct virgl_resource *src_res,
                     const struct pipe_blit_info *blit)
{
   virgl_encoder_write_cmd_dword(ctx, VIRGL_CMD0(VIRGL_CCMD_BLIT, 0, 23));
   virgl_encoder_write_dword(ctx->cbuf, blit->mask);
   virgl_encoder_write_dword(ctx->cbuf, blit->filter);
   virgl_encoder_write_dword(ctx->cbuf, blit->scissor_enable);
   virgl_encoder_write_dword(ctx->cbuf, (blit->scissor.minx | blit->scissor.miny << 16));
   virgl_encoder_write_dword(ctx->cbuf, (blit->scissor.maxx | blit->scissor.maxy << 16));

   virgl_encoder_write_res(ctx, dst_res);
   virgl_encoder_write_dword(ctx->cbuf, blit->dst.level);
   virgl_encoder_write_dword(ctx->cbuf, blit->dst.format);
   virgl_encoder_write_dword(ctx->cbuf, blit->dst.box.x);
   virgl_encoder_write_dword(ctx->cbuf, blit->dst.box.y);
   virgl_encoder_write_dword(ctx->cbuf, blit->dst.box.z);
   virgl_encoder_write_dword(ctx->cbuf, blit->dst.box.width);
   virgl_encoder_write_dword(ctx->cbuf, blit->dst.box.height);
   virgl_encoder_write_dword(ctx->cbuf, blit->dst.box.depth);

   virgl_encoder_write_res(ctx, src_res);
   virgl_encoder_write_dword(ctx->cbuf, blit->src.level);
   virgl_encoder_write_dword(ctx->cbuf, blit->src.format);
   virgl_encoder_write_dword(ctx->cbuf, blit->src.box.x);
   virgl_encoder_write_dword(ctx->cbuf, blit->src.box.y);
   virgl_encoder_write_dword(ctx->cbuf, blit->src.box.z);
   virgl_encoder_write_dword(ctx->cbuf, blit->src.box.width);
   virgl_encoder_write_dword(ctx->cbuf, blit->src.box.height);
   virgl_encoder_write_dword(ctx->cbuf, blit->src.box.depth);
   return 0;
}

int virgl_encoder_create_query(struct virgl_context *ctx,
                              uint32_t handle,
                              uint query_type,
                              struct virgl_resource *res,
                              uint32_t offset)
{
   virgl_encoder_write_cmd_dword(ctx, VIRGL_CMD0(VIRGL_CCMD_CREATE_OBJECT, VIRGL_OBJECT_QUERY, 4));
   virgl_encoder_write_dword(ctx->cbuf, handle);
   virgl_encoder_write_dword(ctx->cbuf, query_type);
   virgl_encoder_write_dword(ctx->cbuf, offset);
   virgl_encoder_write_res(ctx, res);
   return 0;
}

int virgl_encoder_begin_query(struct virgl_context *ctx,
                             uint32_t handle)
{
   virgl_encoder_write_cmd_dword(ctx, VIRGL_CMD0(VIRGL_CCMD_BEGIN_QUERY, 0, 1));
   virgl_encoder_write_dword(ctx->cbuf, handle);
   return 0;
}

int virgl_encoder_end_query(struct virgl_context *ctx,
                           uint32_t handle)
{
   virgl_encoder_write_cmd_dword(ctx, VIRGL_CMD0(VIRGL_CCMD_END_QUERY, 0, 1));
   virgl_encoder_write_dword(ctx->cbuf, handle);
   return 0;
}

int virgl_encoder_get_query_result(struct virgl_context *ctx,
                                  uint32_t handle, boolean wait)
{
   virgl_encoder_write_cmd_dword(ctx, VIRGL_CMD0(VIRGL_CCMD_GET_QUERY_RESULT, 0, 2));
   virgl_encoder_write_dword(ctx->cbuf, handle);
   virgl_encoder_write_dword(ctx->cbuf, wait ? 1 : 0);
   return 0;
}

int virgl_encoder_render_condition(struct virgl_context *ctx,
                                  uint32_t handle, boolean condition,
                                  uint mode)
{
   virgl_encoder_write_cmd_dword(ctx, VIRGL_CMD0(VIRGL_CCMD_SET_RENDER_CONDITION, 0, 3));
   virgl_encoder_write_dword(ctx->cbuf, handle);
   virgl_encoder_write_dword(ctx->cbuf, condition);
   virgl_encoder_write_dword(ctx->cbuf, mode);
   return 0;
}

int virgl_encoder_set_so_targets(struct virgl_context *ctx,
                                unsigned num_targets,
                                struct pipe_stream_output_target **targets,
                                unsigned append_bitmask)
{
   int i;

   virgl_encoder_write_cmd_dword(ctx, VIRGL_CMD0(VIRGL_CCMD_SET_STREAMOUT_TARGETS, 0, num_targets + 1));
   virgl_encoder_write_dword(ctx->cbuf, append_bitmask);
   for (i = 0; i < num_targets; i++) {
      struct virgl_so_target *tg = (struct virgl_so_target *)targets[i];
      virgl_encoder_write_dword(ctx->cbuf, tg->handle);
   }
   return 0;
}

int virgl_encoder_set_query_state(struct virgl_context *ctx,
                                 boolean query_enabled)
{
   virgl_encoder_write_cmd_dword(ctx, VIRGL_CMD0(VIRGL_CCMD_SET_QUERY_STATE, 0, 1));
   virgl_encoder_write_dword(ctx->cbuf, query_enabled ? (1 << 0) : 0);
   return 0;
}
