/**************************************************************************
 *
 * Copyright (C) 2014 Red Hat Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 **************************************************************************/

#include "tgsi/tgsi_info.h"
#include "tgsi/tgsi_iterate.h"
#include <string.h>
#include <stdio.h>
#include <math.h>
#include "vrend_shader.h"
extern int vrend_dump_shaders;
/*
 * TODO list
 * loops
 * DDX/DDY/TXD
 * missing opcodes
 */

/* start convert of tgsi to glsl */

#define INTERP_PREFIX "               "

int vrend_shader_use_explicit = 0;

struct vrend_shader_io {
   unsigned		name;
   unsigned		gpr;
   unsigned		done;
   int			sid;
   unsigned		interpolate;
   boolean                 centroid;
   unsigned first;
   boolean glsl_predefined_no_emit;
   boolean glsl_no_index;
   boolean override_no_wm;
   char glsl_name[64];
};

struct vrend_shader_sampler {
   int tgsi_sampler_type;
};

struct immed {
   int type;
   union imm {
      uint32_t ui;
      int32_t i;
      float f;
   } val[4];
};

struct dump_ctx {
   struct tgsi_iterate_context iter;
   struct vrend_shader_cfg *cfg;
   int prog_type;
   char *glsl_main;
   int size;
   uint instno;

   int num_interps;
   int num_inputs;
   struct vrend_shader_io inputs[32];
   int num_outputs;
   struct vrend_shader_io outputs[32];
   int num_system_values;
   struct vrend_shader_io system_values[32];

   int num_temps;
   struct vrend_shader_sampler samplers[32];
   uint32_t samplers_used;
   int num_consts;

   int num_imm;
   struct immed imm[32];
   unsigned fragcoord_input;

   int num_ubo;
   int ubo_idx[32];
   int ubo_sizes[32];
   int num_address;
   
   struct pipe_stream_output_info *so;
   bool uses_cube_array;
   bool uses_sampler_ms;
   bool uses_sampler_buf;
   bool uses_sampler_rect;
   /* create a shader with lower left if upper left is primary variant
      or vice versa */
   uint32_t shadow_samp_mask;
   boolean write_all_cbufs;
   int fs_coord_origin, fs_pixel_center;

   int gs_in_prim, gs_out_prim, gs_max_out_verts;

   struct vrend_shader_key *key;
   boolean has_ints;
   boolean has_instanceid;
   int indent_level;
   int num_clip_dist;

   int glsl_ver_required;
   bool front_face_emitted;
   int color_in_mask;
   bool has_clipvertex;
};

static inline const char *tgsi_proc_to_prefix(int shader_type)
{
   switch (shader_type) {
   case TGSI_PROCESSOR_VERTEX: return "vs";
   case TGSI_PROCESSOR_FRAGMENT: return "fs";
   case TGSI_PROCESSOR_GEOMETRY: return "gs";
   };
   return NULL;
}

static inline const char *prim_to_name(int prim)
{
   switch (prim) {
   case PIPE_PRIM_POINTS: return "points";
   case PIPE_PRIM_LINES: return "lines";
   case PIPE_PRIM_LINE_STRIP: return "line_strip";
   case PIPE_PRIM_LINES_ADJACENCY: return "lines_adjacency";
   case PIPE_PRIM_TRIANGLES: return "triangles";
   case PIPE_PRIM_TRIANGLE_STRIP: return "triangle_strip";
   case PIPE_PRIM_TRIANGLES_ADJACENCY: return "triangles_adjacency";
   default: return "UNKNOWN";
   };
}

static inline int gs_input_prim_to_size(int prim)
{
   switch (prim) {
   case PIPE_PRIM_POINTS: return 1;
   case PIPE_PRIM_LINES: return 2;
   case PIPE_PRIM_LINES_ADJACENCY: return 4;
   case PIPE_PRIM_TRIANGLES: return 3;
   case PIPE_PRIM_TRIANGLES_ADJACENCY: return 6;
   default: return -1;
   };
}

static inline boolean fs_emit_layout(struct dump_ctx *ctx)
{
   if (ctx->fs_pixel_center)
      return TRUE;
   /* if coord origin is 0 and invert is 0 - emit origin_upper_left,
      if coord_origin is 0 and invert is 1 - emit nothing (lower)
      if coord origin is 1 and invert is 0 - emit nothing (lower)
      if coord_origin is 1 and invert is 1 - emit origin upper left */
   if (!(ctx->fs_coord_origin ^ ctx->key->invert_fs_origin))
       return TRUE;
   return FALSE;
}

static boolean
iter_declaration(struct tgsi_iterate_context *iter,
                 struct tgsi_full_declaration *decl )
{
   struct dump_ctx *ctx = (struct dump_ctx *)iter;
   int i;
   int color_offset = 0;
   char *name_prefix = "";
   bool add_two_side = false;

