-include $(TOPDIR)/rules.mk
-include $(INCLUDE_DIR)/package.mk
-include $(BUILD_DIR)/package.mk

TARGET_APP := ai_glass_normal

INCLUDES += -I $(CONFIG_PREFIX)/usr/include \
			-I $(STAGING_DIR)/usr/include \
			-I $(LINUX_USER_HEADERS)/include/bsp \
			-I ./

install:
	-@mkdir -p $(CONFIG_PREFIX)/usr/bin

DEAMON_LIBS += -laglink -lwirelesscom

DEAMON_SRCS += \
   main.c \
   normal.c

$(TARGET_APP): $(DEAMON_SRCS) ../../src/misc/media_owner.h
	$(CC) -o $@ $(DEAMON_SRCS) $(CFLAGS) $(INCLUDES) $(LDFLAGS) -L$(CONFIG_PREFIX)/usr/lib -L$(CONFIG_PREFIX)/lib -L$(STAGING_DIR)/usr/lib $(DEAMON_LIBS)

all: install $(TARGET_APP)

clean:
	rm -rf $(TARGET_APP) *.o
