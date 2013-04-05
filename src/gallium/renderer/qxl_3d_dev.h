
#ifndef QXL_3D_DEV_H
#define QXL_3D_DEV_H

#include "ipc_ring.h"

enum qxl_3d_cmd_type {
	QXL_3D_CMD_NOP,
	QXL_3D_CMD_CREATE_CONTEXT,
	QXL_3D_CMD_CREATE_RESOURCE,
	QXL_3D_CMD_SUBMIT,
	QXL_3D_DESTROY_CONTEXT,
	QXL_3D_TRANSFER_GET,
	QXL_3D_TRANSFER_PUT,
	QXL_3D_FENCE,
};

struct drm_qxl_3d_box {
	uint32_t x, y, z;
	uint32_t w, h, d;
};

struct qxl_3d_transfer_put {
        uint64_t phy_addr;
	uint32_t res_handle;
	struct drm_qxl_3d_box box;
	struct drm_qxl_3d_box transfer_box;
	uint32_t level;
};

struct qxl_3d_transfer_get {
        uint64_t phy_addr;
	uint32_t res_handle;
	struct drm_qxl_3d_box box;
};

struct qxl_3d_flush_buffer {
	uint32_t res_handle;
};

struct qxl_3d_resource_create {
	uint32_t handle;
	uint32_t target;
	uint32_t format;
	uint32_t bind;
	uint32_t width;
	uint32_t height;
        uint32_t depth;
        uint32_t array_size;
        uint32_t last_level;
        uint32_t nr_samples;
        uint32_t pad;
};

struct qxl_3d_cmd_submit {
	uint64_t phy_addr;
	uint32_t size;
};

typedef struct QXL3DCommand {
   uint32_t type;
   uint32_t pad;
   union qxl_3d_cmds {
      struct qxl_3d_resource_create res_create;
      struct qxl_3d_transfer_put transfer_put;
      struct qxl_3d_transfer_get transfer_get;
      uint64_t fence_id;
      struct qxl_3d_cmd_submit cmd_submit;
   } u;
} QXL3DCommand;

#define QXL_3D_COMMAND_RING_SIZE 32

SPICE_RING_DECLARE(QXL3DCommandRing, QXL3DCommand, QXL_3D_COMMAND_RING_SIZE);

struct qxl_3d_ram {
	uint32_t version;
	uint32_t pad;
        uint64_t last_fence;
        QXL3DCommandRing cmd_3d_ring;
};

#endif
