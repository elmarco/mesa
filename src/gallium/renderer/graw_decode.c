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
   uint32_t surf_handle = ctx->ds->buf[ctx->ds->buf_offset + 2];
   grend_set_framebuffer_state(ctx->grctx, nr_cbufs, surf_handle);
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
   grend_draw_vbo(gdctx->grctx, &info);
}

static void graw_decode_create_blend(struct grend_decode_ctx *ctx, uint32_t handle, uint16_t length)
{
   struct pipe_blend_state *blend_state = CALLOC_STRUCT(pipe_blend_state);
   
   graw_object_insert(blend_state, sizeof(struct pipe_blend_state), handle,
                      GRAW_OBJECT_BLEND);
}

static void graw_decode_create_surface(struct grend_decode_ctx *ctx, uint32_t handle)
{
   uint32_t res_handle;

   res_handle = ctx->ds->buf[ctx->ds->buf_offset + 2];

   grend_create_surface(ctx->grctx, handle, res_handle);
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
      //   graw_object_bind_blend(ctx, handle);
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
   }
      

}

static void graw_decode_destroy_object(struct grend_decode_ctx *ctx)
{

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

      fprintf(stderr,"cmd is %d (obj %d) len %d\n", header & 0xff, (header >> 8 & 0xff), (header >> 16));
      
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
      case GRAW_FLUSH_FRONTBUFFER:
         break;
      }
      gdctx->ds->buf_offset += (header >> 16) + 1;
      
   }

}

