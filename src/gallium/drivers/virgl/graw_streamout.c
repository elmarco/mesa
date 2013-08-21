#include "util/u_memory.h"
#include "graw_context.h"
#include "graw_encode.h"
#include "util/u_inlines.h"

static struct pipe_stream_output_target *graw_create_so_target(
   struct pipe_context *ctx,
   struct pipe_resource *buffer,
   unsigned buffer_offset,
   unsigned buffer_size)
{
   struct graw_context *grctx = (struct graw_context *)ctx;
   struct graw_resource *res = (struct graw_resource *)buffer;
   struct graw_so_target *t = CALLOC_STRUCT(graw_so_target);
   uint32_t handle;

   if (!t)
      return NULL;
   handle = graw_object_assign_handle();

   t->base.reference.count = 1;
   t->base.context = ctx;
   pipe_resource_reference(&t->base.buffer, buffer);
   t->base.buffer_offset = buffer_offset;
   t->base.buffer_size = buffer_size;
   t->handle = handle;
   graw_encoder_create_so_target(grctx, handle, res, buffer_offset, buffer_size);
   return &t->base;
}

static void graw_destroy_so_target(struct pipe_context *ctx,
                                   struct pipe_stream_output_target *target)
{
   struct graw_context *grctx = (struct graw_context *)ctx;
   struct graw_so_target *t = (struct graw_so_target *)target;

   pipe_resource_reference(&t->base.buffer, NULL);
   graw_encode_delete_object(grctx, t->handle, VIRGL_OBJECT_STREAMOUT_TARGET);
   FREE(t);
}

static void graw_set_so_targets(struct pipe_context *ctx,
                                unsigned num_targets,
                                struct pipe_stream_output_target **targets,
                                unsigned append_bitmask)
{
   struct graw_context *grctx = (struct graw_context *)ctx;
   graw_encoder_set_so_targets(grctx, num_targets, targets, append_bitmask);
}

void graw_init_so_functions(struct graw_context *grctx)
{
   grctx->base.create_stream_output_target = graw_create_so_target;
   grctx->base.stream_output_target_destroy = graw_destroy_so_target;
   grctx->base.set_stream_output_targets = graw_set_so_targets;
}