   ctx->prog_type = iter->processor.Processor;
   switch (decl->Declaration.File) {
   case TGSI_FILE_INPUT:
      i = ctx->num_inputs++;
      ctx->inputs[i].name = decl->Semantic.Name;
      ctx->inputs[i].sid = decl->Semantic.Index;
      ctx->inputs[i].interpolate = decl->Interp.Interpolate;
      ctx->inputs[i].centroid = decl->Interp.Centroid;
      ctx->inputs[i].first = decl->Range.First;
      ctx->inputs[i].glsl_predefined_no_emit = FALSE;
      ctx->inputs[i].glsl_no_index = FALSE;
      ctx->inputs[i].override_no_wm = FALSE;

      switch (ctx->inputs[i].name) {
      case TGSI_SEMANTIC_COLOR:
         if (iter->processor.Processor == TGSI_PROCESSOR_FRAGMENT) {
            if (ctx->glsl_ver_required < 140) {
               if (decl->Semantic.Index == 0)
                  name_prefix = "gl_Color";
               else if (decl->Semantic.Index == 1)
                  name_prefix = "gl_SecondaryColor";
               else
                  fprintf(stderr, "got illegal color semantic index %d\n", decl->Semantic.Index);
               ctx->inputs[i].glsl_no_index = TRUE;
            } else {
               if (ctx->key->color_two_side) {
                  int j = ctx->num_inputs++;
                  ctx->inputs[j].name = TGSI_SEMANTIC_BCOLOR;
                  ctx->inputs[j].sid = decl->Semantic.Index;
                  ctx->inputs[j].interpolate = decl->Interp.Interpolate;
                  ctx->inputs[j].centroid = decl->Interp.Centroid;
                  ctx->inputs[j].first = decl->Range.First;
                  ctx->inputs[j].glsl_predefined_no_emit = FALSE;
                  ctx->inputs[j].glsl_no_index = FALSE;
                  ctx->inputs[j].override_no_wm = FALSE;

                  ctx->color_in_mask |= (1 << decl->Semantic.Index);

                  if (ctx->front_face_emitted == false) {
                     int k = ctx->num_inputs++;
                     ctx->inputs[k].name = TGSI_SEMANTIC_FACE;
                     ctx->inputs[k].sid = 0;
                     ctx->inputs[k].interpolate = 0;
                     ctx->inputs[k].centroid = 0;
                     ctx->inputs[k].first = 0;
                     ctx->inputs[k].override_no_wm = FALSE;
                     ctx->inputs[k].glsl_predefined_no_emit = TRUE;
                     ctx->inputs[k].glsl_no_index = TRUE;
                  }
                  add_two_side = true;
               }
            }
            name_prefix = "ex";
            break;
         }
         /* fallthrough */
      case TGSI_SEMANTIC_POSITION:
         if (iter->processor.Processor == TGSI_PROCESSOR_FRAGMENT) {
            name_prefix = "gl_FragCoord";
            ctx->inputs[i].glsl_predefined_no_emit = TRUE;
            ctx->inputs[i].glsl_no_index = TRUE;
            break;
         }
         /* fallthrough for vertex shader */
      case TGSI_SEMANTIC_FACE:
         if (iter->processor.Processor == TGSI_PROCESSOR_FRAGMENT) {
            if (ctx->front_face_emitted) {
               ctx->num_inputs--;
               return 0;
            }
            name_prefix = "gl_FrontFacing";
            ctx->inputs[i].glsl_predefined_no_emit = TRUE;
            ctx->inputs[i].glsl_no_index = TRUE;
            ctx->front_face_emitted = TRUE;
            break;
         }
      case TGSI_SEMANTIC_GENERIC:
         if (iter->processor.Processor == TGSI_PROCESSOR_FRAGMENT) {
            if (ctx->key->coord_replace & (1 << ctx->inputs[i].sid)) {
               fprintf(stderr,"replacing generic %d with coord\n", ctx->inputs[i].sid);
               name_prefix = "vec4(gl_PointCoord, 0.0, 1.0)";
               ctx->inputs[i].glsl_predefined_no_emit = TRUE;
               ctx->inputs[i].glsl_no_index = TRUE;
               break;
            }
         }
      default:
         if (iter->processor.Processor == TGSI_PROCESSOR_FRAGMENT ||
             iter->processor.Processor == TGSI_PROCESSOR_GEOMETRY)
            name_prefix = "ex";
         else
            name_prefix = "in";
         break;
      }

      if (ctx->inputs[i].glsl_no_index)
         snprintf(ctx->inputs[i].glsl_name, 64, "%s", name_prefix);
      else {
         if (ctx->inputs[i].name == TGSI_SEMANTIC_FOG)
            snprintf(ctx->inputs[i].glsl_name, 64, "%s_f%d", name_prefix, ctx->inputs[i].sid);
         else if (ctx->inputs[i].name == TGSI_SEMANTIC_COLOR)
            snprintf(ctx->inputs[i].glsl_name, 64, "%s_c%d", name_prefix, ctx->inputs[i].sid);
         else if (ctx->inputs[i].name == TGSI_SEMANTIC_GENERIC)
            snprintf(ctx->inputs[i].glsl_name, 64, "%s_g%d", name_prefix, ctx->inputs[i].sid);
         else
            snprintf(ctx->inputs[i].glsl_name, 64, "%s_%d", name_prefix, ctx->inputs[i].first);
      }
      if (add_two_side) {
         snprintf(ctx->inputs[i + 1].glsl_name, 64, "%s_bc%d", name_prefix, ctx->inputs[i + 1].sid);
         if (!ctx->front_face_emitted) {
            snprintf(ctx->inputs[i + 2].glsl_name, 64, "%s", "gl_FrontFacing");
            ctx->front_face_emitted = true;
         }
      }
      break;
   case TGSI_FILE_OUTPUT:
      i = ctx->num_outputs++;
      ctx->outputs[i].name = decl->Semantic.Name;
      ctx->outputs[i].sid = decl->Semantic.Index;
      ctx->outputs[i].interpolate = decl->Interp.Interpolate;
      ctx->outputs[i].first = decl->Range.First;
      ctx->outputs[i].glsl_predefined_no_emit = FALSE;
      ctx->outputs[i].glsl_no_index = FALSE;
      ctx->outputs[i].override_no_wm = FALSE;
      switch (ctx->outputs[i].name) {
      case TGSI_SEMANTIC_POSITION:
         if (iter->processor.Processor == TGSI_PROCESSOR_VERTEX ||
             iter->processor.Processor == TGSI_PROCESSOR_GEOMETRY) {
            if (ctx->outputs[i].first > 0)
               fprintf(stderr,"Illegal position input\n");
            name_prefix = "gl_Position";
            ctx->outputs[i].glsl_predefined_no_emit = TRUE;
            ctx->outputs[i].glsl_no_index = TRUE;
         } else if (iter->processor.Processor == TGSI_PROCESSOR_FRAGMENT) {
            if (ctx->outputs[i].first > 0)
               fprintf(stderr,"Illegal position input\n");
            name_prefix = "gl_FragDepth";
            ctx->outputs[i].glsl_predefined_no_emit = TRUE;
            ctx->outputs[i].glsl_no_index = TRUE;
            ctx->outputs[i].override_no_wm = TRUE;
         }
         break;
      case TGSI_SEMANTIC_CLIPDIST:
         name_prefix = "gl_ClipDistance";
         ctx->outputs[i].glsl_predefined_no_emit = TRUE;
         ctx->outputs[i].glsl_no_index = TRUE;
         ctx->num_clip_dist += 4;
         break;
      case TGSI_SEMANTIC_CLIPVERTEX:
         name_prefix = "gl_ClipVertex";
         ctx->outputs[i].glsl_predefined_no_emit = TRUE;
         ctx->outputs[i].glsl_no_index = TRUE;
         ctx->outputs[i].override_no_wm = TRUE;
         if (ctx->glsl_ver_required >= 140)
            ctx->has_clipvertex = true;
         break;

      case TGSI_SEMANTIC_COLOR:
         if (iter->processor.Processor == TGSI_PROCESSOR_VERTEX) {
            if (ctx->glsl_ver_required < 140) {
               ctx->outputs[i].glsl_no_index = TRUE;
               if (ctx->outputs[i].sid == 0)
                  name_prefix = "gl_FrontColor";
               else if (ctx->outputs[i].sid == 1)
                  name_prefix = "gl_FrontSecondaryColor";
            } else
               name_prefix = "ex";
            break;
         }

      case TGSI_SEMANTIC_BCOLOR:
         if (iter->processor.Processor == TGSI_PROCESSOR_VERTEX) {
            if (ctx->glsl_ver_required < 140) {
               ctx->outputs[i].glsl_no_index = TRUE;
               if (ctx->outputs[i].sid == 0)
                  name_prefix = "gl_BackColor";
               else if (ctx->outputs[i].sid == 1)
                  name_prefix = "gl_BackSecondaryColor";
               break;
            } else
               name_prefix = "ex";
            break;
         }
      case TGSI_SEMANTIC_PSIZE:
         if (iter->processor.Processor == TGSI_PROCESSOR_VERTEX) {
            ctx->outputs[i].glsl_predefined_no_emit = TRUE;
            ctx->outputs[i].glsl_no_index = TRUE;
            ctx->outputs[i].override_no_wm = TRUE;
            name_prefix = "gl_PointSize";
            break;
         }
      case TGSI_SEMANTIC_GENERIC:
         if (iter->processor.Processor == TGSI_PROCESSOR_VERTEX)
            if (ctx->outputs[i].name == TGSI_SEMANTIC_GENERIC)
               color_offset = -1;
      default:
         if (iter->processor.Processor == TGSI_PROCESSOR_VERTEX)
            name_prefix = "ex";
         else
            name_prefix = "out";
         break;
      }

      if (ctx->outputs[i].glsl_no_index)
         snprintf(ctx->outputs[i].glsl_name, 64, "%s", name_prefix);
      else {
         if (ctx->outputs[i].name == TGSI_SEMANTIC_FOG)
            snprintf(ctx->outputs[i].glsl_name, 64, "%s_f%d", name_prefix, ctx->outputs[i].sid);
         else if (ctx->outputs[i].name == TGSI_SEMANTIC_COLOR)
            snprintf(ctx->outputs[i].glsl_name, 64, "%s_c%d", name_prefix, ctx->outputs[i].sid);
         else if (ctx->outputs[i].name == TGSI_SEMANTIC_BCOLOR)
            snprintf(ctx->outputs[i].glsl_name, 64, "%s_bc%d", name_prefix, ctx->outputs[i].sid);
         else if (ctx->outputs[i].name == TGSI_SEMANTIC_GENERIC)
            snprintf(ctx->outputs[i].glsl_name, 64, "%s_g%d", name_prefix, ctx->outputs[i].sid);
         else
            snprintf(ctx->outputs[i].glsl_name, 64, "%s_%d", name_prefix, ctx->outputs[i].first + color_offset);

      }
      break;
   case TGSI_FILE_TEMPORARY:
      if (decl->Range.Last) {
         if (decl->Range.Last + 1 > ctx->num_temps)
            ctx->num_temps = decl->Range.Last + 1;
      } else
         ctx->num_temps++;

      break;
   case TGSI_FILE_SAMPLER:
      ctx->samplers_used |= (1 << decl->Range.Last);
      break;
   case TGSI_FILE_CONSTANT:
      if (decl->Declaration.Dimension) {
         ctx->ubo_idx[ctx->num_ubo] = decl->Dim.Index2D;
         ctx->ubo_sizes[ctx->num_ubo] = decl->Range.Last;
         ctx->num_ubo++;
      } else {
         if (decl->Range.Last) {
            if (decl->Range.Last + 1 > ctx->num_consts)
               ctx->num_consts = decl->Range.Last + 1;
         } else
            ctx->num_consts++;
      }
      break;
   case TGSI_FILE_ADDRESS:
      ctx->num_address = 1;
      break;
   case TGSI_FILE_SYSTEM_VALUE:
      i = ctx->num_system_values++;
      ctx->system_values[i].name = decl->Semantic.Name;
      ctx->system_values[i].sid = decl->Semantic.Index;
      ctx->system_values[i].glsl_predefined_no_emit = TRUE;
      ctx->system_values[i].glsl_no_index = TRUE;
      ctx->system_values[i].override_no_wm = TRUE;
      ctx->system_values[i].first = decl->Range.First;
      if (decl->Semantic.Name == TGSI_SEMANTIC_INSTANCEID) {
         name_prefix = "gl_InstanceID";
         ctx->has_instanceid = TRUE;
      } else if (decl->Semantic.Name == TGSI_SEMANTIC_VERTEXID) {
	 name_prefix = "gl_VertexID";
      } else {
         fprintf(stderr, "unsupported system value %d\n", decl->Semantic.Name);
         name_prefix = "unknown";
      }
      snprintf(ctx->system_values[i].glsl_name, 64, "%s", name_prefix);
      break;
   default:
      fprintf(stderr,"unsupported file %d declaration\n", decl->Declaration.File);
      break;
   }


   return TRUE;
}

static boolean
iter_property(struct tgsi_iterate_context *iter,
              struct tgsi_full_property *prop)
{
   struct dump_ctx *ctx = (struct dump_ctx *) iter;

   if (prop->Property.PropertyName == TGSI_PROPERTY_FS_COLOR0_WRITES_ALL_CBUFS) {
      if (prop->u[0].Data == 1)
         ctx->write_all_cbufs = TRUE;
   }

   if (prop->Property.PropertyName == TGSI_PROPERTY_FS_COORD_ORIGIN) {
      ctx->fs_coord_origin = prop->u[0].Data;
   }

   if (prop->Property.PropertyName == TGSI_PROPERTY_FS_COORD_PIXEL_CENTER) {
      ctx->fs_pixel_center = prop->u[0].Data;
   }

   if (prop->Property.PropertyName == TGSI_PROPERTY_GS_INPUT_PRIM) {
      ctx->gs_in_prim = prop->u[0].Data;
   }

   if (prop->Property.PropertyName == TGSI_PROPERTY_GS_OUTPUT_PRIM) {
      ctx->gs_out_prim = prop->u[0].Data;
   }

   if (prop->Property.PropertyName == TGSI_PROPERTY_GS_MAX_OUTPUT_VERTICES) {
      ctx->gs_max_out_verts = prop->u[0].Data;
   }
   return TRUE;
}

static boolean
iter_immediate(
   struct tgsi_iterate_context *iter,
   struct tgsi_full_immediate *imm )
{
   struct dump_ctx *ctx = (struct dump_ctx *) iter;
   int i;
   int first = ctx->num_imm;
   ctx->imm[first].type = imm->Immediate.DataType;
   for (i = 0; i < 4; i++) {
      if (imm->Immediate.DataType == TGSI_IMM_FLOAT32) {
         ctx->imm[first].val[i].f = imm->u[i].Float;
      } else if (imm->Immediate.DataType == TGSI_IMM_UINT32) {
         ctx->has_ints = TRUE;
         ctx->imm[first].val[i].ui  = imm->u[i].Uint;
      } else if (imm->Immediate.DataType == TGSI_IMM_INT32) {
         ctx->has_ints = TRUE;
         ctx->imm[first].val[i].i = imm->u[i].Int;
      } 
   }
   ctx->num_imm++;
   return TRUE;
}

static char get_swiz_char(int swiz)
{
   switch(swiz){
   case TGSI_SWIZZLE_X: return 'x';
   case TGSI_SWIZZLE_Y: return 'y';
   case TGSI_SWIZZLE_Z: return 'z';
   case TGSI_SWIZZLE_W: return 'w';
   }
   return 0;
}

static void emit_cbuf_writes(struct dump_ctx *ctx)
{
   char buf[255];
   int i;

   for (i = 1; i < 8; i++) {
      snprintf(buf, 255, "out_c%d = out_c0;\n", i);
      strcat(ctx->glsl_main, buf);
   }
}

static const char *atests[PIPE_FUNC_ALWAYS + 1] = {
   "false",
   "%s < %f",
   "%s == %f",
   "%s <= %f",
   "%s > %f",
   "%s != %f",
   "%s >= %f",
   "true",
};

static void emit_alpha_test(struct dump_ctx *ctx)
{
   char buf[255];
   char comp_buf[128];

   snprintf(comp_buf, 128, atests[ctx->key->alpha_test], "out_c0.w", ctx->key->alpha_ref_val);

   snprintf(buf, 255, "if (!(%s)) {\n\tdiscard;\n}\n", comp_buf);
   strcat(ctx->glsl_main, buf);

}

