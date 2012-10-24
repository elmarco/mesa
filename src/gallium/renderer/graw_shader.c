#include "tgsi/tgsi_info.h"
#include "tgsi/tgsi_iterate.h"
#include <string.h>
#include <stdio.h>
#include "graw_shader.h"
/* start convert of tgsi to glsl */

struct graw_shader_io {
   unsigned		name;
   unsigned		gpr;
   unsigned		done;
   int			sid;
   unsigned		interpolate;
   boolean                 centroid;
   unsigned		write_mask;
   unsigned first;
   boolean glsl_predefined;
   boolean glsl_no_index;
   char glsl_name[64];
};

struct graw_shader_sampler {
   int tgsi_sampler_type;
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

   int num_flt_imm;
   float flt_imm[32];
   unsigned fragcoord_input;
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
      if (iter->processor.Processor == TGSI_PROCESSOR_FRAGMENT)
         name_prefix = "ex";
      else
         name_prefix = "in";
      snprintf(ctx->inputs[i].glsl_name, 64, "%s_%d", name_prefix, ctx->inputs[i].first);
      break;
   case TGSI_FILE_OUTPUT:
      i = ctx->num_outputs++;
      ctx->outputs[i].name = decl->Semantic.Name;
      ctx->outputs[i].sid = decl->Semantic.Index;
      ctx->outputs[i].interpolate = decl->Interp.Interpolate;
      ctx->outputs[i].first = decl->Range.First;
      switch (ctx->outputs[i].name) {
      case TGSI_SEMANTIC_POSITION:
         if (iter->processor.Processor == TGSI_PROCESSOR_VERTEX) {
            if (ctx->outputs[i].first > 0)
               fprintf(stderr,"Illegal position input\n");
            name_prefix = "gl_Position";
            ctx->outputs[i].glsl_predefined = true;
            ctx->outputs[i].glsl_no_index = true;
         }
         break;
      case TGSI_SEMANTIC_COLOR:
      case TGSI_SEMANTIC_GENERIC:
         if (iter->processor.Processor == TGSI_PROCESSOR_VERTEX)
            if (ctx->outputs[i].name == TGSI_SEMANTIC_COLOR || ctx->outputs[i].name == TGSI_SEMANTIC_GENERIC)
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
      else
         snprintf(ctx->outputs[i].glsl_name, 64, "%s_%d", name_prefix, ctx->outputs[i].first + color_offset);

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
   
   if (imm->Immediate.DataType == TGSI_IMM_FLOAT32) {
      int first = ctx->num_flt_imm;
      for (i = 0; i < 4; i++)
         ctx->flt_imm[first + i] = imm->u[i].Float;
      ctx->num_flt_imm += 4;
   }
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
}

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
   if (instno == 0)
      strcat(ctx->glsl_main, "void main(void)\n{\n");
   for (i = 0; i < inst->Instruction.NumDstRegs; i++) {
      const struct tgsi_full_dst_register *dst = &inst->Dst[i];
      char writemask[6] = {0};
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
      }
      if (dst->Register.File == TGSI_FILE_OUTPUT) {
         for (j = 0; j < ctx->num_outputs; j++)
            if (ctx->outputs[j].first == dst->Register.Index) {
               snprintf(dsts[i], 255, "%s%s", ctx->outputs[j].glsl_name, writemask);
               break;
            }
      }
      else if (dst->Register.File == TGSI_FILE_TEMPORARY)
          snprintf(dsts[i], 255, "temp%d%s", dst->Register.Index, writemask);
   }
      
   for (i = 0; i < inst->Instruction.NumSrcRegs; i++) {
      const struct tgsi_full_src_register *src = &inst->Src[i];
      char swizzle[6] = {0};
      
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
               snprintf(srcs[i], 255, "%s%s", ctx->inputs[j].glsl_name, swizzle);
               break;
            }
      }
      else if (src->Register.File == TGSI_FILE_TEMPORARY)
          snprintf(srcs[i], 255, "temp%d%s", src->Register.Index, swizzle);
      else if (src->Register.File == TGSI_FILE_CONSTANT)
          snprintf(srcs[i], 255, "const%d%s", src->Register.Index, swizzle);
      else if (src->Register.File == TGSI_FILE_SAMPLER) {
          snprintf(srcs[i], 255, "samp%d%s", src->Register.Index, swizzle);
	  sreg_index = src->Register.Index;
      } else if (src->Register.File == TGSI_FILE_IMMEDIATE) {
         snprintf(srcs[i], 255, "%.4f", ctx->flt_imm[(src->Register.Index)]);
      }
   }
   switch (inst->Instruction.Opcode) {

   case TGSI_OPCODE_DP3:
      snprintf(buf, 255, "%s = %s(dot(%s.xyzw, %s.xyzw));\n", dsts[0], dstconv, srcs[0], srcs[1]);
      strcat(ctx->glsl_main, buf);
      break;
   case TGSI_OPCODE_MAX:
      snprintf(buf, 255, "%s = %s(max(%s));\n", dsts[0], dstconv, srcs[0]);
      strcat(ctx->glsl_main, buf);
      break;
   case TGSI_OPCODE_MIN:
      snprintf(buf, 255, "%s = %s(min(%s));\n", dsts[0], dstconv, srcs[0]);
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
   case TGSI_OPCODE_RSQ:
      snprintf(buf, 255, "%s = %s(inversesqrt(%s.x));\n", dsts[0], dstconv, srcs[0]);
      strcat(ctx->glsl_main, buf);
      break;
   case TGSI_OPCODE_MOV:
      snprintf(buf, 255, "%s = %s(%s);\n", dsts[0], dstconv, srcs[0]);
      strcat(ctx->glsl_main, buf);
      break;
   case TGSI_OPCODE_ADD:
      snprintf(buf, 255, "%s = %s(%s + %s);\n", dsts[0], dstconv, srcs[0], srcs[1]);
      strcat(ctx->glsl_main, buf);
      break;
   case TGSI_OPCODE_MUL:
      snprintf(buf, 255, "%s = %s(%s * %s);\n", dsts[0], dstconv, srcs[0], srcs[1]);
      strcat(ctx->glsl_main, buf);
      break;
   case TGSI_OPCODE_MAD:
      snprintf(buf, 255, "%s = %s(%s * %s + %s);\n", dsts[0], dstconv, srcs[0], srcs[1], srcs[2]);
      strcat(ctx->glsl_main, buf);
      break;
   case TGSI_OPCODE_TEX:
      ctx->samplers[sreg_index].tgsi_sampler_type = inst->Texture.Texture;
      snprintf(buf, 255, "%s = %s(texture(%s, %s.xy));\n", dsts[0], dstconv, srcs[1], srcs[0]);
      strcat(ctx->glsl_main, buf);
      break;
   case TGSI_OPCODE_TXP:
      ctx->samplers[sreg_index].tgsi_sampler_type = inst->Texture.Texture;
      snprintf(buf, 255, "%s = %s(textureProj(%s, %s));\n", dsts[0], dstconv, srcs[1], srcs[0]);

      strcat(ctx->glsl_main, buf);
      break;
   case TGSI_OPCODE_END:
      strcat(ctx->glsl_main, "}\n");
      break;
   default:
      assert(0);
      return FALSE;
      break;
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
   if (ctx->prog_type == TGSI_PROCESSOR_VERTEX)
      strcat(glsl_final, "#extension GL_ARB_explicit_attrib_location : enable\n");

}

