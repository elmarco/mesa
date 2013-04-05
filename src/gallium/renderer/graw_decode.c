#include <stdint.h>
#include <string.h>
#include <stdio.h>

#include "util/u_memory.h"
#include "pipe/p_state.h"
#include "pipe/p_shader_tokens.h"
#include "graw_protocol.h"
#include "graw_decode.h"
#include "graw_renderer.h"
#include "graw_object.h"
#include "tgsi/tgsi_text.h"

/* decode side */
#define DECODE_MAX_TOKENS 300

struct grend_decode_ctx {
   struct graw_decoder_state *ds;
   struct grend_context *grctx;
};

static struct grend_decode_ctx dec_ctx;

struct grend_decode_ctx *gdctx = &dec_ctx;

static int graw_decode_create_shader(struct grend_decode_ctx *ctx, uint32_t type,
                              uint32_t handle,
   uint16_t length)
{
   struct pipe_shader_state *state = CALLOC_STRUCT(pipe_shader_state);
   struct tgsi_token *tokens;

   if (!state)
      return NULL;
   
   tokens = calloc(DECODE_MAX_TOKENS, sizeof(struct tgsi_token));
   if (!tokens) {
      free(state);
      return -1;
   }

   fprintf(stderr,"shader\n%s\n", &ctx->ds->buf[ctx->ds->buf_offset + 2]);
   if (!tgsi_text_translate(&ctx->ds->buf[ctx->ds->buf_offset + 2], tokens, DECODE_MAX_TOKENS)) {
      fprintf(stderr,"failed to translate\n");
      free(tokens);
      free(state);
      return -1;
   }

   state->tokens = tokens;

   if (type == GRAW_OBJECT_FS)
      grend_create_fs(ctx->grctx, handle, state);
   else
      grend_create_vs(ctx->grctx, handle, state);

   free(tokens);
   free(state);
   return 0;
}

static void graw_decode_set_framebuffer_state(struct grend_decode_ctx *ctx)
{
   uint32_t nr_cbufs = ctx->ds->buf[ctx->ds->buf_offset + 1];
   uint32_t zsurf_handle = ctx->ds->buf[ctx->ds->buf_offset + 2];
   uint32_t surf_handle[8];
   int i;

   for (i = 0; i < nr_cbufs; i++)
      surf_handle[i] = ctx->ds->buf[ctx->ds->buf_offset + 3 + i];
   grend_set_framebuffer_state(ctx->grctx, nr_cbufs, surf_handle, zsurf_handle);
}

static void graw_decode_clear(struct grend_decode_ctx *ctx)
{
   union pipe_color_union color;
   double depth;
   unsigned stencil, buffers;
   int i;
   int index = ctx->ds->buf_offset + 1;
   
   buffers = ctx->ds->buf[index++];
   for (i = 0; i < 4; i++)
      color.ui[i] = ctx->ds->buf[index++];
   depth = *(double *)(uint64_t *)(&ctx->ds->buf[index]);
   index += 2;
   stencil = ctx->ds->buf[index++];

   grend_clear(ctx->grctx, buffers, &color, depth, stencil);
}

static float fui(unsigned int ui)
{
   union { float f; unsigned int ui; } myuif;
   myuif.ui = ui;
   return myuif.f;
}

static void graw_decode_set_viewport_state(struct grend_decode_ctx *ctx)
{
   struct pipe_viewport_state vps;
   int i;

   for (i = 0; i < 4; i++)
      vps.scale[i] = fui(ctx->ds->buf[ctx->ds->buf_offset + 1 + i]);
   for (i = 0; i < 4; i++)
      vps.translate[i] = fui(ctx->ds->buf[ctx->ds->buf_offset + 5 + i]);
   
   grend_set_viewport_state(ctx->grctx, &vps);
}

static void graw_decode_set_index_buffer(struct grend_decode_ctx *ctx)
{
   int offset = ctx->ds->buf_offset;
   grend_set_index_buffer(ctx->grctx, ctx->ds->buf[offset + 1],
                          ctx->ds->buf[offset + 2],
                          ctx->ds->buf[offset + 3]);
}

static void graw_decode_set_constant_buffer(struct grend_decode_ctx *ctx, uint16_t length)
{
   int offset = ctx->ds->buf_offset;
   uint32_t shader = ctx->ds->buf[offset + 1];
   uint32_t index = ctx->ds->buf[offset + 2];
   int nc = (length - 2);
   grend_set_constants(ctx->grctx, shader, index, nc, &ctx->ds->buf[offset + 3]);
}

