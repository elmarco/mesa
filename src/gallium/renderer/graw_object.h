#ifndef GRAW_OBJECT_H
#define GRAW_OBJECT_H

void graw_object_init_hash(void);
uint32_t graw_object_create_handle(void *object);
void graw_object_destroy_handle(uint32_t handle);
void *graw_object_lookup(uint32_t handle);

#endif
