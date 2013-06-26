#include <GL/glew.h>
#include <GL/gl.h>

#include <stdio.h>
#include "pipe/p_shader_tokens.h"

#include "pipe/p_context.h"
#include "pipe/p_defines.h"
#include "pipe/p_screen.h"
#include "pipe/p_state.h"
#include "util/u_inlines.h"
#include "util/u_memory.h"
#include "util/u_transfer.h"
#include "util/u_double_list.h"
#include "util/u_format.h"
#include "tgsi/tgsi_text.h"

#include "state_tracker/graw.h"

#include "graw_protocol.h"
#include "graw_object.h"
#include "graw_shader.h"

#include "graw_renderer.h"
#include "graw_renderer_glx.h"
#include "graw_decode.h"

/* transfer boxes from the guest POV are in y = 0 = top orientation */
/* blit/copy operations from the guest POV are in y = 0 = top orientation */

/* since we are storing things in OpenGL FBOs we need to flip transfer operations by default */

extern int graw_shader_use_explicit;
int localrender;
static int have_invert_mesa = 0;

struct grend_fence {
   uint32_t fence_id;
   GLsync syncobj;
   struct list_head fences;
};

struct global_renderer_state {
   bool viewport_dirty;
   bool scissor_dirty;
   GLboolean blend_enabled;
   GLboolean depth_test_enabled;
   GLboolean alpha_test_enabled;
   GLboolean stencil_test_enabled;
   GLuint program_id;
   struct list_head fence_list;
};

static struct global_renderer_state grend_state;

struct grend_linked_shader_program {
  struct list_head head;
  GLuint id;

  struct grend_shader_state *vs;
  struct grend_shader_state *fs;

  GLuint num_samplers[PIPE_SHADER_TYPES];
  GLuint *samp_locs[PIPE_SHADER_TYPES];

  GLuint *const_locs[PIPE_SHADER_TYPES];

  GLuint *attrib_locs;
};

struct grend_shader_state {
   GLuint id;
   unsigned type;
   char *glsl_prog;
   int num_samplers;
   int num_consts;
   int num_inputs;
};
struct grend_resource {
   struct pipe_resource base;
   GLuint id;
   GLenum target;
   /* fb id if we need to readback this resource */
   GLuint readback_fb_id;
   GLuint readback_fb_level;
   int is_front;
   GLboolean renderer_flipped;
};

struct grend_buffer {
   struct grend_resource base;
};

struct grend_texture {
   struct grend_resource base;
   struct pipe_sampler_state state;
   GLenum cur_swizzle_a;
};

struct grend_surface {
   GLuint id;
   GLuint res_handle;
   GLuint format;
   GLuint val0, val1;
   struct grend_resource *texture;
};

struct grend_sampler {

};

struct grend_sampler_view {
   GLuint id;
   GLuint res_handle;
   GLuint format;
   GLuint val0, val1;
   GLuint swizzle_r:3;
   GLuint swizzle_g:3;
   GLuint swizzle_b:3;
   GLuint swizzle_a:3;
   GLuint gl_swizzle_a;
   GLuint cur_base, cur_max;
   struct grend_resource *texture;
};

struct grend_vertex_element {
   struct pipe_vertex_element base;
   GLenum type;
   GLboolean norm;
   GLuint nr_chan;
};

struct grend_vertex_element_array {
   unsigned count;
   struct grend_vertex_element elements[PIPE_MAX_ATTRIBS];
};

struct grend_constants {
   float *consts;
   uint32_t num_consts;
};

struct grend_shader_view {
   int num_views;
   struct grend_sampler_view *views[PIPE_MAX_SHADER_SAMPLER_VIEWS];
   uint32_t res_id[PIPE_MAX_SHADER_SAMPLER_VIEWS];
   uint32_t old_ids[PIPE_MAX_SHADER_SAMPLER_VIEWS];
};

struct grend_context {
   struct pipe_context base;
   GLuint vaoid;
   GLuint num_enabled_attribs;

   struct grend_vertex_element_array *ve;
   int num_vbos;
   struct pipe_vertex_buffer vbo[PIPE_MAX_ATTRIBS];
   uint32_t vbo_res_ids[PIPE_MAX_ATTRIBS];
   struct grend_shader_state *vs;
   struct grend_shader_state *fs;

   bool shader_dirty;
   struct grend_linked_shader_program *prog;

   struct grend_shader_view views[PIPE_SHADER_TYPES];

   struct pipe_rasterizer_state *rs_state;

   struct pipe_index_buffer ib;
   uint32_t index_buffer_res_id;

   struct grend_constants consts[PIPE_SHADER_TYPES];
   bool const_dirty[PIPE_SHADER_TYPES];
   struct pipe_sampler_state *sampler_state[PIPE_SHADER_TYPES][PIPE_MAX_SAMPLERS];

   int num_sampler_states[PIPE_SHADER_TYPES];
   uint32_t fb_id;

   uint8_t stencil_refs[2];

   struct pipe_depth_stencil_alpha_state *dsa;
   boolean stencil_state_dirty;
   struct list_head programs;

   GLint view_cur_x, view_cur_y;
   GLsizei view_width, view_height;
   GLclampd view_near_val, view_far_val;
   GLboolean view_flipped;

   GLenum old_targets[PIPE_MAX_SAMPLERS];

   struct pipe_scissor_state ss;
   boolean scissor_state_dirty;
   boolean viewport_state_dirty;
   uint32_t fb_height;

   int nr_cbufs;
   struct grend_surface *zsurf;
   struct grend_surface *surf[8];
};

