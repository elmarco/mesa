#include "util/u_memory.h"
#include "util/u_inlines.h"
#include "virgl_resource.h"
#include "virgl_context.h"
#include "virgl_encode.h"

struct virgl_query {
   uint32_t handle;
   struct virgl_resource *buf;

   unsigned type;
   unsigned result_size;
   unsigned result_gotten_sent;
};

static void virgl_render_condition(struct pipe_context *ctx,
                                  struct pipe_query *q,
                                  boolean condition,
                                  uint mode)
{
   struct virgl_context *vctx = (struct virgl_context *)ctx;
   struct virgl_query *query = (struct virgl_query *)q;
   uint32_t handle = 0;
   if (q)
      handle = query->handle;
   virgl_encoder_render_condition(vctx, handle, condition, mode);
}

static struct pipe_query *virgl_create_query(struct pipe_context *ctx,
                                            unsigned query_type, unsigned index)
{
   struct virgl_context *vctx = (struct virgl_context *)ctx;
   struct virgl_query *query;
   uint32_t handle;

   query = CALLOC_STRUCT(virgl_query);
   if (!query)
      return NULL;

   query->buf = (struct virgl_resource *)pipe_buffer_create(ctx->screen, PIPE_BIND_CUSTOM,
                                                           PIPE_USAGE_STAGING, sizeof(struct virgl_host_query_state));
   if (!query->buf) {
      FREE(query);
      return NULL;
   }

   handle = virgl_object_assign_handle();
   query->type = query_type;
   query->handle = handle;
   query->buf->clean = FALSE;
   virgl_encoder_create_query(vctx, handle, query_type, query->buf, 0);

   return (struct pipe_query *)query;
}

static void virgl_destroy_query(struct pipe_context *ctx,
                        struct pipe_query *q)
{
   struct virgl_context *vctx = (struct virgl_context *)ctx;
   struct virgl_query *query = (struct virgl_query *)q;

   virgl_encode_delete_object(vctx, query->handle, VIRGL_OBJECT_QUERY);

   pipe_resource_reference((struct pipe_resource **)&query->buf, NULL);
   FREE(query);
}

static void virgl_begin_query(struct pipe_context *ctx,
                             struct pipe_query *q)
{
   struct virgl_context *vctx = (struct virgl_context *)ctx;
   struct virgl_query *query = (struct virgl_query *)q;

   query->buf->clean = FALSE;
   virgl_encoder_begin_query(vctx, query->handle);
}

static void virgl_end_query(struct pipe_context *ctx,
                           struct pipe_query *q)
{
   struct virgl_context *vctx = (struct virgl_context *)ctx;
   struct virgl_query *query = (struct virgl_query *)q;
   struct pipe_box box;

   uint32_t qs = VIRGL_QUERY_STATE_WAIT_HOST;
   u_box_1d(0, 4, &box);
   virgl_transfer_inline_write(ctx, &query->buf->u.b, 0, PIPE_TRANSFER_WRITE,
                              &box, &qs, 0, 0);


   virgl_encoder_end_query(vctx, query->handle);
}

static boolean virgl_get_query_result(struct pipe_context *ctx,
                                     struct pipe_query *q,
                                     boolean wait,
                                     union pipe_query_result *result)
{
   struct virgl_context *vctx = (struct virgl_context *)ctx;
   struct virgl_query *query = (struct virgl_query *)q;
   struct pipe_transfer *transfer;
   struct virgl_host_query_state *host_state;

   /* ask host for query result */
   if (!query->result_gotten_sent) {
      query->result_gotten_sent = 1;
      virgl_encoder_get_query_result(vctx, query->handle, 0);
      ctx->flush(ctx, NULL, 0);
   }

   /* do we  have to flush? */
   /* now we can do the transfer to get the result back? */
 remap:
   host_state = pipe_buffer_map(ctx, &query->buf->u.b,
                               PIPE_TRANSFER_READ, &transfer);

   if (host_state->query_state != VIRGL_QUERY_STATE_DONE) {
      pipe_buffer_unmap(ctx, transfer);
      if (wait)
         goto remap;
      else
         return FALSE;
   }

   if (query->type == PIPE_QUERY_TIMESTAMP || query->type == PIPE_QUERY_TIME_ELAPSED)
      result->u64 = host_state->result;
   else
      result->u64 = (uint32_t)host_state->result;

   pipe_buffer_unmap(ctx, transfer);
   query->result_gotten_sent = 0;
   return TRUE;
}

void virgl_init_query_functions(struct virgl_context *vctx)
{
   vctx->base.render_condition = virgl_render_condition;
   vctx->base.create_query = virgl_create_query;
   vctx->base.destroy_query = virgl_destroy_query;
   vctx->base.begin_query = virgl_begin_query;
   vctx->base.end_query = virgl_end_query;
   vctx->base.get_query_result = virgl_get_query_result;
}
