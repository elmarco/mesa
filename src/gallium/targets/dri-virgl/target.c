#include "state_tracker/drm_driver.h"
#include "target-helpers/inline_debug_helper.h"

#include "virgl/drm/virgl_drm_public.h"
#include "virgl/virgl_public.h"

int global_hack_fd;

static struct pipe_screen *create_screen(int fd)
{
   struct pipe_screen *screen = NULL;
   struct virgl_winsys *vws;

   global_hack_fd = fd;
   vws = virgl_drm_winsys_create(fd);
   if (!vws)
      return NULL;

   screen = virgl_create_screen(vws);
   if (!screen)
      return NULL;

   screen = debug_screen_wrap(screen);

   return screen;
}

DRM_DRIVER_DESCRIPTOR("virgl", "virgl", create_screen, NULL);
