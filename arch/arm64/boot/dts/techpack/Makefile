# SPDX-License-Identifier: GPL-2.0
#
# The devicetree overlays of the Qualcomm techpacks, normally built one
# by one as external devicetree modules. Export the variables their
# makefiles would pass and build them along with the other devicetrees.
# The overlays are merged into the base and board devicetrees when the
# dtb and dtbo images are packed.

techpack := $(srctree)/techpack

export MSM_ARCH := sun

# Keep the labels in the overlays, so that the merged base devicetrees
# still have them for the board overlay applied by the bootloader.
export DTC_FLAGS := -@

export AUDIO_DEVICETREE_ROOT := $(srctree)/$(src)/audio-devicetree
export CAMERA_DEVICETREE_ROOT := $(srctree)/$(src)/camera-devicetree
export SYNX_DEVICETREE_ROOT := $(srctree)/$(src)/synx-devicetree

$(obj)/audio-devicetree: export KBUILD_DTC_INCLUDE := $(techpack)/audio-kernel/include
$(obj)/camera-devicetree: export KBUILD_DTC_INCLUDE := $(techpack)/camera-kernel
$(obj)/synx-devicetree: export KBUILD_DTC_INCLUDE := $(techpack)/synx-kernel

subdir-y += audio-devicetree
subdir-y += bt-devicetree
subdir-y += camera-devicetree
subdir-y += data-devicetree
subdir-y += display-devicetree
subdir-y += dsp-devicetree
subdir-y += eva-devicetree
subdir-y += graphics-devicetree
subdir-y += mm-devicetree
subdir-y += mmrm-devicetree
subdir-y += synx-devicetree
subdir-y += video-devicetree
subdir-y += wlan-devicetree
