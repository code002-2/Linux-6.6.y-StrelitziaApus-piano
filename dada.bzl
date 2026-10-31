load(":target_variants.bzl", "la_variants")
load(":msm_kernel_la.bzl", "define_msm_la")
load(":image_opts.bzl", "boot_image_opts")
load(":xiaomi_sm8750_common.bzl", "xiaomi_common_in_tree_modules")
load(":sun.bzl", 
        "target_arch",
        "target_arch_in_tree_modules",
        "target_arch_consolidate_in_tree_modules",
        "consolidate_board_kernel_cmdline_extras",
        "consolidate_board_bootconfig_extras",
        "consolidate_kernel_vendor_cmdline_extras",
        "perf_board_kernel_cmdline_extras",
        "perf_board_bootconfig_extras",
        "perf_kernel_vendor_cmdline_extras",
)

target_name = "dada"

def define_dada():
    for variant in la_variants:
        _target_in_tree_modules = target_arch_in_tree_modules + xiaomi_common_in_tree_modules + [
            # keep sorted
            "drivers/media/rc/ir-spi.ko",
            # MIUI ADD: Stability_DebugEnhance
            "drivers/xiaomi/boottime/boottime.ko",
            # END Stability_DebugEnhance
            "drivers/xiaomi/mi_stack/mi_stack.ko",
            "drivers/xiaomi/mi_ubt/mi_ubt.ko",
            "drivers/xiaomi/mi_ubt/test/mi_ubt_test.ko",
            "drivers/xiaomi/kshrink_slabd/kshrink_slabd.ko",
            "drivers/xiaomi/lz4asm/lz4asm.ko",
            "drivers/xiaomi/printk_enhance/printk_enhance.ko",
            "fs/nls/nls_ucs2_utils.ko",
            "fs/netfs/netfs.ko",
            "net/dns_resolver/dns_resolver.ko",
            "fs/smb/common/cifs_md4.ko",
            "fs/smb/common/cifs_arc4.ko",
            "fs/smb/client/cifs.ko",
            "block/cpq/cpq.ko",
            "drivers/xiaomi/hangdetect/hangdetect.ko",
	    "drivers/gpio/gpio-mi-t1.ko",
            "drivers/xiaomi/bootmonitor/bootmonitor.ko",
            "drivers/mtd/mtd_blkdevs.ko",
            "drivers/mtd/parsers/ofpart.ko",
            "drivers/mtd/mtdoops.ko",
            "drivers/mtd/devices/block2mtd.ko",
            "drivers/mtd/chips/chipreg.ko",
            "drivers/mtd/mtdblock.ko",
            "drivers/mtd/mtd.ko",
	    # MIUI ADD
	    "drivers/mihw/dcvs_arbi/dcvs_main.ko",
            "drivers/sandbox/rt_mod.ko",
            "drivers/power/xm_power/xm_power.ko",
            "drivers/mihw/powersave/powersave.ko",
            "drivers/block/zram/zram.ko",
            "mm/zsmalloc.ko",
            "drivers/xiaomi/mi_trace/mi_trace.ko",
            "mm/launch_boost/lb.ko"
            ]

        _target_consolidate_in_tree_modules = _target_in_tree_modules + \
                target_arch_consolidate_in_tree_modules + [
            # keep sorted
            "drivers/dma-buf/dma-buf-ref.ko",
            ]

        if variant == "consolidate":
            mod_list = _target_consolidate_in_tree_modules
            board_kernel_cmdline_extras = consolidate_board_kernel_cmdline_extras
            board_bootconfig_extras = consolidate_board_bootconfig_extras
            kernel_vendor_cmdline_extras = consolidate_kernel_vendor_cmdline_extras
        else:
            mod_list = _target_in_tree_modules
            board_kernel_cmdline_extras = perf_board_kernel_cmdline_extras
            board_bootconfig_extras = perf_board_bootconfig_extras
            kernel_vendor_cmdline_extras = perf_kernel_vendor_cmdline_extras

        define_msm_la(
            msm_target = target_name,
            msm_arch = target_arch,
            variant = variant,
            in_tree_module_list = mod_list,
            boot_image_opts = boot_image_opts(
                earlycon_addr = "qcom_geni,0x00a9c000",
                kernel_vendor_cmdline_extras = kernel_vendor_cmdline_extras,
                board_kernel_cmdline_extras = board_kernel_cmdline_extras,
                board_bootconfig_extras = board_bootconfig_extras,
            ),
        )

