-include $(TOPDIR)/rules.mk
-include $(INCLUDE_DIR)/package.mk
-include $(BUILD_DIR)/package.mk

CUR_PATH := .
# STAGING_DIR is exported in rules.mk, so it can be used directly here.
# STAGING_DIR:=xxx/out/v316-perfnor/staging_dir/target
PREFIX_INCLUDE:=$(CONFIG_PREFIX)/usr/include
STAGING_INCLUDE:=$(STAGING_DIR)/usr/include
RTMEDIA_INCLUDE:=$(STAGING_DIR)/usr/include/rt_media
PDET_INCLUDE:=$(STAGING_DIR)/usr/include/pdet
EYESEE_MPP_INCLUDE:=$(STAGING_DIR)/usr/include/eyesee-mpp

PREFIX_USR_LIBDIR:=$(CONFIG_PREFIX)/usr/lib
PREFIX_LIBDIR:=$(CONFIG_PREFIX)/lib
STAGING_USR_LIBDIR:=$(STAGING_DIR)/usr/lib
STAGING_LIBDIR:=$(STAGING_DIR)/lib
RTMEDIA_LIBDIR:=$(STAGING_DIR)/usr/lib/rt_media
EYESEE_MPP_LIBDIR:=$(STAGING_DIR)/usr/lib/eyesee-mpp
PDET_LIBDIR:=$(STAGING_DIR)/usr/lib/pdet

-include $(EYESEE_MPP_INCLUDE)/middleware/config/mpp_config.mk

LOCAL_TARGET_BIN := ai_glass_livestream

SRCCS := main.c \
	livestream.c \
	rtsp_server.cpp \
	take_video_rtsp.c

DEAMON_LIBS += -laglink -lwirelesscom -lwifimg-v2.0

LOCAL_SHARED_LIBS :=
LOCAL_STATIC_LIBS :=
ifeq ($(MPPCFG_TOOLCHAIN_LIBC), glibc)
LOCAL_SHARED_LIBS += \
    libdl \
    librt \
    libpthread
endif
LOCAL_SHARED_LIBS += liblog

ifeq ($(MPPCFG_COMPILE_DYNAMIC_LIB), Y)
######################## dynamic lib mode ########################
# Public dynamic library
LOCAL_SHARED_LIBS += \
    libawion \
    librt_media \
    libTinyServer \
    libMotionDetect

ifneq ($(filter Y, $(RT_MEDIA_SUPPORT_VENC_PARAM_DEBUG) $(MPP_VENC_SUPPORT_VENC_PARAM_DEBUG)),)
LOCAL_SHARED_LIBS += \
    libexpat
endif

LOCAL_SHARED_LIBS += librt_aiservice_pdet libnn_p libnn_qt_dq_p libaw_person_det_nn_v821 libaw_person_det_v821 libaw_utilities_standalone_v821

else
######################## static lib mode ########################
# These only provide dynamic libraries
LOCAL_SHARED_LIBS +=
# Public static library
LOCAL_STATIC_LIBS += \
    libz \
    libawion \
    librt_media \
    libTinyServer \
    libMotionDetect

ifneq ($(filter Y, $(RT_MEDIA_SUPPORT_VENC_PARAM_DEBUG) $(MPP_VENC_SUPPORT_VENC_PARAM_DEBUG)),)
LOCAL_STATIC_LIBS += \
    libexpat
endif

LOCAL_STATIC_LIBS += librt_aiservice_pdet libnn_p libnn_qt_dq_p libaw_person_det_nn_v821 libaw_person_det_v821 libaw_utilities_standalone_v821

endif

INCLUDE_DIRS := \
    $(CUR_PATH) \
    $(PREFIX_INCLUDE) \
    $(STAGING_INCLUDE) \
    $(RTMEDIA_INCLUDE) \
    $(PDET_INCLUDE) \
    $(EYESEE_MPP_INCLUDE)/system/private/rtsp/IPCProgram/interface \
    $(EYESEE_MPP_INCLUDE)/system/public/include/utils \
    $(EYESEE_MPP_INCLUDE)/system/public/include \
    $(EYESEE_MPP_INCLUDE)/middleware/media/include/utils \
    $(EYESEE_MPP_INCLUDE)/middleware/include/utils \
    $(LINUX_USER_HEADERS)/include \
    $(LINUX_USER_HEADERS)/include/bsp \
    $(LINUX_USER_HEADERS)/include/bsp/linux

