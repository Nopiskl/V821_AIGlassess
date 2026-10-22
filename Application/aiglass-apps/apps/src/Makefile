-include $(TOPDIR)/rules.mk
-include $(INCLUDE_DIR)/package.mk
-include $(BUILD_DIR)/package.mk

target = libaglink.so

INCLUDES        += -I ./aglink/include
INCLUDES        += -I ./aglink
INCLUDES        += -I $(CONFIG_PREFIX)/usr/include
INCLUDES        += -I $(LINUX_USER_HEADERS)/include/bsp

SRCS +=	log/aglink_log.c \
		misc/sys_info.c \
		net/http.c net/network.c

#AGLINK_SRCS :=
#
#AGLINK_SRCS += aglink/aglink.c \
#		aglink/ag_wrap.c \

SRCS += $(AGLINK_SRCS)

LIBS +=
LOCAL_STATIC_LIBS :=
LOCAL_SHARED_LIBS :=

LOCAL_STATIC_LIBS += libaglink
#LOCAL_STATIC_LIBS += libaw_license_auth
#LOCAL_SHARED_LIBS += libcrypto libstdc++

LOCAL_CFLAGS = #-DCONFIG_AILM_ENABLE

AGLINK_PATH := ./aglink
PEER_LIBDIR:=$(STAGING_DIR)/usr/lib
ROOT_LIBDIR:=$(STAGING_DIR)/root-v821-aiglass/lib
LIB_SEARCH_PATHS := \
    $(AGLINK_PATH) \
	$(PEER_LIBDIR) \
	$(PDET_LIBDIR) \
	$(ROOT_LIBDIR)

empty:=
space:= $(empty) $(empty)
LOCAL_LDFLAGS := $(LDFLAGS)
LOCAL_BIN_LDFLAGS := $(LOCAL_LDFLAGS) \
    $(patsubst %,-L%,$(LIB_SEARCH_PATHS)) \
    -Wl,-rpath-link=$(subst $(space),:,$(strip $(LIB_SEARCH_PATHS))) \
    -Wl,-Bstatic \
    -Wl,--start-group $(foreach n, $(LOCAL_STATIC_LIBS), -l$(patsubst lib%,%,$(patsubst %.a,%,$(notdir $(n))))) -Wl,--end-group \
    -Wl,-Bdynamic \
    $(foreach y, $(LOCAL_SHARED_LIBS), -l$(patsubst lib%,%,$(patsubst %.so,%,$(notdir $(y)))))

DEPEND_LIBS := $(wildcard $(foreach p, $(patsubst %/,%,$(LIB_SEARCH_PATHS)), \
                            $(addprefix $(p)/, \
                              $(foreach y,$(LOCAL_SHARED_LIBS),$(patsubst %,%.so,$(patsubst %.so,%,$(notdir $(y))))) \
                              $(foreach n,$(LOCAL_STATIC_LIBS),$(patsubst %,%.a,$(patsubst %.a,%,$(notdir $(n))))) \
                            ) \
                          ) \
               )

#LOCAL_CFLAGS := $(CEDARX_EXT_CFLAGS) -fPIC -Wall -Wno-unused-but-set-variable -Wno-unused-variable -Wno-unused-label -Wno-unused-function
OBJS := $(SRCS:%=%.o) #OBJS=$(patsubst %,%.o,$(SRCCS))

install:
	-@mkdir -p $(CONFIG_PREFIX)/usr/lib
	-@mkdir -p $(CONFIG_PREFIX)/usr/include
	-@mkdir -p $(CONFIG_PREFIX)/usr/include/aglink
	@cp ./aglink/*.h $(CONFIG_PREFIX)/usr/include/aglink
	@cp ./misc/*.h $(CONFIG_PREFIX)/usr/include
	@cp ./log/*.h $(CONFIG_PREFIX)/usr/include
	@cp ./net/*.h $(CONFIG_PREFIX)/usr/include

$(target): $(OBJS) $(DEPEND_LIBS)
	$(CC) -shared $(OBJS) $(LOCAL_BIN_LDFLAGS) -o $@ $(LIBS)
	@cp $@ $(CONFIG_PREFIX)/usr/lib

$(filter %.c.o, $(OBJS)): %.o: %
	$(CC) -fPIC $(LOCAL_CFLAGS) $(CFLAGS) $(INCLUDES) -MD -MP -MF $(@:%=%.d) -c -o $@ $<

all: install $(target)

clean:
	rm -rf $(target) *.o *.a
