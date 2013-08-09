#include "util/u_memory.h"
#include "util/u_inlines.h"
#include "graw_context.h"
#include "graw_encode.h"

struct graw_query {
   uint32_t handle;
   struct graw_resource *buf;

   unsigned type;
   unsigned result_size;
};

static void graw_render_condition(struct pipe_context *pipe,
                                  struct pipe_query *query,
                                  uint mode)
{

}

static struct pipe_query *graw_create_query(struct pipe_context *ctx,
                                            unsigned query_type)
{
   struct graw_context *grctx = (struct graw_context *)ctx;
   struct graw_query *query;
   uint32_t handle;

   query = CALLOC_STRUCT(graw_query);
   if (!query)
      return NULL;

   query->buf = (struct graw_resource *)pipe_buffer_create(ctx->screen, PIPE_BIND_CUSTOM,
                                                           PIPE_USAGE_STAGING, sizeof(union pipe_query_result) + sizeof(uint32_t));
   
   if (!query->buf) {
      FREE(query);
      return NULL;
   }
   
   handle = graw_object_assign_handle();
   query->type = query_type;
   query->handle = handle;

   graw_encoder_create_query(grctx, handle, query_type, query->buf, 0);

   return (struct pipe_query *)query;
}

static void graw_destroy_query(struct pipe_context *ctx,
                        struct pipe_query *q)
{
   struct graw_context *grctx = (struct graw_context *)ctx;
   struct graw_query *query = (struct graw_query *)q;

   graw_encode_delete_object(grctx, query->handle, GRAW_QUERY);

   pipe_resource_reference((struct pipe_resource **)&query->buf, NULL);
   FREE(query);
}

static void graw_begin_query(struct pipe_context *ctx,
                             struct pipe_query *q)
{
   struct graw_context *grctx = (struct graw_context *)ctx;
   struct graw_query *query = (struct graw_query *)q;

   query->buf->clean = FALSE;
   graw_encoder_begin_query(grctx, query->handle);
}

static void graw_end_query(struct pipe_context *ctx,
                           struct pipe_query *q)
{
   struct graw_context *grctx = (struct graw_context *)ctx;
   struct graw_query *query = (struct graw_query *)q;
   graw_encoder_end_query(grctx, query->handle);
}

static boolean graw_get_query_result(struct pipe_context *ctx,
                                     struct pipe_query *q,
                                     boolean wait,
                                     union pipe_query_result *result)
{
   struct graw_context *grctx = (struct graw_context *)ctx;
   struct graw_query *query = (struct graw_query *)q;
   void *map;
   struct pipe_transfer *transfer;

   graw_encoder_get_query_result(grctx, query->handle, wait);

   ctx->flush(ctx, NULL, 0);
   /* do we  have to flush? */
   /* now we can do the transfer to get the result back? */

   map = pipe_buffer_map_range(ctx, &query->buf->base, 4, sizeof(union pipe_query_result),
                               PIPE_TRANSFER_READ, &transfer);

   if (query->type == PIPE_QUERY_TIMESTAMP || query->type == PIPE_QUERY_TIME_ELAPSED)
      memcpy(result, map, sizeof(uint64_t));
   else
      memcpy(result, map, sizeof(uint32_t));
   pipe_buffer_unmap(ctx, transfer);
   return TRUE;
}

void graw_init_query_functions(struct graw_context *grctx)
{
   grctx->base.render_condition = graw_render_condition;
   grctx->base.create_query = graw_create_query;
   grctx->base.destroy_query = graw_destroy_query;
   grctx->base.begin_query = graw_begin_query;
   grctx->base.end_query = graw_end_query;
   grctx->base.get_query_result = graw_get_query_result;
}
