#include "state_tracker/drm_driver.h"
#include "target-helpers/inline_debug_helper.h"

#include "target-helpers/inline_wrapper_sw_helper.h"

#include "qxl/drm/qxl_drm_public.h"
#include "qxl/qxl_public.h"

static INLINE struct pipe_screen *
always_sw_screen_wrap(struct pipe_screen *screen)
{
#if defined(GALLIUM_SOFTPIPE) || defined(GALLIUM_LLVMPIPE) || defined(GALLIUM_REMPIPE)
   struct sw_winsys *sws;
   struct pipe_screen *sw_screen = NULL;
   const char *driver = "rempipe";

   sws = wrapper_sw_winsys_wrap_pipe_screen(screen);
   if (!sws)
      goto err;

   sw_screen = sw_screen_create_named(sws, driver);
   if (!sw_screen)
      goto err_winsys;

   return sw_screen;

err_winsys:
   return wrapper_sw_winsys_dewrap_pipe_screen(sws);
err:
#endif
   return NULL;
}

int global_hack_fd;

static struct pipe_screen *create_screen(int fd)
{
   struct pipe_screen *screen = NULL;
   struct qxl_winsys *qws;

   global_hack_fd = fd;
   qws = qxl_drm_winsys_create(fd);
   if (!qws)
      return NULL;

   screen = qxl_screen_create(qws);
   if (!screen)
      return NULL;
   screen = always_sw_screen_wrap(screen);
   if (!screen)
      return NULL;

   screen = debug_screen_wrap(screen);

   return screen;
}

DRM_DRIVER_DESCRIPTOR("qxl", "qxl", create_screen, NULL);
