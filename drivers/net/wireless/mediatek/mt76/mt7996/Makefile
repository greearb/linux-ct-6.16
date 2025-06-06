# SPDX-License-Identifier: ISC

obj-$(CONFIG_MT7996E) += mt7996e.o

mt7996e-y := pci.o init.o dma.o eeprom.o main.o mcu.o mac.o \
	     debugfs.o mmio.o mtk_debugfs_i.o vendor.o mtk_mcu.o \
	     mtk_debugfs.o

mt7996e-$(CONFIG_DEV_COREDUMP) += coredump.o
mt7996e-$(CONFIG_NL80211_TESTMODE) += testmode.o

ccflags-y += -DCONFIG_MTK_DEBUG -DCONFIG_MTK_VENDOR -DCONFIG_MT76_LEDS
