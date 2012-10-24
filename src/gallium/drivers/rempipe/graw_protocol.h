#ifndef GRAW_PROTOCOL_H
#define GRAW_PROTOCOL_H

enum graw_object_type {
   GRAW_OBJECT_NULL,
   GRAW_OBJECT_BLEND,
   GRAW_OBJECT_RASTERIZER,
   GRAW_OBJECT_DSA,
   GRAW_OBJECT_VS,
   GRAW_OBJECT_FS,
   GRAW_OBJECT_VERTEX_ELEMENTS,
   GRAW_OBJECT_SAMPLER_VIEW,
   GRAW_OBJECT_SAMPLER_STATE,
   GRAW_RESOURCE,
   GRAW_SURFACE,
};

/* context cmds to be encoded in the command stream */
enum graw_cmd {
   GRAW_NOP = 0,
   GRAW_CREATE_OBJECT = 1,
   GRAW_BIND_OBJECT,
   GRAW_DESTROY_OBJECT,
   GRAW_SET_VIEWPORT_STATE,
   GRAW_SET_FRAMEBUFFER_STATE,
   GRAW_SET_VERTEX_BUFFERS,
   GRAW_CLEAR,
   GRAW_DRAW_VBO,
   GRAW_RESOURCE_INLINE_WRITE,
   GRAW_SET_FRAGMENT_SAMPLER_VIEWS,
   GRAW_SET_INDEX_BUFFER,
   GRAW_SET_CONSTANT_BUFFER,
};

enum graw_scrn_cmd {
   GRAW_CREATE_RENDERER = 1,
   GRAW_CREATE_CONTEXT = 2,
   GRAW_CREATE_RESOURCE = 3,
   GRAW_FLUSH_FRONTBUFFER = 4,
   GRAW_SUBMIT_CMD = 5,
   GRAW_DESTROY_CONTEXT = 6,
   GRAW_DESTROY_RENDERER = 7,
   GRAW_TRANSFER_GET = 8,
   GRAW_TRANSFER_PUT = 9,
};

/* 
 8-bit cmd headers
 8-bit object type
 16-bit length
*/

#define GRAW_CMD0(cmd, obj, len) ((cmd) | ((obj) << 8) | ((len) << 16))

void graw_renderer_init(void);
void graw_renderer_resource_create(uint32_t handle, enum pipe_texture_target target, uint32_t format, uint32_t bind, uint32_t width, uint32_t height);
void graw_decode_block(uint32_t *block, int ndw);

#endif
