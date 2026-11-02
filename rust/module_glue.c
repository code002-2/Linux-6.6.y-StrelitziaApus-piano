// SPDX-License-Identifier: GPL-2.0
/*
 * C entry points for Rust modules.
 *
 * The kernel calls these through function pointers (`struct module` init and
 * exit), and KCFI compares the type hash stored in the callee preamble with
 * the one the caller computed for `int (*)(void)`. rustc and clang hash types
 * differently: measured on this toolchain, clang gives a6c5b136 and rustc
 * 607320d2 for that signature, so a Rust function there traps and the machine
 * panics.
 *
 * Both sides could be aligned with -fsanitize-cfi-icall-experimental-
 * normalize-integers, but that changes every C side hash too and the vendor
 * modules were built against the unnormalized GKI; a kernel built that way
 * bootlooped on the device.
 *
 * Instead, compile the entry points with clang so the preamble has the hash
 * the kernel expects, and call into Rust directly, because direct calls are
 * not checked. `module!` therefore generates `__rust_module_init` and
 * `__rust_module_exit`, and a module that wants to be loadable links this
 * file as well:
 *
 *	obj-m := hello.o
 *	hello-objs := hello_main.o module_glue.o
 *
 * Callbacks that a module hands to C still need the same treatment; wrap them
 * in a C shim for now.
 */
#include <linux/module.h>

extern int __rust_module_init(void);
extern void __rust_module_exit(void);

int __init init_module(void)
{
	return __rust_module_init();
}

void __exit cleanup_module(void)
{
	__rust_module_exit();
}
