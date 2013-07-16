#ifndef VIRGL_HW_H
#define VIRGL_HW_H

typedef uint64_t VIRGLPHYSICAL;
/* specification for the HW command processor */

struct virgl_box {
	uint32_t x, y, z;
	uint32_t w, h, d;
};

enum virgl_cmd_type {
	VIRGL_CMD_NOP,
	VIRGL_CMD_CREATE_CONTEXT,
	VIRGL_CMD_CREATE_RESOURCE,
	VIRGL_CMD_SUBMIT,
	VIRGL_CMD_DESTROY_CONTEXT,
	VIRGL_CMD_TRANSFER_GET,
	VIRGL_CMD_TRANSFER_PUT,
	VIRGL_CMD_SET_SCANOUT,
	VIRGL_CMD_FLUSH_BUFFER,
	VIRGL_CMD_RESOURCE_UNREF,
	VIRGL_CMD_ATTACH_RES_CTX,
	VIRGL_CMD_DETACH_RES_CTX,
};

/* put a box of data from a BO into a tex/buffer resource */
struct virgl_transfer_put {
	VIRGLPHYSICAL data;
	uint32_t res_handle;
	struct virgl_box dst_box; /* dst box */
	uint32_t dst_level;
	uint32_t src_stride;
        uint32_t ctx_id;
};

struct virgl_transfer_get {
	VIRGLPHYSICAL data;
	uint32_t res_handle;
	struct virgl_box box;
	int level;
	uint32_t dx, dy;
        uint32_t ctx_id;
};

struct virgl_flush_buffer {
	uint32_t res_handle;
        uint32_t ctx_id;
	struct virgl_box box;
};

struct virgl_set_scanout {
	uint32_t res_handle;
        uint32_t ctx_id;
	struct virgl_box box;
};

struct virgl_resource_create {
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

struct virgl_resource_unref {
	uint32_t res_handle;
};

struct virgl_cmd_submit {
	uint64_t phy_addr;
	uint32_t size;
        uint32_t ctx_id;
};

struct virgl_cmd_context {
        uint32_t handle;
        uint32_t pad;
};

struct virgl_cmd_resource_context {
	uint32_t resource;
	uint32_t ctx_id;
};

#define VIRGL_COMMAND_EMIT_FENCE (1 << 0)

struct virgl_command {
	uint32_t type;
	uint32_t flags;
	uint64_t fence_id;
	union virgl_cmds {
		struct virgl_cmd_context ctx;
		struct virgl_resource_create res_create;
		struct virgl_transfer_put transfer_put;
		struct virgl_transfer_get transfer_get;
		struct virgl_cmd_submit cmd_submit;
		struct virgl_set_scanout set_scanout;
		struct virgl_flush_buffer flush_buffer;
		struct virgl_resource_unref res_unref;
		struct virgl_cmd_resource_context res_ctx;
	} u;
};

#endif
