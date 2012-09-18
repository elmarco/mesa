#include "tgsi/tgsi_info.h"
#include "tgsi/tgsi_iterate.h"
#include <string.h>
#include <stdio.h>
#include "graw_shader.h"
/* start convert of tgsi to glsl */

struct dump_ctx {
   struct tgsi_iterate_context iter;

   char *glsl_prog;
   int size;
   uint instno;
};
static boolean
iter_declaration(struct tgsi_iterate_context *iter,
                 struct tgsi_full_declaration *decl )
{
   struct dump_ctx *ctx = (struct dump_ctx *)iter;
   char buf[255];
   char *ds;
   if (decl->Declaration.File == TGSI_FILE_INPUT) {
         ds = "in";
      if (iter->processor.Processor == TGSI_PROCESSOR_VERTEX) {
         ds = "in";
         snprintf(buf, 255, "layout(location=%d) ", decl->Range.First);
         strcat(ctx->glsl_prog, buf);
      }
      snprintf(buf, 255, "in vec4 in_%d;\n", decl->Range.First);
      strcat(ctx->glsl_prog, buf);
   } else    if (decl->Declaration.File == TGSI_FILE_OUTPUT) {
      snprintf(buf, 255, "out vec4 out_%d;\n", decl->Range.First);
      strcat(ctx->glsl_prog, buf);
   }      
   
}

static boolean
iter_property(struct tgsi_iterate_context *iter,
              struct tgsi_full_property *prop)
{
 

}

static boolean
iter_immediate(
   struct tgsi_iterate_context *iter,
   struct tgsi_full_immediate *imm )
{
   struct dump_ctx *ctx = (struct dump_ctx *) iter;
}

static boolean
iter_instruction(struct tgsi_iterate_context *iter,
                 struct tgsi_full_instruction *inst)
{
   struct dump_ctx *ctx = (struct dump_ctx *)iter;
   char srcs[3][255], dsts[3][255], buf[255];
   uint instno = ctx->instno++;
   int i;

   if (instno == 0)
      strcat(ctx->glsl_prog, "void main(void)\n{\n");
   for (i = 0; i < inst->Instruction.NumDstRegs; i++) {
      const struct tgsi_full_dst_register *dst = &inst->Dst[i];
      if (dst->Register.File == TGSI_FILE_OUTPUT) {\
         snprintf(dsts[i], 255, "out_%d", dst->Register.Index);
      }
   }
      
   for (i = 0; i < inst->Instruction.NumSrcRegs; i++) {
      const struct tgsi_full_src_register *src = &inst->Src[i];

      if (src->Register.File == TGSI_FILE_INPUT) {
         
         snprintf(srcs[i], 255, "in_%d", src->Register.Index);
      }
   }
   switch (inst->Instruction.Opcode) {

   case TGSI_OPCODE_MOV:
      snprintf(buf, 255, "%s = %s;\n", dsts[0], srcs[0]);
      strcat(ctx->glsl_prog, buf);
      break;
   case TGSI_OPCODE_END:
      strcat(ctx->glsl_prog, "}\n");
      break;
   }
   
}

static boolean
prolog(struct tgsi_iterate_context *iter)
{
   struct dump_ctx *ctx = (struct dump_ctx *)iter;

}

static void emit_header(struct dump_ctx *ctx)
{
   strcat(ctx->glsl_prog, "#version 130\n");
   strcat(ctx->glsl_prog, "#extension ARB_explicit_attrib_location : enable\n");

}

char *tgsi_convert(const struct tgsi_token *tokens,
                  int flags)
{
   struct dump_ctx ctx;

   ctx.iter.prolog = prolog;
   ctx.iter.iterate_instruction = iter_instruction;
   ctx.iter.iterate_declaration = iter_declaration;
   ctx.iter.iterate_immediate = iter_immediate;
   ctx.iter.iterate_property = iter_property;
   ctx.iter.epilog = NULL;

   ctx.glsl_prog = malloc(65536);
   ctx.glsl_prog[0] = '\0';
   emit_header(&ctx);
   tgsi_iterate_shader(tokens, &ctx.iter);
   return ctx.glsl_prog;
}
