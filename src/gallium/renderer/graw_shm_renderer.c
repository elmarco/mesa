#include <stdio.h>

#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include "qxl_3d_dev.h"
#include "pipe/p_state.h"
#include "graw_renderer.h"
#include "graw_renderer_glx.h"
#define MAPPING_SIZE (64*1024*1024)
int fd;
void *mapping;

struct qxl_3d_ram *ramp;

void graw_renderer_init(void);
int main(int argc, char **argv)
{
   fd = shm_open("dave", O_RDWR, 0600);
   if (fd == -1)
      return -1;

   mapping = mmap(0, MAPPING_SIZE, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);

   if (!mapping)
      return -1;

   ramp = mapping;

   SPICE_RING_INIT(&ramp->cmd_3d_ring);
   ramp->version = 1;
   {
      QXL3DCommand *cmd;
      int notify;
   restart:
      while (SPICE_RING_IS_EMPTY(&ramp->cmd_3d_ring)) {
         sleep(1);
      }
      
      cmd = SPICE_RING_CONS_ITEM(&ramp->cmd_3d_ring);
    
      fprintf(stderr, "got cmd %x\n", cmd->type);

      switch (cmd->type) {
      case QXL_3D_CMD_CREATE_RESOURCE:
         fprintf(stderr,"got res create %d %d\n", cmd->u.res_create.width,
                 cmd->u.res_create.height);
         graw_renderer_resource_create(cmd->u.res_create.handle,
                                       cmd->u.res_create.target,
                                       cmd->u.res_create.format,
                                       cmd->u.res_create.bind,
                                       cmd->u.res_create.width,
                                       cmd->u.res_create.height,
                                       cmd->u.res_create.depth);
         break;
      case QXL_3D_CMD_SUBMIT:
         fprintf(stderr,"cmd submit %lx %d\n", cmd->u.cmd_submit.phy_addr, cmd->u.cmd_submit.size);

         {
            uint32_t *cmdmap = mapping + cmd->u.cmd_submit.phy_addr;
            fprintf(stderr,"%08x %08x\n", cmdmap[0], cmdmap[1]);
            graw_decode_block(cmdmap, cmd->u.cmd_submit.size / 4);
         }

         break;
      case QXL_3D_TRANSFER_GET:
         fprintf(stderr,"got transfer get %d\n", cmd->u.transfer_get.res_handle);
	 graw_renderer_transfer_send(cmd->u.transfer_get.res_handle,
				     (struct pipe_box *)&cmd->u.transfer_get.box,
				     mapping + cmd->u.transfer_get.phy_addr);
	 break;
      case QXL_3D_TRANSFER_PUT:
         fprintf(stderr,"got transfer putt %d\n", cmd->u.transfer_put.res_handle);
	 graw_renderer_transfer_write(cmd->u.transfer_put.res_handle,
				      cmd->u.transfer_put.level,
				      (struct pipe_box *)&cmd->u.transfer_put.transfer_box,
				      (struct pipe_box *)&cmd->u.transfer_put.box,
				      mapping + cmd->u.transfer_put.phy_addr);
	 break;
      case QXL_3D_FENCE:
         ramp->last_fence = cmd->u.fence_id;
         break;
      case 0xdeadbeef:
         ramp->last_fence = 0;
         graw_renderer_init_glx();
         graw_renderer_init();
         break;
      }
      SPICE_RING_POP(&ramp->cmd_3d_ring, notify);
      goto restart;
   }
   
}

void graw_transfer_write_return(void *data, uint32_t ndw, void *myptr)
{
  memcpy(myptr, data, ndw);
}