static void graw_decode_set_vertex_buffers(struct grend_decode_ctx *ctx, uint16_t length)
{
   int num_vbo;
   int i;
   num_vbo = (length / 3);

   for (i = 0; i < num_vbo; i++) {
      int element_offset = ctx->ds->buf_offset + 1 + (i * 3);
      grend_set_single_vbo(ctx->grctx, i,
                           ctx->ds->buf[element_offset],
                           ctx->ds->buf[element_offset + 1],
                           ctx->ds->buf[element_offset + 2]);
   }
   grend_set_num_vbo(ctx->grctx, num_vbo);
}

static void graw_decode_set_fragment_sampler_views(struct grend_decode_ctx *ctx, uint16_t length)
{
   int num_samps;
   int i;
   num_samps = length;
   for (i = 0; i < num_samps; i++) {
      uint32_t handle = ctx->ds->buf[ctx->ds->buf_offset + 1 + i];
      grend_set_single_fs_sampler_view(ctx->grctx, i, handle);
   }
   grend_set_num_fs_sampler_views(ctx->grctx, num_samps);
}

static void graw_decode_set_vertex_sampler_views(struct grend_decode_ctx *ctx, uint16_t length)
{
   int num_samps;
   int i;
   num_samps = length;
   for (i = 0; i < num_samps; i++) {
      uint32_t handle = ctx->ds->buf[ctx->ds->buf_offset + 1 + i];
      grend_set_single_vs_sampler_view(ctx->grctx, i, handle);
   }
   grend_set_num_vs_sampler_views(ctx->grctx, num_samps);
}

static void graw_decode_resource_inline_write(struct grend_decode_ctx *ctx, uint16_t length)
{
   struct pipe_box box;
   uint32_t res_handle = ctx->ds->buf[ctx->ds->buf_offset + 1];
   uint32_t level, usage, stride, layer_stride;
   void *data;

   level = ctx->ds->buf[ctx->ds->buf_offset + 2];
   usage = ctx->ds->buf[ctx->ds->buf_offset + 3];
   stride = ctx->ds->buf[ctx->ds->buf_offset + 4];
   layer_stride = ctx->ds->buf[ctx->ds->buf_offset + 5];
   box.x = ctx->ds->buf[ctx->ds->buf_offset + 6];
   box.y = ctx->ds->buf[ctx->ds->buf_offset + 7];
   box.z = ctx->ds->buf[ctx->ds->buf_offset + 8];
   box.width = ctx->ds->buf[ctx->ds->buf_offset + 9];
   box.height = ctx->ds->buf[ctx->ds->buf_offset + 10];
   box.depth = ctx->ds->buf[ctx->ds->buf_offset + 11];

   data = &ctx->ds->buf[ctx->ds->buf_offset + 12];
   grend_transfer_inline_write(ctx->grctx, res_handle, level,
                               usage, &box, data, stride, layer_stride);
                               
}

static void graw_decode_draw_vbo(struct grend_decode_ctx *ctx)
{
   struct pipe_draw_info info;

   memset(&info, 0, sizeof(struct pipe_draw_info));

   info.start = ctx->ds->buf[ctx->ds->buf_offset + 1];
   info.count = ctx->ds->buf[ctx->ds->buf_offset + 2];
   info.mode = ctx->ds->buf[ctx->ds->buf_offset + 3];
   info.indexed = ctx->ds->buf[ctx->ds->buf_offset + 4];
   info.instance_count = ctx->ds->buf[ctx->ds->buf_offset + 5];
   grend_draw_vbo(gdctx->grctx, &info);
}

