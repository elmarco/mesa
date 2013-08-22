
#include "tgsi/tgsi_info.h"
#include "tgsi/tgsi_iterate.h"
#include <string.h>
#include <stdio.h>
#include "graw_shader.h"
extern int vrend_dump_shaders;
/*
 * TODO list
 * loops
 * DDX/DDY/TXD
 * missing opcodes
 */

/* start convert of tgsi to glsl */

int graw_shader_use_explicit = 0;

struct graw_shader_io {
   unsigned		name;
   unsigned		gpr;
   unsigned		done;
   int			sid;
   unsigned		interpolate;
   boolean                 centroid;
   unsigned first;
   boolean glsl_predefined;
   boolean glsl_no_index;
   boolean override_no_wm;
   char glsl_name[64];
};

struct graw_shader_sampler {
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
   int prog_type;
   char *glsl_main;
   int size;
   uint instno;

   int num_inputs;
   struct graw_shader_io inputs[32];
   int num_outputs;
   struct graw_shader_io outputs[32];

   int num_temps;
   struct graw_shader_sampler samplers[32];
   int num_samps;
   int num_consts;

   int num_imm;
   struct immed imm[32];
   unsigned fragcoord_input;

   int num_address;
   
   struct pipe_stream_output_info *so;
};

static boolean
iter_declaration(struct tgsi_iterate_context *iter,
                 struct tgsi_full_declaration *decl )
{
   struct dump_ctx *ctx = (struct dump_ctx *)iter;
   char buf[255];
   char *ds;
   int i;
   int color_offset = 0;
   char *name_prefix;