static const char *samplertypeconv(int sampler_type)
{
	switch (sampler_type) {
	case TGSI_TEXTURE_1D: return "1D";
	case TGSI_TEXTURE_2D: return "2D";
	case TGSI_TEXTURE_3D: return "3D";
	default: return "UNK";
        }
}

static void emit_ios(struct dump_ctx *ctx, char *glsl_final)
{
   int i;
   char buf[255];

   for (i = 0; i < ctx->num_inputs; i++) {
      if (ctx->prog_type == TGSI_PROCESSOR_VERTEX) {
         snprintf(buf, 255, "layout(location=%d) ", ctx->inputs[i].first);
         strcat(glsl_final, buf);
      }
      snprintf(buf, 255, "in vec4 %s;\n", ctx->inputs[i].glsl_name);
      strcat(glsl_final, buf);
   }
   for (i = 0; i < ctx->num_outputs; i++) {
      if (!ctx->outputs[i].glsl_predefined) {
         snprintf(buf, 255, "out vec4 %s;\n", ctx->outputs[i].glsl_name);
         strcat(glsl_final, buf);
      }
   }
   for (i = 0; i < ctx->num_temps; i++) {
      snprintf(buf, 255, "vec4 temp%d;\n", i);
      strcat(glsl_final, buf);
   }
   for (i = 0; i < ctx->num_consts; i++) {
      snprintf(buf, 255, "uniform vec4 const%d;\n", i);
      strcat(glsl_final, buf);
   }
   for (i = 0; i < ctx->num_samps; i++) {
      snprintf(buf, 255, "uniform sampler%s samp%d;\n", samplertypeconv(ctx->samplers[i].tgsi_sampler_type), i);
      strcat(glsl_final, buf);
   }
}

char *tgsi_convert(const struct tgsi_token *tokens,
                  int flags)
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

   ctx.glsl_main = malloc(65536);
   ctx.glsl_main[0] = '\0';
   tgsi_iterate_shader(tokens, &ctx.iter);

   glsl_final = malloc(65536);
   glsl_final[0] = '\0';
   emit_header(&ctx, glsl_final);
   emit_ios(&ctx, glsl_final);
   strcat(glsl_final, ctx.glsl_main);
   free(ctx.glsl_main);
   return glsl_final;
}