LIB_SEARCH_PATHS := \
	$(PREFIX_USR_LIBDIR) \
	$(PREFIX_LIBDIR) \
	$(STAGING_USR_LIBDIR) \
	$(STAGING_LIBDIR) \
    $(RTMEDIA_LIBDIR) \
    $(LIBCEDARC_LIBDIR) \
    $(EYESEE_MPP_LIBDIR) \
    $(PDET_LIBDIR)

CEDARX_EXT_CFLAGS += -DCONFIG_LOG_TOOL=1#LOG_TOOL_GLOG, defined in [aw_log.h]
#generate include directory flags for gcc.
inc_paths := $(foreach inc,$(filter-out -I%,$(INCLUDE_DIRS)),$(addprefix -I, $(inc))) $(filter -I%, $(INCLUDE_DIRS))
#Extra flags to give to the C compiler
LOCAL_CFLAGS := $(CFLAGS) $(CEDARX_EXT_CFLAGS) $(inc_paths) -fPIC -Wall -Wno-unused-but-set-variable -Wno-unused-variable -Wno-unused-label -Wno-unused-function
#Extra flags to give to the C++ compiler
LOCAL_CXXFLAGS := $(CXXFLAGS) $(CEDARX_EXT_CFLAGS) $(inc_paths) -fPIC -Wall -Wno-unused-but-set-variable -Wno-unused-variable -Wno-unused-label -Wno-unused-function
#Extra flags to give to the C preprocessor and programs that use it (the C and Fortran compilers).
LOCAL_CPPFLAGS := $(CPPFLAGS)
#target device arch: x86, arm
LOCAL_TARGET_ARCH := $(ARCH)
#Extra flags to give to compilers when they are supposed to invoke the linker,‘ld’.
LOCAL_LDFLAGS := $(LDFLAGS)

empty:=
space:= $(empty) $(empty)

LOCAL_BIN_LDFLAGS := $(LOCAL_LDFLAGS) \
    $(patsubst %,-L%,$(LIB_SEARCH_PATHS)) \
    -Wl,-rpath-link=$(subst $(space),:,$(strip $(LIB_SEARCH_PATHS))) \
    -Wl,-Bstatic \
    -Wl,--start-group $(foreach n, $(LOCAL_STATIC_LIBS), -l$(patsubst lib%,%,$(patsubst %.a,%,$(notdir $(n))))) -Wl,--end-group \
    -Wl,-Bdynamic \
    $(foreach y, $(LOCAL_SHARED_LIBS), -l$(patsubst lib%,%,$(patsubst %.so,%,$(notdir $(y)))))
#-Wl,--whole-archive \
#-Wl,--no-whole-archive \

#generate object files
OBJS := $(SRCCS:%=%.o) #OBJS=$(patsubst %,%.o,$(SRCCS))
DEPEND_LIBS := $(wildcard $(foreach p, $(patsubst %/,%,$(LIB_SEARCH_PATHS)), \
                            $(addprefix $(p)/, \
                              $(foreach y,$(LOCAL_SHARED_LIBS),$(patsubst %,%.so,$(patsubst %.so,%,$(notdir $(y))))) \
                              $(foreach n,$(LOCAL_STATIC_LIBS),$(patsubst %,%.a,$(patsubst %.a,%,$(notdir $(n))))) \
                            ) \
                          ) \
               )

install:
	-@mkdir -p $(CONFIG_PREFIX)/usr/bin

$(LOCAL_TARGET_BIN):$(OBJS) $(DEPEND_LIBS)
	$(CXX) $(OBJS) $(LOCAL_BIN_LDFLAGS) $(DEAMON_LIBS) -o $@

$(filter %.c.o, $(OBJS)): %.o: %
	$(CC) $(LOCAL_CFLAGS) $(LOCAL_CPPFLAGS) -MD -MP -MF $(@:%=%.d) -c -o $@ $<

$(filter %.cpp.o, $(OBJS)): %.o: %
	$(CXX) $(LOCAL_CFLAGS) $(LOCAL_CPPFLAGS) -MD -MP -MF $(@:%=%.d) -c -o $@ $<

.PHONY: all
all: install $(LOCAL_TARGET_BIN)
	@echo "build $(LOCAL_TARGET_BIN) done"

.PHONY: clean
clean:
	-rm -rf $(OBJS) $(OBJS:%=%.d) $(LOCAL_TARGET_BIN)

#add *.h prerequisites
-include $(OBJS:%=%.d)
