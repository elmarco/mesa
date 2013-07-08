#include "util/u_memory.h"
#include "graw_context.h"
#include "graw_encode.h"
struct graw_query_buffer {
   struct graw_resource *buf;
   unsigned results_end;
   struct graw_query_buffer *previous;
};

struct graw_query {
   uint32_t handle;
   struct graw_query_buffer buffer;
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

   handle = graw_object_assign_handle();
   query->type = query_type;
   query->handle = handle;

   graw_encoder_create_query(grctx, handle, query_type);

   return (struct pipe_query *)query;
}

static void graw_destroy_query(struct pipe_context *ctx,
                        struct pipe_query *q)
{
   struct graw_context *grctx = (struct graw_context *)ctx;
   struct graw_query *query = (struct graw_query *)q;

   graw_encode_delete_object(grctx, query->handle, GRAW_QUERY);

   FREE(query);
}

static void graw_begin_query(struct pipe_context *ctx,
                             struct pipe_query *q)
{
   struct graw_context *grctx = (struct graw_context *)ctx;
   struct graw_query *query = (struct graw_query *)q;
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
   return FALSE;
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