static void emit_pstipple_pass(struct dump_ctx *ctx)
{
   char buf[255];

   snprintf(buf, 255, "stip_temp = texture(pstipple_sampler, vec2(gl_FragCoord.x / 32, gl_FragCoord.y / 32)).x;\n");
   strcat(ctx->glsl_main, buf);
   snprintf(buf, 255, "if (stip_temp > 0) {\n\tdiscard;\n}\n");
   strcat(ctx->glsl_main, buf);
}

static void emit_color_select(struct dump_ctx *ctx)
{
   char buf[255];

   if (!ctx->key->color_two_side)
      return;

   if (ctx->color_in_mask & 1) {
      snprintf(buf, 255, "realcolor0 = gl_FrontFacing ? ex_c0 : ex_bc0;\n");
      strcat(ctx->glsl_main, buf);
   }
   if (ctx->color_in_mask & 2) {
      snprintf(buf, 255, "realcolor1 = gl_FrontFacing ? ex_c1 : ex_bc1;\n");
      strcat(ctx->glsl_main, buf);
   }
}

static void emit_prescale(struct dump_ctx *ctx)
{
   char buf[255];

   snprintf(buf, 255, "gl_Position.y = gl_Position.y * winsys_adjust.y;\n");
   strcat(ctx->glsl_main, buf);
   snprintf(buf, 255, "gl_Position.z = dot(gl_Position, vec4(0.0, 0.0, winsys_adjust.zw));\n");
   strcat(ctx->glsl_main, buf);
}

static void emit_so_movs(struct dump_ctx *ctx)
{
   char buf[255];
   int i, j;
   char outtype[6] = {0};
   char writemask[6];
   for (i = 0; i < ctx->so->num_outputs; i++) {
      if (ctx->so->output[i].start_component != 0) {
         int wm_idx = 0;
         writemask[wm_idx++] = '.';
         for (j = 0; j < ctx->so->output[i].num_components; j++) {
            unsigned idx = ctx->so->output[i].start_component + j;
            if (idx >= 4)
               break;
            if (idx <= 2)
               writemask[wm_idx++] = 'x' + idx;
            else
               writemask[wm_idx++] = 'w';
         }
         writemask[wm_idx] = '\0';
      } else
         writemask[0] = 0;
      
      if (ctx->so->output[i].num_components == 1)
         snprintf(outtype, 6, "float");
      else
         snprintf(outtype, 6, "vec%d", ctx->so->output[i].num_components);

      if (ctx->so->output[i].register_index >= 255)
         continue;

      if (ctx->outputs[ctx->so->output[i].register_index].name == TGSI_SEMANTIC_CLIPDIST) {
         snprintf(buf, 255, "tfout%d = %s(clip_dist_temp[%d]%s);\n", i, outtype, ctx->outputs[ctx->so->output[i].register_index].sid,
                  writemask);
      } else {
         snprintf(buf, 255, "tfout%d = %s(%s%s);\n", i, outtype, ctx->outputs[ctx->so->output[i].register_index].glsl_name, writemask);
      }
      strcat(ctx->glsl_main, buf);
   }

}

static void emit_clip_dist_movs(struct dump_ctx *ctx)
{
   char buf[255];
   int i;

   if (ctx->num_clip_dist == 0) {
      for (i = 0; i < 8; i++) {
         snprintf(buf, 255, "gl_ClipDistance[%d] = dot(%s, clipp[%d]);\n", i, ctx->has_clipvertex ? "clipv_tmp" : "gl_Position", i);
         strcat(ctx->glsl_main, buf);
      }
      return;
   }
   for (i = 0; i < ctx->num_clip_dist ? ctx->num_clip_dist : 8; i++) {
      int clipidx = i < 4 ? 0 : 1;
      char swiz = i & 3;
      char wm = 0;
      switch (swiz) {
      case 0: wm = 'x'; break;
      case 1: wm = 'y'; break;
      case 2: wm = 'z'; break;
      case 3: wm = 'w'; break;
      }
      snprintf(buf, 255, "gl_ClipDistance[%d] = clip_dist_temp[%d].%c;\n",
               i, clipidx, wm);
      strcat(ctx->glsl_main, buf);
   }
}



#define emit_arit_op2(op) snprintf(buf, 255, "%s = %s(%s((%s %s %s))%s);\n", dsts[0], dstconv, dtypeprefix, srcs[0], op, srcs[1], writemask)
#define emit_op1(op) snprintf(buf, 255, "%s = %s(%s(%s(%s)));\n", dsts[0], dstconv, dtypeprefix, op, srcs[0])
#define emit_compare(op) snprintf(buf, 255, "%s = %s(%s((%s(%s, %s)))%s);\n", dsts[0], dstconv, dtypeprefix, op, srcs[0], srcs[1], writemask)

#define emit_ucompare(op) snprintf(buf, 255, "%s = %s(uintBitsToFloat(%s(%s(%s, %s)%s) * %s(0xffffffff)));\n", dsts[0], dstconv, udstconv, op, srcs[0], srcs[1], writemask, udstconv)

static void emit_buf(struct dump_ctx *ctx, char *buf)
{
   int i;
   for (i = 0; i < ctx->indent_level; i++)
      strcat(ctx->glsl_main, "\t");

   strcat(ctx->glsl_main, buf);
}

