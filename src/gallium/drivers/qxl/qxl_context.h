#ifndef QXL_CONTEXT_H
#define QXL_CONTEXT_H

#include "pipe/p_context.h"
#include "util/u_slab.h"

struct qxl_context {
   struct pipe_context base;
   struct qxl_winsys *qws;

   struct util_slab_mempool texture_transfer_pool;
};

struct pipe_context *qxl_create_context(struct pipe_screen *screen, void *priv);

static INLINE struct qxl_context *
qxl_context(struct pipe_context *pipe)
{
   return (struct qxl_context *)pipe;
}

#endif
