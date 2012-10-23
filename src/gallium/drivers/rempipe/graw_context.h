#ifndef GRAW_CONTEXT_H
#define GRAW_CONTEXT_H

#include "pipe/p_state.h"
#include "graw_protocol.h"
struct pipe_context *graw_context_create(struct pipe_screen *pscreen,
                                         void *priv);

struct pipe_resource *graw_resource_create(struct pipe_screen *pscreen,
                                           const struct pipe_resource *template);

void
graw_resource_destroy(struct pipe_screen *pscreen,
                      struct pipe_resource *pt);

void graw_flush_frontbuffer(struct pipe_screen *screen,
                            struct pipe_resource *res,
                            unsigned level, unsigned layer,
                            void *winsys_drawable_handle);

void graw_transfer_block(uint32_t res_handle, const struct pipe_box *box,
                         void *data, int ndw);
void graw_transfer_get_block(uint32_t res_handle, const struct pipe_box *box,
                             void *data, int ndw);
#endif
