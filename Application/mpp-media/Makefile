SHELL := /bin/sh
.DEFAULT_GOAL := help

# Set these to a complete V821 sun300iw1 BSP. No V851 headers or AGLink libs.
V821_STAGING ?=
V821_KERNEL_HEADERS ?=
MPP_ROOT := $(V821_STAGING)/usr/include/eyesee-mpp
OUTPUT := output
MPP_CFLAGS ?=
CPPFLAGS += -D_FILE_OFFSET_BITS=64
CFLAGS ?= -O2
INCLUDES := $(V821_STAGING)/usr/include \
    $(V821_STAGING)/usr/include/libcedarc/include \
    $(V821_STAGING)/usr/include/libcedarc/base/include \
    $(V821_KERNEL_HEADERS)/include $(V821_KERNEL_HEADERS)/include/bsp \
    $(MPP_ROOT)/system/public/include $(MPP_ROOT)/system/public/include/utils \
    $(MPP_ROOT)/system/public/libion/include \
    $(MPP_ROOT)/middleware/config $(MPP_ROOT)/middleware/include \
    $(MPP_ROOT)/middleware/include/utils $(MPP_ROOT)/middleware/include/media \
    $(MPP_ROOT)/middleware/include/media/utils $(MPP_ROOT)/middleware/media/include \
    $(MPP_ROOT)/middleware/media/include/component \
    $(MPP_ROOT)/middleware/media/LIBRARY/libcedarc/include \
    $(MPP_ROOT)/middleware/media/LIBRARY/libisp/include \
    $(MPP_ROOT)/middleware/media/LIBRARY/libisp/include/device \
    $(MPP_ROOT)/middleware/media/LIBRARY/libisp/include/V4l2Camera \
    $(MPP_ROOT)/middleware/media/LIBRARY/AudioLib/midware/encoding/include \
    $(MPP_ROOT)/middleware/media/LIBRARY/include_FsWriter \
    $(MPP_ROOT)/middleware/media/LIBRARY/include_muxer

.PHONY: help all check clean
help:
	@echo 'V821 local MPP library (no UI executable, AGLink, or RT Media dependency)'
	@echo 'make all CC=<V821-gcc> AR=<V821-ar> V821_STAGING=<target> V821_KERNEL_HEADERS=<user_headers>'
	@echo 'Use MPP_CFLAGS for the exact flags of the matching V821 CDR/IPC target.'
	@echo 'The host must link this archive followed by its V821 MPP/AIO/AENC/MUX dependencies.'

check:
	@test -n "$(V821_STAGING)" -a -f "$(MPP_ROOT)/middleware/include/media/mpi_vi.h" || \
	    { echo 'Missing complete V821 MPP development headers' >&2; exit 1; }
	@test -f "$(MPP_ROOT)/middleware/config/mpp_config.mk" || \
	    { echo 'Missing target mpp_config.mk' >&2; exit 1; }
	@test -n "$(V821_KERNEL_HEADERS)" -a -d "$(V821_KERNEL_HEADERS)/include/bsp" || \
	    { echo 'Missing V821 BSP user headers' >&2; exit 1; }

all: check $(OUTPUT)/libv821_mpp_media.a $(OUTPUT)/include/vm_media.h

$(OUTPUT)/vm_media.o: vm_media.c vm_media.h ../aiglass-apps/apps/src/misc/media_owner.h | check
	mkdir -p $(OUTPUT)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(MPP_CFLAGS) -std=gnu11 -Wall -Wextra -pthread \
	    $(addprefix -I,$(INCLUDES)) -MMD -MP -c $< -o $@

$(OUTPUT)/libv821_mpp_media.a: $(OUTPUT)/vm_media.o
	$(AR) rcs $@ $^

$(OUTPUT)/include/vm_media.h: vm_media.h
	mkdir -p $(OUTPUT)/include
	cp $< $@

clean:
	rm -f $(OUTPUT)/vm_media.o $(OUTPUT)/vm_media.d $(OUTPUT)/libv821_mpp_media.a $(OUTPUT)/include/vm_media.h

-include $(OUTPUT)/vm_media.d
