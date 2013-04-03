
#include "qxl_context.h"
#include "qxl_screen.h"
#include "util/u_memory.h"
static void qxl_destroy(struct pipe_context *pipe)
{
  struct qxl_context *qxl = qxl_context(pipe);

  FREE(qxl);
}

static void qxl_flush(struct pipe_context *pipe,
			struct pipe_fence_handle **fence,
			unsigned flags)
{

}


struct pipe_context *
qxl_create_context(struct pipe_screen *screen, void *priv)
{
  struct qxl_context *qxl;

  qxl = CALLOC_STRUCT(qxl_context);
  if (qxl == NULL)
    return NULL;

  qxl->qws = qxl_screen(screen)->qws;
  qxl->base.screen = screen;
  qxl->base.priv = priv;

  qxl->base.destroy = qxl_destroy;
  qxl->base.flush = qxl_flush;
  qxl->base.transfer_map = u_transfer_map_vtbl;
  qxl->base.transfer_flush_region = u_transfer_flush_region_vtbl;
  qxl->base.transfer_unmap = u_transfer_unmap_vtbl;
  qxl->base.transfer_inline_write = u_transfer_inline_write_vtbl;

  util_slab_create(&qxl->texture_transfer_pool, sizeof(struct qxl_transfer),
		   16, UTIL_SLAB_SINGLETHREADED);
  return &qxl->base;

}
