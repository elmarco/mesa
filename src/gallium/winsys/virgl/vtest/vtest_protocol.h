
#ifndef VTEST_PROTOCOL
#define VTEST_PROTOCOL

#define VTEST_DEFAULT_SOCKET_NAME "/tmp/.virgl_test"

/* vtest cmds */
#define VCMD_GET_CAPS 1
#define VCMD_CONTEXT_CREATE 2
#define VCMD_CONTEXT_DESTROY 3
#define VCMD_RESOURCE_CREATE 4
#define VCMD_RESOURCE_UNREF 5

#define VCMD_TRANSFER_GET 6
#define VCMD_TRANSFER_PUT 7

#define VCMD_SUBMIT_CMD 8

/* 32-bit length field */
/* 32-bit cmd field */
/* data */
#endif
