LOCAL_PATH := $(call my-dir)
ifneq (, $(filter $(call get-miodm-device-name), yupei))
$(info "touch driver build for $(call get-miodm-device-name)")
include $(LOCAL_PATH)/p82/Android.mk
endif

ifneq (, $(filter $(call get-miodm-device-name), piano))
$(info "touch driver build for $(call get-miodm-device-name)")
include $(LOCAL_PATH)/p81/Android.mk
endif