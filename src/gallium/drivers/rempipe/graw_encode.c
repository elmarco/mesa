#include <stdint.h>
#include <string.h>

#include "util/u_memory.h"
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

int graw_encode_bind_object(struct graw_encoder_state *enc,
			    uint32_t handle, uint32_t object)
{
   graw_encoder_write_dword(enc, GRAW_CMD0(GRAW_BIND_OBJECT, object, 1));
   graw_encoder_write_dword(enc, handle);
}

int graw_encode_delete_object(struct graw_encoder_state *enc,
			      uint32_t handle, uint32_t object)
{
   graw_encoder_write_dword(enc, GRAW_CMD0(GRAW_DESTROY_OBJECT, object, 1));
   graw_encoder_write_dword(enc, handle);
}

int graw_encode_blend_state(struct graw_encoder_state *enc,
                            uint32_t handle,
                            struct pipe_blend_state *blend_state)
{
   uint32_t tmp;
   int i;

   graw_encoder_write_dword(enc, GRAW_CMD0(GRAW_CREATE_OBJECT, GRAW_OBJECT_BLEND, 2 + 1 + PIPE_MAX_COLOR_BUFS));
   graw_encoder_write_dword(enc, handle);

   tmp = (blend_state->independent_blend_enable << 0) |
      (blend_state->logicop_enable << 1) |
      (blend_state->dither << 2) |
      (blend_state->alpha_to_coverage << 3) |
      (blend_state->alpha_to_one << 4);

   graw_encoder_write_dword(enc, tmp);

   tmp = blend_state->logicop_func << 0;
   graw_encoder_write_dword(enc, tmp);

   for (i = 0; i < PIPE_MAX_COLOR_BUFS; i++) {
      tmp = ((blend_state->rt[i].blend_enable << 0) |
             (blend_state->rt[i].rgb_func << 1) |
             (blend_state->rt[i].rgb_src_factor << 4) |
             (blend_state->rt[i].rgb_dst_factor << 9) |
             (blend_state->rt[i].alpha_func << 14) |
             (blend_state->rt[i].alpha_src_factor << 17) |
             (blend_state->rt[i].alpha_dst_factor << 22) |
             (blend_state->rt[i].colormask << 27));
      graw_encoder_write_dword(enc, tmp);
   }
   return 0;
}

int graw_encode_dsa_state(struct graw_encoder_state *enc,
                          uint32_t handle,
                          struct pipe_depth_stencil_alpha_state *dsa_state)
{
   uint32_t tmp;
   int i;
   graw_encoder_write_dword(enc, GRAW_CMD0(GRAW_CREATE_OBJECT, GRAW_OBJECT_DSA, 1 + 4));
   graw_encoder_write_dword(enc, handle);

   tmp = dsa_state->depth.enabled |
      dsa_state->depth.writemask << 1 |
      dsa_state->depth.func << 2 |
      dsa_state->alpha.enabled << 8 |
      dsa_state->alpha.func << 9;
   graw_encoder_write_dword(enc, tmp);

   for (i = 0; i < 2; i++) {
      tmp = dsa_state->stencil[i].enabled |
         dsa_state->stencil[i].func << 1 |
         dsa_state->stencil[i].fail_op << 4 |
         dsa_state->stencil[i].zpass_op << 7 |
         dsa_state->stencil[i].zfail_op << 10 |
         dsa_state->stencil[i].valuemask << 13 |
         dsa_state->stencil[i].writemask << 21;
      graw_encoder_write_dword(enc, tmp);
   }
      
   graw_encoder_write_dword(enc, uif(dsa_state->alpha.ref_value));
   return 0;
}
int graw_encode_rasterizer_state(struct graw_encoder_state *enc,
                                  uint32_t handle,
                                  struct pipe_rasterizer_state *state)
{
   uint32_t tmp;

   graw_encoder_write_dword(enc, GRAW_CMD0(GRAW_CREATE_OBJECT, GRAW_OBJECT_RASTERIZER, 2 + 1));
   graw_encoder_write_dword(enc, handle);

   tmp = (state->flatshade << 0) |
      (state->depth_clip << 1) |
      (state->gl_rasterization_rules << 2);
   graw_encoder_write_dword(enc, tmp);
   graw_encoder_write_dword(enc, 0);
}

int graw_encode_shader_state(struct graw_encoder_state *enc,
                             uint32_t handle,
			     uint32_t type,
                             const struct pipe_shader_state *shader)
{
   uint32_t tmp;
   static char str[8192];
   uint32_t len;

   memset(str, 0, 8192);
   tgsi_dump_str(shader->tokens, 0, str, sizeof(str));

   len = strlen(str) + 1;
   graw_encoder_write_dword(enc, GRAW_CMD0(GRAW_CREATE_OBJECT, type, ((len + 3)/ 4) + 1));
   graw_encoder_write_dword(enc, handle);
   graw_encoder_write_block(enc, str, len);
   return 0;
}