static struct grend_resource *frontbuffer;
static struct {
   GLenum internalformat;
   GLenum glformat;
   GLenum gltype;
} tex_conv_table[PIPE_FORMAT_COUNT] =
   {
      [PIPE_FORMAT_B8G8R8X8_UNORM] = {GL_RGBA8, GL_BGRA, GL_UNSIGNED_BYTE },
      [PIPE_FORMAT_B8G8R8A8_UNORM] = {GL_RGBA8, GL_BGRA, GL_UNSIGNED_BYTE },

      [PIPE_FORMAT_R8G8B8A8_UNORM] = {GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE },
      [PIPE_FORMAT_R8G8B8X8_UNORM] = {GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE },

      [PIPE_FORMAT_A8R8G8B8_UNORM] = {GL_RGBA8, GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV },

      [PIPE_FORMAT_A8B8G8R8_UNORM] = {GL_ABGR_EXT, GL_RGBA, GL_UNSIGNED_BYTE },
      [PIPE_FORMAT_B5G6R5_UNORM] = {GL_RGB4, GL_RGBA, GL_UNSIGNED_SHORT_5_6_5 },

      [PIPE_FORMAT_B4G4R4A4_UNORM] = {GL_RGBA4, GL_RGBA, GL_UNSIGNED_SHORT_4_4_4_4 },

      [PIPE_FORMAT_S8_UINT] = {GL_DEPTH24_STENCIL8_EXT, GL_DEPTH_STENCIL, GL_UNSIGNED_BYTE},
      [PIPE_FORMAT_Z16_UNORM] = {GL_DEPTH_COMPONENT16, GL_DEPTH_COMPONENT, GL_UNSIGNED_SHORT},

      [PIPE_FORMAT_Z24_UNORM_S8_UINT] = {GL_DEPTH24_STENCIL8_EXT, GL_DEPTH_STENCIL, GL_UNSIGNED_INT_24_8},

//      [PIPE_FORMAT_S8_UINT_Z24_UNORM] = {GL_DEPTH24_STENCIL8_EXT, GL_DEPTH_STENCIL, GL_UNSIGNED_INT_24_8},
      [PIPE_FORMAT_Z24X8_UNORM] = {GL_DEPTH_COMPONENT24, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT},
      [PIPE_FORMAT_Z32_UNORM] = {GL_DEPTH_COMPONENT32, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT},
      [PIPE_FORMAT_Z32_FLOAT] = {GL_DEPTH_COMPONENT32F, GL_DEPTH_COMPONENT, GL_FLOAT}, 
      [PIPE_FORMAT_Z32_FLOAT_S8X24_UINT] = {GL_DEPTH32F_STENCIL8, GL_DEPTH_STENCIL, GL_FLOAT_32_UNSIGNED_INT_24_8_REV}, 

      [PIPE_FORMAT_A8_UNORM] = {GL_ALPHA8, GL_ALPHA, GL_UNSIGNED_BYTE },
      [PIPE_FORMAT_L8_UNORM] = {GL_LUMINANCE8, GL_LUMINANCE, GL_UNSIGNED_BYTE },
      [PIPE_FORMAT_L8A8_UNORM] = {GL_LUMINANCE8_ALPHA8, GL_LUMINANCE_ALPHA, GL_UNSIGNED_BYTE },

      [PIPE_FORMAT_A16_UNORM] = {GL_ALPHA16, GL_ALPHA, GL_UNSIGNED_SHORT },
      [PIPE_FORMAT_L16_UNORM] = {GL_LUMINANCE16, GL_LUMINANCE, GL_UNSIGNED_BYTE },
      [PIPE_FORMAT_L16A16_UNORM] = {GL_LUMINANCE16_ALPHA16, GL_LUMINANCE_ALPHA, GL_UNSIGNED_SHORT },

      [PIPE_FORMAT_I8_UNORM] = {GL_INTENSITY8, GL_RGB, GL_UNSIGNED_BYTE },

      [PIPE_FORMAT_B4G4R4X4_UNORM] = {GL_RGB4, GL_RGB, GL_UNSIGNED_SHORT_4_4_4_4 },
      [PIPE_FORMAT_B2G3R3_UNORM] = {GL_R3_G3_B2, GL_RGB, GL_UNSIGNED_BYTE_3_3_2 },

      [PIPE_FORMAT_B5G5R5X1_UNORM] = {GL_RGB5_A1, GL_RGB, GL_UNSIGNED_SHORT_5_5_5_1 },
      [PIPE_FORMAT_B5G5R5A1_UNORM] = {GL_RGB5_A1, GL_RGBA, GL_UNSIGNED_SHORT_5_5_5_1 },

      [PIPE_FORMAT_R16G16B16X16_UNORM] = {GL_RGBA16, GL_RGBA, GL_UNSIGNED_SHORT },
      [PIPE_FORMAT_R16G16B16X16_SNORM] = {GL_RGBA16_SNORM, GL_RGBA, GL_SHORT },
      [PIPE_FORMAT_R16G16B16A16_UNORM] = {GL_RGBA16, GL_RGBA, GL_UNSIGNED_SHORT },
      [PIPE_FORMAT_B10G10R10X2_UNORM] = {GL_RGB10_A2, GL_RGBA, GL_UNSIGNED_INT_2_10_10_10_REV },
      [PIPE_FORMAT_B10G10R10A2_UNORM] = {GL_RGB10_A2, GL_RGB, GL_UNSIGNED_INT_2_10_10_10_REV },
      [PIPE_FORMAT_B10G10R10A2_UINT] = {GL_RGB10_A2UI, GL_RGBA, GL_UNSIGNED_INT_2_10_10_10_REV },

      [PIPE_FORMAT_R8_SNORM] = {GL_R8_SNORM, GL_RED, GL_BYTE },
      [PIPE_FORMAT_R8G8_SNORM] = {GL_RG8_SNORM, GL_RG, GL_BYTE },
      [PIPE_FORMAT_R8G8B8_SNORM] = {GL_RGB8_SNORM, GL_RGB, GL_BYTE },
      [PIPE_FORMAT_R8G8B8A8_SNORM] = {GL_RGBA8_SNORM, GL_RGBA, GL_BYTE },
      [PIPE_FORMAT_R8G8B8X8_SNORM] = {GL_RGBA8_SNORM, GL_RGB, GL_BYTE },

      [PIPE_FORMAT_R8_UNORM] = {GL_RED, GL_RED, GL_UNSIGNED_BYTE },
      [PIPE_FORMAT_R8G8_UNORM] = {GL_RG, GL_RG, GL_UNSIGNED_BYTE },

      [PIPE_FORMAT_R16_UNORM] = {GL_R16, GL_RED, GL_UNSIGNED_SHORT },
      [PIPE_FORMAT_R16G16_UNORM] = {GL_RG16, GL_RG, GL_UNSIGNED_SHORT },

      [PIPE_FORMAT_R16_SNORM] = {GL_R16_SNORM, GL_RED, GL_SHORT },
      [PIPE_FORMAT_R16G16_SNORM] = {GL_RG16_SNORM, GL_RG, GL_SHORT },
      [PIPE_FORMAT_R16G16B16A16_SNORM] = {GL_RGBA16_SNORM, GL_RGBA, GL_SHORT },

      [PIPE_FORMAT_A16_FLOAT] = {GL_ALPHA16F_ARB, GL_ALPHA, GL_HALF_FLOAT },
      [PIPE_FORMAT_L16_FLOAT] = {GL_LUMINANCE16F_ARB, GL_LUMINANCE, GL_HALF_FLOAT },
      [PIPE_FORMAT_L16A16_FLOAT] = {GL_LUMINANCE_ALPHA16F_ARB, GL_LUMINANCE_ALPHA, GL_HALF_FLOAT },

      [PIPE_FORMAT_R16_FLOAT] = {GL_R16F, GL_RED, GL_HALF_FLOAT },

      [PIPE_FORMAT_R16G16_FLOAT] = {GL_RG16F, GL_RG, GL_HALF_FLOAT },
      [PIPE_FORMAT_R16G16B16_FLOAT] = {GL_RGB16F, GL_RGB, GL_HALF_FLOAT },
      [PIPE_FORMAT_R16G16B16A16_FLOAT] = {GL_RGBA16F, GL_RGBA, GL_HALF_FLOAT },

      [PIPE_FORMAT_B8G8R8X8_SRGB] = {GL_SRGB8, GL_RGB, GL_UNSIGNED_BYTE },
      [PIPE_FORMAT_B8G8R8A8_SRGB] = {GL_SRGB8_ALPHA8, GL_RGB, GL_UNSIGNED_BYTE },
      [PIPE_FORMAT_R8G8B8X8_SRGB] = {GL_SRGB8, GL_RGB, GL_UNSIGNED_BYTE },

      [PIPE_FORMAT_A8_SNORM] = {GL_ALPHA8_SNORM, GL_ALPHA, GL_BYTE },
      [PIPE_FORMAT_L8_SNORM] = {GL_LUMINANCE8_SNORM, GL_LUMINANCE, GL_BYTE },
      [PIPE_FORMAT_L8A8_SNORM] = {GL_LUMINANCE8_ALPHA8_SNORM, GL_LUMINANCE_ALPHA, GL_BYTE },

      [PIPE_FORMAT_L8_SRGB] = {GL_SLUMINANCE8_EXT, GL_LUMINANCE, GL_BYTE },
      [PIPE_FORMAT_L8A8_SRGB] = {GL_SLUMINANCE8_ALPHA8_EXT, GL_LUMINANCE_ALPHA, GL_BYTE },

      [PIPE_FORMAT_A16_SNORM] = {GL_ALPHA16_SNORM, GL_ALPHA, GL_SHORT },
      [PIPE_FORMAT_L16_SNORM] = {GL_LUMINANCE16_SNORM, GL_LUMINANCE, GL_SHORT },
      [PIPE_FORMAT_L16A16_SNORM] = {GL_LUMINANCE16_ALPHA16_SNORM, GL_LUMINANCE_ALPHA, GL_SHORT },

      [PIPE_FORMAT_A32_FLOAT] = {GL_ALPHA32F_ARB, GL_ALPHA, GL_FLOAT },
      [PIPE_FORMAT_L32_FLOAT] = {GL_LUMINANCE32F_ARB, GL_LUMINANCE, GL_FLOAT },
      [PIPE_FORMAT_L32A32_FLOAT] = {GL_LUMINANCE_ALPHA32F_ARB, GL_LUMINANCE_ALPHA, GL_FLOAT },

      [PIPE_FORMAT_R32_FLOAT] = {GL_R32F, GL_RED, GL_FLOAT },
      [PIPE_FORMAT_R32G32_FLOAT] = {GL_RG32F, GL_RG, GL_FLOAT },
      [PIPE_FORMAT_R32G32B32_FLOAT] = {GL_RGB32F, GL_RGB, GL_FLOAT },
      [PIPE_FORMAT_R32G32B32A32_FLOAT] = {GL_RGBA32F, GL_RGBA, GL_FLOAT },

      [PIPE_FORMAT_RGTC1_UNORM] = {GL_COMPRESSED_RED_RGTC1, GL_RED, GL_UNSIGNED_BYTE },
      [PIPE_FORMAT_RGTC1_SNORM] = {GL_COMPRESSED_SIGNED_RED_RGTC1, GL_RED, GL_BYTE },

      [PIPE_FORMAT_RGTC2_UNORM] = {GL_COMPRESSED_RG_RGTC2, GL_RG, GL_UNSIGNED_BYTE },
      [PIPE_FORMAT_RGTC2_SNORM] = {GL_COMPRESSED_SIGNED_RG_RGTC2, GL_RG, GL_BYTE },

      [PIPE_FORMAT_R8_UINT] = {GL_R8UI, GL_RED_INTEGER, GL_UNSIGNED_BYTE },
      [PIPE_FORMAT_R8G8_UINT] = {GL_RG8UI, GL_RG_INTEGER, GL_UNSIGNED_BYTE },
      [PIPE_FORMAT_R8G8B8_UINT] = {GL_RGB8UI, GL_RGB_INTEGER, GL_UNSIGNED_BYTE },
      [PIPE_FORMAT_R8G8B8A8_UINT] = {GL_RGBA8UI, GL_RGBA_INTEGER, GL_UNSIGNED_BYTE },

      [PIPE_FORMAT_R8_SINT] = {GL_R8I, GL_RED_INTEGER, GL_BYTE },
      [PIPE_FORMAT_R8G8_SINT] = {GL_RG8I, GL_RG_INTEGER, GL_BYTE },
      [PIPE_FORMAT_R8G8B8_SINT] = {GL_RGB8I, GL_RGB_INTEGER, GL_BYTE },
      [PIPE_FORMAT_R8G8B8A8_SINT] = {GL_RGBA8I, GL_RGBA_INTEGER, GL_BYTE },

      [PIPE_FORMAT_R16_UINT] = {GL_R16UI, GL_RED_INTEGER, GL_UNSIGNED_SHORT },
      [PIPE_FORMAT_R16G16_UINT] = {GL_RG16UI, GL_RG_INTEGER, GL_UNSIGNED_SHORT },
      [PIPE_FORMAT_R16G16B16_UINT] = {GL_RGB16UI, GL_RGB_INTEGER, GL_UNSIGNED_SHORT },
      [PIPE_FORMAT_R16G16B16A16_UINT] = {GL_RGBA16UI, GL_RGBA_INTEGER, GL_UNSIGNED_SHORT },

      [PIPE_FORMAT_R16_SINT] = {GL_R16I, GL_RED_INTEGER, GL_SHORT },
      [PIPE_FORMAT_R16G16_SINT] = {GL_RG16I, GL_RG_INTEGER, GL_SHORT },
      [PIPE_FORMAT_R16G16B16_SINT] = {GL_RGB16I, GL_RGB_INTEGER, GL_SHORT },
      [PIPE_FORMAT_R16G16B16A16_SINT] = {GL_RGBA16I, GL_RGBA_INTEGER, GL_SHORT },

      [PIPE_FORMAT_R32_UINT] = {GL_R32UI, GL_RED_INTEGER, GL_UNSIGNED_INT },
      [PIPE_FORMAT_R32G32_UINT] = {GL_RG32UI, GL_RG_INTEGER, GL_UNSIGNED_INT },
      [PIPE_FORMAT_R32G32B32_UINT] = {GL_RGB32UI, GL_RGB_INTEGER, GL_UNSIGNED_INT },
      [PIPE_FORMAT_R32G32B32A32_UINT] = {GL_RGBA32UI, GL_RGBA_INTEGER, GL_UNSIGNED_INT },

      [PIPE_FORMAT_R32_SINT] = {GL_R32I, GL_RED_INTEGER, GL_INT },
      [PIPE_FORMAT_R32G32_SINT] = {GL_RG32I, GL_RG_INTEGER, GL_INT },
      [PIPE_FORMAT_R32G32B32_SINT] = {GL_RGB32I, GL_RGB_INTEGER, GL_INT },
      [PIPE_FORMAT_R32G32B32A32_SINT] = {GL_RGBA32I, GL_RGBA_INTEGER, GL_INT },

      [PIPE_FORMAT_A8_UINT] = {GL_ALPHA8UI_EXT, GL_ALPHA_INTEGER, GL_UNSIGNED_BYTE},
      [PIPE_FORMAT_L8_UINT] = {GL_LUMINANCE8UI_EXT, GL_LUMINANCE_INTEGER_EXT, GL_UNSIGNED_BYTE},
      [PIPE_FORMAT_L8A8_UINT] = {GL_LUMINANCE_ALPHA8UI_EXT, GL_LUMINANCE_ALPHA_INTEGER_EXT, GL_UNSIGNED_BYTE},

      [PIPE_FORMAT_A8_SINT] = {GL_ALPHA8I_EXT, GL_ALPHA_INTEGER, GL_BYTE},
      [PIPE_FORMAT_L8_SINT] = {GL_LUMINANCE8I_EXT, GL_LUMINANCE_INTEGER_EXT, GL_BYTE},
      [PIPE_FORMAT_L8A8_SINT] = {GL_LUMINANCE_ALPHA8I_EXT, GL_LUMINANCE_ALPHA_INTEGER_EXT, GL_BYTE},

      [PIPE_FORMAT_A16_UINT] = {GL_ALPHA16UI_EXT, GL_ALPHA_INTEGER, GL_UNSIGNED_SHORT},
      [PIPE_FORMAT_L16_UINT] = {GL_LUMINANCE16UI_EXT, GL_LUMINANCE_INTEGER_EXT, GL_UNSIGNED_SHORT},
      [PIPE_FORMAT_L16A16_UINT] = {GL_LUMINANCE_ALPHA16UI_EXT, GL_LUMINANCE_ALPHA_INTEGER_EXT, GL_UNSIGNED_SHORT},

      [PIPE_FORMAT_A16_SINT] = {GL_ALPHA16I_EXT, GL_ALPHA_INTEGER, GL_SHORT},
      [PIPE_FORMAT_L16_SINT] = {GL_LUMINANCE16I_EXT, GL_LUMINANCE_INTEGER_EXT, GL_SHORT},
      [PIPE_FORMAT_L16A16_SINT] = {GL_LUMINANCE_ALPHA16I_EXT, GL_LUMINANCE_ALPHA_INTEGER_EXT, GL_SHORT},

      [PIPE_FORMAT_A32_UINT] = {GL_ALPHA32UI_EXT, GL_ALPHA_INTEGER, GL_UNSIGNED_INT},
      [PIPE_FORMAT_L32_UINT] = {GL_LUMINANCE32UI_EXT, GL_LUMINANCE_INTEGER_EXT, GL_UNSIGNED_INT},
      [PIPE_FORMAT_L32A32_UINT] = {GL_LUMINANCE_ALPHA32UI_EXT, GL_LUMINANCE_ALPHA_INTEGER_EXT, GL_UNSIGNED_INT},

      [PIPE_FORMAT_A32_SINT] = {GL_ALPHA32I_EXT, GL_ALPHA_INTEGER, GL_INT},
      [PIPE_FORMAT_L32_SINT] = {GL_LUMINANCE32I_EXT, GL_LUMINANCE_INTEGER_EXT, GL_INT},
      [PIPE_FORMAT_L32A32_SINT] = {GL_LUMINANCE_ALPHA32I_EXT, GL_LUMINANCE_ALPHA_INTEGER_EXT, GL_INT},
   };

static void grend_use_program(GLuint program_id)
{
   if (grend_state.program_id != program_id) {
      glUseProgram(program_id);
      grend_state.program_id = program_id;
   }
}

static void grend_blend_enable(GLboolean blend_enable)
{
   if (grend_state.blend_enabled != blend_enable) {
      grend_state.blend_enabled = blend_enable;
      if (blend_enable)
         glEnable(GL_BLEND);
      else
         glDisable(GL_BLEND);
   }
}

static void grend_depth_test_enable(GLboolean depth_test_enable)
{
   if (grend_state.depth_test_enabled != depth_test_enable) {
      grend_state.depth_test_enabled = depth_test_enable;
      if (depth_test_enable)
         glEnable(GL_DEPTH_TEST);
      else
         glDisable(GL_DEPTH_TEST);
   }
}