static void translate_tex(struct dump_ctx *ctx,
                          struct tgsi_full_instruction *inst,
                          int sreg_index,
                          char srcs[4][255],
                          char  dsts[3][255],
                          const char *writemask,
                          const char *dstconv,
                          const char *dtypeprefix)
{
   char *twm, *gwm, *txfi;
   bool is_shad = false;
   char buf[512];
   char offbuf[64] = {0};
   char bias[64] = {0};
   int sampler_index;
   char *tex_ext;

   ctx->samplers[sreg_index].tgsi_sampler_type = inst->Texture.Texture;

   switch (inst->Texture.Texture) {
   case TGSI_TEXTURE_SHADOWCUBE_ARRAY:
      is_shad = true;
   case TGSI_TEXTURE_CUBE_ARRAY:
      ctx->uses_cube_array = true;
      break;
   case TGSI_TEXTURE_2D_MSAA:
   case TGSI_TEXTURE_2D_ARRAY_MSAA:
      ctx->uses_sampler_ms = true;
      break;
   case TGSI_TEXTURE_BUFFER:
      ctx->uses_sampler_buf = true;
      break;
   case TGSI_TEXTURE_SHADOWRECT:
      is_shad = true;
   case TGSI_TEXTURE_RECT:
      ctx->uses_sampler_rect = true;
      break;
   case TGSI_TEXTURE_SHADOW1D:
   case TGSI_TEXTURE_SHADOW2D:
   case TGSI_TEXTURE_SHADOWCUBE:
   case TGSI_TEXTURE_SHADOW1D_ARRAY:
   case TGSI_TEXTURE_SHADOW2D_ARRAY:
      is_shad = true;
      break;
   }

   if (ctx->cfg->glsl_version >= 140)
      if (ctx->uses_sampler_rect || ctx->uses_sampler_buf)
         ctx->glsl_ver_required = 140;

   sampler_index = 1;

   if (inst->Instruction.Opcode == TGSI_OPCODE_TXQ) {
      /* no lod parameter for txq for these */
      if (inst->Texture.Texture != TGSI_TEXTURE_RECT &&
          inst->Texture.Texture != TGSI_TEXTURE_SHADOWRECT &&
          inst->Texture.Texture != TGSI_TEXTURE_BUFFER &&
          inst->Texture.Texture != TGSI_TEXTURE_2D_MSAA &&
          inst->Texture.Texture != TGSI_TEXTURE_2D_ARRAY_MSAA)
         snprintf(bias, 64, ", int(%s.w)", srcs[0]);
      snprintf(buf, 255, "%s = %s(%s(textureSize(%s%s)));\n", dsts[0], dstconv, dtypeprefix, srcs[sampler_index], bias);
      emit_buf(ctx, buf);
      return;
   }

   switch (inst->Texture.Texture) {
   case TGSI_TEXTURE_1D:
   case TGSI_TEXTURE_BUFFER:
      if (inst->Instruction.Opcode == TGSI_OPCODE_TXP)
         twm = "";
      else
         twm = ".x";
      txfi = "int";
      break;
   case TGSI_TEXTURE_1D_ARRAY:
      twm = ".xy";
      txfi = "ivec2";
      break;
   case TGSI_TEXTURE_2D:
   case TGSI_TEXTURE_RECT:
      if (inst->Instruction.Opcode == TGSI_OPCODE_TXP)
         twm = "";
      else
         twm = ".xy";
      txfi = "ivec2";
      break;
   case TGSI_TEXTURE_SHADOW1D:
   case TGSI_TEXTURE_SHADOW2D:
   case TGSI_TEXTURE_SHADOW1D_ARRAY:
   case TGSI_TEXTURE_SHADOWRECT:
   case TGSI_TEXTURE_3D:
      if (inst->Instruction.Opcode == TGSI_OPCODE_TXP)
         twm = "";
      else
         twm = ".xyz";
      txfi = "ivec3";
      break;
   case TGSI_TEXTURE_CUBE:
   case TGSI_TEXTURE_2D_ARRAY:
      twm = ".xyz";
      txfi = "ivec3";
      break;
   case TGSI_TEXTURE_2D_MSAA:
      twm = ".xy";
      txfi = "ivec2";
      break;
   case TGSI_TEXTURE_2D_ARRAY_MSAA:
      twm = ".xyz";
      txfi = "ivec3";
      break;

   case TGSI_TEXTURE_SHADOWCUBE:
   case TGSI_TEXTURE_SHADOW2D_ARRAY:
   case TGSI_TEXTURE_SHADOWCUBE_ARRAY:
   case TGSI_TEXTURE_CUBE_ARRAY:
   default:
      twm = "";
      txfi = "";
      break;
   }

   if (inst->Instruction.Opcode == TGSI_OPCODE_TXD) {
      switch (inst->Texture.Texture) {
      case TGSI_TEXTURE_1D:
      case TGSI_TEXTURE_SHADOW1D:
      case TGSI_TEXTURE_1D_ARRAY:
      case TGSI_TEXTURE_SHADOW1D_ARRAY:
         gwm = ".x";
         break;
      case TGSI_TEXTURE_2D:
      case TGSI_TEXTURE_SHADOW2D:
      case TGSI_TEXTURE_2D_ARRAY:
      case TGSI_TEXTURE_SHADOW2D_ARRAY:
      case TGSI_TEXTURE_RECT:
      case TGSI_TEXTURE_SHADOWRECT:
         gwm = ".xy";
         break;
      case TGSI_TEXTURE_3D:
      case TGSI_TEXTURE_CUBE:
      case TGSI_TEXTURE_SHADOWCUBE:
      case TGSI_TEXTURE_CUBE_ARRAY:
         gwm = ".xyz";
         break;
      default:
         gwm = "";
         break;
      }
   }

   if (inst->Instruction.Opcode == TGSI_OPCODE_TXB2 || inst->Instruction.Opcode == TGSI_OPCODE_TXL2 || inst->Instruction.Opcode == TGSI_OPCODE_TEX2) {
      sampler_index = 2;
      if (inst->Instruction.Opcode != TGSI_OPCODE_TEX2)
         snprintf(bias, 64, ", %s.x", srcs[1]);
      else if (inst->Texture.Texture == TGSI_TEXTURE_SHADOWCUBE_ARRAY)
         snprintf(bias, 64, ", float(%s)", srcs[1]);
   } else if (inst->Instruction.Opcode == TGSI_OPCODE_TXB || inst->Instruction.Opcode == TGSI_OPCODE_TXL)
      snprintf(bias, 64, ", %s.w", srcs[0]);
   else if (inst->Instruction.Opcode == TGSI_OPCODE_TXF) {
      if (inst->Texture.Texture == TGSI_TEXTURE_1D ||
          inst->Texture.Texture == TGSI_TEXTURE_2D ||
          inst->Texture.Texture == TGSI_TEXTURE_2D_MSAA ||
          inst->Texture.Texture == TGSI_TEXTURE_2D_ARRAY_MSAA ||
          inst->Texture.Texture == TGSI_TEXTURE_3D ||
          inst->Texture.Texture == TGSI_TEXTURE_1D_ARRAY ||
          inst->Texture.Texture == TGSI_TEXTURE_2D_ARRAY) {
         snprintf(bias, 64, ", int(%s.w)", srcs[0]);
      }
   } else if (inst->Instruction.Opcode == TGSI_OPCODE_TXD) {
      snprintf(bias, 64, ", %s%s, %s%s", srcs[1], gwm, srcs[2], gwm);
      sampler_index = 3;
   }
   else
      bias[0] = 0;

   if (inst->Instruction.Opcode == TGSI_OPCODE_TXP) {
      if (inst->Texture.Texture == TGSI_TEXTURE_CUBE || inst->Texture.Texture == TGSI_TEXTURE_2D_ARRAY || inst->Texture.Texture == TGSI_TEXTURE_1D_ARRAY)
         tex_ext = "";
      else if (inst->Texture.NumOffsets == 1)
         tex_ext = "ProjOffset";
      else
         tex_ext = "Proj";
   } else if (inst->Instruction.Opcode == TGSI_OPCODE_TXL || inst->Instruction.Opcode == TGSI_OPCODE_TXL2) {
      if (inst->Texture.NumOffsets == 1)
         tex_ext = "LodOffset";
      else
         tex_ext = "Lod";
   } else if (inst->Instruction.Opcode == TGSI_OPCODE_TXD) {
      if (inst->Texture.NumOffsets == 1)
         tex_ext = "GradOffset";
      else
         tex_ext = "Grad";
   } else {
      if (inst->Texture.NumOffsets == 1)
         tex_ext = "Offset";
      else
         tex_ext = "";
   }

   if (inst->Texture.NumOffsets == 1) {
      struct immed *imd = &ctx->imm[(inst->TexOffsets[0].Index)];
      switch (inst->Texture.Texture) {
      case TGSI_TEXTURE_1D:
      case TGSI_TEXTURE_1D_ARRAY:
      case TGSI_TEXTURE_SHADOW1D:
      case TGSI_TEXTURE_SHADOW1D_ARRAY:
         snprintf(offbuf, 25, ", int(%d)", imd->val[inst->TexOffsets[0].SwizzleX].i);
         break;
      case TGSI_TEXTURE_RECT:
      case TGSI_TEXTURE_SHADOWRECT:
      case TGSI_TEXTURE_2D:
      case TGSI_TEXTURE_2D_ARRAY:
      case TGSI_TEXTURE_SHADOW2D:
      case TGSI_TEXTURE_SHADOW2D_ARRAY:
         snprintf(offbuf, 25, ", ivec2(%d, %d)", imd->val[inst->TexOffsets[0].SwizzleX].i, imd->val[inst->TexOffsets[0].SwizzleY].i);
         break;
      case TGSI_TEXTURE_3D:
         snprintf(offbuf, 25, ", ivec3(%d, %d, %d)", imd->val[inst->TexOffsets[0].SwizzleX].i, imd->val[inst->TexOffsets[0].SwizzleY].i,
                  imd->val[inst->TexOffsets[0].SwizzleZ].i);
         break;
      }

      if (inst->Instruction.Opcode == TGSI_OPCODE_TXL || inst->Instruction.Opcode == TGSI_OPCODE_TXL2 || inst->Instruction.Opcode == TGSI_OPCODE_TXD) {
         char tmp[64];
         strcpy(tmp, offbuf);
         strcpy(offbuf, bias);
         strcpy(bias, tmp);

      }
   }
   if (inst->Instruction.Opcode == TGSI_OPCODE_TXF) {
      snprintf(buf, 255, "%s = %s(texelFetch%s(%s, %s(%s%s)%s%s)%s);\n", dsts[0], dstconv, tex_ext, srcs[sampler_index], txfi, srcs[0], twm, bias, offbuf, ctx->outputs[0].override_no_wm ? "" : writemask);
   } else if (ctx->cfg->glsl_version < 140 && ctx->uses_sampler_rect) {
      /* rect is special in GLSL 1.30 */
      if (inst->Texture.Texture == TGSI_TEXTURE_RECT)
         snprintf(buf, 255, "%s = texture2DRect(%s, %s.xy)%s;\n", dsts[0], srcs[sampler_index], srcs[0], writemask);
      else if (inst->Texture.Texture == TGSI_TEXTURE_SHADOWRECT)
         snprintf(buf, 255, "%s = shadow2DRect(%s, %s.xyz)%s;\n", dsts[0], srcs[sampler_index], srcs[0], writemask);
   } else if (is_shad) { /* TGSI returns 1.0 in alpha */
      const char *cname = tgsi_proc_to_prefix(ctx->prog_type);
      const struct tgsi_full_src_register *src = &inst->Src[sampler_index];
      snprintf(buf, 255, "%s = %s(vec4(vec4(texture%s(%s, %s%s%s%s)) * %sshadmask%d + %sshadadd%d)%s);\n", dsts[0], dstconv, tex_ext, srcs[sampler_index], srcs[0], twm, offbuf, bias, cname, src->Register.Index, cname, src->Register.Index, writemask);
   } else
      snprintf(buf, 255, "%s = %s(texture%s(%s, %s%s%s%s)%s);\n", dsts[0], dstconv, tex_ext, srcs[sampler_index], srcs[0], twm, offbuf, bias, ctx->outputs[0].override_no_wm ? "" : writemask);
   emit_buf(ctx, buf);
}

static boolean
iter_instruction(struct tgsi_iterate_context *iter,
                 struct tgsi_full_instruction *inst)
{
   struct dump_ctx *ctx = (struct dump_ctx *)iter;
   char srcs[4][255], dsts[3][255], buf[512];
   uint instno = ctx->instno++;
   int i;
   int j;
   int sreg_index = 0;
   char dstconv[32] = {0};
   char udstconv[32] = {0};
   char writemask[6] = {0};
   enum tgsi_opcode_type dtype = tgsi_opcode_infer_dst_type(inst->Instruction.Opcode);
   enum tgsi_opcode_type stype = tgsi_opcode_infer_src_type(inst->Instruction.Opcode);
   char *dtypeprefix="", *stypeprefix = "";
   bool stprefix = false;
   bool override_no_wm[4];

   if (dtype == TGSI_TYPE_SIGNED || dtype == TGSI_TYPE_UNSIGNED ||
       stype == TGSI_TYPE_SIGNED || stype == TGSI_TYPE_UNSIGNED)
      ctx->has_ints = TRUE;

   if (inst->Instruction.Opcode == TGSI_OPCODE_TXQ) {
      dtypeprefix = "intBitsToFloat";
   } else {
      switch (dtype) {
      case TGSI_TYPE_UNSIGNED:
	 dtypeprefix = "uintBitsToFloat";
	 break;
      case TGSI_TYPE_SIGNED:
	 dtypeprefix = "intBitsToFloat";
	 break;
      default:
	 break;
      }
   }

   switch (stype) {
   case TGSI_TYPE_UNSIGNED:
      stypeprefix = "floatBitsToUint";
      stprefix = true;
      break;
   case TGSI_TYPE_SIGNED:
      stypeprefix = "floatBitsToInt";
      stprefix = true;
      break;
   default:
      break;
   }

   if (instno == 0) {
      strcat(ctx->glsl_main, "void main(void)\n{\n");
      if (iter->processor.Processor == TGSI_PROCESSOR_FRAGMENT)
         emit_color_select(ctx);
   }
   for (i = 0; i < inst->Instruction.NumDstRegs; i++) {
      const struct tgsi_full_dst_register *dst = &inst->Dst[i];
      if (dst->Register.WriteMask != TGSI_WRITEMASK_XYZW) {
         int wm_idx = 0;
         writemask[wm_idx++] = '.';
         if (dst->Register.WriteMask & 0x1)
            writemask[wm_idx++] = 'x';
         if (dst->Register.WriteMask & 0x2)
            writemask[wm_idx++] = 'y';
         if (dst->Register.WriteMask & 0x4)
            writemask[wm_idx++] = 'z';
         if (dst->Register.WriteMask & 0x8)
            writemask[wm_idx++] = 'w';
         if (wm_idx == 2) {
            snprintf(dstconv, 6, "float");
            snprintf(udstconv, 6, "uint");
         } else {
            snprintf(dstconv, 6, "vec%d", wm_idx-1);
            snprintf(udstconv, 6, "uvec%d", wm_idx-1);
         }
      } else {
            snprintf(dstconv, 6, "vec4");
            snprintf(udstconv, 6, "uvec4");
      }
      if (dst->Register.File == TGSI_FILE_OUTPUT) {
         for (j = 0; j < ctx->num_outputs; j++) {
            if (ctx->outputs[j].first == dst->Register.Index) {
               if (ctx->glsl_ver_required >= 140 && ctx->outputs[j].name == TGSI_SEMANTIC_CLIPVERTEX) {
                  snprintf(dsts[i], 255, "clipv_tmp");
               } else if (ctx->outputs[j].name == TGSI_SEMANTIC_CLIPDIST) {
                  snprintf(dsts[i], 255, "clip_dist_temp[%d]", ctx->outputs[j].sid);
               } else {
                  snprintf(dsts[i], 255, "%s%s", ctx->outputs[j].glsl_name, ctx->outputs[j].override_no_wm ? "" : writemask);
                  if (ctx->outputs[j].name == TGSI_SEMANTIC_PSIZE) {
                     snprintf(dstconv, 6, "float");
                     break;
                  }
               }
            }
         }
      }
      else if (dst->Register.File == TGSI_FILE_TEMPORARY) {
         if (dst->Register.Indirect) {
            snprintf(dsts[i], 255, "temps[addr0 + %d]%s", dst->Register.Index, writemask);
         } else
            snprintf(dsts[i], 255, "temps[%d]%s", dst->Register.Index, writemask);
      }
   }
      
