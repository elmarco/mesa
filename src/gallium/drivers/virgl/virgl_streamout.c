#include "util/u_memory.h"
#include "util/u_inlines.h"
#include "virgl_context.h"
#include "virgl_encode.h"
#include "virgl_resource.h"

static struct pipe_stream_output_target *virgl_create_so_target(
   struct pipe_context *ctx,
   struct pipe_resource *buffer,
   unsigned buffer_offset,
   unsigned buffer_size)
{
   struct virgl_context *vctx = (struct virgl_context *)ctx;
   struct virgl_resource *res = (struct virgl_resource *)buffer;
   struct virgl_so_target *t = CALLOC_STRUCT(virgl_so_target);
   uint32_t handle;

   if (!t)
      return NULL;
   handle = virgl_object_assign_handle();

   t->base.reference.count = 1;
   t->base.context = ctx;
   pipe_resource_reference(&t->base.buffer, buffer);
   t->base.buffer_offset = buffer_offset;
   t->base.buffer_size = buffer_size;
   t->handle = handle;
   res->clean = FALSE;
   virgl_encoder_create_so_target(vctx, handle, res, buffer_offset, buffer_size);
   return &t->base;
}

static void virgl_destroy_so_target(struct pipe_context *ctx,
                                   struct pipe_stream_output_target *target)
{
   struct virgl_context *vctx = (struct virgl_context *)ctx;
   struct virgl_so_target *t = (struct virgl_so_target *)target;

   pipe_resource_reference(&t->base.buffer, NULL);
   virgl_encode_delete_object(vctx, t->handle, VIRGL_OBJECT_STREAMOUT_TARGET);
   FREE(t);
}

static void virgl_set_so_targets(struct pipe_context *ctx,
                                unsigned num_targets,
                                struct pipe_stream_output_target **targets,
                                const unsigned *offset)
{
   struct virgl_context *vctx = (struct virgl_context *)ctx;
   virgl_encoder_set_so_targets(vctx, num_targets, targets, 0);//append_bitmask);
}

void virgl_init_so_functions(struct virgl_context *vctx)
{
   vctx->base.create_stream_output_target = virgl_create_so_target;
   vctx->base.stream_output_target_destroy = virgl_destroy_so_target;
   vctx->base.set_stream_output_targets = virgl_set_so_targets;
}