static void grend_alpha_test_enable(GLboolean alpha_test_enable)
{
   if (grend_state.alpha_test_enabled != alpha_test_enable) {
      grend_state.alpha_test_enabled = alpha_test_enable;
      if (alpha_test_enable)
         glEnable(GL_ALPHA_TEST);
      else
         glDisable(GL_ALPHA_TEST);
   }
}
static void grend_stencil_test_enable(GLboolean stencil_test_enable)
{
   if (grend_state.stencil_test_enabled != stencil_test_enable) {
      grend_state.stencil_test_enabled = stencil_test_enable;
      if (stencil_test_enable)
         glEnable(GL_STENCIL_TEST);
      else
         glDisable(GL_STENCIL_TEST);
   }
}

static struct grend_linked_shader_program *add_shader_program(struct grend_context *ctx,
							       struct grend_shader_state *vs,
							       struct grend_shader_state *fs) {
  struct grend_linked_shader_program *sprog = malloc(sizeof(struct grend_linked_shader_program));
  char name[16];
  int i;
  GLuint prog_id;
  GLenum ret;

  prog_id = glCreateProgram();
  glAttachShader(prog_id, vs->id);
  glAttachShader(prog_id, fs->id);
  ret = glGetError();
  glLinkProgram(prog_id);
  ret = glGetError();
  if (ret) {
     fprintf(stderr,"got error linking %d\n", ret);
  }

  sprog->vs = vs;
  sprog->fs = fs;
  sprog->id = prog_id;
  list_add(&sprog->head, &ctx->programs);

  if (vs->num_samplers) {
    sprog->samp_locs[PIPE_SHADER_VERTEX] = calloc(vs->num_samplers, sizeof(uint32_t));
    if (sprog->samp_locs[PIPE_SHADER_VERTEX]) {
      for (i = 0; i < vs->num_samplers; i++) {
	snprintf(name, 10, "vssamp%d", i);
	sprog->samp_locs[PIPE_SHADER_VERTEX][i] = glGetUniformLocation(prog_id, name);
      }
    }
  } else
    sprog->samp_locs[PIPE_SHADER_VERTEX] = NULL;
  sprog->num_samplers[PIPE_SHADER_VERTEX] = vs->num_samplers;
  if (fs->num_samplers) {
    sprog->samp_locs[PIPE_SHADER_FRAGMENT] = calloc(fs->num_samplers, sizeof(uint32_t));
    if (sprog->samp_locs[PIPE_SHADER_FRAGMENT]) {
      for (i = 0; i < fs->num_samplers; i++) {
	snprintf(name, 10, "fssamp%d", i);
	sprog->samp_locs[PIPE_SHADER_FRAGMENT][i] = glGetUniformLocation(prog_id, name);
      }
    }
  } else
    sprog->samp_locs[PIPE_SHADER_FRAGMENT] = NULL;
  sprog->num_samplers[PIPE_SHADER_FRAGMENT] = fs->num_samplers;
  
  if (vs->num_consts) {
    sprog->const_locs[PIPE_SHADER_VERTEX] = calloc(vs->num_consts, sizeof(uint32_t));
    if (sprog->const_locs[PIPE_SHADER_VERTEX]) {
      for (i = 0; i < vs->num_consts; i++) {
	snprintf(name, 16, "vsconst[%d]", i);
	sprog->const_locs[PIPE_SHADER_VERTEX][i] = glGetUniformLocation(prog_id, name);
      }
    }
  } else
    sprog->const_locs[PIPE_SHADER_VERTEX] = NULL;

  if (fs->num_consts) {
    sprog->const_locs[PIPE_SHADER_FRAGMENT] = calloc(fs->num_consts, sizeof(uint32_t));
    if (sprog->const_locs[PIPE_SHADER_FRAGMENT]) {
      for (i = 0; i < fs->num_consts; i++) {
	snprintf(name, 16, "fsconst[%d]", i);
	sprog->const_locs[PIPE_SHADER_FRAGMENT][i] = glGetUniformLocation(prog_id, name);
      }
    }
  } else
    sprog->const_locs[PIPE_SHADER_FRAGMENT] = NULL;

  if (vs->num_inputs) {
    sprog->attrib_locs = calloc(vs->num_inputs, sizeof(uint32_t));
    if (sprog->attrib_locs) {
      for (i = 0; i < vs->num_inputs; i++) {
	snprintf(name, 10, "in_%d", i);
	sprog->attrib_locs[i] = glGetAttribLocation(prog_id, name);
      }
    }
  } else
    sprog->attrib_locs = NULL;
   
  return sprog;
}

static struct grend_linked_shader_program *lookup_shader_program(struct grend_context *ctx,
			   GLuint vs_id, GLuint fs_id)
{
  struct grend_linked_shader_program *ent;
  LIST_FOR_EACH_ENTRY(ent, &ctx->programs, head) {
    if (ent->vs->id == vs_id && ent->fs->id == fs_id)
      return ent;
  }
  return 0;
}
  

static void grend_apply_sampler_state(struct grend_context *ctx,
                                      struct grend_resource *res,
                                      uint32_t shader_type,
                                      int id);

void grend_update_stencil_state(struct grend_context *ctx);

void grend_create_surface(struct grend_context *ctx,
                          uint32_t handle,
                          uint32_t res_handle, uint32_t format,
                          uint32_t val0, uint32_t val1)
   
{
   struct grend_surface *surf;

   surf = CALLOC_STRUCT(grend_surface);

   surf->res_handle = res_handle;
   surf->format = format;
   surf->val0 = val0;
   surf->val1 = val1;
   surf->texture = graw_object_lookup(res_handle, GRAW_RESOURCE);

   if (!surf->texture) {
      fprintf(stderr,"%s: failed to find resource for surface %d\n", __func__, res_handle);
      free(surf);
      return;
   }
   graw_object_insert(surf, sizeof(*surf), handle, GRAW_SURFACE);
}

void grend_create_sampler_view(struct grend_context *ctx,
                               uint32_t handle,
                               uint32_t res_handle, uint32_t format,
                               uint32_t val0, uint32_t val1, uint32_t swizzle_packed)
{
   struct grend_sampler_view *view;

   view = CALLOC_STRUCT(grend_sampler_view);
   view->res_handle = res_handle;
   view->format = format;
   view->val0 = val0;
   view->val1 = val1;
   view->swizzle_r = swizzle_packed & 0x7;
   view->swizzle_g = (swizzle_packed >> 3) & 0x7;
   view->swizzle_b = (swizzle_packed >> 6) & 0x7;
   view->swizzle_a = (swizzle_packed >> 9) & 0x7;
   view->cur_base = 0;
   view->cur_max = 1000;

   view->texture = graw_object_lookup(res_handle, GRAW_RESOURCE);

   if (!view->texture) {
      fprintf(stderr,"%s: failed to find resource %d\n", __func__, res_handle);
      FREE(view);
      return;
   }
      
   if (view->format != view->texture->base.format) {
      fprintf(stderr,"%d %d swizzles %d %d %d %d\n", view->format, view->texture->base.format, view->swizzle_r, view->swizzle_g, view->swizzle_b, view->swizzle_a);
      /* hack to make tfp work for now */
      view->gl_swizzle_a = GL_ONE;
   } else
      view->gl_swizzle_a = GL_ALPHA;
   graw_object_insert(view, sizeof(*view), handle, GRAW_OBJECT_SAMPLER_VIEW);
}

void grend_set_framebuffer_state(struct grend_context *ctx,
                                 uint32_t nr_cbufs, uint32_t surf_handle[8],
                                 uint32_t zsurf_handle)
{
   struct grend_surface *surf, *zsurf;
   struct grend_resource *tex;
   int i;
   static const GLenum buffers[8] = {
      GL_COLOR_ATTACHMENT0_EXT,
      GL_COLOR_ATTACHMENT1_EXT,
      GL_COLOR_ATTACHMENT2_EXT,
      GL_COLOR_ATTACHMENT3_EXT,
      GL_COLOR_ATTACHMENT4_EXT,
      GL_COLOR_ATTACHMENT5_EXT,
      GL_COLOR_ATTACHMENT6_EXT,
      GL_COLOR_ATTACHMENT7_EXT,
   };

   if (ctx->fb_id)
      glDeleteFramebuffers(1, &ctx->fb_id);
   glGenFramebuffers(1, &ctx->fb_id);

   glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, ctx->fb_id);

   if (zsurf_handle) {
      GLuint attachment;
      zsurf = graw_object_lookup(zsurf_handle, GRAW_SURFACE);
      if (!zsurf) {
         fprintf(stderr,"%s: can't find surface for Z %d\n", __func__, zsurf_handle);
         return;
      }
      tex = zsurf->texture;
      if (!tex)
         return;
      if (tex->base.format == PIPE_FORMAT_S8_UINT)
         attachment = GL_STENCIL_ATTACHMENT;
      else if (tex->base.format == PIPE_FORMAT_Z24X8_UNORM || tex->base.format == PIPE_FORMAT_Z32_UNORM)
         attachment = GL_DEPTH_ATTACHMENT;
      else
         attachment = GL_DEPTH_STENCIL_ATTACHMENT;
      glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, attachment,
                                tex->target, tex->id, zsurf->val0);
      ctx->zsurf = zsurf;
   }

   ctx->nr_cbufs = nr_cbufs;
   for (i = 0; i < nr_cbufs; i++) {
      surf = graw_object_lookup(surf_handle[i], GRAW_SURFACE);
      if (!surf) {
         fprintf(stderr,"%s: can't find surface for cbuf %d %d\n", __func__, i, surf_handle[i]);
         return;
      }
      tex = surf->texture;

      ctx->surf[i] = surf;
      if (tex->target == GL_TEXTURE_CUBE_MAP) {
         glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, buffers[i],
                                   GL_TEXTURE_CUBE_MAP_POSITIVE_X + (surf->val1 & 0xffff), tex->id, surf->val0);
      } else
         glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, buffers[i],
                                   tex->target, tex->id, surf->val0);

      if (i == 0 && tex->base.height0 != ctx->fb_height) {
         ctx->fb_height = tex->base.height0;
         ctx->scissor_state_dirty = TRUE;
         ctx->viewport_state_dirty = TRUE;
      }

   }

   glDrawBuffers(nr_cbufs, buffers);
}

void grend_set_viewport_state(struct grend_context *ctx,
                              const struct pipe_viewport_state *state)
{
   /* convert back to glViewport */
   GLint x, y;
   GLsizei width, height;
   GLclampd near_val, far_val;
   GLboolean view_flipped = state->scale[1] < 0 ? GL_TRUE : GL_FALSE;
   
   width = state->scale[0] * 2.0f;
   height = abs(state->scale[1]) * 2.0f;
   x = state->translate[0] - state->scale[0];
   y = state->translate[1] - abs(state->scale[1]);

   near_val = state->translate[2] - state->scale[2];
   far_val = near_val + (state->scale[2] * 2.0f);

   if (ctx->view_cur_x != x ||
       ctx->view_cur_y != y ||
       ctx->view_width != width ||
       ctx->view_height != height ||
      ctx->view_flipped != view_flipped) {
      ctx->viewport_state_dirty = TRUE;
      ctx->view_cur_x = x;
      ctx->view_cur_y = y;
      ctx->view_width = width;
      ctx->view_height = height;
      ctx->view_flipped = view_flipped;
   }

   if (ctx->view_near_val != near_val ||
       ctx->view_far_val != far_val) {
      glDepthRange(near_val, far_val);
      ctx->view_near_val = near_val;
      ctx->view_far_val = far_val;
   }
}