static void graw_decode_create_blend(struct grend_decode_ctx *ctx, uint32_t handle, uint16_t length)
{
   struct pipe_blend_state *blend_state = CALLOC_STRUCT(pipe_blend_state);
   uint32_t tmp;
   int i;
   tmp = ctx->ds->buf[ctx->ds->buf_offset + 2];
   blend_state->independent_blend_enable = (tmp & 1);
   blend_state->logicop_enable = (tmp >> 1) & 0x1;
   blend_state->dither = (tmp >> 2) & 0x1;
   blend_state->alpha_to_coverage = (tmp >> 3) & 0x1;
   blend_state->alpha_to_one = (tmp >> 4) & 0x1;

   tmp = ctx->ds->buf[ctx->ds->buf_offset + 3];
   blend_state->logicop_func = tmp & 0xf;

   for (i = 0; i < PIPE_MAX_COLOR_BUFS; i++) {
      tmp = ctx->ds->buf[ctx->ds->buf_offset + 4 + i];
      blend_state->rt[i].blend_enable = tmp & 0x1;
      blend_state->rt[i].rgb_func = (tmp >> 1) & 0x7;
      blend_state->rt[i].rgb_src_factor = (tmp >> 4) & 0x1f;
      blend_state->rt[i].rgb_dst_factor = (tmp >> 9) & 0x1f;
      blend_state->rt[i].alpha_func = (tmp >> 14) & 0x7;
      blend_state->rt[i].alpha_src_factor = (tmp >> 17) & 0x1f;
      blend_state->rt[i].alpha_dst_factor = (tmp >> 22) & 0x1f;
      blend_state->rt[i].colormask = (tmp >> 27) & 0xf;
   }

   graw_object_insert(blend_state, sizeof(struct pipe_blend_state), handle,
                      GRAW_OBJECT_BLEND);
}

static void graw_decode_create_dsa(struct grend_decode_ctx *ctx, uint32_t handle, uint16_t length)
{
   int i;
   struct pipe_depth_stencil_alpha_state *dsa_state = CALLOC_STRUCT(pipe_depth_stencil_alpha_state);
   uint32_t tmp;
   
   tmp = ctx->ds->buf[ctx->ds->buf_offset + 2];
   dsa_state->depth.enabled = tmp & 0x1;
   dsa_state->depth.writemask = (tmp >> 1) & 0x1;
   dsa_state->depth.func = (tmp >> 2) & 0x7;

   dsa_state->alpha.enabled = (tmp >> 8) & 0x1;
   dsa_state->alpha.func = (tmp >> 9) & 0x7;

   for (i = 0; i < 2; i++) {
      tmp = ctx->ds->buf[ctx->ds->buf_offset + 3 + i];
      dsa_state->stencil[i].enabled = tmp & 0x1;
      dsa_state->stencil[i].func = (tmp >> 1) & 0x7;
      dsa_state->stencil[i].fail_op = (tmp >> 4) & 0x7;
      dsa_state->stencil[i].zpass_op = (tmp >> 7) & 0x7;
      dsa_state->stencil[i].zfail_op = (tmp >> 10) & 0x7;
      dsa_state->stencil[i].valuemask = (tmp >> 13) & 0xff;
      dsa_state->stencil[i].writemask = (tmp >> 21) & 0xff;
   }

   tmp = ctx->ds->buf[ctx->ds->buf_offset + 5];
   dsa_state->alpha.ref_value = fui(tmp);

   graw_object_insert(dsa_state, sizeof(struct pipe_depth_stencil_alpha_state), handle,
                      GRAW_OBJECT_DSA);
}

static void graw_decode_create_rasterizer(struct grend_decode_ctx *ctx, uint32_t handle, uint16_t length)
{
   struct pipe_rasterizer_state *rasterizer_state = CALLOC_STRUCT(pipe_rasterizer_state);
   uint32_t tmp;

   tmp = ctx->ds->buf[ctx->ds->buf_offset + 2];
   rasterizer_state->flatshade = tmp & (1 << 0);
   rasterizer_state->depth_clip = tmp & (1 << 1);
   rasterizer_state->gl_rasterization_rules = tmp & (1 << 2);
   graw_object_insert(rasterizer_state, sizeof(struct pipe_rasterizer_state), handle,
                      GRAW_OBJECT_RASTERIZER);
}

static void graw_decode_create_surface(struct grend_decode_ctx *ctx, uint32_t handle)
{
   uint32_t res_handle;

   res_handle = ctx->ds->buf[ctx->ds->buf_offset + 2];

   grend_create_surface(ctx->grctx, handle, res_handle);
}

static void graw_decode_create_sampler_state(struct grend_decode_ctx *ctx, uint32_t handle, uint16_t length)
{
   struct pipe_sampler_state *state = CALLOC_STRUCT(pipe_sampler_state);

   uint32_t tmp;

   tmp = ctx->ds->buf[ctx->ds->buf_offset + 2];
   state->wrap_s = tmp & 0x7;
   state->wrap_t = (tmp >> 3) & 0x7;
   state->wrap_r = (tmp >> 6) & 0x7;
   state->min_img_filter = (tmp >> 9) & 0x3;
   state->min_mip_filter = (tmp >> 11) & 0x3;
   state->mag_img_filter = (tmp >> 13) & 0x3;
   graw_object_insert(state, sizeof(struct pipe_sampler_state), handle,
                      GRAW_OBJECT_SAMPLER_STATE);
}

