# Copyright © 2012 Intel Corporation
#
# Permission is hereby granted, free of charge, to any person obtaining a
# copy of this software and associated documentation files (the "Software"),
# to deal in the Software without restriction, including without limitation
# the rights to use, copy, modify, merge, publish, distribute, sublicense,
# and/or sell copies of the Software, and to permit persons to whom the
# Software is furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice (including the next
# paragraph) shall be included in all copies or substantial portions of the
# Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
# EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
# MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
# NONINFRINGEMENT.  IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
# HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
# WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
# DEALINGS IN THE SOFTWARE.

include $(top_srcdir)/src/gallium/Automake.inc

AM_CFLAGS = \
	$(GALLIUM_DRI_CFLAGS)
AM_CPPFLAGS = \
	-DGALLIUM_RBUG \
	-DGALLIUM_TRACE \
	-DGALLIUM_NOOP \
        -DGALLIUM_SOFTPIPE

dridir = $(DRI_DRIVER_INSTALL_DIR)
dri_LTLIBRARIES = virtio_gpu_dri.la

virtio_gpu_dri_la_SOURCES = \
	target.c

virtio_gpu_dri_la_LDFLAGS = $(GALLIUM_DRI_LINKER_FLAGS)

virtio_gpu_dri_la_LIBADD = \
	$(top_builddir)/src/mesa/drivers/dri/common/libdricommon.la \
	$(top_builddir)/src/gallium/state_trackers/dri/drm/libdridrm.la \
	$(top_builddir)/src/gallium/winsys/virgl/drm/libvirgldrm.la \
	$(top_builddir)/src/gallium/drivers/trace/libtrace.la \
	$(top_builddir)/src/gallium/drivers/rbug/librbug.la \
	$(top_builddir)/src/gallium/drivers/noop/libnoop.la \
	$(top_builddir)/src/gallium/drivers/virgl/libvirgl.la \
	$(GALLIUM_DRI_LIB_DEPS) \
	$(LIBDRM_LIBS)

nodist_EXTRA_virtio_gpu_dri_la_SOURCES = dummy.cpp

if HAVE_MESA_LLVM
virtio_gpu_dri_la_LDFLAGS += $(LLVM_LDFLAGS)
virtio_gpu_dri_la_LIBADD += $(LLVM_LIBS)
endif

# Provide compatibility with scripts for the old Mesa build system for
# a while by putting a link to the driver into /lib of the build tree.
all-local: virtio-gpu_dri.la
	$(MKDIR_P) $(top_builddir)/$(LIB_DIR)/gallium
	ln -f .libs/virtio_gpu_dri.so $(top_builddir)/$(LIB_DIR)/gallium/virtio_gpu_dri.so
