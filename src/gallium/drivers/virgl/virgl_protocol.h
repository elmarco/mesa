#ifndef VIRGL_PROTOCOL_H
#define VIRGL_PROTOCOL_H

#define VIRGL_QUERY_STATE_NEW 0
#define VIRGL_QUERY_STATE_DONE 1
#define VIRGL_QUERY_STATE_WAIT_HOST 2

struct virgl_host_query_state {
   uint32_t query_state;
   uint32_t result_size;
   uint64_t result;
};

enum virgl_object_type {
   VIRGL_OBJECT_NULL,
   VIRGL_OBJECT_BLEND,
   VIRGL_OBJECT_RASTERIZER,
   VIRGL_OBJECT_DSA,
   VIRGL_OBJECT_VS,
   VIRGL_OBJECT_FS,
   VIRGL_OBJECT_VERTEX_ELEMENTS,
   VIRGL_OBJECT_SAMPLER_VIEW,
   VIRGL_OBJECT_SAMPLER_STATE,
   VIRGL_OBJECT_SURFACE,
   VIRGL_OBJECT_QUERY,
   VIRGL_OBJECT_STREAMOUT_TARGET,
   VIRGL_OBJECT_GS,
   VIRGL_MAX_OBJECTS,
};

/* context cmds to be encoded in the command stream */
enum virgl_context_cmd {
   VIRGL_CCMD_NOP = 0,
   VIRGL_CCMD_CREATE_OBJECT = 1,
   VIRGL_CCMD_BIND_OBJECT,
   VIRGL_CCMD_DESTROY_OBJECT,
   VIRGL_CCMD_SET_VIEWPORT_STATE,
   VIRGL_CCMD_SET_FRAMEBUFFER_STATE,
   VIRGL_CCMD_SET_VERTEX_BUFFERS,
   VIRGL_CCMD_CLEAR,
   VIRGL_CCMD_DRAW_VBO,
   VIRGL_CCMD_RESOURCE_INLINE_WRITE,
   VIRGL_CCMD_SET_SAMPLER_VIEWS,
   VIRGL_CCMD_SET_INDEX_BUFFER,
   VIRGL_CCMD_SET_CONSTANT_BUFFER,
   VIRGL_CCMD_SET_STENCIL_REF,
   VIRGL_CCMD_SET_BLEND_COLOR,
   VIRGL_CCMD_SET_SCISSOR_STATE,
   VIRGL_CCMD_BLIT,
   VIRGL_CCMD_RESOURCE_COPY_REGION,
   VIRGL_CCMD_BIND_SAMPLER_STATES,
   VIRGL_CCMD_BEGIN_QUERY,
   VIRGL_CCMD_END_QUERY,
   VIRGL_CCMD_GET_QUERY_RESULT,
   VIRGL_CCMD_SET_POLYGON_STIPPLE,
   VIRGL_CCMD_SET_CLIP_STATE,
   VIRGL_CCMD_SET_SAMPLE_MASK,
   VIRGL_CCMD_SET_STREAMOUT_TARGETS,
   VIRGL_CCMD_SET_QUERY_STATE,
   VIRGL_CCMD_SET_RENDER_CONDITION,
};

/* 
 8-bit cmd headers
 8-bit object type
 16-bit length
*/

#define VIRGL_CMD0(cmd, obj, len) ((cmd) | ((obj) << 8) | ((len) << 16))

