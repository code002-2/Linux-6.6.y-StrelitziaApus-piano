ifeq (true, $(strip $(call is-factory-build)))
TOUCH_FACTORY_SELECT := CONFIG_TOUCH_FACTORY_SELECT=1
$(info CONFIG_TOUCH_FACTORY_SELECT=1)
endif

$(info get-miodm-device-name = $(call get-miodm-device-name))
ifeq ($(call get-miodm-device-name), $(filter $(call get-miodm-device-name), piano))
TOUCH_KBUILD_OPTIONS += CONFIG_TOUCH_THP_SUPPORT=1
TOUCH_KBUILD_OPTIONS += CONFIG_TOUCH_TDDI_SUPPORT=1
TOUCH_KBUILD_OPTIONS += CONFIG_TOUCH_STYLUS_SUPPORT=1
TOUCH_KBUILD_OPTIONS += CONFIG_DISABLE_POWER_STATUS=1
endif

# This makefile is only for DLKM
ifneq ($(findstring vendor,$(LOCAL_PATH)),)

ifneq ($(findstring opensource,$(LOCAL_PATH)),)
	TOUCH_BLD_DIR := $(TOP)/vendor/xiaomi/proprietary/touch/touchfeature_v2/touch_driver/p82
endif # opensource

# Build xiaomi_touch.ko
###########################################################
# This is set once per LOCAL_PATH, not per (kernel) module
KBUILD_OPTIONS := TOUCH_ROOT=$(TOUCH_BLD_DIR)
KBUILD_OPTIONS += BOARD_PLATFORM=$(TARGET_BOARD_PLATFORM)
KBUILD_OPTIONS += $(TOUCH_FACTORY_SELECT)
KBUILD_OPTIONS += $(TOUCH_KBUILD_OPTIONS)
KBUILD_OPTIONS += KBUILD_EXTRA_SYMBOLS=$(PWD)/$(call intermediates-dir-for,DLKM,msm_drm-module-symvers)/Module.symvers
###########################################################

DLKM_DIR   := device/qcom/common/dlkm

LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)
# For incremental compilation
LOCAL_SRC_FILES           := $(wildcard $(LOCAL_PATH)/**/*) $(wildcard $(LOCAL_PATH)/*)
LOCAL_MODULE              := xiaomi_touch.ko
LOCAL_MODULE_KBUILD_NAME  := ../xiaomi/xiaomi_touch.ko
LOCAL_MODULE_TAGS         := optional
LOCAL_MODULE_DEBUG_ENABLE := true
LOCAL_MODULE_OWNER        := xiaomi
LOCAL_MODULE_PATH         := $(KERNEL_MODULES_OUT)
# Include kp_module.ko in the /vendor/lib/modules (vendor.img)
# BOARD_VENDOR_KERNEL_MODULES += $(LOCAL_MODULE_PATH)/$(LOCAL_MODULE)
include $(DLKM_DIR)/Build_external_kernelmodule.mk


#
include $(CLEAR_VARS)
# For incremental compilation
LOCAL_SRC_FILES           := $(wildcard $(LOCAL_PATH)/**/*) $(wildcard $(LOCAL_PATH)/*)
LOCAL_MODULE              := nt36532_touch.ko
LOCAL_MODULE_KBUILD_NAME  := nt36532/nt36532_touch.ko
LOCAL_MODULE_TAGS         := optional
LOCAL_MODULE_DEBUG_ENABLE := true
LOCAL_MODULE_PATH         := $(KERNEL_MODULES_OUT)
# Include kp_module.ko in the /vendor/lib/modules (vendor.img)


# export to kbuild
LOCAL_ADDITIONAL_DEPENDENCY := msm_drm.ko
LOCAL_REQUIRED_MODULES    := msm_drm-module-symvers
LOCAL_ADDITIONAL_DEPENDENCIES := $(call intermediates-dir-for,DLKM,msm_drm-module-symvers)/Module.symvers
KBUILD_OPTIONS += KBUILD_EXTRA_SYMBOLS=$(PWD)/$(call intermediates-dir-for,DLKM,msm_drm-module-symvers)/Module.symvers
$(info KBUILD_OPTIONS = $(KBUILD_OPTIONS))
$(info intermediates msm_drm symvers path = $(call intermediates-dir-for,DLKM,msm_drm-module-symvers))
$(info LOCAL_ADDITIONAL_DEPENDENCY = $(LOCAL_ADDITIONAL_DEPENDENCY))
$(info LOCAL_ADDITIONAL_DEPENDENCIES = $(LOCAL_ADDITIONAL_DEPENDENCIES))
$(info LOCAL_REQUIRED_MODULES = $(LOCAL_REQUIRED_MODULES))
$(info DLKM_DIR = $(DLKM_DIR))

include $(DLKM_DIR)/Build_external_kernelmodule.mk
endif
