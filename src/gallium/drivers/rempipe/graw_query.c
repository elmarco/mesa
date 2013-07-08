
#include "graw_context.h"

static void graw_render_condition(struct pipe_context *pipe,
                                  struct pipe_query *query,
                                  uint mode)
{

}

static struct pipe_query *graw_create_query(struct pipe_context *pipe,
                                     unsigned query_type)
{
   return NULL;
}

static void graw_destroy_query(struct pipe_context *ctx,
                        struct pipe_query *q)
{

}

static void graw_begin_query(struct pipe_context *ctx,
                             struct pipe_query *q)
{

}

static void graw_end_query(struct pipe_context *ctx,
                           struct pipe_query *q)
{

}

static boolean graw_get_query_result(struct pipe_context *ctx,
                                     struct pipe_query *q,
                                     boolean wait,
                                     union pipe_query_result *result)
{
   return false;
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