/* bit offsets for blend state object */
#define VIRGL_OBJ_BLEND_HANDLE 1
#define VIRGL_OBJ_BLEND_S0 2
#define VIRGL_OBJ_BLEND_S0_INDEPENDENT_BLEND_ENABLE(x) ((x) & 0x1 << 0)
#define VIRGL_OBJ_BLEND_S0_LOGICOP_ENABLE(x) (((x) & 0x1) << 1)
#define VIRGL_OBJ_BLEND_S0_DITHER(x) (((x) & 0x1) << 2)
#define VIRGL_OBJ_BLEND_S0_ALPHA_TO_COVERAGE(x) (((x) & 0x1) << 3)
#define VIRGL_OBJ_BLEND_S0_ALPHA_TO_ONE(x) (((x) & 0x1) << 4)
#define VIRGL_OBJ_BLEND_S1 3
#define VIRGL_OBJ_BLEND_S1_LOGICOP_FUNC(x) (((x) & 0xf) << 0)
/* repeated once per number of cbufs */
#define VIRGL_MAX_COLOR_BUFS 8
#define VIRGL_OBJ_BLEND_S2(cbuf) (4 + (cbuf))
#define VIRGL_OBJ_BLEND_S2_RT_BLEND_ENABLE(x) (((x) & 0x1) << 0)
#define VIRGL_OBJ_BLEND_S2_RT_RGB_FUNC(x) (((x) & 0x7) << 1)
#define VIRGL_OBJ_BLEND_S2_RT_RGB_SRC_FACTOR(x) (((x) & 0x1f) << 4)
#define VIRGL_OBJ_BLEND_S2_RT_RGB_DST_FACTOR(x) (((x) & 0x1f) << 9)
#define VIRGL_OBJ_BLEND_S2_RT_ALPHA_FUNC(x) (((x) & 0x7) << 14)
#define VIRGL_OBJ_BLEND_S2_RT_ALPHA_SRC_FACTOR(x) (((x) & 0x1f) << 17)
#define VIRGL_OBJ_BLEND_S2_RT_ALPHA_DST_FACTOR(x) (((x) & 0x1f) << 22)
#define VIRGL_OBJ_BLEND_S2_RT_COLORMASK(x) (((x) & 0xf) << 27)

/* bit offsets for DSA state */
#define VIRGL_OBJ_DSA_HANDLE 1
#define VIRGL_OBJ_DSA_S0 2
#define VIRGL_OBJ_DSA_S0_DEPTH_ENABLE(x) (((x) & 0x1) << 0)
#define VIRGL_OBJ_DSA_S0_DEPTH_WRITEMASK(x) (((x) & 0x1) << 1)
#define VIRGL_OBJ_DSA_S0_DEPTH_FUNC(x) (((x) & 0x7) << 2)
#define VIRGL_OBJ_DSA_S0_ALPHA_ENABLED(x) (((x) & 0x1) << 8)
#define VIRGL_OBJ_DSA_S0_ALPHA_FUNC(x) (((x) & 0x3) << 9)
#define VIRGL_OBJ_DSA_S1 3
#define VIRGL_OBJ_DSA_S2 4
#define VIRGL_OBJ_DSA_S1_STENCIL_ENABLED(x) (((x) & 0x1) << 0)
#define VIRGL_OBJ_DSA_S1_STENCIL_FUNC(x) (((x) & 0x7) << 1)
#define VIRGL_OBJ_DSA_S1_STENCIL_FAIL_OP(x) (((x) & 0x7) << 4)
#define VIRGL_OBJ_DSA_S1_STENCIL_ZPASS_OP(x) (((x) & 0x7) << 7)
#define VIRGL_OBJ_DSA_S1_STENCIL_ZFAIL_OP(x) (((x) & 0x7) << 10)
#define VIRGL_OBJ_DSA_S1_STENCIL_VALUEMASK(x) (((x) & 0xff) << 13)
#define VIRGL_OBJ_DSA_S1_STENCIL_WRITEMASK(x) (((x) & 0xff) << 21)
#define VIRGL_OBJ_DSA_S3_ALPHA_REF 5

