#include "util/u_pointer.h"
#include "util/u_hash_table.h"

#include "graw_object.h"
static unsigned
hash_func(void *key)
{
   intptr_t ip = pointer_to_intptr(key);
   return (unsigned)(ip & 0xffffffff);
}

static int
compare(void *key1, void *key2)
{
   if (key1 < key2)
      return -1;
   if (key1 > key2)
      return 1;
   else
      return 0;
}

static struct util_hash_table *handle_hash;
static uint32_t next_handle;

void
graw_object_init_hash(void)
{
   if (!handle_hash)
      handle_hash = util_hash_table_create(hash_func, compare);
}

uint32_t
graw_object_create_handle(void *object)
{
   uint32_t h = next_handle++;
   util_hash_table_set(handle_hash, intptr_to_pointer(h), object);
   return h;
}

void
graw_object_destroy_handle(uint32_t handle)
{
   util_hash_table_remove(handle_hash, intptr_to_pointer(handle));
}

void *graw_object_lookup(uint32_t handle)
{
   return util_hash_table_get(handle_hash, intptr_to_pointer(handle));
}
