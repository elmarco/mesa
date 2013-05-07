#ifndef REM_PIPE_H
#define REM_PIPE_H

#include "state_tracker/sw_winsys.h"
#include "../qxl/qxl_winsys.h"
#include "pipe/p_screen.h"
struct rempipe_screen {
   struct pipe_screen base;
   struct sw_winsys *winsys;
   struct qxl_winsys *qws;
};


static INLINE struct rempipe_screen *
rempipe_screen( struct pipe_screen *pipe )
{
   return (struct rempipe_screen *)pipe;
}
#endif