/* offsets for rasterizer state */
#define VIRGL_OBJ_RS_HANDLE 1
#define VIRGL_OBJ_RS_S0 2
#define VIRGL_OBJ_RS_S0_FLATSHADE(x) (((x) & 0x1) << 0)
#define VIRGL_OBJ_RS_S0_DEPTH_CLIP(x) (((x) & 0x1) << 1)
#define VIRGL_OBJ_RS_S0_CLIP_HALFZ(x) (((x) & 0x1) << 2)
#define VIRGL_OBJ_RS_S0_RASTERIZER_DISCARD(x) (((x) & 0x1) << 3)
#define VIRGL_OBJ_RS_S0_FLATSHADE_FIRST(x) (((x) & 0x1) << 4)
#define VIRGL_OBJ_RS_S0_LIGHT_TWOSIZE(x) (((x) & 0x1) << 5)
#define VIRGL_OBJ_RS_S0_SPRITE_COORD_MODE(x) (((x) & 0x1) << 6)
#define VIRGL_OBJ_RS_S0_POINT_QUAD_RASTERIZATION(x) (((x) & 0x1) << 7)
#define VIRGL_OBJ_RS_S0_CULL_FACE(x) (((x) & 0x3) << 8)
#define VIRGL_OBJ_RS_S0_FILL_FRONT(x) (((x) & 0x3) << 10)
#define VIRGL_OBJ_RS_S0_FILL_BACK(x) (((x) & 0x3) << 12)
#define VIRGL_OBJ_RS_S0_SCISSOR(x) (((x) & 0x1) << 14)
#define VIRGL_OBJ_RS_S0_FRONT_CCW(x) (((x) & 0x1) << 15)
#define VIRGL_OBJ_RS_S0_CLAMP_VERTEX_COLOR(x) (((x) & 0x1) << 16)
#define VIRGL_OBJ_RS_S0_CLAMP_FRAGMENT_COLOR(x) (((x) & 0x1) << 17)
#define VIRGL_OBJ_RS_S0_OFFSET_LINE(x) (((x) & 0x1) << 18)
#define VIRGL_OBJ_RS_S0_OFFSET_POINT(x) (((x) & 0x1) << 19)
#define VIRGL_OBJ_RS_S0_OFFSET_TRI(x) (((x) & 0x1) << 20)
#define VIRGL_OBJ_RS_S0_POLY_SMOOTH(x) (((x) & 0x1) << 21)
#define VIRGL_OBJ_RS_S0_POLY_STIPPLE_ENABLE(x) (((x) & 0x1) << 22)
#define VIRGL_OBJ_RS_S0_POINT_SMOOTH(x) (((x) & 0x1) << 23)
#define VIRGL_OBJ_RS_S0_POINT_SIZE_PER_VERTEX(x) (((x) & 0x1) << 24)
#define VIRGL_OBJ_RS_S0_MULTISAMPLE(x) (((x) & 0x1) << 25)
#define VIRGL_OBJ_RS_S0_LINE_SMOOTH(x) (((x) & 0x1) << 26)
#define VIRGL_OBJ_RS_S0_LINE_STIPPLE_ENABLE(x) (((x) & 0x1) << 27)
#define VIRGL_OBJ_RS_S0_LINE_LAST_PIXEL(x) (((x) & 0x1) << 28)
#define VIRGL_OBJ_RS_S0_HALF_PIXEL_CENTER(x) (((x) & 0x1) << 29)
#define VIRGL_OBJ_RS_S0_BOTTOM_EDGE_RULE(x) (((x) & 0x1) << 30)

#define VIRGL_OBJ_RS_S1_POINT_SIZE 3
#define VIRGL_OBJ_RS_S2_SPRITE_COORD_ENABLE 4
#define VIRGL_OBJ_RS_S3 5
#define VIRGL_MAX_CLIP_PLANES 8
#define VIRGL_OBJ_RS_S3_LINE_STIPPLE_PATTERN(x) (((x) & 0xffff) << 0)
#define VIRGL_OBJ_RS_S3_LINE_STIPPLE_FACTOR(x) (((x) & 0xff) << 16)
#define VIRGL_OBJ_RS_S3_CLIP_PLANE_ENABLE(x) (((x) & 0xff) << 24)
#define VIRGL_OBJ_RS_S4_LINE_WIDTH 6
#define VIRGL_OBJ_RS_S5_OFFSET_UNITS 7
#define VIRGL_OBJ_RS_S6_OFFSET_SCALE 8
#define VIRGL_OBJ_RS_S7_OFFSET_CLAMP 9

#endif