   for (i = 0; i < inst->Instruction.NumSrcRegs; i++) {
      const struct tgsi_full_src_register *src = &inst->Src[i];
      char swizzle[8] = {0};
      char prefix[6] = {0};
      char arrayname[8] = {0};
      int swz_idx = 0, pre_idx = 0;
      boolean isabsolute = src->Register.Absolute;
      
      override_no_wm[i] = false;
      if (isabsolute)
         swizzle[swz_idx++] = ')';

      if (src->Register.Negate)
         prefix[pre_idx++] = '-';
      if (isabsolute)
         strcpy(&prefix[pre_idx++], "abs(");

      if (src->Register.Dimension)
         sprintf(arrayname, "[%d]", src->Dimension.Index);

      if (src->Register.SwizzleX != TGSI_SWIZZLE_X ||
          src->Register.SwizzleY != TGSI_SWIZZLE_Y ||
          src->Register.SwizzleZ != TGSI_SWIZZLE_Z ||
          src->Register.SwizzleW != TGSI_SWIZZLE_W) {
         swizzle[swz_idx++] = '.';
         swizzle[swz_idx++] = get_swiz_char(src->Register.SwizzleX);
         swizzle[swz_idx++] = get_swiz_char(src->Register.SwizzleY);
         swizzle[swz_idx++] = get_swiz_char(src->Register.SwizzleZ);
         swizzle[swz_idx++] = get_swiz_char(src->Register.SwizzleW);
      }
      if (src->Register.File == TGSI_FILE_INPUT) {
         for (j = 0; j < ctx->num_inputs; j++)
            if (ctx->inputs[j].first == src->Register.Index) {
               if (ctx->key->color_two_side && ctx->inputs[j].name == TGSI_SEMANTIC_COLOR)
                  snprintf(srcs[i], 255, "%s(%s%s%d%s%s)", stypeprefix, prefix, "realcolor", ctx->inputs[j].sid, arrayname, swizzle);
               else
                  snprintf(srcs[i], 255, "%s(%s%s%s%s)", stypeprefix, prefix, ctx->inputs[j].glsl_name, arrayname, swizzle);
               break;
            }
      }
      else if (src->Register.File == TGSI_FILE_TEMPORARY) {
         if (src->Register.Indirect) {
            snprintf(srcs[i], 255, "%s%c%stemps[addr0 + %d]%s%c", stypeprefix, stprefix ? '(' : ' ', prefix, src->Register.Index, swizzle, stprefix ? ')' : ' ');
         } else
            snprintf(srcs[i], 255, "%s%c%stemps[%d]%s%c", stypeprefix, stprefix ? '(' : ' ', prefix, src->Register.Index, swizzle, stprefix ? ')' : ' ');
      } else if (src->Register.File == TGSI_FILE_CONSTANT) {
          const char *cname = tgsi_proc_to_prefix(ctx->prog_type);
          int dim = 0;
          if (src->Register.Dimension) {
             dim = src->Dimension.Index;
             if (src->Register.Indirect) {
                snprintf(srcs[i], 255, "%s(%s%subo%dcontents[addr0 + %d]%s)", stypeprefix, prefix, cname, dim, src->Register.Index, swizzle);
             } else
                snprintf(srcs[i], 255, "%s(%s%subo%dcontents[%d]%s)", stypeprefix, prefix, cname, dim, src->Register.Index, swizzle);
          } else {
             const char *csp;
             ctx->has_ints = TRUE;
             if (stype == TGSI_TYPE_FLOAT || stype == TGSI_TYPE_UNTYPED)
                csp = "uintBitsToFloat";
             else if (stype == TGSI_TYPE_SIGNED)
                csp = "ivec4";
             else
                csp = "";

             if (src->Register.Indirect) {
                snprintf(srcs[i], 255, "%s%s(%sconst%d[addr0 + %d]%s)", prefix, csp, cname, dim, src->Register.Index, swizzle);
             } else
                snprintf(srcs[i], 255, "%s%s(%sconst%d[%d]%s)", prefix, csp, cname, dim, src->Register.Index, swizzle);
          }
      } else if (src->Register.File == TGSI_FILE_SAMPLER) {
          const char *cname = tgsi_proc_to_prefix(ctx->prog_type);
          snprintf(srcs[i], 255, "%ssamp%d%s", cname, src->Register.Index, swizzle);
	  sreg_index = src->Register.Index;
      } else if (src->Register.File == TGSI_FILE_IMMEDIATE) {
         struct immed *imd = &ctx->imm[(src->Register.Index)];
         int idx = src->Register.SwizzleX;
         char temp[48];
         const char *vtype = "vec4";
         const char *imm_stypeprefix = stypeprefix;

         if (imd->type == TGSI_IMM_UINT32 || imd->type == TGSI_IMM_INT32) {
            if (imd->type == TGSI_IMM_UINT32)
               vtype = "uvec4";
            else
               vtype = "ivec4";

            if (stype == TGSI_TYPE_UNSIGNED && imd->type == TGSI_IMM_INT32)
               imm_stypeprefix = "uvec4";
            else if (stype == TGSI_TYPE_SIGNED && imd->type == TGSI_IMM_UINT32)
               imm_stypeprefix = "ivec4";
            else if (stype == TGSI_TYPE_FLOAT || stype == TGSI_TYPE_UNTYPED) {
               if (imd->type == TGSI_IMM_INT32)
                  imm_stypeprefix = "intBitsToFloat";
               else
                  imm_stypeprefix = "uintBitsToFloat";
            } else if (stype == TGSI_TYPE_UNSIGNED || stype == TGSI_TYPE_SIGNED)
               imm_stypeprefix = "";
            
         }

         /* build up a vec4 of immediates */
         snprintf(srcs[i], 255, "%s(%s%s(", imm_stypeprefix, prefix, vtype);
         for (j = 0; j < 4; j++) {
            if (j == 0)
               idx = src->Register.SwizzleX;
            else if (j == 1)
               idx = src->Register.SwizzleY;
            else if (j == 2)
               idx = src->Register.SwizzleZ;
            else if (j == 3)
               idx = src->Register.SwizzleW;
            switch (imd->type) {
            case TGSI_IMM_FLOAT32:
               if (isinf(imd->val[idx].f) || isnan(imd->val[idx].f)) {
                  ctx->has_ints = TRUE;
                  snprintf(temp, 48, "uintBitsToFloat(%uU)", imd->val[idx].ui);
               } else
                  snprintf(temp, 25, "%.8g", imd->val[idx].f);
               break;
            case TGSI_IMM_UINT32:
               snprintf(temp, 25, "%uU", imd->val[idx].ui);
               break;
            case TGSI_IMM_INT32:
               snprintf(temp, 25, "%d", imd->val[idx].i);
               break;
            }
            strncat(srcs[i], temp, 255);
            if (j < 3)
               strcat(srcs[i], ",");
            else {
               snprintf(temp, 4, "))%c", isabsolute ? ')' : 0);
               strncat(srcs[i], temp, 255);
            }
         }
      } else if (src->Register.File == TGSI_FILE_SYSTEM_VALUE) {
         for (j = 0; j < ctx->num_system_values; j++)
            if (ctx->system_values[j].first == src->Register.Index) {
               snprintf(srcs[i], 255, "%s%s", prefix, ctx->system_values[j].glsl_name);
               override_no_wm[i] = ctx->system_values[j].override_no_wm;
            }
      }
   }
   switch (inst->Instruction.Opcode) {
   case TGSI_OPCODE_SQRT:
      snprintf(buf, 255, "%s = sqrt(vec4(%s))%s;\n", dsts[0], srcs[0], writemask);
      emit_buf(ctx, buf);
      break;
   case TGSI_OPCODE_LRP:
      snprintf(buf, 255, "%s = mix(vec4(%s), vec4(%s), vec4(%s))%s;\n", dsts[0], srcs[2], srcs[1], srcs[0], writemask);
      emit_buf(ctx, buf);
      break;
   case TGSI_OPCODE_DP2:
      snprintf(buf, 255, "%s = %s(dot(vec2(%s), vec2(%s)));\n", dsts[0], dstconv, srcs[0], srcs[1]);
      emit_buf(ctx, buf);
      break;
   case TGSI_OPCODE_DP3:
      snprintf(buf, 255, "%s = %s(dot(vec3(%s), vec3(%s)));\n", dsts[0], dstconv, srcs[0], srcs[1]);
      emit_buf(ctx, buf);
      break;
   case TGSI_OPCODE_DP4:
      snprintf(buf, 255, "%s = %s(dot(vec4(%s), vec4(%s)));\n", dsts[0], dstconv, srcs[0], srcs[1]);
      emit_buf(ctx, buf);
      break;
   case TGSI_OPCODE_DPH:
      snprintf(buf, 255, "%s = %s(dot(vec4(vec3(%s), 1.0), vec4(%s)));\n", dsts[0], dstconv, srcs[0], srcs[1]);
      emit_buf(ctx, buf);
      break;
   case TGSI_OPCODE_MAX:
   case TGSI_OPCODE_IMAX:
   case TGSI_OPCODE_UMAX:
      snprintf(buf, 255, "%s = %s(%s(max(%s, %s)));\n", dsts[0], dstconv, dtypeprefix, srcs[0], srcs[1]);
      emit_buf(ctx, buf);
      break;
   case TGSI_OPCODE_MIN:
   case TGSI_OPCODE_IMIN:
   case TGSI_OPCODE_UMIN:
      snprintf(buf, 255, "%s = %s(%s(min(%s, %s)));\n", dsts[0], dstconv, dtypeprefix, srcs[0], srcs[1]);
      emit_buf(ctx, buf);
      break;
   case TGSI_OPCODE_ABS:
   case TGSI_OPCODE_IABS:
      emit_op1("abs");
      emit_buf(ctx, buf);
      break;
   case TGSI_OPCODE_KILL_IF:
      snprintf(buf, 255, "if (any(lessThan(%s, vec4(0.0))))\ndiscard;\n", srcs[0]);
      emit_buf(ctx, buf);
      break;
   case TGSI_OPCODE_IF:
   case TGSI_OPCODE_UIF:
      snprintf(buf, 255, "if (any(bvec4(%s))) {\n", srcs[0]);
      emit_buf(ctx, buf);
      ctx->indent_level++;
      break;
   case TGSI_OPCODE_ELSE:
      snprintf(buf, 255, "} else {\n");
      ctx->indent_level--;
      emit_buf(ctx, buf);
      ctx->indent_level++;
      break;
   case TGSI_OPCODE_ENDIF:
      snprintf(buf, 255, "}\n");
      ctx->indent_level--;
      emit_buf(ctx, buf);
      break;
   case TGSI_OPCODE_KILL:
      snprintf(buf, 255, "discard;\n");
      emit_buf(ctx, buf);
      break;
   case TGSI_OPCODE_DST:
      snprintf(buf, 512, "%s = vec4(1.0, %s.y * %s.y, %s.z, %s.w);\n", dsts[0],
               srcs[0], srcs[1], srcs[0], srcs[1]);
      emit_buf(ctx, buf);
      break;
   case TGSI_OPCODE_LIT:
      snprintf(buf, 512, "%s = %s(vec4(1.0, max(%s.x, 0.0), step(0.0, %s.x) * pow(max(0.0, %s.y), clamp(%s.w, -128.0, 128.0)), 1.0)%s);\n", dsts[0], dstconv, srcs[0], srcs[0], srcs[0], srcs[0], writemask);
      emit_buf(ctx, buf);
      break;
   case TGSI_OPCODE_EX2:
      emit_op1("exp2");
      emit_buf(ctx, buf);
      break;
   case TGSI_OPCODE_LG2:
      emit_op1("log2");
      emit_buf(ctx, buf);
      break;
   case TGSI_OPCODE_EXP:
      snprintf(buf, 512, "%s = %s(vec4(pow(2.0, floor(%s.x)), %s.x - floor(%s.x), exp2(%s.x), 1.0)%s);\n", dsts[0], dstconv, srcs[0], srcs[0], srcs[0], srcs[0], writemask);
      emit_buf(ctx, buf);
      break;
   case TGSI_OPCODE_LOG:
      snprintf(buf, 512, "%s = %s(vec4(floor(log2(%s.x)), %s.x / pow(2.0, floor(log2(%s.x))), log2(%s.x), 1.0)%s);\n", dsts[0], dstconv, srcs[0], srcs[0], srcs[0], srcs[0], writemask);
      emit_buf(ctx, buf);
      break;
   case TGSI_OPCODE_COS:
      emit_op1("cos");
      emit_buf(ctx, buf);
      break;
   case TGSI_OPCODE_SIN:
      emit_op1("sin");
      emit_buf(ctx, buf);
      break;
   case TGSI_OPCODE_SCS:
      snprintf(buf, 255, "%s = %s(vec4(cos(%s.x), sin(%s.x), 0, 1)%s);\n", dsts[0], dstconv,
               srcs[0], srcs[0], writemask);
      emit_buf(ctx, buf);      
      break;
   case TGSI_OPCODE_DDX:
      emit_op1("dFdx");
      emit_buf(ctx, buf);
      break;
   case TGSI_OPCODE_DDY:
      emit_op1("dFdy");
      emit_buf(ctx, buf);
      break;
   case TGSI_OPCODE_RCP:
      snprintf(buf, 255, "%s = %s(1.0/(%s));\n", dsts[0], dstconv, srcs[0]);
      emit_buf(ctx, buf);
      break;
   case TGSI_OPCODE_FLR:
      emit_op1("floor");
      emit_buf(ctx, buf);
      break;
   case TGSI_OPCODE_ROUND:
      emit_op1("round");
      emit_buf(ctx, buf);
      break;
   case TGSI_OPCODE_ISSG:
      emit_op1("sign");
      emit_buf(ctx, buf);
      break;
   case TGSI_OPCODE_CEIL:
      emit_op1("ceil");
      emit_buf(ctx, buf);
      break;
   case TGSI_OPCODE_FRC:
      emit_op1("fract");
      emit_buf(ctx, buf);
      break;
   case TGSI_OPCODE_TRUNC:
      emit_op1("trunc");
      emit_buf(ctx, buf);
      break;
   case TGSI_OPCODE_SSG:
      emit_op1("sign");
      emit_buf(ctx, buf);
      break;
   case TGSI_OPCODE_RSQ:
      snprintf(buf, 255, "%s = %s(inversesqrt(%s.x));\n", dsts[0], dstconv, srcs[0]);
      emit_buf(ctx, buf);
      break;
   case TGSI_OPCODE_MOV:
      snprintf(buf, 255, "%s = %s(%s(%s%s));\n", dsts[0], dstconv, dtypeprefix, srcs[0], override_no_wm[0] ? "" : writemask);
      emit_buf(ctx, buf);
      break;
   case TGSI_OPCODE_ADD:
      emit_arit_op2("+");
      emit_buf(ctx, buf);
      break;
   case TGSI_OPCODE_UADD:
      snprintf(buf, 255, "%s = %s(%s(ivec4((uvec4(%s) + uvec4(%s))))%s);\n", dsts[0], dstconv, dtypeprefix, srcs[0], srcs[1], writemask);
      emit_buf(ctx, buf);
      break;
   case TGSI_OPCODE_SUB:
      emit_arit_op2("-");
      emit_buf(ctx, buf);
      break;
   case TGSI_OPCODE_MUL:
      emit_arit_op2("*");
      emit_buf(ctx, buf);
      break;
   case TGSI_OPCODE_DIV:
      emit_arit_op2("/");
      emit_buf(ctx, buf);
      break;
   case TGSI_OPCODE_UMUL:
      snprintf(buf, 255, "%s = %s(%s((uvec4(%s) * uvec4(%s)))%s);\n", dsts[0], dstconv, dtypeprefix, srcs[0], srcs[1], writemask);
      emit_buf(ctx, buf);
      break;
   case TGSI_OPCODE_UMOD:
      snprintf(buf, 255, "%s = %s(%s((uvec4(%s) %% uvec4(%s)))%s);\n", dsts[0], dstconv, dtypeprefix, srcs[0], srcs[1], writemask);
      strcat(ctx->glsl_main, buf);
      break;
   case TGSI_OPCODE_IDIV:
      snprintf(buf, 255, "%s = %s(%s((ivec4(%s) / ivec4(%s)))%s);\n", dsts[0], dstconv, dtypeprefix, srcs[0], srcs[1], writemask);
      emit_buf(ctx, buf);
      break;
   case TGSI_OPCODE_UDIV:
      snprintf(buf, 255, "%s = %s(%s((uvec4(%s) / uvec4(%s)))%s);\n", dsts[0], dstconv, dtypeprefix, srcs[0], srcs[1], writemask);
      emit_buf(ctx, buf);
      break;
   case TGSI_OPCODE_ISHR:
   case TGSI_OPCODE_USHR:
      emit_arit_op2(">>");
      emit_buf(ctx, buf);
      break;
   case TGSI_OPCODE_SHL:
      emit_arit_op2("<<");
      emit_buf(ctx, buf);
      break;
   case TGSI_OPCODE_MAD:
      snprintf(buf, 255, "%s = %s((%s * %s + %s)%s);\n", dsts[0], dstconv, srcs[0], srcs[1], srcs[2], writemask);
      emit_buf(ctx, buf);
      break;
   case TGSI_OPCODE_UMAD:
      snprintf(buf, 255, "%s = %s(%s((%s * %s + %s)%s));\n", dsts[0], dstconv, dtypeprefix, srcs[0], srcs[1], srcs[2], writemask);
      emit_buf(ctx, buf);
      break;
   case TGSI_OPCODE_OR:
      emit_arit_op2("|");
      emit_buf(ctx, buf);
      break;
   case TGSI_OPCODE_AND:
      emit_arit_op2("&");
      emit_buf(ctx, buf);
      break;
   case TGSI_OPCODE_XOR:
      emit_arit_op2("^");
      emit_buf(ctx, buf);
      break;
   case TGSI_OPCODE_MOD:
      emit_arit_op2("%");
      emit_buf(ctx, buf);
      break;
   case TGSI_OPCODE_TEX:
   case TGSI_OPCODE_TEX2:
   case TGSI_OPCODE_TXB:
   case TGSI_OPCODE_TXL:
   case TGSI_OPCODE_TXB2:
   case TGSI_OPCODE_TXL2:
   case TGSI_OPCODE_TXD:
   case TGSI_OPCODE_TXF:
   case TGSI_OPCODE_TXP:
   case TGSI_OPCODE_TXQ:
      translate_tex(ctx, inst, sreg_index, srcs, dsts, writemask, dstconv, dtypeprefix);
      break;
   case TGSI_OPCODE_I2F:
      snprintf(buf, 255, "%s = %s(ivec4(%s));\n", dsts[0], dstconv, srcs[0]);
      emit_buf(ctx, buf);
      break;
   case TGSI_OPCODE_U2F:
      snprintf(buf, 255, "%s = %s(uvec4(%s));\n", dsts[0], dstconv, srcs[0]);
      emit_buf(ctx, buf);
      break;
   case TGSI_OPCODE_F2I:
      snprintf(buf, 255, "%s = %s(%s(ivec4(%s)));\n", dsts[0], dstconv, dtypeprefix, srcs[0]);      
      emit_buf(ctx, buf);
      break;
   case TGSI_OPCODE_F2U:
      snprintf(buf, 255, "%s = %s(%s(uvec4(%s)));\n", dsts[0], dstconv, dtypeprefix, srcs[0]);      
      emit_buf(ctx, buf);
      break;
   case TGSI_OPCODE_NOT:
      snprintf(buf, 255, "%s = %s(uintBitsToFloat(~(uvec4(%s))));\n", dsts[0], dstconv, srcs[0]);
      emit_buf(ctx, buf);
      break;
   case TGSI_OPCODE_INEG:
      snprintf(buf, 255, "%s = %s(intBitsToFloat(-(ivec4(%s))));\n", dsts[0], dstconv, srcs[0]);
      emit_buf(ctx, buf);
      break;
   case TGSI_OPCODE_SEQ:
      emit_compare("equal");
      emit_buf(ctx, buf);
      break;
   case TGSI_OPCODE_USEQ:
   case TGSI_OPCODE_FSEQ:
      emit_ucompare("equal");
      emit_buf(ctx, buf);
      break;
   case TGSI_OPCODE_SLT:
      emit_compare("lessThan");
      emit_buf(ctx, buf);
      break;
   case TGSI_OPCODE_ISLT:
   case TGSI_OPCODE_USLT:
   case TGSI_OPCODE_FSLT:
      emit_ucompare("lessThan");
      emit_buf(ctx, buf);
      break;
   case TGSI_OPCODE_SNE:
      emit_compare("notEqual");
      emit_buf(ctx, buf);
      break;
   case TGSI_OPCODE_USNE:
   case TGSI_OPCODE_FSNE:
      emit_ucompare("notEqual");
      emit_buf(ctx, buf);
      break;
   case TGSI_OPCODE_SGE:
      emit_compare("greaterThanEqual");
      emit_buf(ctx, buf);
      break;
   case TGSI_OPCODE_ISGE:
   case TGSI_OPCODE_USGE:
   case TGSI_OPCODE_FSGE:
      emit_ucompare("greaterThanEqual");
      emit_buf(ctx, buf);
      break;
   case TGSI_OPCODE_POW:
      snprintf(buf, 255, "%s = %s(pow(%s, %s));\n", dsts[0], dstconv, srcs[0], srcs[1]);
      emit_buf(ctx, buf);
      break;
   case TGSI_OPCODE_CMP:
   case TGSI_OPCODE_UCMP:
      snprintf(buf, 255, "%s = mix(%s, %s, greaterThanEqual(%s, vec4(0.0)))%s;\n", dsts[0], srcs[1], srcs[2], srcs[0], writemask);
      emit_buf(ctx, buf);
      break;
   case TGSI_OPCODE_END:
      if (iter->processor.Processor == TGSI_PROCESSOR_VERTEX) {
         emit_prescale(ctx);
         if (ctx->so)
            emit_so_movs(ctx);
         emit_clip_dist_movs(ctx);
      } else if (iter->processor.Processor == TGSI_PROCESSOR_GEOMETRY) {

      } else if (iter->processor.Processor == TGSI_PROCESSOR_FRAGMENT) {
         if (ctx->key->pstipple_tex)
            emit_pstipple_pass(ctx);
         if (ctx->key->add_alpha_test)
            emit_alpha_test(ctx);
         if (ctx->write_all_cbufs)
            emit_cbuf_writes(ctx);
      }
      strcat(ctx->glsl_main, "}\n");
      break;
   case TGSI_OPCODE_RET:
      strcat(ctx->glsl_main, "return;\n");
      break;
   case TGSI_OPCODE_ARL:
      snprintf(buf, 255, "addr0 = int(floor(%s)%s);\n", srcs[0], writemask);
      emit_buf(ctx, buf);
      break;
   case TGSI_OPCODE_UARL:
      snprintf(buf, 255, "addr0 = int(%s);\n", srcs[0]);
      emit_buf(ctx, buf);
      break;
   case TGSI_OPCODE_XPD:
      snprintf(buf, 255, "%s = %s(cross(vec3(%s), vec3(%s)));\n", dsts[0], dstconv, srcs[0], srcs[1]);
      emit_buf(ctx, buf);
      break;
   case TGSI_OPCODE_BGNLOOP:
      snprintf(buf, 255, "do {\n");
      emit_buf(ctx, buf);
      ctx->indent_level++;
      break;
   case TGSI_OPCODE_ENDLOOP:
      ctx->indent_level--;
      snprintf(buf, 255, "} while(true);\n");
      emit_buf(ctx, buf);
      break;
   case TGSI_OPCODE_BRK:
      snprintf(buf, 255, "break;\n");
      emit_buf(ctx, buf);
      break;
   case TGSI_OPCODE_EMIT:
      snprintf(buf, 255, "EmitVertex();\n");
      emit_buf(ctx, buf);
      break;
   case TGSI_OPCODE_ENDPRIM:
      snprintf(buf, 255, "EndPrimitive();\n");
      emit_buf(ctx, buf);
      break;
   default:
      fprintf(stderr,"failed to convert opcode %d\n", inst->Instruction.Opcode);
      break;
   }

