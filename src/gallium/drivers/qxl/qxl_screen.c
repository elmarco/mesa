#include "qxl_screen.h"
#include "qxl_context.h"
#include "qxl_winsys.h"
#include "util/u_memory.h"
#include "util/u_inlines.h"

static int
qxl_get_param(struct pipe_screen *screen, enum pipe_cap cap)
{
   switch(cap) {
   case PIPE_CAP_NPOT_TEXTURES:
      return 1;

   default:
      debug_printf("got cap %d\n", cap);
      return 0;
   }
   return 0;
}

static void
qxl_destroy_screen(struct pipe_screen *screen)
{
   struct qxl_screen *qs = qxl_screen(screen);

   if (qs->qws)
      qs->qws->destroy(qs->qws);
   FREE(qs);
}

struct pipe_screen *
qxl_screen_create(struct qxl_winsys *qws)
{
   struct qxl_screen *qs = CALLOC_STRUCT(qxl_screen);

   if (!qs)
      return NULL;

   qs->qws = qws;
   qs->base.destroy = qxl_destroy_screen;

   qs->base.context_create = qxl_create_context;
   qs->base.get_param = qxl_get_param;
   qxl_screen_texture_functions_init(qs);
   return &qs->base;
}
