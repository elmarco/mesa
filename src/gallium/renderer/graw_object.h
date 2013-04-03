#ifndef GRAW_OBJECT_H
#define GRAW_OBJECT_H

#include "graw_protocol.h"
void graw_object_init_hash(void);
void graw_object_fini_hash(void);
uint32_t graw_object_create(void *object, uint32_t length, enum graw_object_type obj);
void graw_object_destroy(uint32_t handle, enum graw_object_type obj);
void *graw_object_lookup(uint32_t handle, enum graw_object_type obj);

uint32_t graw_object_insert(void *data, uint32_t length, uint32_t handle, enum graw_object_type type);

uint32_t graw_object_assign_handle(void);
#endif