void grend_create_vertex_elements_state(struct grend_context *ctx,
                                        uint32_t handle,
                                        unsigned num_elements,
                                        const struct pipe_vertex_element *elements)
{
   struct grend_vertex_element_array *v = CALLOC_STRUCT(grend_vertex_element_array);
   const struct util_format_description *desc;
   GLenum type = GL_FALSE;
   int i;

   v->count = num_elements;
   for (i = 0; i < num_elements; i++) {
      memcpy(&v->elements[i].base, &elements[i], sizeof(struct pipe_vertex_element));

      desc = util_format_description(elements[i].src_format);

      if (desc->channel[0].type == UTIL_FORMAT_TYPE_FLOAT) {
	 if (desc->channel[0].size == 32)
	    type = GL_FLOAT;
	 else if (desc->channel[0].size == 64)
	    type = GL_DOUBLE;
      } else if (desc->channel[0].type == UTIL_FORMAT_TYPE_UNSIGNED &&
                 desc->channel[0].size == 8) 
         type = GL_UNSIGNED_BYTE;
      else if (desc->channel[0].type == UTIL_FORMAT_TYPE_SIGNED &&
               desc->channel[0].size == 8) 
         type = GL_BYTE;
      else if (desc->channel[0].type == UTIL_FORMAT_TYPE_UNSIGNED &&
               desc->channel[0].size == 16) 
         type = GL_UNSIGNED_SHORT;
      else if (desc->channel[0].type == UTIL_FORMAT_TYPE_SIGNED &&
               desc->channel[0].size == 16) 
         type = GL_SHORT;
      else if (desc->channel[0].type == UTIL_FORMAT_TYPE_UNSIGNED &&
               desc->channel[0].size == 32) 
         type = GL_UNSIGNED_INT;
      else if (desc->channel[0].type == UTIL_FORMAT_TYPE_SIGNED &&
               desc->channel[0].size == 32) 
         type = GL_INT;
      v->elements[i].type = type;
      if (desc->channel[0].normalized)
         v->elements[i].norm = GL_TRUE;
      v->elements[i].nr_chan = desc->nr_channels;
   }

   graw_object_insert(v, sizeof(struct grend_vertex_element), handle,
                      GRAW_OBJECT_VERTEX_ELEMENTS);
}

void grend_bind_vertex_elements_state(struct grend_context *ctx,
                                      uint32_t handle)
{
   struct grend_vertex_element_array *v;

   if (!handle) {
      ctx->ve = NULL;
      return;
   }
   v = graw_object_lookup(handle, GRAW_OBJECT_VERTEX_ELEMENTS);
   if (!v) {
      fprintf(stderr, "illegal ve lookup\n");
   }
      
   ctx->ve = v;
}

void grend_set_constants(struct grend_context *ctx,
                         uint32_t shader,
                         uint32_t index,
                         uint32_t num_constant,
                         float *data)
{
   struct grend_constants *consts;
   int i;

   consts = &ctx->consts[shader];
   ctx->const_dirty[shader] = TRUE;

   consts->consts = realloc(consts->consts, num_constant * sizeof(float));
   if (!consts->consts)
      return;

   consts->num_consts = num_constant;
   for (i = 0; i < num_constant; i++)
      consts->consts[i] = data[i];
}

void grend_set_index_buffer(struct grend_context *ctx,
                            uint32_t res_handle,
                            uint32_t index_size,
                            uint32_t offset)
{
   struct grend_resource *res;

   ctx->ib.index_size = index_size;
   ctx->ib.offset = offset;
   if (res_handle) {
      if (ctx->index_buffer_res_id != res_handle) {
         res = graw_object_lookup(res_handle, GRAW_RESOURCE);
         ctx->ib.buffer = &res->base;
         ctx->index_buffer_res_id = res_handle;
      }
   } else {
      ctx->ib.buffer = NULL;
      ctx->index_buffer_res_id = 0;
   }
}

void grend_set_single_vbo(struct grend_context *ctx,
                         int index,
                         uint32_t stride,
                         uint32_t buffer_offset,
                         uint32_t res_handle)
{
   struct grend_resource *res;
   ctx->vbo[index].stride = stride;
   ctx->vbo[index].buffer_offset = buffer_offset;

   if (res_handle == 0) {
      ctx->vbo[index].buffer = NULL;
      ctx->vbo_res_ids[index] = 0;
   } else if (ctx->vbo_res_ids[index] != res_handle) {
      res = graw_object_lookup(res_handle, GRAW_RESOURCE);
      ctx->vbo[index].buffer = &res->base;
      ctx->vbo_res_ids[index] = res_handle;
   }
}

void grend_set_num_vbo(struct grend_context *ctx,
                      int num_vbo)
{
   ctx->num_vbos = num_vbo;
}

void grend_set_single_sampler_view(struct grend_context *ctx,
                                   uint32_t shader_type,
                                   int index,
                                   uint32_t handle)
{
   struct grend_sampler_view *view = NULL;
   struct grend_texture *tex;
   if (handle) {
      view = graw_object_lookup(handle, GRAW_OBJECT_SAMPLER_VIEW);
      if (!view) {
         fprintf(stderr,"%s: failed to find view %d\n", __func__, handle);
         return;
      }
      glBindTexture(view->texture->target, view->texture->id);
      if (view->texture->target != PIPE_BUFFER) {
         tex = (struct grend_texture *)view->texture;
         if (view->cur_base != (view->val1 & 0xff)) {
            glTexParameteri(view->texture->target, GL_TEXTURE_BASE_LEVEL, (view->val1) & 0xff);
            view->cur_base = view->val1 & 0xff;
         }
         if (view->cur_max != ((view->val1 >> 8) & 0xff)) {
            glTexParameteri(view->texture->target, GL_TEXTURE_MAX_LEVEL, (view->val1 >> 8) & 0xff);
            view->cur_max = (view->val1 >> 8) & 0xff;
         }
         if (tex->cur_swizzle_a != view->gl_swizzle_a) {
            glTexParameteri(view->texture->target, GL_TEXTURE_SWIZZLE_A, view->gl_swizzle_a);
            tex->cur_swizzle_a = view->gl_swizzle_a;
         }
      }
   }

   ctx->views[shader_type].views[index] = view;
}

void grend_set_num_sampler_views(struct grend_context *ctx,
                                    uint32_t shader_type,
                                    int num_sampler_views)
{
   ctx->views[shader_type].num_views = num_sampler_views;
}

void grend_transfer_inline_write(struct grend_context *ctx,
                                 uint32_t res_handle,
                                 unsigned level,
                                 unsigned usage,
                                 const struct pipe_box *box,
                                 const void *data,
                                 unsigned stride,
                                 unsigned layer_stride)
{
   struct grend_resource *res;

   res = graw_object_lookup(res_handle, GRAW_RESOURCE);
   if (res->target == GL_ELEMENT_ARRAY_BUFFER_ARB) {
      glBindBufferARB(GL_ELEMENT_ARRAY_BUFFER_ARB, res->id);
      glBufferSubData(GL_ELEMENT_ARRAY_BUFFER_ARB, box->x, box->width, data);
   } else if (res->target == GL_ARRAY_BUFFER_ARB) {
      glBindBufferARB(GL_ARRAY_BUFFER_ARB, res->id);
      glBufferSubData(GL_ARRAY_BUFFER_ARB, box->x, box->width, data);
   } else {
      GLenum glformat, gltype;
      glBindTexture(res->target, res->id);
      glformat = tex_conv_table[res->base.format].glformat;
      gltype = tex_conv_table[res->base.format].gltype; 

      glTexSubImage2D(res->target, level, box->x, box->y, box->width, box->height,
                      glformat, gltype, data);
   }
}


void grend_create_vs(struct grend_context *ctx,
                     uint32_t handle,
                     const struct pipe_shader_state *vs)
{
   struct grend_shader_state *state = CALLOC_STRUCT(grend_shader_state);
   const GLchar *glsl_prog;
   GLenum ret;
   state->id = glCreateShader(GL_VERTEX_SHADER);
   glsl_prog = tgsi_convert(vs->tokens, 0, &state->num_samplers, &state->num_consts, &state->num_inputs);
   if (glsl_prog) {
      glShaderSource(state->id, 1, &glsl_prog, NULL);
      ret = glGetError();
      glCompileShader(state->id);
      ret = glGetError();
      if (ret) {
         fprintf(stderr,"got error compiling %d\n", ret);
         fprintf(stderr,"VS:\n%s\n", glsl_prog);
      }
   } else
     fprintf(stderr,"failed to convert VS prog\n");
   graw_object_insert(state, sizeof(*state), handle, GRAW_OBJECT_VS);

   return;
}

void grend_create_fs(struct grend_context *ctx,
                     uint32_t handle,
                     const struct pipe_shader_state *fs)
{
   struct grend_shader_state *state = CALLOC_STRUCT(grend_shader_state);
   GLchar *glsl_prog;
   GLenum ret;
   state->id = glCreateShader(GL_FRAGMENT_SHADER);
   glsl_prog = tgsi_convert(fs->tokens, 0, &state->num_samplers, &state->num_consts, &state->num_inputs);
   if (glsl_prog) {
      glShaderSource(state->id, 1, (const char **)&glsl_prog, NULL);
      ret = glGetError();
      glCompileShader(state->id);
      ret = glGetError();
      if (ret) {
         fprintf(stderr,"got error compiling %d\n", ret);
         fprintf(stderr,"VS:\n%s\n", glsl_prog);
      }
      //      fprintf(stderr,"FS:\n%s\n", glsl_prog);
   } else
     fprintf(stderr,"failed to convert FS prog\n");
   graw_object_insert(state, sizeof(*state), handle, GRAW_OBJECT_FS);

   return;
}

void grend_bind_vs(struct grend_context *ctx,
                   uint32_t handle)
{
   struct grend_shader_state *state;

   state = graw_object_lookup(handle, GRAW_OBJECT_VS);

   if (ctx->vs != state)
      ctx->shader_dirty = true;
   ctx->vs = state;
}


void grend_bind_fs(struct grend_context *ctx,
                   uint32_t handle)
{
   struct grend_shader_state *state;

   state = graw_object_lookup(handle, GRAW_OBJECT_FS);

   if (ctx->fs != state)
      ctx->shader_dirty = true;
   ctx->fs = state;
}

void grend_clear(struct grend_context *ctx,
                 unsigned buffers,
                 const union pipe_color_union *color,
                 double depth, unsigned stencil)
{
   GLbitfield bits = 0;
   glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, ctx->fb_id);

   grend_use_program(0);

   if (buffers & PIPE_CLEAR_COLOR)
      glClearColor(color->f[0], color->f[1], color->f[2], color->f[3]);

   if (buffers & PIPE_CLEAR_DEPTH)
      glClearDepth(depth);

   if (buffers & PIPE_CLEAR_STENCIL)
      glClearStencil(stencil);

   if (buffers & PIPE_CLEAR_COLOR)
      bits |= GL_COLOR_BUFFER_BIT;
   if (buffers & PIPE_CLEAR_DEPTH)
      bits |= GL_DEPTH_BUFFER_BIT;
   if (buffers & PIPE_CLEAR_STENCIL)
      bits |= GL_STENCIL_BUFFER_BIT;
   glClear(bits);

}

static void grend_update_scissor_state(struct grend_context *ctx)
{
   struct pipe_scissor_state *ss = &ctx->ss;
   
   glScissor(ss->minx, ctx->fb_height - ss->maxy, ss->maxx - ss->minx, ss->maxy - ss->miny);
   ctx->scissor_state_dirty = FALSE;
   grend_state.scissor_dirty = FALSE;
}

static void grend_update_viewport_state(struct grend_context *ctx)
{
   int i;
   glViewport(ctx->view_cur_x, ctx->fb_height - ctx->view_height - ctx->view_cur_y, ctx->view_width, ctx->view_height);

   for (i = 0; i < ctx->nr_cbufs; i++) {
      if (ctx->surf[i])
         ctx->surf[i]->texture->renderer_flipped = ctx->view_flipped;
   }
   if (ctx->zsurf)
      ctx->zsurf->texture->renderer_flipped = ctx->view_flipped;
   ctx->viewport_state_dirty = FALSE;
   grend_state.viewport_dirty = FALSE;
}