   ctx->prog_type = iter->processor.Processor;
   switch (decl->Declaration.File) {
   case TGSI_FILE_INPUT:
      i = ctx->num_inputs++;
      ctx->inputs[i].name = decl->Semantic.Name;
      ctx->inputs[i].sid = decl->Semantic.Index;
      ctx->inputs[i].interpolate = decl->Interp.Interpolate;
      ctx->inputs[i].centroid = decl->Interp.Centroid;
      ctx->inputs[i].first = decl->Range.First;
      ctx->inputs[i].glsl_predefined = false;
      ctx->inputs[i].glsl_no_index = false;
      ctx->inputs[i].override_no_wm = false;
      switch (ctx->inputs[i].name) {
      case TGSI_SEMANTIC_COLOR:
         if (iter->processor.Processor == TGSI_PROCESSOR_FRAGMENT) {
            if (decl->Semantic.Index == 0)
               name_prefix = "gl_Color";
            else if (decl->Semantic.Index == 1)
               name_prefix = "gl_SecondaryColor";
            else
               fprintf(stderr, "got illegal color semantic index %d\n", decl->Semantic.Index);
            ctx->inputs[i].glsl_predefined = true;
            ctx->inputs[i].glsl_no_index = true;
            break;
         }
         /* fallthrough */
      case TGSI_SEMANTIC_POSITION:
         if (iter->processor.Processor == TGSI_PROCESSOR_FRAGMENT) {
            name_prefix = "gl_FragCoord";
            ctx->inputs[i].glsl_predefined = true;
            ctx->inputs[i].glsl_no_index = true;
            break;
         }
         /* fallthrough for vertex shader */
      case TGSI_SEMANTIC_FACE:
         if (iter->processor.Processor == TGSI_PROCESSOR_FRAGMENT) {
            name_prefix = "gl_FrontFacing";
            ctx->inputs[i].glsl_predefined = true;
            ctx->inputs[i].glsl_no_index = true;
            break;
         }
      default:
         if (iter->processor.Processor == TGSI_PROCESSOR_FRAGMENT)
            name_prefix = "ex";
         else
            name_prefix = "in";
         break;
      }

      if (ctx->inputs[i].glsl_no_index)
         snprintf(ctx->inputs[i].glsl_name, 64, "%s", name_prefix);
      else {
         if (ctx->inputs[i].name == TGSI_SEMANTIC_COLOR)
            snprintf(ctx->inputs[i].glsl_name, 64, "%s_c%d", name_prefix, ctx->inputs[i].sid);
         else if (ctx->inputs[i].name == TGSI_SEMANTIC_GENERIC)
            snprintf(ctx->inputs[i].glsl_name, 64, "%s_g%d", name_prefix, ctx->inputs[i].sid);
         else
            snprintf(ctx->inputs[i].glsl_name, 64, "%s_%d", name_prefix, ctx->inputs[i].first);
      }
      break;
   case TGSI_FILE_OUTPUT:
      i = ctx->num_outputs++;
      ctx->outputs[i].name = decl->Semantic.Name;
      ctx->outputs[i].sid = decl->Semantic.Index;
      ctx->outputs[i].interpolate = decl->Interp.Interpolate;
      ctx->outputs[i].first = decl->Range.First;
      ctx->outputs[i].glsl_predefined = false;
      ctx->outputs[i].glsl_no_index = false;
      ctx->outputs[i].override_no_wm = false;
      switch (ctx->outputs[i].name) {
      case TGSI_SEMANTIC_POSITION:
         if (iter->processor.Processor == TGSI_PROCESSOR_VERTEX) {
            if (ctx->outputs[i].first > 0)
               fprintf(stderr,"Illegal position input\n");
            name_prefix = "gl_Position";
            ctx->outputs[i].glsl_predefined = true;
            ctx->outputs[i].glsl_no_index = true;
         } else if (iter->processor.Processor == TGSI_PROCESSOR_FRAGMENT) {
            if (ctx->outputs[i].first > 0)
               fprintf(stderr,"Illegal position input\n");
            name_prefix = "gl_FragDepth";
            ctx->outputs[i].glsl_predefined = true;
            ctx->outputs[i].glsl_no_index = true;
            ctx->outputs[i].override_no_wm = true;
         }
         break;
      case TGSI_SEMANTIC_COLOR:
         if (iter->processor.Processor == TGSI_PROCESSOR_VERTEX) {
            ctx->outputs[i].glsl_predefined = true;
            ctx->outputs[i].glsl_no_index = true;
            if (ctx->outputs[i].sid == 0)
               name_prefix = "gl_FrontColor";
            else if (ctx->outputs[i].sid == 1)
               name_prefix = "gl_FrontSecondaryColor";
            break;
         }
      case TGSI_SEMANTIC_BCOLOR:
         if (iter->processor.Processor == TGSI_PROCESSOR_VERTEX) {
            ctx->outputs[i].glsl_predefined = true;
            ctx->outputs[i].glsl_no_index = true;
            if (ctx->outputs[i].sid == 0)
               name_prefix = "gl_BackColor";
            else if (ctx->outputs[i].sid == 1)
               name_prefix = "gl_BackSecondaryColor";
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
         if (ctx->outputs[i].name == TGSI_SEMANTIC_COLOR)
            snprintf(ctx->outputs[i].glsl_name, 64, "%s_c%d", name_prefix, ctx->outputs[i].sid);
         else if (ctx->outputs[i].name == TGSI_SEMANTIC_GENERIC)
            snprintf(ctx->outputs[i].glsl_name, 64, "%s_g%d", name_prefix, ctx->outputs[i].sid);
         else
            snprintf(ctx->outputs[i].glsl_name, 64, "%s_%d", name_prefix, ctx->outputs[i].first + color_offset);

      }
      break;
   case TGSI_FILE_TEMPORARY:
      if (decl->Range.Last)
         ctx->num_temps+=decl->Range.Last + 1;
      else
         ctx->num_temps++;

      break;
   case TGSI_FILE_SAMPLER:
      ctx->num_samps++;
      break;
   case TGSI_FILE_CONSTANT:
      ctx->num_consts+=decl->Range.Last + 1;
      break;
   case TGSI_FILE_ADDRESS:
      ctx->num_address = 1;
      break;
   case TGSI_FILE_SYSTEM_VALUE:
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
         ctx->imm[first].val[i].ui  = imm->u[i].Uint;
      } else if (imm->Immediate.DataType == TGSI_IMM_INT32) {
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

static void emit_so_movs(struct dump_ctx *ctx)
{
   char buf[255];
   char dst[255], src[255];
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

      snprintf(buf, 255, "tfout%d = %s(%s%s);\n", i, outtype, ctx->outputs[ctx->so->output[i].register_index].glsl_name, writemask);
      strcat(ctx->glsl_main, buf);
   }

}

#define emit_arit_op2(op) snprintf(buf, 255, "%s = %s(%s %s %s);\n", dsts[0], dstconv, srcs[0], op, srcs[1])
#define emit_op1(op) snprintf(buf, 255, "%s = %s(%s(%s));\n", dsts[0], dstconv, op, srcs[0])
#define emit_compare(op) snprintf(buf, 255, "%s = %s((%s(%s, %s))%s);\n", dsts[0], dstconv, op, srcs[0], srcs[1], writemask)
static boolean
iter_instruction(struct tgsi_iterate_context *iter,
                 struct tgsi_full_instruction *inst)
{
   struct dump_ctx *ctx = (struct dump_ctx *)iter;
   char srcs[3][255], dsts[3][255], buf[255];
   uint instno = ctx->instno++;
   int i;
   int j;
   int sreg_index;
   char dstconv[6] = {0};
   char writemask[6] = {0};
   char *twm;
   char bias[64] = {0};
   char *lod;
   boolean is_shad = FALSE;
   if (instno == 0)
      strcat(ctx->glsl_main, "void main(void)\n{\n");
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
         if (wm_idx == 2)
            snprintf(dstconv, 6, "float");
         else
            snprintf(dstconv, 6, "vec%d", wm_idx-1);
      } else {
         snprintf(dstconv, 6, "vec4");
      }
      if (dst->Register.File == TGSI_FILE_OUTPUT) {
         for (j = 0; j < ctx->num_outputs; j++)
            if (ctx->outputs[j].first == dst->Register.Index) {
               snprintf(dsts[i], 255, "%s%s", ctx->outputs[j].glsl_name, ctx->outputs[j].override_no_wm ? "" : writemask);
               break;
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
      char swizzle[6] = {0};
      char negate = src->Register.Negate ? '-' : ' ';
      if (src->Register.SwizzleX != TGSI_SWIZZLE_X ||
          src->Register.SwizzleY != TGSI_SWIZZLE_Y ||
          src->Register.SwizzleZ != TGSI_SWIZZLE_Z ||
          src->Register.SwizzleW != TGSI_SWIZZLE_W) {
         swizzle[0] = '.';
         swizzle[1] = get_swiz_char(src->Register.SwizzleX);
         swizzle[2] = get_swiz_char(src->Register.SwizzleY);
         swizzle[3] = get_swiz_char(src->Register.SwizzleZ);
         swizzle[4] = get_swiz_char(src->Register.SwizzleW);
      }
      if (src->Register.File == TGSI_FILE_INPUT) {
         for (j = 0; j < ctx->num_inputs; j++)
            if (ctx->inputs[j].first == src->Register.Index) {
               snprintf(srcs[i], 255, "%c%s%s", negate, ctx->inputs[j].glsl_name, swizzle);
               break;
            }
      }
      else if (src->Register.File == TGSI_FILE_TEMPORARY) {
         if (src->Register.Indirect) {
            snprintf(srcs[i], 255, "%ctemps[addr0 + %d]%s", negate, src->Register.Index, swizzle);
         } else
             snprintf(srcs[i], 255, "%ctemps[%d]%s", negate, src->Register.Index, swizzle);
      } else if (src->Register.File == TGSI_FILE_CONSTANT) {
	  const char *cname = ctx->prog_type == TGSI_PROCESSOR_VERTEX ? "vsconst" : "fsconst";
          if (src->Register.Indirect) {
             snprintf(srcs[i], 255, "%c%s[addr0 + %d]%s", negate, cname, src->Register.Index, swizzle);
          } else
             snprintf(srcs[i], 255, "%c%s[%d]%s", negate, cname, src->Register.Index, swizzle);
      } else if (src->Register.File == TGSI_FILE_SAMPLER) {
	  const char *cname = ctx->prog_type == TGSI_PROCESSOR_VERTEX ? "vssamp" : "fssamp";
          snprintf(srcs[i], 255, "%s%d%s", cname, src->Register.Index, swizzle);
	  sreg_index = src->Register.Index;
      } else if (src->Register.File == TGSI_FILE_IMMEDIATE) {
         struct immed *imd = &ctx->imm[(src->Register.Index)];
         int idx = src->Register.SwizzleX;
         char temp[25];
         /* build up a vec4 of immediates */
         snprintf(srcs[i], 255, "vec4(");
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
               snprintf(temp, 25, "%c%.4f", negate, imd->val[idx].f);
               break;
            case TGSI_IMM_UINT32:
               snprintf(temp, 25, "%c%u", negate, imd->val[idx].ui);
               break;
            case TGSI_IMM_INT32:
               snprintf(temp, 25, "%c%d", negate, imd->val[idx].i);
               break;
            }
            strncat(srcs[i], temp, 255);
            if (j < 3)
               strcat(srcs[i], ",");
            else
               strcat(srcs[i], ")");
         }
      }
   }
   switch (inst->Instruction.Opcode) {
   case TGSI_OPCODE_SQRT:
      snprintf(buf, 255, "%s = sqrt(vec4(%s))%s;\n", dsts[0], srcs[0], writemask);
      strcat(ctx->glsl_main, buf);
      break;
   case TGSI_OPCODE_LRP:
      snprintf(buf, 255, "%s = mix(vec4(%s), vec4(%s), vec4(%s))%s;\n", dsts[0], srcs[2], srcs[1], srcs[0], writemask);
      strcat(ctx->glsl_main, buf);
      break;
   case TGSI_OPCODE_DP2:
      snprintf(buf, 255, "%s = %s(dot(vec2(%s), vec2(%s)));\n", dsts[0], dstconv, srcs[0], srcs[1]);
      strcat(ctx->glsl_main, buf);
      break;
   case TGSI_OPCODE_DP3:
      snprintf(buf, 255, "%s = %s(dot(vec3(%s), vec3(%s)));\n", dsts[0], dstconv, srcs[0], srcs[1]);
      strcat(ctx->glsl_main, buf);
      break;
   case TGSI_OPCODE_DP4:
      snprintf(buf, 255, "%s = %s(dot(vec4(%s), vec4(%s)));\n", dsts[0], dstconv, srcs[0], srcs[1]);
      strcat(ctx->glsl_main, buf);
      break;
   case TGSI_OPCODE_DPH:
      snprintf(buf, 255, "%s = %s(dot(vec4(%s), vec4(vec3(%s), 1.0)));\n", dsts[0], dstconv, srcs[0], srcs[1]);
      strcat(ctx->glsl_main, buf);
      break;
   case TGSI_OPCODE_MAX:
   case TGSI_OPCODE_IMAX:
   case TGSI_OPCODE_UMAX:
      snprintf(buf, 255, "%s = %s(max(%s, %s));\n", dsts[0], dstconv, srcs[0], srcs[1]);
      strcat(ctx->glsl_main, buf);
      break;
   case TGSI_OPCODE_MIN:
   case TGSI_OPCODE_IMIN:
   case TGSI_OPCODE_UMIN:
      snprintf(buf, 255, "%s = %s(min(%s, %s));\n", dsts[0], dstconv, srcs[0], srcs[1]);
      strcat(ctx->glsl_main, buf);
      break;
   case TGSI_OPCODE_ABS:
   case TGSI_OPCODE_IABS:
      emit_op1("abs");
      strcat(ctx->glsl_main, buf);
      break;
   case TGSI_OPCODE_KIL:
      snprintf(buf, 255, "if (any(lessThan(%s, vec4(0.0))))\ndiscard;\n", srcs[0]);
      strcat(ctx->glsl_main, buf);
      break;
   case TGSI_OPCODE_IF:
      snprintf(buf, 255, "if (any(bvec4(%s))) {\n", srcs[0]);
      strcat(ctx->glsl_main, buf);
      break;
   case TGSI_OPCODE_ELSE:
      snprintf(buf, 255, "} else {\n");
      strcat(ctx->glsl_main, buf);
      break;
   case TGSI_OPCODE_ENDIF:
      snprintf(buf, 255, "}\n");
      strcat(ctx->glsl_main, buf);
      break;
   case TGSI_OPCODE_KILP:
      snprintf(buf, 255, "discard;\n");
      strcat(ctx->glsl_main, buf);
      break;
   case TGSI_OPCODE_LIT:
      snprintf(buf, 255, "%s.x = 1.0;\n", dsts[0]);
      strcat(ctx->glsl_main, buf);
      snprintf(buf, 255, "%s.y = max(%s.x, 0.0);\n", dsts[0], srcs[0]);
      strcat(ctx->glsl_main, buf);
      snprintf(buf, 255, "%s.z = pow(max(0.0, %s.y) * step(0.0, %s.x), clamp(%s.w, -128.0, 128.0));\n", dsts[0], srcs[0], srcs[0], srcs[0]);
      strcat(ctx->glsl_main, buf);
      snprintf(buf, 255, "%s.w = 1.0;\n", dsts[0]);
      strcat(ctx->glsl_main, buf);
      break;
   case TGSI_OPCODE_EX2:
      emit_op1("exp2");
      strcat(ctx->glsl_main, buf);
      break;
   case TGSI_OPCODE_LG2:
      emit_op1("log2");
      strcat(ctx->glsl_main, buf);
      break;
   case TGSI_OPCODE_LOG:
      emit_op1("log");
      strcat(ctx->glsl_main, buf);
      break;
   case TGSI_OPCODE_COS:
      emit_op1("cos");
      strcat(ctx->glsl_main, buf);
      break;
   case TGSI_OPCODE_SIN:
      emit_op1("sin");
      strcat(ctx->glsl_main, buf);
      break;
   case TGSI_OPCODE_DDX:
      emit_op1("dFdx");
      strcat(ctx->glsl_main, buf);
      break;
   case TGSI_OPCODE_DDY:
      emit_op1("dFdy");
      strcat(ctx->glsl_main, buf);
      break;
   case TGSI_OPCODE_RCP:
      snprintf(buf, 255, "%s = %s(1.0/(%s));\n", dsts[0], dstconv, srcs[0]);
      strcat(ctx->glsl_main, buf);
      break;
   case TGSI_OPCODE_FLR:
      emit_op1("floor");
      strcat(ctx->glsl_main, buf);
      break;
   case TGSI_OPCODE_CEIL:
      emit_op1("ceil");
      strcat(ctx->glsl_main, buf);
      break;
   case TGSI_OPCODE_FRC:
      emit_op1("fract");
      strcat(ctx->glsl_main, buf);
      break;
   case TGSI_OPCODE_TRUNC:
      emit_op1("trunc");
      strcat(ctx->glsl_main, buf);
      break;
   case TGSI_OPCODE_SSG:
      emit_op1("sign");
      strcat(ctx->glsl_main, buf);
      break;
   case TGSI_OPCODE_RSQ:
      snprintf(buf, 255, "%s = %s(inversesqrt(%s.x));\n", dsts[0], dstconv, srcs[0]);
      strcat(ctx->glsl_main, buf);
      break;
   case TGSI_OPCODE_MOV:
      snprintf(buf, 255, "%s = %s%s;\n", dsts[0], srcs[0], writemask);
      strcat(ctx->glsl_main, buf);
      break;
   case TGSI_OPCODE_ADD:
      emit_arit_op2("+");
      strcat(ctx->glsl_main, buf);
      break;
   case TGSI_OPCODE_SUB:
      emit_arit_op2("-");
      strcat(ctx->glsl_main, buf);
      break;
   case TGSI_OPCODE_MUL:
   case TGSI_OPCODE_UMUL:
      emit_arit_op2("*");
      strcat(ctx->glsl_main, buf);
      break;
   case TGSI_OPCODE_ISHR:
   case TGSI_OPCODE_USHR:
      emit_arit_op2(">>");
      strcat(ctx->glsl_main, buf);
      break;
   case TGSI_OPCODE_SHL:
      emit_arit_op2("<<");
      strcat(ctx->glsl_main, buf);
      break;
   case TGSI_OPCODE_MAD:
      snprintf(buf, 255, "%s = %s(%s * %s + %s);\n", dsts[0], dstconv, srcs[0], srcs[1], srcs[2]);
      strcat(ctx->glsl_main, buf);
      break;
   case TGSI_OPCODE_OR:
      emit_arit_op2("|");
      strcat(ctx->glsl_main, buf);
      break;
   case TGSI_OPCODE_AND:
      emit_arit_op2("&");
      strcat(ctx->glsl_main, buf);
      break;
   case TGSI_OPCODE_XOR:
      emit_arit_op2("^");
      strcat(ctx->glsl_main, buf);
      break;
   case TGSI_OPCODE_MOD:
      emit_arit_op2("%");
      strcat(ctx->glsl_main, buf);
      break;
   case TGSI_OPCODE_TEX:
   case TGSI_OPCODE_TXB:
   case TGSI_OPCODE_TXL:
      ctx->samplers[sreg_index].tgsi_sampler_type = inst->Texture.Texture;
      switch (inst->Texture.Texture) {
      case TGSI_TEXTURE_1D:
         twm = ".x";
         break;
      case TGSI_TEXTURE_2D:
      case TGSI_TEXTURE_1D_ARRAY:
         twm = ".xy";
         break;
      case TGSI_TEXTURE_SHADOW1D:
      case TGSI_TEXTURE_SHADOW2D:
      case TGSI_TEXTURE_SHADOW1D_ARRAY:
         is_shad = TRUE;
      case TGSI_TEXTURE_3D:
      case TGSI_TEXTURE_CUBE:
      case TGSI_TEXTURE_2D_ARRAY:
         twm = ".xyz";
         break;
      case TGSI_TEXTURE_SHADOWCUBE:
      case TGSI_TEXTURE_SHADOW2D_ARRAY:
         is_shad = TRUE;
      default:
         twm = "";
         break;
      }

      if (inst->Instruction.Opcode == TGSI_OPCODE_TXB || inst->Instruction.Opcode == TGSI_OPCODE_TXL)
         snprintf(bias, 64, ", %s.w", srcs[0]);
      else
         bias[0] = 0;

      if (inst->Instruction.Opcode == TGSI_OPCODE_TXL)
         lod = "Lod";
      else
         lod = "";

      /* rect is special in GLSL 1.30 */
      if (inst->Texture.Texture == TGSI_TEXTURE_RECT)
         snprintf(buf, 255, "%s = texture2DRect(%s, %s.xy)%s;\n", dsts[0], srcs[1], srcs[0], writemask);
      else if (inst->Texture.Texture == TGSI_TEXTURE_SHADOWRECT)
         snprintf(buf, 255, "%s = shadow2DRect(%s, %s.xyz)%s;\n", dsts[0], srcs[1], srcs[0], writemask);
      else if (is_shad) /* TGSI returns 1.0 in alpha */
         snprintf(buf, 255, "%s = %s(vec4(vec3(texture%s(%s, %s%s%s)), 1.0)%s);\n", dsts[0], dstconv, lod, srcs[1], srcs[0], twm, bias, writemask);
      else
         snprintf(buf, 255, "%s = %s(texture%s(%s, %s%s%s)%s);\n", dsts[0], dstconv, lod, srcs[1], srcs[0], twm, bias, writemask);
      strcat(ctx->glsl_main, buf);
      break;
   case TGSI_OPCODE_TXP:
      ctx->samplers[sreg_index].tgsi_sampler_type = inst->Texture.Texture;
      if (inst->Texture.Texture == TGSI_TEXTURE_RECT)
         snprintf(buf, 255, "%s = texture2DRectProj(%s, %s)%s;\n", dsts[0], srcs[1], srcs[0], writemask);
      else if (inst->Texture.Texture == TGSI_TEXTURE_SHADOWRECT)
         snprintf(buf, 255, "%s = shadow2DRectProj(%s, %s)%s;\n", dsts[0], srcs[1], srcs[0], writemask);
      else if (inst->Texture.Texture == TGSI_TEXTURE_CUBE || inst->Texture.Texture == TGSI_TEXTURE_2D_ARRAY)
         snprintf(buf, 255, "%s = texture(%s, %s.xyz)%s;\n", dsts[0], srcs[1], srcs[0], writemask);
      else if (inst->Texture.Texture == TGSI_TEXTURE_1D_ARRAY)
         snprintf(buf, 255, "%s = texture(%s, %s.xy)%s;\n", dsts[0], srcs[1], srcs[0], writemask);
      else
         snprintf(buf, 255, "%s = %s(textureProj(%s, %s)%s);\n", dsts[0], dstconv, srcs[1], srcs[0], writemask);

      strcat(ctx->glsl_main, buf);
      break;
   case TGSI_OPCODE_I2F:
      snprintf(buf, 255, "%s = float(%s);\n", dsts[0], srcs[0]);      
      strcat(ctx->glsl_main, buf);
      break;
   case TGSI_OPCODE_F2I:
      snprintf(buf, 255, "%s = int(%s);\n", dsts[0], srcs[0]);      
      strcat(ctx->glsl_main, buf);
      break;
   case TGSI_OPCODE_F2U:
      snprintf(buf, 255, "%s = uint(%s);\n", dsts[0], srcs[0]);      
      strcat(ctx->glsl_main, buf);
      break;
   case TGSI_OPCODE_NOT:
      snprintf(buf, 255, "%s = not(%s);\n", dsts[0], srcs[0]);
      strcat(ctx->glsl_main, buf);
      break;
   case TGSI_OPCODE_USEQ:
   case TGSI_OPCODE_SEQ:
      emit_compare("equal");
      strcat(ctx->glsl_main, buf);
      break;
   case TGSI_OPCODE_SLT:
   case TGSI_OPCODE_ISLT:
   case TGSI_OPCODE_USLT:
      emit_compare("lessThan");
      strcat(ctx->glsl_main, buf);
      break;
   case TGSI_OPCODE_SNE:
   case TGSI_OPCODE_USNE:
      emit_compare("notEqual");
      strcat(ctx->glsl_main, buf);
      break;
   case TGSI_OPCODE_SGE:
   case TGSI_OPCODE_ISGE:
   case TGSI_OPCODE_USGE:
      emit_compare("greaterThanEqual");
      strcat(ctx->glsl_main, buf);
      break;
   case TGSI_OPCODE_POW:
      snprintf(buf, 255, "%s = %s(pow(%s, %s));\n", dsts[0], dstconv, srcs[0], srcs[1]);
      strcat(ctx->glsl_main, buf);
      break;
   case TGSI_OPCODE_CMP:
      snprintf(buf, 255, "%s = ((float(%s) >= 0) ? %s : %s)%s;\n", dsts[0], srcs[0], srcs[2], srcs[1], writemask);
      strcat(ctx->glsl_main, buf);
      break;
   case TGSI_OPCODE_END:
      if (ctx->so)
         emit_so_movs(ctx);
      strcat(ctx->glsl_main, "}\n");
      break;
   case TGSI_OPCODE_RET:
      strcat(ctx->glsl_main, "return;\n");
      break;
   case TGSI_OPCODE_ARL:
      snprintf(buf, 255, "addr0 = int(floor(%s)%s);\n", srcs[0], writemask);
      strcat(ctx->glsl_main, buf);
      break;
   case TGSI_OPCODE_XPD:
      snprintf(buf, 255, "%s = %s(cross(vec3(%s), vec3(%s)));\n", dsts[0], dstconv, srcs[0], srcs[1]);
      strcat(ctx->glsl_main, buf);
      break;
   case TGSI_OPCODE_BGNLOOP:
      snprintf(buf, 255, "do {\n");
      strcat(ctx->glsl_main, buf);
      break;
   case TGSI_OPCODE_ENDLOOP:
      snprintf(buf, 255, "} while(true);\n");
      strcat(ctx->glsl_main, buf);
      break;
   case TGSI_OPCODE_BRK:
      snprintf(buf, 255, "break;\n");
      strcat(ctx->glsl_main, buf);
      break;
   default:
      fprintf(stderr,"failed to convert opcode %d\n", inst->Instruction.Opcode);
      break;
   }