int graw_encode_clear(struct graw_encoder_state *enc,
                      unsigned buffers,
                      const union pipe_color_union *color,
                      double depth, unsigned stencil)
{
   int i;
   
   graw_encoder_write_dword(enc, GRAW_CMD0(GRAW_CLEAR, 0, 8));
                            
   graw_encoder_write_dword(enc, buffers);
   for (i = 0; i < 4; i++)
      graw_encoder_write_dword(enc, color->ui[i]);
   graw_encoder_write_qword(enc, *(uint64_t *)&depth);
   graw_encoder_write_dword(enc, stencil);
}

int graw_encoder_set_framebuffer_state(struct graw_encoder_state *enc,
				       const struct pipe_framebuffer_state *state)
{
   struct graw_surface *zsurf = state->zsbuf;
   int i;

   graw_encoder_write_dword(enc, GRAW_CMD0(GRAW_SET_FRAMEBUFFER_STATE, 0, 2 + state->nr_cbufs));
   graw_encoder_write_dword(enc, state->nr_cbufs);
   graw_encoder_write_dword(enc, zsurf ? zsurf->handle : 0);
   for (i = 0; i < state->nr_cbufs; i++) {
      struct graw_surface *surf = state->cbufs[i];
      graw_encoder_write_dword(enc, surf->handle);
   }
   return 0;
}

int graw_encoder_set_viewport_state(struct graw_encoder_state *enc,
				    const struct pipe_viewport_state *state)
{
   int i;
   graw_encoder_write_dword(enc, GRAW_CMD0(GRAW_SET_VIEWPORT_STATE, 0, 8));
   for (i = 0; i < 4; i++)
      graw_encoder_write_dword(enc, uif(state->scale[i]));
   for (i = 0; i < 4; i++)
      graw_encoder_write_dword(enc, uif(state->translate[i]));
   return 0;
}

int graw_encoder_create_vertex_elements(struct graw_encoder_state *enc,
                                        uint32_t handle,
                                        unsigned num_elements,
                                        const struct pipe_vertex_element *element)
{
   int i;
   graw_encoder_write_dword(enc, GRAW_CMD0(GRAW_CREATE_OBJECT, GRAW_OBJECT_VERTEX_ELEMENTS, (4 * num_elements) + 1));
   graw_encoder_write_dword(enc, handle);
   for (i = 0; i < num_elements; i++) {
      graw_encoder_write_dword(enc, element[i].src_offset);
      graw_encoder_write_dword(enc, element[i].instance_divisor);
      graw_encoder_write_dword(enc, element[i].vertex_buffer_index);
      graw_encoder_write_dword(enc, element[i].src_format);
   }
   return 0;
}

int graw_encoder_set_vertex_buffers(struct graw_encoder_state *enc,
                                    unsigned num_buffers,
                                    const struct pipe_vertex_buffer *buffers,
                                    uint32_t *res_handles)
{
   int i;
   graw_encoder_write_dword(enc, GRAW_CMD0(GRAW_SET_VERTEX_BUFFERS, 0, (3 * num_buffers)));
   for (i = 0; i < num_buffers; i++) {
      graw_encoder_write_dword(enc, buffers[i].stride);
      graw_encoder_write_dword(enc, buffers[i].buffer_offset);
      graw_encoder_write_dword(enc, res_handles[i]);
   }
}

int graw_encoder_set_index_buffer(struct graw_encoder_state *enc,
                                  const struct pipe_index_buffer *ib,
                                  uint32_t res_handle)
{
   int length = 1 + ib ? 2 : 0;
   graw_encoder_write_dword(enc, GRAW_CMD0(GRAW_SET_INDEX_BUFFER, 0, length));
   graw_encoder_write_dword(enc, res_handle);
   if (ib) {
      graw_encoder_write_dword(enc, ib->index_size);
      graw_encoder_write_dword(enc, ib->offset);
   }
}

int graw_encoder_draw_vbo(struct graw_encoder_state *enc,
			  const struct pipe_draw_info *info)
{
   graw_encoder_write_dword(enc, GRAW_CMD0(GRAW_DRAW_VBO, 0, 5));
   graw_encoder_write_dword(enc, info->start);
   graw_encoder_write_dword(enc, info->count);
   graw_encoder_write_dword(enc, info->mode);
   graw_encoder_write_dword(enc, info->indexed);
   graw_encoder_write_dword(enc, info->instance_count);
   return 0;
}

int graw_encoder_create_surface(struct graw_encoder_state *enc,
				uint32_t handle,
				uint32_t res_handle,
				const struct pipe_surface *templat)
{
   graw_encoder_write_dword(enc, GRAW_CMD0(GRAW_CREATE_OBJECT, GRAW_SURFACE, 6));
   graw_encoder_write_dword(enc, handle);
   graw_encoder_write_dword(enc, res_handle);
   graw_encoder_write_dword(enc, templat->width);
   graw_encoder_write_dword(enc, templat->height);
   graw_encoder_write_dword(enc, templat->usage);
   graw_encoder_write_dword(enc, templat->format);
   return 0;
}