static void graw_decode_create_ve(struct grend_decode_ctx *ctx, uint32_t handle, uint16_t length)
{
   struct pipe_vertex_element *ve;
   int num_elements;
   int i;

   num_elements = (length - 1) / 4;
   ve = calloc(num_elements, sizeof(struct pipe_vertex_element));
   if (!ve)
      return;
      
   for (i = 0; i < num_elements; i++) {
      uint32_t element_offset = ctx->ds->buf_offset + 2 + (i * 4);
      ve[i].src_offset = ctx->ds->buf[element_offset];
      ve[i].instance_divisor = ctx->ds->buf[element_offset + 1];
      ve[i].vertex_buffer_index = ctx->ds->buf[element_offset + 2];
      ve[i].src_format = ctx->ds->buf[element_offset + 3];
   }

   grend_create_vertex_elements_state(ctx->grctx, handle, num_elements,
                                      ve);
}

static void graw_decode_create_object(struct grend_decode_ctx *ctx)
{
   uint32_t header = ctx->ds->buf[ctx->ds->buf_offset];
   uint32_t handle = ctx->ds->buf[ctx->ds->buf_offset+1];
   uint16_t length;
   uint8_t obj_type = (header >> 8) & 0xff;

   length = header >> 16;

   switch (obj_type){
   case GRAW_OBJECT_BLEND:
      graw_decode_create_blend(ctx, handle, length);
      break;
   case GRAW_OBJECT_DSA:
      graw_decode_create_dsa(ctx, handle, length);
      break;
   case GRAW_OBJECT_RASTERIZER:
      graw_decode_create_rasterizer(ctx, handle, length);
      break;
   case GRAW_OBJECT_VS:
   case GRAW_OBJECT_FS:
      graw_decode_create_shader(ctx, obj_type, handle, length);
      break;
   case GRAW_OBJECT_VERTEX_ELEMENTS:
      graw_decode_create_ve(ctx, handle, length);
      break;
   case GRAW_RESOURCE:
      break;
   case GRAW_SURFACE:
      graw_decode_create_surface(ctx, handle);
      break;
   case GRAW_OBJECT_SAMPLER_VIEW:
      break;
   case GRAW_OBJECT_SAMPLER_STATE:
      graw_decode_create_sampler_state(ctx, handle, length);
      break;
   }
}

static void graw_decode_bind_object(struct grend_decode_ctx *ctx)
{
   uint32_t header = ctx->ds->buf[ctx->ds->buf_offset];
   uint32_t handle = ctx->ds->buf[ctx->ds->buf_offset+1];
   uint16_t length;
   uint8_t obj_type = (header >> 8) & 0xff;

   length = header >> 16;

   switch (obj_type) {
   case GRAW_OBJECT_BLEND:
      grend_object_bind_blend(ctx->grctx, handle);
      break;
   case GRAW_OBJECT_DSA:
      grend_object_bind_dsa(ctx->grctx, handle);
      break;
   case GRAW_OBJECT_RASTERIZER:
      grend_object_bind_rasterizer(ctx->grctx, handle);
      break;
   case GRAW_OBJECT_VS:
      grend_bind_vs(ctx->grctx, handle);
      break;
   case GRAW_OBJECT_FS:
      grend_bind_fs(ctx->grctx, handle);
      break;
   case GRAW_OBJECT_VERTEX_ELEMENTS:
      grend_bind_vertex_elements_state(ctx->grctx, handle);
      break;
   case GRAW_OBJECT_SAMPLER_STATE: {
      grend_object_bind_sampler_states(ctx->grctx, length, &ctx->ds->buf[ctx->ds->buf_offset + 1]);

   }
      break;
   }
}

static void graw_decode_destroy_object(struct grend_decode_ctx *ctx)
{
   uint32_t handle = ctx->ds->buf[ctx->ds->buf_offset+1];
   graw_object_destroy(handle, 0);
}

void graw_reset_decode(void)
{
   free(gdctx->grctx);
   gdctx->grctx = NULL;
   gdctx->ds = NULL;
}