   if (inst->Instruction.Saturate == TGSI_SAT_ZERO_ONE) {
      snprintf(buf, 255, "%s = clamp(%s, 0.0, 1.0);\n", dsts[0], dsts[0]);
      emit_buf(ctx, buf);
   }
     
   return TRUE;
}

static boolean
prolog(struct tgsi_iterate_context *iter)
{
   return TRUE;
}

static void emit_header(struct dump_ctx *ctx, char *glsl_final)
{
   if (ctx->prog_type == TGSI_PROCESSOR_GEOMETRY)
      strcat(glsl_final, "#version 150\n");
   else if (ctx->glsl_ver_required == 140)
      strcat(glsl_final, "#version 140\n");
   else
      strcat(glsl_final, "#version 130\n");
   if (ctx->prog_type == TGSI_PROCESSOR_VERTEX && vrend_shader_use_explicit)
      strcat(glsl_final, "#extension GL_ARB_explicit_attrib_location : enable\n");
   if (ctx->prog_type == TGSI_PROCESSOR_FRAGMENT && fs_emit_layout(ctx))
      strcat(glsl_final, "#extension GL_ARB_fragment_coord_conventions : enable\n");
   if (ctx->glsl_ver_required < 140 && ctx->uses_sampler_rect)
      strcat(glsl_final, "#extension GL_ARB_texture_rectangle : require\n");
   if (ctx->uses_cube_array)
      strcat(glsl_final, "#extension GL_ARB_texture_cube_map_array : require\n");
   if (ctx->has_ints)
      strcat(glsl_final, "#extension GL_ARB_shader_bit_encoding : require\n");
   if (ctx->uses_sampler_ms)
      strcat(glsl_final, "#extension GL_ARB_texture_multisample : require\n");
   if (ctx->has_instanceid)
      strcat(glsl_final, "#extension GL_ARB_draw_instanced : require\n");
   if (ctx->num_ubo)
      strcat(glsl_final, "#extension GL_ARB_uniform_buffer_object : require\n");
}

