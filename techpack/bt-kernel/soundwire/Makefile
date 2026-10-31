ccflags-y += -I$(BT_ROOT)/include
ccflags-y += \
	-I$(BT_ROOT)/../audio-kernel/include\
	-I$(BT_ROOT)/btfmcodec/include
bt_fm_swr-objs := btfm_swr.o btfm_swr_hw_interface.o btfm_swr_slave.o
obj-$(CONFIG_BTFM_SWR) += bt_fm_swr.o