void grend_draw_vbo(struct grend_context *ctx,
                    const struct pipe_draw_info *info)
{
   int i;
   int sampler_id;
   bool new_program = FALSE;
   uint32_t shader_type;
   uint32_t num_enable;

   if (ctx->stencil_state_dirty)
      grend_update_stencil_state(ctx);
   if (ctx->scissor_state_dirty || grend_state.scissor_dirty)
      grend_update_scissor_state(ctx);

   if (ctx->viewport_state_dirty || grend_state.viewport_dirty)
      grend_update_viewport_state(ctx);

   if (ctx->shader_dirty) {
     struct grend_linked_shader_program *prog;

     if (!ctx->vs || !ctx->fs) {
        fprintf(stderr,"dropping rendering due to missing shaders\n");
        return;
     }
     prog = lookup_shader_program(ctx, ctx->vs->id, ctx->fs->id);
     if (!prog) {
       prog = add_shader_program(ctx, ctx->vs, ctx->fs);
     }
     if (ctx->prog != prog) {
       new_program = TRUE;
       ctx->prog = prog;
     }
   }

   glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, ctx->fb_id);
   
   grend_use_program(ctx->prog->id);

   glBindVertexArray(ctx->vaoid);

   for (shader_type = PIPE_SHADER_VERTEX; shader_type <= PIPE_SHADER_FRAGMENT; shader_type++) {
      if (ctx->prog->const_locs[shader_type] && (ctx->const_dirty[shader_type] || new_program)) {
         int mval;
         if (shader_type == 0) {
            if (ctx->consts[shader_type].num_consts / 4 > ctx->vs->num_consts + 10) {
               fprintf(stderr,"possible memory corruption vs consts TODO %d %d\n", ctx->consts[0].num_consts, ctx->vs->num_consts);
               return;
            }
         } else if (shader_type == 1) {
            if (ctx->consts[shader_type].num_consts / 4 > ctx->fs->num_consts + 10) {
               fprintf(stderr,"possible memory corruption fs consts TODO\n");
               return;
            }
         }
         for (i = 0; i < ctx->consts[shader_type].num_consts / 4; i++) {
            if (ctx->prog->const_locs[shader_type][i] != -1)
               glUniform4fv(ctx->prog->const_locs[shader_type][i], 1, &ctx->consts[shader_type].consts[i * 4]);
         }
         ctx->const_dirty[shader_type] = FALSE;
      }
   }

   sampler_id = 0;
   for (shader_type = PIPE_SHADER_VERTEX; shader_type <= PIPE_SHADER_FRAGMENT; shader_type++) {
      for (i = 0; i < ctx->views[shader_type].num_views; i++) {
         struct grend_resource *texture = NULL;

         if (ctx->views[shader_type].views[i]) {
            texture = ctx->views[shader_type].views[i]->texture;
         }
         if (i >= ctx->prog->num_samplers[shader_type])
             break;
         if (ctx->prog->samp_locs[shader_type])
            glUniform1i(ctx->prog->samp_locs[shader_type][i], sampler_id);
         glActiveTexture(GL_TEXTURE0 + sampler_id);
         if (texture) {
            ctx->old_targets[sampler_id] = texture->target;
            glBindTexture(texture->target, texture->id);
            if (ctx->views[shader_type].old_ids[i] != texture->id) {
               grend_apply_sampler_state(ctx, texture, shader_type, sampler_id);
               ctx->views[shader_type].old_ids[i] = texture->id;
            }
            if (ctx->rs_state->point_quad_rasterization) {
               if (ctx->rs_state->sprite_coord_enable & (1 << i))
                  glTexEnvi(GL_POINT_SPRITE_ARB, GL_COORD_REPLACE_ARB, GL_TRUE);
               else
                  glTexEnvi(GL_POINT_SPRITE_ARB, GL_COORD_REPLACE_ARB, GL_FALSE);
            }
            sampler_id++;
         
         } else {
            if (ctx->old_targets[sampler_id]) {
               glDisable(ctx->old_targets[sampler_id]);
               ctx->old_targets[sampler_id] = 0;
            }
         }
      }
   } 

   num_enable = ctx->ve->count;
   for (i = 0; i < ctx->ve->count; i++) {
      struct grend_vertex_element *ve = &ctx->ve->elements[i];
      int vbo_index = ctx->ve->elements[i].base.vertex_buffer_index;
      struct grend_buffer *buf;
      GLint loc;

      if (i >= ctx->prog->vs->num_inputs) {
         /* XYZZY: debug this? */
         num_enable = ctx->prog->vs->num_inputs;
         break;
      }
      buf = (struct grend_buffer *)ctx->vbo[vbo_index].buffer;
      glBindBuffer(GL_ARRAY_BUFFER, buf->base.id);

      if (graw_shader_use_explicit) {
         loc = i;
      } else {
	if (ctx->prog->attrib_locs) {
	  loc = ctx->prog->attrib_locs[i];
	} else loc = -1;

	if (loc == -1) {
           fprintf(stderr,"cannot find loc %d %d %d\n", i, ctx->ve->count, ctx->prog->vs->num_inputs);
          fprintf(stderr,"shader probably didn't compile - skipping rendering\n");
          num_enable--;
          if (i == 0)
             return;
          continue;
        }
      }
      glVertexAttribPointer(loc, ve->nr_chan, ve->type, ve->norm, ctx->vbo[vbo_index].stride, (void *)(unsigned long)(ctx->ve->elements[i].base.src_offset + ctx->vbo[vbo_index].buffer_offset));
      glVertexAttribDivisorARB(i, ctx->ve->elements[i].base.instance_divisor);
   }


   if (ctx->num_enabled_attribs != num_enable) {
      if (num_enable > ctx->num_enabled_attribs) {
         for (i = ctx->num_enabled_attribs; i < num_enable; i++)
            glEnableVertexAttribArray(i);
      } else if (num_enable < ctx->num_enabled_attribs) {
         for (i = num_enable; i < ctx->num_enabled_attribs; i++)
            glDisableVertexAttribArray(i);
      }
      ctx->num_enabled_attribs = num_enable;
   }

   if (info->indexed) {
      struct grend_resource *res = (struct grend_resource *)ctx->ib.buffer;
      glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, res->id);
   }
   
   /* set the vertex state up now on a delay */
   if (!info->indexed) {
      GLenum mode = info->mode;
      if (info->instance_count <= 1)
         glDrawArrays(mode, info->start, info->count);
      else
         glDrawArraysInstancedARB(mode, info->start, info->count, info->instance_count);
   } else {
      GLenum elsz;
      GLenum mode = info->mode;
      switch (ctx->ib.index_size) {
      case 1:
         elsz = GL_UNSIGNED_BYTE;
         break;
      case 2: 
         elsz = GL_UNSIGNED_SHORT;
         break;
      case 4: 
         elsz = GL_UNSIGNED_INT;
         break;
      }

      if (info->instance_count <= 1)
         glDrawElements(mode, info->count, elsz, (void *)(unsigned long)ctx->ib.offset);
      else
         glDrawElementsInstancedARB(mode, info->count, elsz, (void *)(unsigned long)ctx->ib.offset, info->instance_count);
   }

   glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
   glBindVertexArray(0);
   
   glActiveTexture(GL_TEXTURE0);
      
}

static GLenum translate_blend_func(uint32_t pipe_blend)
{
   switch(pipe_blend){
   case PIPE_BLEND_ADD: return GL_FUNC_ADD;
   case PIPE_BLEND_SUBTRACT: return GL_FUNC_SUBTRACT;
   case PIPE_BLEND_REVERSE_SUBTRACT: return GL_FUNC_REVERSE_SUBTRACT;
   case PIPE_BLEND_MIN: return GL_MIN;
   case PIPE_BLEND_MAX: return GL_MAX;
   default:
      assert("invalid blend token()" == NULL);
      return 0;
   }
}

static GLenum translate_blend_factor(uint32_t pipe_factor)
{
   switch (pipe_factor) {
   case PIPE_BLENDFACTOR_ONE: return GL_ONE;
   case PIPE_BLENDFACTOR_SRC_COLOR: return GL_SRC_COLOR;
   case PIPE_BLENDFACTOR_SRC_ALPHA: return GL_SRC_ALPHA;

   case PIPE_BLENDFACTOR_DST_COLOR: return GL_DST_COLOR;
   case PIPE_BLENDFACTOR_DST_ALPHA: return GL_DST_ALPHA;

   case PIPE_BLENDFACTOR_CONST_COLOR: return GL_CONSTANT_COLOR;
   case PIPE_BLENDFACTOR_CONST_ALPHA: return GL_CONSTANT_ALPHA;

   case PIPE_BLENDFACTOR_SRC1_COLOR: return GL_SRC1_COLOR;
   case PIPE_BLENDFACTOR_SRC1_ALPHA: return GL_SRC1_ALPHA;
   case PIPE_BLENDFACTOR_SRC_ALPHA_SATURATE: return GL_SRC_ALPHA_SATURATE;
   case PIPE_BLENDFACTOR_ZERO: return GL_ZERO;


   case PIPE_BLENDFACTOR_INV_SRC_COLOR: return GL_ONE_MINUS_SRC_COLOR;
   case PIPE_BLENDFACTOR_INV_SRC_ALPHA: return GL_ONE_MINUS_SRC_ALPHA;

   case PIPE_BLENDFACTOR_INV_DST_COLOR: return GL_ONE_MINUS_DST_COLOR;
   case PIPE_BLENDFACTOR_INV_DST_ALPHA: return GL_ONE_MINUS_DST_ALPHA;

   case PIPE_BLENDFACTOR_INV_CONST_COLOR: return GL_ONE_MINUS_CONSTANT_COLOR;
   case PIPE_BLENDFACTOR_INV_CONST_ALPHA: return GL_ONE_MINUS_CONSTANT_ALPHA;

   case PIPE_BLENDFACTOR_INV_SRC1_COLOR: return GL_ONE_MINUS_SRC1_COLOR;
   case PIPE_BLENDFACTOR_INV_SRC1_ALPHA: return GL_ONE_MINUS_SRC1_ALPHA;

   default:
      assert("invalid blend token()" == NULL);
      return 0;
   }
}

static GLenum
translate_logicop(GLuint pipe_logicop)
{
   switch (pipe_logicop) {
#define CASE(x) case PIPE_LOGICOP_##x: return GL_##x
      CASE(CLEAR);
      CASE(NOR);
      CASE(AND_INVERTED);
      CASE(COPY_INVERTED);
      CASE(AND_REVERSE);
      CASE(INVERT);
      CASE(XOR);
      CASE(NAND);
      CASE(AND);
      CASE(EQUIV);
      CASE(NOOP);
      CASE(OR_INVERTED);
      CASE(COPY);
      CASE(OR_REVERSE);
      CASE(OR);
      CASE(SET);
   default:
      assert("invalid logicop token()" == NULL);
      return 0;
   }
#undef CASE
}

static GLenum
translate_stencil_op(GLuint op)
{
   switch (op) {
#define CASE(x) case PIPE_STENCIL_OP_##x: return GL_##x   
      CASE(KEEP);
      CASE(ZERO);
      CASE(REPLACE);
      CASE(INCR);
      CASE(DECR);
      CASE(INCR_WRAP);
      CASE(DECR_WRAP);
      CASE(INVERT);
   default:
      assert("invalid stencilop token()" == NULL);
      return 0;
   }
#undef CASE
}

void grend_object_bind_blend(struct grend_context *ctx,
                             uint32_t handle)
{
   struct pipe_blend_state *state;

   if (handle == 0) {
      grend_blend_enable(GL_FALSE);
      return;
   }
   state = graw_object_lookup(handle, GRAW_OBJECT_BLEND);
   if (!state) {
      fprintf(stderr,"%s: got illegal handle %d\n", __func__, handle);
      return;
   }
   if (state->logicop_enable) {
      glEnable(GL_COLOR_LOGIC_OP);
      glLogicOp(translate_logicop(state->logicop_func));
   } else
      glDisable(GL_COLOR_LOGIC_OP);

   if (state->independent_blend_enable) {
      fprintf(stderr,"TODO bind independendt blend state\n");
   } else {
      if (state->rt[0].blend_enable) {
         glBlendFuncSeparate(translate_blend_factor(state->rt[0].rgb_src_factor),
                             translate_blend_factor(state->rt[0].rgb_dst_factor),
                             translate_blend_factor(state->rt[0].alpha_src_factor),
                             translate_blend_factor(state->rt[0].alpha_dst_factor));
         glBlendEquationSeparate(translate_blend_func(state->rt[0].rgb_func),
                                 translate_blend_func(state->rt[0].alpha_func));
         grend_blend_enable(GL_TRUE);
      } 
      else
         grend_blend_enable(GL_FALSE);

      glColorMask(state->rt[0].colormask & PIPE_MASK_R ? GL_TRUE : GL_FALSE,
                  state->rt[0].colormask & PIPE_MASK_G ? GL_TRUE : GL_FALSE,
                  state->rt[0].colormask & PIPE_MASK_B ? GL_TRUE : GL_FALSE,
                  state->rt[0].colormask & PIPE_MASK_A ? GL_TRUE : GL_FALSE);
   }
}