static const char *samplertypeconv(int sampler_type, int *is_shad)
{
	switch (sampler_type) {
	case TGSI_TEXTURE_BUFFER: return "Buffer";
	case TGSI_TEXTURE_1D: return "1D";
	case TGSI_TEXTURE_2D: return "2D";
	case TGSI_TEXTURE_3D: return "3D";
	case TGSI_TEXTURE_CUBE: return "Cube";
	case TGSI_TEXTURE_RECT: return "2DRect";
	case TGSI_TEXTURE_SHADOW1D: *is_shad = 1; return "1DShadow";
	case TGSI_TEXTURE_SHADOW2D: *is_shad = 1; return "2DShadow";
	case TGSI_TEXTURE_SHADOWRECT: *is_shad = 1; return "2DRectShadow";
        case TGSI_TEXTURE_1D_ARRAY: return "1DArray";
        case TGSI_TEXTURE_2D_ARRAY: return "2DArray";
        case TGSI_TEXTURE_SHADOW1D_ARRAY: *is_shad = 1; return "1DArrayShadow";
        case TGSI_TEXTURE_SHADOW2D_ARRAY: *is_shad = 1; return "2DArrayShadow";
	case TGSI_TEXTURE_SHADOWCUBE: *is_shad = 1; return "CubeShadow";
	case TGSI_TEXTURE_CUBE_ARRAY: return "CubeArray";
	case TGSI_TEXTURE_SHADOWCUBE_ARRAY: *is_shad = 1; return "CubeArrayShadow";
	case TGSI_TEXTURE_2D_MSAA: return "2DMS";
	case TGSI_TEXTURE_2D_ARRAY_MSAA: return "2DMSArray";
	default: return NULL;
        }
}

static const char *get_interp_string(int interpolate)
{
   switch (interpolate) {
   case TGSI_INTERPOLATE_LINEAR:
      return "noperspective ";
   case TGSI_INTERPOLATE_PERSPECTIVE:
      return "smooth ";
   case TGSI_INTERPOLATE_CONSTANT:
      return "flat ";
   }
   return NULL;
}

