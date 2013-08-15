#ifndef GRAW_OBJECT_H
#define GRAW_OBJECT_H

#include "graw_protocol.h"
void graw_object_init_resource_hash(void);
void graw_object_fini_resource_hash(void);
struct grend_context;
struct util_hash_table *graw_init_ctx_hash(void);
void graw_fini_ctx_hash(struct util_hash_table *ctx_hash, struct grend_context *ctx);
uint32_t graw_object_create(struct util_hash_table *handle_hash, void *object, uint32_t length, enum graw_object_type obj);
void graw_object_destroy(struct grend_context *ctx, struct util_hash_table *handle_hash, uint32_t handle, enum graw_object_type obj);
void *graw_object_lookup(struct util_hash_table *handle_hash, uint32_t handle, enum graw_object_type obj);

uint32_t graw_object_insert(struct util_hash_table *handle_hash, void *data, uint32_t length, uint32_t handle, enum graw_object_type type);

uint32_t graw_object_assign_handle(void);

/* resources are global */
void *graw_insert_resource(void *data, uint32_t length, uint32_t handle);
void graw_destroy_resource(uint32_t handle);
void *graw_lookup_resource(uint32_t handle, uint32_t ctx_id);

struct grend_query;
struct grend_shader_state;
struct grend_surface;
void grend_destroy_shader(struct grend_shader_state *state);
void grend_destroy_query(struct grend_query *query);
void grend_destroy_surface(struct grend_context *ctx, struct grend_surface *surf);
#endif