void grend_object_bind_dsa(struct grend_context *ctx,
                           uint32_t handle)
{
   struct pipe_depth_stencil_alpha_state *state;

   if (handle == 0) {
      grend_depth_test_enable(GL_FALSE);
      grend_alpha_test_enable(GL_FALSE);
      grend_stencil_test_enable(GL_FALSE);
      ctx->dsa = NULL;
      return;
   }

   state = graw_object_lookup(handle, GRAW_OBJECT_DSA);

   if (state->depth.enabled) {
      grend_depth_test_enable(GL_TRUE);
      glDepthFunc(GL_NEVER + state->depth.func);
      if (state->depth.writemask)
         glDepthMask(GL_TRUE);
      else
         glDepthMask(GL_FALSE);
   } else
      grend_depth_test_enable(GL_FALSE);
 
   if (state->alpha.enabled) {
      grend_alpha_test_enable(GL_TRUE);
      glAlphaFunc(GL_NEVER + state->alpha.func, state->alpha.ref_value);
   } else
      grend_alpha_test_enable(GL_FALSE);

   if (ctx->dsa != state)
      ctx->stencil_state_dirty = TRUE;
   ctx->dsa = state;
}
 
void grend_update_stencil_state(struct grend_context *ctx)
{
   struct pipe_depth_stencil_alpha_state *state = ctx->dsa;
   int i;
   if (!state)
      return;

   if (!state->stencil[1].enabled) {
      if (state->stencil[0].enabled) {
         grend_stencil_test_enable(GL_TRUE);

         glStencilOp(translate_stencil_op(state->stencil[0].fail_op), 
                     translate_stencil_op(state->stencil[0].zpass_op),
                     translate_stencil_op(state->stencil[0].zfail_op));
         glStencilFunc(GL_NEVER + state->stencil[0].func,
                       ctx->stencil_refs[0],
                       state->stencil[0].valuemask);
         glStencilMask(state->stencil[0].writemask);
      } else
         grend_stencil_test_enable(GL_FALSE);
   } else {
      grend_stencil_test_enable(GL_TRUE);

      for (i = 0; i < 2; i++) {
         GLenum face = (i == 1) ? GL_BACK : GL_FRONT;
         glStencilOpSeparate(face, translate_stencil_op(state->stencil[i].fail_op), 
                             translate_stencil_op(state->stencil[i].zpass_op),
                             translate_stencil_op(state->stencil[i].zfail_op));
         glStencilFuncSeparate(face, GL_NEVER + state->stencil[i].func,
                               ctx->stencil_refs[i],
                               state->stencil[i].valuemask);
         glStencilMaskSeparate(face, state->stencil[i].writemask);
      }
   }
   ctx->stencil_state_dirty = FALSE;
}

static inline GLenum translate_fill(uint32_t mode)
{
   switch (mode) {
   case PIPE_POLYGON_MODE_POINT:
      return GL_POINT;
   case PIPE_POLYGON_MODE_LINE:
      return GL_LINE;
   case PIPE_POLYGON_MODE_FILL:
      return GL_FILL;
   }
   assert(0);
   return 0;
}
void grend_object_bind_rasterizer(struct grend_context *ctx,
                                  uint32_t handle)
{
   struct pipe_rasterizer_state *state;

   if (handle == 0)
      return;
   state = graw_object_lookup(handle, GRAW_OBJECT_RASTERIZER);
   
   if (!state) {
      fprintf(stderr,"%s: cannot find object %d\n", __func__, handle);
      return;
   }

#if 0
   if (state->depth_clip) {
      glEnable(GL_DEPTH_CLAMP);
   } else {
      glDisable(GL_DEPTH_CLAMP);
   }
#endif
   if (state->point_size)
      glPointSize(state->point_size);

   glPolygonMode(GL_FRONT, translate_fill(state->fill_front));
   glPolygonMode(GL_BACK, translate_fill(state->fill_back));
   if (state->flatshade) {
      glShadeModel(GL_FLAT);
   } else {
      glShadeModel(GL_SMOOTH);
   }

   if (state->front_ccw)
      glFrontFace(GL_CCW);
   else
      glFrontFace(GL_CW);

   if (state->scissor)
      glEnable(GL_SCISSOR_TEST);
   else
      glDisable(GL_SCISSOR_TEST);

   if (state->point_quad_rasterization) {
      glEnable(GL_POINT_SPRITE);

      if (state->sprite_coord_mode)
         glPointParameteri(GL_POINT_SPRITE_COORD_ORIGIN, state->sprite_coord_mode ? GL_LOWER_LEFT : GL_UPPER_LEFT);
   } else
      glDisable(GL_POINT_SPRITE);

   if (state->cull_face != PIPE_FACE_NONE) {
      switch (state->cull_face) {
      case PIPE_FACE_FRONT:
         glCullFace(GL_FRONT);
         break;
      case PIPE_FACE_BACK:
         glCullFace(GL_BACK);
         break;
      case PIPE_FACE_FRONT_AND_BACK:
         glCullFace(GL_FRONT_AND_BACK);
         break;
      }
      glEnable(GL_CULL_FACE);
   } else
      glDisable(GL_CULL_FACE);

   ctx->rs_state = state;
}

static GLuint convert_wrap(int wrap)
{
   switch(wrap){
   case PIPE_TEX_WRAP_REPEAT: return GL_REPEAT;
   case PIPE_TEX_WRAP_CLAMP: return GL_CLAMP;

   case PIPE_TEX_WRAP_CLAMP_TO_EDGE: return GL_CLAMP_TO_EDGE;
   case PIPE_TEX_WRAP_CLAMP_TO_BORDER: return GL_CLAMP_TO_BORDER;

   case PIPE_TEX_WRAP_MIRROR_REPEAT: return GL_MIRRORED_REPEAT;
   default:
      assert(0);
      return -1;
   }
} 

void grend_bind_sampler_states(struct grend_context *ctx,
                               uint32_t shader_type,
                               uint32_t num_states,
                               uint32_t *handles)
{
   int i;
   struct pipe_sampler_state *state;

   ctx->num_sampler_states[shader_type] = num_states;

   for (i = 0; i < num_states; i++) {
      if (handles[i] == 0)
         state = NULL;
      else
         state = graw_object_lookup(handles[i], GRAW_OBJECT_SAMPLER_STATE);
      
      ctx->sampler_state[shader_type][i] = state;
   }
}

static inline GLenum convert_mag_filter(unsigned int filter)
{
   if (filter == PIPE_TEX_FILTER_NEAREST)
      return GL_NEAREST;
   return GL_LINEAR;
}

static inline GLenum convert_min_filter(unsigned int filter, unsigned int mip_filter)
{
   if (mip_filter == PIPE_TEX_MIPFILTER_NONE)
      return convert_mag_filter(filter);
   else if (mip_filter == PIPE_TEX_MIPFILTER_LINEAR) {
      if (filter == PIPE_TEX_FILTER_NEAREST)
         return GL_NEAREST_MIPMAP_LINEAR;
      else
         return GL_LINEAR_MIPMAP_LINEAR;
   } else if (mip_filter == PIPE_TEX_MIPFILTER_NEAREST) {
      if (filter == PIPE_TEX_FILTER_NEAREST)
         return GL_NEAREST_MIPMAP_NEAREST;
      else
         return GL_LINEAR_MIPMAP_NEAREST;
   }
   assert(0);
   return 0;
}

static void grend_apply_sampler_state(struct grend_context *ctx, 
                                      struct grend_resource *res,
                                      uint32_t shader_type,
                                      int id)
{
   struct grend_texture *tex = (struct grend_texture *)res;
   struct pipe_sampler_state *state = ctx->sampler_state[shader_type][id];
   bool set_all = FALSE;
   GLenum target = tex->base.target;

   if (!state) {
      fprintf(stderr, "cannot find sampler state for %d %d\n", shader_type, id);
      return;
   }
   if (tex->state.max_lod == -1)
      set_all = TRUE;

   if (tex->state.wrap_s != state->wrap_s || set_all)
      glTexParameteri(target, GL_TEXTURE_WRAP_S, convert_wrap(state->wrap_s));
   if (tex->state.wrap_t != state->wrap_t || set_all)
      glTexParameteri(target, GL_TEXTURE_WRAP_T, convert_wrap(state->wrap_t));
   if (tex->state.wrap_r != state->wrap_r || set_all)
      glTexParameteri(target, GL_TEXTURE_WRAP_R, convert_wrap(state->wrap_r));
   if (tex->state.min_img_filter != state->min_img_filter ||
       tex->state.min_mip_filter != state->min_mip_filter || set_all)
      glTexParameterf(target, GL_TEXTURE_MIN_FILTER, convert_min_filter(state->min_img_filter, state->min_mip_filter));
   if (tex->state.min_img_filter != state->mag_img_filter || set_all)
      glTexParameterf(target, GL_TEXTURE_MAG_FILTER, convert_mag_filter(state->mag_img_filter));
   if (tex->state.min_lod != state->min_lod || set_all)
      glTexParameterf(target, GL_TEXTURE_MIN_LOD, state->min_lod);
   if (tex->state.max_lod != state->max_lod || set_all)
      glTexParameterf(target, GL_TEXTURE_MAX_LOD, state->max_lod);
   if (tex->state.lod_bias != state->lod_bias || set_all)
      glTexParameterf(target, GL_TEXTURE_LOD_BIAS, state->lod_bias);

   tex->state = *state;
}

void grend_flush(struct grend_context *ctx)
{
   glFlush();
}