   if (inst->Instruction.Saturate == TGSI_SAT_ZERO_ONE) {
      snprintf(buf, 255, "%s = clamp(%s, 0.0, 1.0);\n", dsts[0], dsts[0]);
      strcat(ctx->glsl_main, buf);
   }
     
   return TRUE;
}

static boolean
prolog(struct tgsi_iterate_context *iter)
{
   struct dump_ctx *ctx = (struct dump_ctx *)iter;
   return TRUE;
}

static void emit_header(struct dump_ctx *ctx, char *glsl_final)
{
   strcat(glsl_final, "#version 130\n");
   if (ctx->prog_type == TGSI_PROCESSOR_VERTEX && graw_shader_use_explicit)
      strcat(glsl_final, "#extension GL_ARB_explicit_attrib_location : enable\n");
   strcat(glsl_final, "#extension GL_ARB_texture_rectangle : require\n");

}

static const char *samplertypeconv(int sampler_type)
{
	switch (sampler_type) {
	case TGSI_TEXTURE_1D: return "1D";
	case TGSI_TEXTURE_2D: return "2D";
	case TGSI_TEXTURE_3D: return "3D";
	case TGSI_TEXTURE_CUBE: return "Cube";
	case TGSI_TEXTURE_RECT: return "2DRect";
	case TGSI_TEXTURE_SHADOW1D: return "1DShadow";
	case TGSI_TEXTURE_SHADOW2D: return "2DShadow";
	case TGSI_TEXTURE_SHADOWRECT: return "2DRectShadow";
        case TGSI_TEXTURE_1D_ARRAY: return "1DArray";
        case TGSI_TEXTURE_2D_ARRAY: return "2DArray";
        case TGSI_TEXTURE_SHADOW1D_ARRAY: return "1DArrayShadow";
        case TGSI_TEXTURE_SHADOW2D_ARRAY: return "2DArrayShadow";
	default: return "UNK";
        }
}