int graw_encoder_inline_write(struct graw_encoder_state *enc,
                              uint32_t res_handle,
                              unsigned level, unsigned usage,
                              const struct pipe_box *box,
                              void *data, unsigned stride,
                              unsigned layer_stride)
{
   uint32_t size = (stride ? stride : box->width) * box->height;
   uint32_t length = 11 + size / 4;
   graw_encoder_write_dword(enc, GRAW_CMD0(GRAW_RESOURCE_INLINE_WRITE, 0, length));
   graw_encoder_write_dword(enc, res_handle);
   graw_encoder_write_dword(enc, level);
   graw_encoder_write_dword(enc, usage);
   graw_encoder_write_dword(enc, stride);
   graw_encoder_write_dword(enc, layer_stride);
   graw_encoder_write_dword(enc, box->x);
   graw_encoder_write_dword(enc, box->y);
   graw_encoder_write_dword(enc, box->z);
   graw_encoder_write_dword(enc, box->width);
   graw_encoder_write_dword(enc, box->height);
   graw_encoder_write_dword(enc, box->depth);

   graw_encoder_write_block(enc, data, size);

}

int graw_encoder_flush_frontbuffer(struct graw_encoder_state *enc,
                                   uint32_t res_handle)
{
//   graw_encoder_write_dword(enc, GRAW_CMD0(GRAW_FLUSH_FRONTUBFFER, 0, 1));
   graw_encoder_write_dword(enc, res_handle);
   return 0;
}

int graw_encode_sampler_state(struct graw_encoder_state *enc,
                              uint32_t handle,
                              const struct pipe_sampler_state *state)
{
   uint32_t tmp;

   graw_encoder_write_dword(enc, GRAW_CMD0(GRAW_CREATE_OBJECT, GRAW_OBJECT_SAMPLER_STATE , 2));
   graw_encoder_write_dword(enc, handle);

   tmp = state->wrap_s |
      state->wrap_t << 3 |
      state->wrap_r << 6 |
      state->min_img_filter << 9 |
      state->min_mip_filter << 11 |
      state->mag_img_filter << 13;

   graw_encoder_write_dword(enc, tmp);
}


int graw_encode_sampler_view(struct graw_encoder_state *enc,
                             uint32_t handle,
                             uint32_t res_handle,
                             const struct pipe_sampler_view *state)
{
   graw_encoder_write_dword(enc, GRAW_CMD0(GRAW_CREATE_OBJECT, GRAW_OBJECT_SAMPLER_VIEW ,2));
   graw_encoder_write_dword(enc, handle);
   graw_encoder_write_dword(enc, res_handle);
}

int graw_encode_set_fragment_sampler_views(struct graw_encoder_state *enc,
                                           uint32_t num_handles,
                                           uint32_t *handles)
{
   int i;
   graw_encoder_write_dword(enc, GRAW_CMD0(GRAW_SET_FRAGMENT_SAMPLER_VIEWS, 0, num_handles));
   for (i = 0; i < num_handles; i++)
      graw_encoder_write_dword(enc, handles[i]);
   return 0;
}


int graw_encode_bind_fragment_sampler_states(struct graw_encoder_state *enc,
                                             uint32_t num_handles,
                                             uint32_t *handles)
{
   int i;
   graw_encoder_write_dword(enc, GRAW_CMD0(GRAW_BIND_OBJECT, GRAW_OBJECT_SAMPLER_STATE, num_handles));
   for (i = 0; i < num_handles; i++)
      graw_encoder_write_dword(enc, handles[i]);
   return 0;
}

int graw_encoder_write_constant_buffer(struct graw_encoder_state *enc,
                                       uint32_t shader,
                                       uint32_t index,
                                       uint32_t size,
                                       void *data)
{
   graw_encoder_write_dword(enc, GRAW_CMD0(GRAW_SET_CONSTANT_BUFFER, 0, size + 2));
   graw_encoder_write_dword(enc, shader);
   graw_encoder_write_dword(enc, index);
   if (data)
      graw_encoder_write_block(enc, data, size * 4);

}

#define EQ_BUF_SIZE (16*1024)

struct graw_encoder_state *graw_encoder_init_queue(void)
{
   struct graw_encoder_state *eq;

   eq = CALLOC_STRUCT(graw_encoder_state);
   if (!eq)
      return NULL;

   eq->buf = malloc(EQ_BUF_SIZE);
   if (!eq->buf){
      free(eq);
      return NULL;
   }
   eq->buf_total = EQ_BUF_SIZE;
   eq->buf_offset = 0;
   return eq;
}