void grend_flush_frontbuffer(uint32_t res_handle)
{
   struct grend_resource *res;

   if (!localrender)
      return;
       
#if 1
   res = graw_object_lookup(res_handle, GRAW_RESOURCE);

   glDrawBuffer(GL_NONE);
   grend_use_program(0);
   
   glMatrixMode(GL_PROJECTION);
   glLoadIdentity();
   glOrtho(0, res->base.width0, 0, res->base.height0, -1, 1);
   glMatrixMode(GL_MODELVIEW);
   glLoadIdentity();
   glDisable(GL_BLEND);
   glBindTexture(res->target, res->id);
   glBindFramebuffer(GL_FRAMEBUFFER_EXT, 0);
   glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
   glTexParameteri(res->target, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
   glTexParameteri(res->target, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

   glTexParameteri(res->target, GL_TEXTURE_BASE_LEVEL, 0);
   glTexParameteri(res->target, GL_TEXTURE_MAX_LEVEL, 0);
   glBegin(GL_QUADS);
#define VAL res->base.width0
   glTexCoord2f(0, 0);
   glVertex2f(0, 0);
   glTexCoord2f(1, 0);
   glVertex2f(VAL, 0);
   glTexCoord2f(1, -1);
   glVertex2f(VAL, VAL);
   glTexCoord2f(0, -1);
   glVertex2f(0, VAL);
   glEnd();
   glDisable(res->target);
   swap_buffers();
#endif
}

static GLenum tgsitargettogltarget(const enum pipe_texture_target target)
{
   switch(target) {
   case PIPE_TEXTURE_1D:
      return GL_TEXTURE_1D;
   case PIPE_TEXTURE_2D:
      return GL_TEXTURE_2D;
   case PIPE_TEXTURE_3D:
      return GL_TEXTURE_3D;
   case PIPE_TEXTURE_RECT:
      return GL_TEXTURE_RECTANGLE_NV;
   case PIPE_TEXTURE_CUBE:
      return GL_TEXTURE_CUBE_MAP;

   case PIPE_TEXTURE_1D_ARRAY:
      return GL_TEXTURE_1D_ARRAY;
   case PIPE_TEXTURE_2D_ARRAY:
      return GL_TEXTURE_2D_ARRAY;
   case PIPE_TEXTURE_CUBE_ARRAY:
      return GL_TEXTURE_CUBE_MAP_ARRAY;
   case PIPE_BUFFER:
   default:
      return PIPE_BUFFER;
   }
   return PIPE_BUFFER;
}

static int inited;

void
graw_renderer_init(void)
{
   if (!inited) {
      inited = 1;
      graw_object_init_hash();
   }
   
   grend_state.viewport_dirty = grend_state.scissor_dirty = TRUE;
   grend_state.program_id = (GLuint)-1;
   list_inithead(&grend_state.fence_list);
}

void
graw_renderer_fini(void)
{
   if (!inited)
      return;

   graw_object_fini_hash();
   inited = 0;
}


struct grend_context *grend_create_context(void)
{
   struct grend_context *grctx = CALLOC_STRUCT(grend_context);
   int i;

   list_inithead(&grctx->programs);

   glGenVertexArrays(1, &grctx->vaoid);
   return grctx;
}


void graw_renderer_resource_create(uint32_t handle, enum pipe_texture_target target, uint32_t format, uint32_t bind, uint32_t width, uint32_t height, uint32_t depth, uint32_t array_size, uint32_t last_level, uint32_t nr_samples)
{
   struct grend_resource *gr = (struct grend_resource *)CALLOC_STRUCT(grend_texture);
   int level;

   gr->base.width0 = width;
   gr->base.height0 = height;
   gr->base.format = format;
   gr->base.target = target;
   if (bind == PIPE_BIND_INDEX_BUFFER) {
      gr->target = GL_ELEMENT_ARRAY_BUFFER_ARB;
      glGenBuffersARB(1, &gr->id);
      glBindBufferARB(gr->target, gr->id);
      glBufferData(gr->target, width, NULL, GL_STREAM_DRAW);
   } else if (target == PIPE_BUFFER) {
      gr->target = GL_ARRAY_BUFFER_ARB;
      glGenBuffersARB(1, &gr->id);
      glBindBufferARB(gr->target, gr->id);
      glBufferData(gr->target, width, NULL, GL_STREAM_DRAW);
   } else {
      struct grend_texture *gt = (struct grend_texture *)gr;
      GLenum internalformat, glformat, gltype;
      gr->target = tgsitargettogltarget(target);
      glGenTextures(1, &gr->id);
      glBindTexture(gr->target, gr->id);

      internalformat = tex_conv_table[format].internalformat;
      glformat = tex_conv_table[format].glformat;
      gltype = tex_conv_table[format].gltype;
      if (internalformat == 0) {
         fprintf(stderr,"unknown format is %d\n", format);
         return 0;
         internalformat = GL_RGBA;
         glformat = GL_RGBA;
         gltype = GL_UNSIGNED_BYTE;
      }

      if (gr->target == GL_TEXTURE_CUBE_MAP) {
         int i;
         for (i = 0; i < 6; i++) {
            GLenum ctarget = GL_TEXTURE_CUBE_MAP_POSITIVE_X + i;
            for (level = 0; level <= last_level; level++) {
               unsigned mwidth = u_minify(width, level);
               unsigned mheight = u_minify(height, level);
               glTexImage2D(ctarget, level, internalformat, mwidth, mheight, 0, glformat,
                            gltype, NULL);
            }
         }
      } else if (gr->target == GL_TEXTURE_3D || gr->target == GL_TEXTURE_2D_ARRAY) {
         glTexImage3D(gr->target, 0, internalformat, width, height, gr->target == GL_TEXTURE_2D_ARRAY ? array_size : depth, 0,
                      glformat,
                      gltype, NULL);
      } else if (gr->target == GL_TEXTURE_1D) {
         glTexImage1D(gr->target, 0, internalformat, width, 0,
                      glformat,
                      gltype, NULL);
      } else {
         for (level = 0; level <= last_level; level++) {
            unsigned mwidth = u_minify(width, level);
            unsigned mheight = u_minify(height, level);
            glTexImage2D(gr->target, level, internalformat, mwidth, gr->target == GL_TEXTURE_1D_ARRAY ? array_size : mheight, 0, glformat,
                         gltype, NULL);
         }
      }

      gt->state.max_lod = -1;
   }

   graw_object_insert(gr, sizeof(*gr), handle, GRAW_RESOURCE);
}

void graw_renderer_resource_unref(uint32_t res_handle)
{
   struct grend_resource *res;

   res = graw_object_lookup(res_handle, GRAW_RESOURCE);

   if (!res)
      return;

   if (res->target == GL_ELEMENT_ARRAY_BUFFER_ARB) {
      glDeleteBuffers(1, &res->id);
   } else if (res->target == GL_ARRAY_BUFFER_ARB) {
      glDeleteBuffers(1, &res->id);
   } else
      glDeleteTextures(1, &res->id);

   graw_object_destroy(res_handle, GRAW_RESOURCE);
}

static int use_sub_data = 0;
static void iov_element_upload(void *cookie, uint32_t doff, void *src, int len)
{
   struct pipe_box *d_box = cookie;
   glBufferSubData(GL_ELEMENT_ARRAY_BUFFER_ARB, d_box->x + doff, len, src);
}

static void iov_vertex_upload(void *cookie, uint32_t doff, void *src, int len)
{
   struct pipe_box *d_box = cookie;
   glBufferSubData(GL_ARRAY_BUFFER_ARB, d_box->x + doff, len, src);
}

void graw_renderer_transfer_write_iov(uint32_t res_handle,
                                      int level,
                                      uint32_t src_stride,
                                      uint32_t flags,
                                      struct pipe_box *dst_box,
                                      uint64_t offset,
                                      struct graw_iovec *iov,
                                      unsigned int num_iovs)
{
   struct grend_resource *res;
   void *data;
   int need_temp;

   res = graw_object_lookup(res_handle, GRAW_RESOURCE);
   if (res == NULL) {
      fprintf(stderr,"transfer for non-existant res %d\n", res_handle);
      return;
   }
   if (res->target == GL_ELEMENT_ARRAY_BUFFER_ARB) {
      glBindBufferARB(GL_ELEMENT_ARRAY_BUFFER_ARB, res->id);

      if (use_sub_data == 1) {
         graw_iov_to_buf_cb(iov, num_iovs, offset, dst_box->width, &iov_element_upload, (void *)dst_box);
      } else {
         data = glMapBufferRange(GL_ELEMENT_ARRAY_BUFFER_ARB, dst_box->x, dst_box->width, GL_MAP_INVALIDATE_RANGE_BIT | GL_MAP_UNSYNCHRONIZED_BIT | GL_MAP_WRITE_BIT);
         if (data == NULL) {
            fprintf(stderr,"map failed for element buffer\n");
            graw_iov_to_buf_cb(iov, num_iovs, offset, dst_box->width, &iov_element_upload, (void *)dst_box);
         } else {
            graw_iov_to_buf(iov, num_iovs, offset, data, dst_box->width);
            glUnmapBuffer(GL_ELEMENT_ARRAY_BUFFER_ARB);
         }
      }
   } else if (res->target == GL_ARRAY_BUFFER_ARB) {
      glBindBufferARB(GL_ARRAY_BUFFER_ARB, res->id);
      if (use_sub_data == 1) {
         graw_iov_to_buf_cb(iov, num_iovs, offset, dst_box->width, &iov_vertex_upload, (void *)dst_box);
      } else {
         data = glMapBufferRange(GL_ARRAY_BUFFER_ARB, dst_box->x, dst_box->width, GL_MAP_INVALIDATE_RANGE_BIT | GL_MAP_UNSYNCHRONIZED_BIT | GL_MAP_WRITE_BIT);
         if (data == NULL) {
            fprintf(stderr,"map failed for array\n");
            graw_iov_to_buf_cb(iov, num_iovs, offset, dst_box->width, &iov_vertex_upload, (void *)dst_box);
         } else {
            graw_iov_to_buf(iov, num_iovs, offset, data, dst_box->width);
            glUnmapBuffer(GL_ARRAY_BUFFER_ARB);
         }
      }
   } else {
      GLenum glformat;
      GLenum gltype;
      int old_stride = 0;
      int invert = flags & (1 << 0);

      grend_use_program(0);

      if (num_iovs > 1) {
         GLuint size = graw_iov_size(iov, num_iovs);
         data = malloc(size);
         graw_iov_to_buf(iov, num_iovs, offset, data, size - offset);
      } else
         data = iov[0].iov_base + offset;

      if (src_stride) {
         glGetIntegerv(GL_UNPACK_ROW_LENGTH, &old_stride);
         glPixelStorei(GL_UNPACK_ROW_LENGTH, src_stride / 4);
      }

      if (res->is_front || invert) {
         if (!res->is_front) {
            if (res->readback_fb_id == 0 || res->readback_fb_level != level) {
               GLuint fb_id;
               if (res->readback_fb_id)
                  glDeleteFramebuffers(1, &res->readback_fb_id);
            
               glGenFramebuffers(1, &fb_id);
               glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, fb_id);
               glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT,
                                         res->target, res->id, level);
               res->readback_fb_id = fb_id;
               res->readback_fb_level = level;
            } else {
               glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, res->readback_fb_id);
            }
            glDrawBuffer(GL_COLOR_ATTACHMENT0_EXT);
         } else {
            glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, 0);
            glDrawBuffer(GL_BACK);
         }
         grend_blend_enable(GL_FALSE);
         grend_depth_test_enable(GL_FALSE);
         grend_alpha_test_enable(GL_FALSE);
         grend_stencil_test_enable(GL_FALSE);
         glPixelZoom(1.0f, (!invert) ? 1.0f : -1.0f);
         glWindowPos2i(dst_box->x, (!invert) ? dst_box->y : res->base.height0 - dst_box->y);
         glDrawPixels(dst_box->width, dst_box->height, GL_BGRA,
                      GL_UNSIGNED_BYTE, data);
      } else {
         glBindTexture(res->target, res->id);

         glformat = tex_conv_table[res->base.format].glformat;
         gltype = tex_conv_table[res->base.format].gltype; 
         if (glformat == 0) {
            glformat = GL_BGRA;
            gltype = GL_UNSIGNED_BYTE;
         }
	 
         if (res->target == GL_TEXTURE_CUBE_MAP) {
            GLenum ctarget = GL_TEXTURE_CUBE_MAP_POSITIVE_X + dst_box->z;
            
            glTexSubImage2D(ctarget, level, dst_box->x, dst_box->y, dst_box->width, dst_box->height,
                            glformat, gltype, data);
         } else if (res->target == GL_TEXTURE_3D || res->target == GL_TEXTURE_2D_ARRAY) {
            glTexSubImage3D(res->target, level, dst_box->x, dst_box->y, dst_box->z,
                            dst_box->width, dst_box->height, dst_box->depth,
                            glformat, gltype, data);
         } else if (res->target == GL_TEXTURE_1D) {
            glTexSubImage1D(res->target, level, dst_box->x, dst_box->width,
                            glformat, gltype, data);
         } else {
            glTexSubImage2D(res->target, level, dst_box->x, res->target == GL_TEXTURE_1D_ARRAY ? dst_box->z : dst_box->y,
                            dst_box->width, dst_box->height,
                            glformat, gltype, data);
         }
      }
      if (src_stride)
         glPixelStorei(GL_UNPACK_ROW_LENGTH, old_stride);

      if (num_iovs > 1)
         free(data);
   }

}

