
#ifndef VREND_IOV_H
#define VREND_IOV_H

#include <sys/uio.h>
/* stolen from qemu for now until later integration */
typedef void (*IOCallback)(void *cookie, unsigned int doff, void *src, int len);

size_t vrend_iov_size(const struct iovec *iov, const unsigned int iov_cnt);
size_t vrend_iov_from_buf(const struct iovec *iov, unsigned int iov_cnt,
                    size_t offset, const void *buf, size_t bytes);
size_t vrend_iov_to_buf(const struct iovec *iov, const unsigned int iov_cnt,
                  size_t offset, void *buf, size_t bytes);

size_t vrend_iov_to_buf_cb(const struct iovec *iov, const unsigned int iov_cnt,
                          size_t offset, size_t bytes, IOCallback iocb, void *cookie);

#endif