static void graw_decode_set_stencil_ref(struct grend_decode_ctx *ctx)
{
   struct pipe_stencil_ref ref;
   uint32_t val = ctx->ds->buf[ctx->ds->buf_offset + 1];
   ref.ref_value[0] = val & 0xff;
   ref.ref_value[1] = (val >> 8) & 0xff;
   grend_set_stencil_ref(gdctx->grctx, &ref);
}

static void graw_decode_set_blend_color(struct grend_decode_ctx *ctx)
{
   struct pipe_blend_color color;
   int i;
   
   for (i = 0; i < 4; i++)
      color.color[i] = fui(ctx->ds->buf[ctx->ds->buf_offset + 1 + i]);

   grend_set_blend_color(gdctx->grctx, &color);
}

void graw_decode_block(uint32_t *block, int ndw)
{
   struct graw_decoder_state ds;
   int i = 0;

   gdctx->ds = &ds;
   if (!gdctx->grctx) {
      gdctx->grctx = grend_create_context();
   }
   gdctx->ds->buf = block;
   gdctx->ds->buf_total = ndw;
   gdctx->ds->buf_offset = 0;

   while (gdctx->ds->buf_offset < gdctx->ds->buf_total) {
      uint32_t header = gdctx->ds->buf[gdctx->ds->buf_offset];

      fprintf(stderr,"[%d] cmd is %d (obj %d) len %d\n", gdctx->ds->buf_offset, header & 0xff, (header >> 8 & 0xff), (header >> 16));
      
      switch (header & 0xff) {
      case GRAW_CREATE_OBJECT:
         graw_decode_create_object(gdctx);
         break;
      case GRAW_BIND_OBJECT:
         graw_decode_bind_object(gdctx);
         break;
      case GRAW_DESTROY_OBJECT:
         graw_decode_destroy_object(gdctx);
         break;
      case GRAW_CLEAR:
         graw_decode_clear(gdctx);
         break;
      case GRAW_DRAW_VBO:
         graw_decode_draw_vbo(gdctx);
         break;
      case GRAW_SET_FRAMEBUFFER_STATE:
         graw_decode_set_framebuffer_state(gdctx);
         break;
      case GRAW_SET_VERTEX_BUFFERS:
         graw_decode_set_vertex_buffers(gdctx, header >> 16);
         break;
      case GRAW_RESOURCE_INLINE_WRITE:
         graw_decode_resource_inline_write(gdctx, header >> 16);
         break;
      case GRAW_SET_VIEWPORT_STATE:
         graw_decode_set_viewport_state(gdctx);
         break;
      case GRAW_SET_FRAGMENT_SAMPLER_VIEWS:
         graw_decode_set_fragment_sampler_views(gdctx, header >> 16);
         break;
      case GRAW_SET_INDEX_BUFFER:
         graw_decode_set_index_buffer(gdctx);
         break;
      case GRAW_SET_CONSTANT_BUFFER:
         graw_decode_set_constant_buffer(gdctx, header >> 16);
         break;
      case GRAW_SET_VERTEX_SAMPLER_VIEWS:
         graw_decode_set_vertex_sampler_views(gdctx, header >> 16);
         break;
      case GRAW_SET_STENCIL_REF:
         graw_decode_set_stencil_ref(gdctx);
         break;
      case GRAW_SET_BLEND_COLOR:
         graw_decode_set_blend_color(gdctx);
         break;
      }
      gdctx->ds->buf_offset += (header >> 16) + 1;
      
   }

}

void graw_decode_transfer(uint32_t *data, uint32_t ndw)
{
   uint32_t handle = data[0];
   struct pipe_box box, transfer_box;
   int level;
   box.x = data[1];
   box.y = data[2];
   box.z = data[3];
   box.width = data[4];
   box.height = data[5];
   box.depth = data[6];

   transfer_box.x = data[7];
   transfer_box.y = data[8];
   transfer_box.z = data[9];
   transfer_box.width = data[10];
   transfer_box.height = data[11];
   transfer_box.depth = data[12];
   level = data[13];

   graw_renderer_transfer_write(handle, level, &transfer_box, &box, data+14);
}

void graw_decode_get_transfer(uint32_t *data, uint32_t ndw)
{
   uint32_t handle = data[0];
   struct pipe_box box;


   box.x = data[1];
   box.y = data[2];
   box.z = data[3];
   box.width = data[4];
   box.height = data[5];
   box.depth = data[6];

   graw_renderer_transfer_send(handle, &box, NULL);
}
