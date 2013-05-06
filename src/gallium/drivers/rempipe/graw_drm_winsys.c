#include <stdint.h>
#include <unistd.h>
#include <stdio.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/mman.h>
#include <sys/un.h>
#include <signal.h>
#include <termios.h>

#include "pipebuffer/pb_buffer.h"
#include "pipe/p_state.h"
#include "graw_protocol.h"
#include "graw_pipe_winsys.h"
#include "graw_context.h"
#include "rempipe.h"
#include "xf86drm.h"
#include "qxl_drm.h"

static uint32_t next_handle;
uint32_t graw_object_assign_handle(void)
{
   return next_handle++;
}

void grend_flush_frontbuffer(uint32_t res_handle)
{
}
void graw_renderer_init()
{
   return;
}
