-include $(TOPDIR)/rules.mk
-include $(INCLUDE_DIR)/package.mk
-include $(BUILD_DIR)/package.mk

TARGET_APP := ai_glass_ota

INCLUDES += -I $(CONFIG_PREFIX)/usr/include \
			-I $(STAGING_DIR)/usr/include \
			-I $(LINUX_USER_HEADERS)/include/bsp \
			-I ./

install:
	-@mkdir -p $(CONFIG_PREFIX)/usr/bin

LIBS += -L$(CONFIG_PREFIX)/usr/lib \
		-L$(CONFIG_PREFIX)/lib \
		-L$(STAGING_DIR)/usr/lib \
		-L$(STAGING_DIR)/lib

DEAMON_LIBS += -laglink -lwirelesscom -lwifimg-v2.0

DEAMON_SRCS += \
   main.c \
   ota.c \
   firmware.c

$(TARGET_APP): $(DEAMON_SRCS)
	$(CC) -o $@ $^ $(CFLAGS) $(INCLUDES) $(LDFLAGS) $(LIBS) $(DEAMON_LIBS)

all: install $(TARGET_APP)

clean:
	rm -rf $(TARGET_APP) *.o