void graw_renderer_transfer_send_iov(uint32_t res_handle, uint32_t level, uint32_t flags, struct pipe_box *box, uint64_t offset, struct graw_iovec *iov, int num_iovs)
{
   struct grend_resource *res;
   void *myptr = iov[0].iov_base + offset;
   int need_temp = 0;

   res = graw_object_lookup(res_handle, GRAW_RESOURCE);
   if (!res) {
      fprintf(stderr,"transfer for non-existant res %d\n", res_handle);
      return;
   }

   if (res->target == GL_ELEMENT_ARRAY_BUFFER_ARB) {
      uint32_t send_size = box->width * util_format_get_blocksize(res->base.format);      
      void *data;
      glBindBufferARB(GL_ELEMENT_ARRAY_BUFFER_ARB, res->id);
      data = glMapBuffer(GL_ELEMENT_ARRAY_BUFFER_ARB, GL_READ_ONLY);
      graw_transfer_write_return(data + box->x, send_size, offset, iov, num_iovs);
      glUnmapBuffer(GL_ELEMENT_ARRAY_BUFFER_ARB);
   } else if (res->target == GL_ARRAY_BUFFER_ARB) {
      uint32_t send_size = box->width * util_format_get_blocksize(res->base.format);      
      void *data;
      glBindBufferARB(GL_ARRAY_BUFFER_ARB, res->id);
      data = glMapBuffer(GL_ARRAY_BUFFER_ARB, GL_READ_ONLY);
      graw_transfer_write_return(data + box->x, send_size, offset, iov, num_iovs);
      glUnmapBuffer(GL_ARRAY_BUFFER_ARB);
   } else {
      uint32_t h = u_minify(res->base.height0, level);
      GLenum format, type;
      GLuint fb_id;
      GLint  y1;
      uint32_t send_size = 0;
      void *data;
      boolean invert = flags & (1 << 0) ? TRUE : FALSE;
      boolean actually_invert, separate_invert = FALSE;

      grend_use_program(0);

      /* if we are asked to invert and reading from a front then don't */
      if (invert && res->is_front)
         actually_invert = FALSE;
      else
         actually_invert = res->renderer_flipped ^ invert;

      if (actually_invert && !have_invert_mesa)
         separate_invert = TRUE;

      if (num_iovs > 1 || separate_invert)
         need_temp = 1;

      if (need_temp) {
         send_size = box->width * box->height * box->depth * util_format_get_blocksize(res->base.format);      
         data = malloc(send_size);
         if (!data)
            fprintf(stderr,"malloc failed %d\n", send_size);
      } else
         data = myptr;
//      fprintf(stderr,"TEXTURE TRANSFER %d %d %d %d %d, temp:%d\n", res_handle, res->readback_fb_id, box->width, box->height, level, need_temp);

      if (!res->is_front) {
         if (res->readback_fb_id == 0 || res->readback_fb_level != level) {
            if (res->readback_fb_id)
               glDeleteFramebuffers(1, &res->readback_fb_id);
            
            glGenFramebuffers(1, &fb_id);
            glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, fb_id);
            glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT,
                                      res->target, res->id, level);
            res->readback_fb_id = fb_id;
            res->readback_fb_level = level;
         } else {
            glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, res->readback_fb_id);
         }
         y1 = h - box->y - box->height;
         if (have_invert_mesa && actually_invert)
            glPixelStorei(GL_PACK_INVERT_MESA, 1);
         glReadBuffer(GL_COLOR_ATTACHMENT0_EXT);      
      } else {
         glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, 0);
         y1 = box->y;
         glReadBuffer(GL_BACK);
      }
            
      switch (res->base.format) {
      case PIPE_FORMAT_B8G8R8A8_UNORM:
      default:
         format = tex_conv_table[res->base.format].glformat;
         type = tex_conv_table[res->base.format].gltype; 
         break;
      }

      glReadPixels(box->x, y1, box->width, box->height, format, type, data);

      if (have_invert_mesa && actually_invert)
         glPixelStorei(GL_PACK_INVERT_MESA, 0);
      if (need_temp) {
         graw_transfer_write_tex_return(&res->base, box, level, offset, iov, num_iovs, data, send_size, separate_invert);
         free(data);
      }
   }
}

void grend_set_stencil_ref(struct grend_context *ctx,
                           struct pipe_stencil_ref *ref)
{
   if (ctx->stencil_refs[0] != ref->ref_value[0] ||
       ctx->stencil_refs[1] != ref->ref_value[1]) {
      ctx->stencil_refs[0] = ref->ref_value[0];
      ctx->stencil_refs[1] = ref->ref_value[1];
      ctx->stencil_state_dirty = TRUE;
   }
   
}

void grend_set_blend_color(struct grend_context *ctx,
                           struct pipe_blend_color *color)
{
   glBlendColor(color->color[0], color->color[1], color->color[2],
                color->color[3]);
}

void grend_set_scissor_state(struct grend_context *ctx,
                             struct pipe_scissor_state *ss)
{
   ctx->ss = *ss;
   ctx->scissor_state_dirty = TRUE;
}

void graw_renderer_resource_copy_region(struct grend_context *ctx,
                                        uint32_t dst_handle, uint32_t dst_level,
                                        uint32_t dstx, uint32_t dsty, uint32_t dstz,
                                        uint32_t src_handle, uint32_t src_level,
                                        const struct pipe_box *src_box)
{
   struct grend_resource *src_res, *dst_res;   
   GLuint fb_ids[2];
   GLbitfield glmask = 0;
   GLint sy1, sy2, dy1, dy2;
   src_res = graw_object_lookup(src_handle, GRAW_RESOURCE);
   dst_res = graw_object_lookup(dst_handle, GRAW_RESOURCE);

   if (!src_res || !dst_res) {
      fprintf(stderr,"illegal handle in copy region %d %d\n", src_handle, dst_handle);
      return;
   }

   glGenFramebuffers(2, fb_ids);
   glBindFramebuffer(GL_FRAMEBUFFER_EXT, fb_ids[0]);
   glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT,
                             src_res->target, src_res->id, src_level);
      
   if (!dst_res->is_front) {
      glBindFramebuffer(GL_FRAMEBUFFER_EXT, fb_ids[1]);
      glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT,
                                dst_res->target, dst_res->id, dst_level);
      glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fb_ids[1]);
   } else
      glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);

   glBindFramebuffer(GL_READ_FRAMEBUFFER, fb_ids[0]);

   glmask = GL_COLOR_BUFFER_BIT;
   glDisable(GL_SCISSOR_TEST);

   if (!src_res->is_front && !src_res->renderer_flipped) {
      sy1 = src_box->y;
      sy2 = src_box->y + src_box->height;
   } else {
      sy1 = src_res->base.height0 - src_box->y - src_box->height;
      sy2 = src_res->base.height0 - src_box->y;
   }

   if (!dst_res->is_front && !dst_res->renderer_flipped) {
      dy1 = dsty + src_box->height;
      dy2 = dsty;
   } else {
      dy1 = dst_res->base.height0 - dsty - src_box->height;
      dy2 = dst_res->base.height0 - dsty;
   }

   glBlitFramebuffer(src_box->x, sy1,
                     src_box->x + src_box->width,
                     sy2,
                     dstx, dy1,
                     dstx + src_box->width,
                     dy2,
                     glmask, GL_NEAREST);

   glDeleteFramebuffers(2, fb_ids);
}

static void graw_renderer_blit_int(uint32_t dst_handle, uint32_t src_handle,
                                   const struct pipe_blit_info *info)
{
   struct grend_resource *src_res, *dst_res;
   GLuint fb_ids[2];
   GLbitfield glmask = 0;
   int src_y1, src_y2, dst_y1, dst_y2;

   src_res = graw_object_lookup(src_handle, GRAW_RESOURCE);
   dst_res = graw_object_lookup(dst_handle, GRAW_RESOURCE);

   if (!src_res || !dst_res) {
      fprintf(stderr,"illegal handle in blit %d %d\n", src_handle, dst_handle);
      return;
   }
   glGenFramebuffers(2, fb_ids);
   glBindFramebuffer(GL_FRAMEBUFFER_EXT, fb_ids[0]);

   if (src_res->target == GL_TEXTURE_CUBE_MAP) {
      glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT,
                                GL_TEXTURE_CUBE_MAP_POSITIVE_X + info->src.level, src_res->id, 0);
   } else
      glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT,
                                src_res->target, src_res->id, info->src.level);


   if (!dst_res->is_front) {
      glBindFramebuffer(GL_FRAMEBUFFER_EXT, fb_ids[1]);
      glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT,
                                dst_res->target, dst_res->id, info->dst.level);
      
      glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fb_ids[1]);
   } else
      glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);

   glBindFramebuffer(GL_READ_FRAMEBUFFER, fb_ids[0]);

   if (info->mask & PIPE_MASK_Z)
      glmask |= GL_DEPTH_BUFFER_BIT;
   if (info->mask & PIPE_MASK_S)
      glmask |= GL_STENCIL_BUFFER_BIT;
   if (info->mask & PIPE_MASK_RGBA)
      glmask |= GL_COLOR_BUFFER_BIT;

   if (!dst_res->is_front && !dst_res->renderer_flipped) {
      dst_y1 = info->dst.box.y + info->dst.box.height;
      dst_y2 = info->dst.box.y;
   } else {
      dst_y1 = dst_res->base.height0 - info->dst.box.y - info->dst.box.height;
      dst_y2 = dst_res->base.height0 - info->dst.box.y;
   }

   if (!src_res->is_front && !src_res->renderer_flipped) {
      src_y1 = info->src.box.y + info->src.box.height;
      src_y2 = info->src.box.y;
   } else {
      src_y1 = src_res->base.height0 - info->src.box.y - info->src.box.height;
      src_y2 = src_res->base.height0 - info->src.box.y;
   }

   if (info->scissor_enable)
      glEnable(GL_SCISSOR_TEST);
   else
      glDisable(GL_SCISSOR_TEST);
      
   glBlitFramebuffer(info->src.box.x,
                     src_y1,
                     info->src.box.x + info->src.box.width,
                     src_y2,
                     info->dst.box.x,
                     dst_y1,
                     info->dst.box.x + info->dst.box.width,
                     dst_y2,
                     glmask, convert_mag_filter(info->filter));

   glDeleteFramebuffers(2, fb_ids);
}

void graw_renderer_blit(struct grend_context *ctx,
                        uint32_t dst_handle, uint32_t src_handle,
                        const struct pipe_blit_info *info)
{
   graw_renderer_blit_int(dst_handle, src_handle, info);
}

int graw_renderer_set_scanout(uint32_t res_handle,
                              struct pipe_box *box)
{
   struct grend_resource *res;
   res = graw_object_lookup(res_handle, GRAW_RESOURCE);
   if (!res)
      return 0;

#if 0
   if (frontbuffer && frontbuffer != res) {
      struct pipe_blit_info bi;

      bi.dst.level = 0;
      bi.dst.box.x = bi.dst.box.y = bi.dst.box.z = 0;
      bi.dst.box.width = res->base.width0;
      bi.dst.box.height = res->base.height0;
      bi.dst.box.depth = 1;

      bi.src.level = 0;
      bi.src.box.x = bi.src.box.y = bi.src.box.z = 0;
      bi.src.box.width = res->base.width0;
      bi.src.box.height = res->base.height0;
      bi.src.box.depth = 1;

      bi.mask = PIPE_MASK_RGBA;
      bi.filter = PIPE_TEX_FILTER_NEAREST;
      bi.scissor_enable = 0;

      res->is_front = 1;
      graw_renderer_blit_int(res_handle, res_handle, &bi);
      
      frontbuffer->is_front = 0;
   }
#endif
   frontbuffer = res;
   fprintf(stderr,"setting frontbuffer to %d\n", res_handle);
   return 0;
}

int graw_renderer_flush_buffer(uint32_t res_handle,
                               struct pipe_box *box)
{
   struct grend_resource *res;
   GLuint fb_id;

   if (!localrender)
      return 0;

   res = graw_object_lookup(res_handle, GRAW_RESOURCE);
   if (!res)
      return 0;

   if (res != frontbuffer) {
      fprintf(stderr,"not the frontbuffer %d\n", res_handle);
      return 0;
   }

   if (!res->is_front) {
      glGenFramebuffers(1, &fb_id);

      glBindFramebuffer(GL_FRAMEBUFFER_EXT, fb_id);
      glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT,
                                res->target, res->id, 0);

      glBindFramebuffer(GL_READ_FRAMEBUFFER, fb_id);
      glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
      
      /* force full scissor and viewport - should probably use supplied box */
      glViewport(0, 0, res->base.width0, res->base.height0);
      glScissor(box->x, box->y, box->width, box->height);

      grend_state.viewport_dirty = TRUE;
      grend_state.scissor_dirty = TRUE;
      /* justification for inversion here required */
      glBlitFramebuffer(0, 0, res->base.width0, res->base.height0,
                        0, res->base.height0, res->base.width0, 0,
                        GL_COLOR_BUFFER_BIT, GL_NEAREST);
      glDeleteFramebuffers(1, &fb_id);
   } else {
      glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
      glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
   }
   swap_buffers();
   return 0;
}

int graw_renderer_create_fence(int client_fence_id)
{
   struct grend_fence *fence;

   fence = malloc(sizeof(struct grend_fence));
   if (!fence)
      return -1;

   fence->fence_id = client_fence_id;
   fence->syncobj = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
   list_addtail(&fence->fences, &grend_state.fence_list);
   return 0;
}

void graw_renderer_check_fences(void)
{
   struct grend_fence *fence, *stor;
   uint32_t latest_id = 0;
   GLenum glret;

   if (!inited)
      return;

   LIST_FOR_EACH_ENTRY_SAFE(fence, stor, &grend_state.fence_list, fences) {
      glret = glClientWaitSync(fence->syncobj, 0, 0);
      if (glret == GL_ALREADY_SIGNALED){
         latest_id = fence->fence_id;
         list_del(&fence->fences);
         glDeleteSync(fence->syncobj);
         free(fence);
      }
      /* don't bother checking any subsequent ones */
      else if (glret == GL_TIMEOUT_EXPIRED) {
         break;
      }
   }

   if (latest_id == 0)
      return;
   graw_write_fence(latest_id);
}