static void emit_ios(struct dump_ctx *ctx, char *glsl_final)
{
   int i;
   char buf[255];
   char postfix[8];
   const char *prefix = "";

   ctx->num_interps = 0;

   if (ctx->prog_type == TGSI_PROCESSOR_FRAGMENT) {
      if (fs_emit_layout(ctx)) {
         boolean upper_left = !(ctx->fs_coord_origin ^ ctx->key->invert_fs_origin); 
         char comma = (upper_left && ctx->fs_pixel_center) ? ',' : ' ';

         snprintf(buf, 255, "layout(%s%c%s) in vec4 gl_FragCoord;\n",
                  upper_left ? "origin_upper_left" : "",
                  comma,
                  ctx->fs_pixel_center ? "pixel_center_integer" : "");
         strcat(glsl_final, buf);
      }
   }
   if (ctx->prog_type == TGSI_PROCESSOR_GEOMETRY) {
      snprintf(buf, 255, "layout(%s) in;\n", prim_to_name(ctx->gs_in_prim));
      strcat(glsl_final, buf);
               snprintf(buf, 255, "layout(%s, max_vertices = %d) out;\n", prim_to_name(ctx->gs_out_prim), ctx->gs_max_out_verts);
      strcat(glsl_final, buf);
   }
   for (i = 0; i < ctx->num_inputs; i++) {
      if (!ctx->inputs[i].glsl_predefined_no_emit) { 
         if (ctx->prog_type == TGSI_PROCESSOR_VERTEX && vrend_shader_use_explicit) {
            snprintf(buf, 255, "layout(location=%d) ", ctx->inputs[i].first);
            strcat(glsl_final, buf);
         }
         if (ctx->prog_type == TGSI_PROCESSOR_FRAGMENT &&
             (ctx->inputs[i].name == TGSI_SEMANTIC_GENERIC ||
              ctx->inputs[i].name == TGSI_SEMANTIC_COLOR)) {
            prefix = get_interp_string(ctx->inputs[i].interpolate);
            if (!prefix)
               prefix = "";
            else
               ctx->num_interps++;
         }

         if (ctx->prog_type == TGSI_PROCESSOR_GEOMETRY) {
            snprintf(postfix, 8, "[%d]", gs_input_prim_to_size(ctx->gs_in_prim));
         } else
            postfix[0] = 0;
         snprintf(buf, 255, "%sin vec4 %s%s;\n", prefix, ctx->inputs[i].glsl_name, postfix);
         strcat(glsl_final, buf);
      }
   }
   if (ctx->write_all_cbufs) {
      for (i = 0; i < 8; i++) {
         snprintf(buf, 255, "out vec4 out_c%d;\n", i);
         strcat(glsl_final, buf);
      }
   } else {
      for (i = 0; i < ctx->num_outputs; i++) {
         if (!ctx->outputs[i].glsl_predefined_no_emit) {
            if (ctx->prog_type == TGSI_PROCESSOR_VERTEX && (ctx->outputs[i].name == TGSI_SEMANTIC_GENERIC || ctx->outputs[i].name == TGSI_SEMANTIC_COLOR || ctx->outputs[i].name == TGSI_SEMANTIC_BCOLOR)) {
               ctx->num_interps++;
               prefix = INTERP_PREFIX;
            } else
               prefix = "";
            /* ugly leave spaces to patch interp in later */
            snprintf(buf, 255, "%sout vec4 %s;\n", prefix, ctx->outputs[i].glsl_name);
            strcat(glsl_final, buf);
         }
      }
   }

   if (ctx->prog_type == TGSI_PROCESSOR_VERTEX) {
      snprintf(buf, 255, "uniform vec4 winsys_adjust;\n");
      strcat(glsl_final, buf);

      if (ctx->has_clipvertex) {
            snprintf(buf, 255, "vec4 clipv_tmp;\n");
            strcat(glsl_final, buf);
      }
      if (ctx->num_clip_dist || ctx->key->clip_plane_enable) {

         if (ctx->key->clip_plane_enable) {
            snprintf(buf, 255, "uniform vec4 clipp[8];\n");
            strcat(glsl_final, buf);
         }
         snprintf(buf, 255, "out float gl_ClipDistance[%d];\n", ctx->num_clip_dist ? ctx->num_clip_dist : 8);
         strcat(glsl_final, buf);
         snprintf(buf, 255, "vec4 clip_dist_temp[2];\n");
         strcat(glsl_final, buf);
      }
   }
   
   if (ctx->so) {
      char outtype[6] = {0};
      for (i = 0; i < ctx->so->num_outputs; i++) {
         if (ctx->so->output[i].num_components == 1)
            snprintf(outtype, 6, "float");
         else
            snprintf(outtype, 6, "vec%d", ctx->so->output[i].num_components);
         snprintf(buf, 255, "out %s tfout%d;\n", outtype, i);
         strcat(glsl_final, buf);
      }
   }
   if (ctx->num_temps) {
      snprintf(buf, 255, "vec4 temps[%d];\n", ctx->num_temps);
      strcat(glsl_final, buf);
   }

   for (i = 0; i < ctx->num_address; i++) {
      snprintf(buf, 255, "int addr%d;\n", i);
      strcat(glsl_final, buf);
   }
   if (ctx->num_consts) {
      const char *cname = tgsi_proc_to_prefix(ctx->prog_type);
      snprintf(buf, 255, "uniform uvec4 %sconst0[%d];\n", cname, ctx->num_consts);
      strcat(glsl_final, buf);
   }

   if (ctx->key->color_two_side) {
      if (ctx->color_in_mask & 1) {
         snprintf(buf, 255, "vec4 realcolor0;\n");
         strcat(glsl_final, buf);
      }
      if (ctx->color_in_mask & 2) {
         snprintf(buf, 255, "vec4 realcolor1;\n");
         strcat(glsl_final, buf);
      }
   }
   if (ctx->num_ubo) {
      for (i = 0; i < ctx->num_ubo; i++) {
         const char *cname = tgsi_proc_to_prefix(ctx->prog_type);
         snprintf(buf, 255, "uniform %subo%d { vec4 %subo%dcontents[%d]; };\n", cname, ctx->ubo_idx[i], cname, ctx->ubo_idx[i], ctx->ubo_sizes[i]);
         strcat(glsl_final, buf);
      }
   }
   for (i = 0; i < 32; i++) {
      int is_shad = 0;
      const char *stc;

      if ((ctx->samplers_used & (1 << i)) == 0)
         continue;

      stc = samplertypeconv(ctx->samplers[i].tgsi_sampler_type, &is_shad);

      if (stc) {
         const char *sname;

         sname = tgsi_proc_to_prefix(ctx->prog_type);
         snprintf(buf, 255, "uniform sampler%s %ssamp%d;\n", stc, sname, i);
         strcat(glsl_final, buf);
         if (is_shad) {
            snprintf(buf, 255, "uniform vec4 %sshadmask%d;\n", sname, i);
            strcat(glsl_final, buf);
            snprintf(buf, 255, "uniform vec4 %sshadadd%d;\n", sname, i);
            strcat(glsl_final, buf);
            ctx->shadow_samp_mask |= (1 << i);
         }
      }
   }
   if (ctx->prog_type == TGSI_PROCESSOR_FRAGMENT &&
       ctx->key->pstipple_tex == true) {
      snprintf(buf, 255, "uniform sampler2D pstipple_sampler;\nfloat stip_temp;\n");
      strcat(glsl_final, buf);
   }
}

static boolean fill_fragment_interpolants(struct dump_ctx *ctx, struct vrend_shader_info *sinfo)
{
   int i, index = 0;

   for (i = 0; i < ctx->num_inputs; i++) {
      if (ctx->inputs[i].glsl_predefined_no_emit)
         continue;

      if (ctx->inputs[i].name != TGSI_SEMANTIC_GENERIC &&
          ctx->inputs[i].name != TGSI_SEMANTIC_COLOR)
         continue;

      if (ctx->inputs[i].interpolate == TGSI_INTERPOLATE_COLOR)
         continue;
      
      sinfo->interpinfo[index].semantic_name = ctx->inputs[i].name;
      sinfo->interpinfo[index].semantic_index = ctx->inputs[i].sid;
      sinfo->interpinfo[index].interpolate = ctx->inputs[i].interpolate;
      index++;
   }
   return TRUE;
}

static boolean fill_interpolants(struct dump_ctx *ctx, struct vrend_shader_info *sinfo)
{
   boolean ret;

   if (!ctx->num_interps)
      return TRUE;
   if (ctx->prog_type == TGSI_PROCESSOR_VERTEX)
      return TRUE;

   sinfo->interpinfo = calloc(ctx->num_interps, sizeof(struct vrend_interp_info));
   if (!sinfo->interpinfo)
      return FALSE;
 
   ret = fill_fragment_interpolants(ctx, sinfo);
   if (ret == FALSE)
      goto out_fail;

   return TRUE;
 out_fail:
   free(sinfo->interpinfo);
   return FALSE;
}

char *vrend_convert_shader(struct vrend_shader_cfg *cfg,
                           const struct tgsi_token *tokens,
                           struct vrend_shader_key *key,
                           struct vrend_shader_info *sinfo)
{
   struct dump_ctx ctx;
   char *glsl_final;
   boolean ret;

   memset(&ctx, 0, sizeof(struct dump_ctx));
   ctx.iter.prolog = prolog;
   ctx.iter.iterate_instruction = iter_instruction;
   ctx.iter.iterate_declaration = iter_declaration;
   ctx.iter.iterate_immediate = iter_immediate;
   ctx.iter.iterate_property = iter_property;
   ctx.iter.epilog = NULL;
   ctx.key = key;
   ctx.cfg = cfg;

   /* if we are in core profile mode we should use GLSL 1.40 */
   if (cfg->use_core_profile && cfg->glsl_version >= 140)
      ctx.glsl_ver_required = 140;

   if (sinfo->so_info.num_outputs) {
      ctx.so = &sinfo->so_info;
   }

   ctx.glsl_main = malloc(65536);
   ctx.glsl_main[0] = '\0';
   tgsi_iterate_shader(tokens, &ctx.iter);

   glsl_final = malloc(65536);
   glsl_final[0] = '\0';
   emit_header(&ctx, glsl_final);
   emit_ios(&ctx, glsl_final);

   ret = fill_interpolants(&ctx, sinfo);
   if (ret == FALSE) {
      free(ctx.glsl_main);
      free(glsl_final);
      return NULL;
   }
   strcat(glsl_final, ctx.glsl_main);
   if (vrend_dump_shaders)
      fprintf(stderr,"GLSL: %s\n", glsl_final);
   free(ctx.glsl_main);
   sinfo->num_ucp = ctx.key->clip_plane_enable ? 8 : 0;
   sinfo->samplers_used_mask = ctx.samplers_used;
   sinfo->num_consts = ctx.num_consts;
   sinfo->num_ubos = ctx.num_ubo;
   sinfo->num_inputs = ctx.num_inputs;
   sinfo->num_interps = ctx.num_interps;
   sinfo->num_outputs = ctx.num_outputs;
   sinfo->shadow_samp_mask = ctx.shadow_samp_mask;
   sinfo->glsl_ver = ctx.glsl_ver_required;
   return glsl_final;
}

static void replace_interp(char *program,
                           const char *var_name,
                           const char *pstring)
{
   char *ptr;
   int mylen = strlen(INTERP_PREFIX) + strlen("out vec4 ");

   ptr = strstr(program, var_name);
   
   if (!ptr)
      return;

   ptr -= mylen;

   memcpy(ptr, pstring, strlen(pstring));
}

boolean vrend_patch_vertex_shader_interpolants(char *program,
                                               struct vrend_shader_info *vs_info,
                                               struct vrend_shader_info *fs_info)
{
   int i;
   const char *pstring;
   char glsl_name[64];
   if (!vs_info || !fs_info)
      return TRUE;

   if (!fs_info->interpinfo)
      return TRUE;

   for (i = 0; i < fs_info->num_interps; i++) {
      pstring = get_interp_string(fs_info->interpinfo[i].interpolate);
      if (!pstring)
         continue;

      switch (fs_info->interpinfo[i].semantic_name) {
      case TGSI_SEMANTIC_COLOR:
         /* color is a bit trickier */
         if (fs_info->glsl_ver < 140) {
            if (fs_info->interpinfo[i].semantic_index == 1) {
               replace_interp(program, "gl_FrontSecondaryColor", pstring);
               replace_interp(program, "gl_BackSecondaryColor", pstring);
            } else {
               replace_interp(program, "gl_FrontColor", pstring);
               replace_interp(program, "gl_BackColor", pstring);
            }
         } else {
            snprintf(glsl_name, 64, "ex_c%d", fs_info->interpinfo[i].semantic_index);
            replace_interp(program, glsl_name, pstring);
            snprintf(glsl_name, 64, "ex_bc%d", fs_info->interpinfo[i].semantic_index);
            replace_interp(program, glsl_name, pstring);
         }
         break;
      case TGSI_SEMANTIC_GENERIC:
         snprintf(glsl_name, 64, "ex_g%d", fs_info->interpinfo[i].semantic_index);
         replace_interp(program, glsl_name, pstring);
         break;
      }
   }

   if (vrend_dump_shaders)
      fprintf(stderr,"GLSL: post interp:  %s\n", program);
   return TRUE;
}