static void emit_ios(struct dump_ctx *ctx, char *glsl_final)
{
   int i;
   char buf[255];

   for (i = 0; i < ctx->num_inputs; i++) {
      if (!ctx->inputs[i].glsl_predefined) { 
         if (ctx->prog_type == TGSI_PROCESSOR_VERTEX && graw_shader_use_explicit) {
            snprintf(buf, 255, "layout(location=%d) ", ctx->inputs[i].first);
            strcat(glsl_final, buf);
         }
         snprintf(buf, 255, "in vec4 %s;\n", ctx->inputs[i].glsl_name);
         strcat(glsl_final, buf);
      }
   }
   for (i = 0; i < ctx->num_outputs; i++) {
      if (!ctx->outputs[i].glsl_predefined) {
         snprintf(buf, 255, "out vec4 %s;\n", ctx->outputs[i].glsl_name);
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
      if (ctx->prog_type == TGSI_PROCESSOR_VERTEX)
         snprintf(buf, 255, "uniform vec4 vsconst[%d];\n", ctx->num_consts);
      else
         snprintf(buf, 255, "uniform vec4 fsconst[%d];\n", ctx->num_consts);
      strcat(glsl_final, buf);
   }
   for (i = 0; i < ctx->num_samps; i++) {
      if (ctx->prog_type == TGSI_PROCESSOR_VERTEX)
         snprintf(buf, 255, "uniform sampler%s vssamp%d;\n", samplertypeconv(ctx->samplers[i].tgsi_sampler_type), i);
      else
         snprintf(buf, 255, "uniform sampler%s fssamp%d;\n", samplertypeconv(ctx->samplers[i].tgsi_sampler_type), i);
      strcat(glsl_final, buf);
   }
}

char *tgsi_convert(const struct tgsi_token *tokens,
                   int flags, int *num_samplers, int *num_consts, int *num_inputs, const struct pipe_stream_output_info *so_info)
{
   struct dump_ctx ctx;
   char *glsl_final;
   memset(&ctx, 0, sizeof(struct dump_ctx));
   ctx.iter.prolog = prolog;
   ctx.iter.iterate_instruction = iter_instruction;
   ctx.iter.iterate_declaration = iter_declaration;
   ctx.iter.iterate_immediate = iter_immediate;
   ctx.iter.iterate_property = iter_property;
   ctx.iter.epilog = NULL;

   if (so_info && so_info->num_outputs) {
      ctx.so = so_info;
   }

   ctx.glsl_main = malloc(65536);
   ctx.glsl_main[0] = '\0';
   tgsi_iterate_shader(tokens, &ctx.iter);

   glsl_final = malloc(65536);
   glsl_final[0] = '\0';
   emit_header(&ctx, glsl_final);
   emit_ios(&ctx, glsl_final);
   strcat(glsl_final, ctx.glsl_main);
   if (vrend_dump_shaders)
      fprintf(stderr,"GLSL: %s\n", glsl_final);
   free(ctx.glsl_main);
   *num_samplers = ctx.num_samps;
   *num_consts = ctx.num_consts;
   *num_inputs = ctx.num_inputs;
   return glsl_final;
}
