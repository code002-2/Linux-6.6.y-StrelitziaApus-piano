/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Interface of the MCA onewire battery authentication core, which keeps the
 * battery serial number, dates, cycle count and UI state of health in the
 * authentication chip of the battery pack.
 *
 * Written by sidharthify (Sidharth Sharma) <wednisegit@gmail.com>
 */
#ifndef _MCA_HARDWARE_ONEWIRE_AUTH_H_
#define _MCA_HARDWARE_ONEWIRE_AUTH_H_

#include <linux/errno.h>
#include <linux/list.h>
#include <linux/types.h>

#define ONEWIRE_PAGE_SIZE	32
#define ONEWIRE_DATE_LEN	6
#define ONEWIRE_UI_SOH_LEN	11

struct device;
struct gpio_desc;

/*
 * Commands of one kind of authentication chip. The core serialises every
 * call, so the chip drivers own the bus while they run.
 */
struct onewire_auth_ops {
	/* 0 when a chip of this kind answers with the expected ids */
	int (*check_chip)(void *data);
	/* 0 when the pack passes the ECDSA challenge */
	int (*authenticate)(void *data);
	/* ONEWIRE_PAGE_SIZE bytes of user memory */
	int (*read_page)(void *data, int page, u8 *buf);
	int (*write_page)(void *data, int page, const u8 *buf);
	/* the decrementing counter, counting down from ONEWIRE_COUNTER_MAX */
	int (*read_counter)(void *data, u32 *val);
	int (*decrement_counter)(void *data);
};

#define ONEWIRE_COUNTER_MAX	0x1ffff

struct onewire_auth_chip {
	const char *name;
	const struct onewire_auth_ops *ops;
	void *data;
	/* the bus, filled in by the core */
	struct gpio_desc *gpiod;
	struct list_head node;
};

#if IS_ENABLED(CONFIG_MCA_ONEWIRE_AUTH)
int onewire_auth_add_chip(struct onewire_auth_chip *chip);
void onewire_auth_remove_chip(struct onewire_auth_chip *chip);
u8 onewire_auth_crc8(const u8 *buf, size_t len);
int onewire_auth_ecdsa_verify(const u8 *pub_x, const u8 *pub_y,
			      const u8 *msg, size_t len,
			      const u8 *sig_r, const u8 *sig_s);

void onewire_check_chip_ok(bool *ok);
void onewire_check_auth_pass(bool *pass);
/* page 0 holds the serial number with the manufacturing date in it */
int onewire_get_batt_sn(u8 *buf);
int onewire_get_manufacturing_date(u8 *buf);
int onewire_get_first_usage_date(u8 *buf);
int onewire_set_first_usage_date(u8 *buf);
int onewire_get_batt_cycle_count(int *count);
/* the chip counts the cycles: each call adds one */
int onewire_set_batt_cycle_count(void);
int onewire_reset_batt_cycle_count(void);
int onewire_get_batt_ui_soh(u8 *buf);
int onewire_set_batt_ui_soh(u8 *buf);
int onewire_get_shutdown_uisoc(int *uisoc);
int onewire_set_shutdown_uisoc(int uisoc);
#else
static inline void onewire_check_chip_ok(bool *ok) { *ok = false; }
static inline void onewire_check_auth_pass(bool *pass) { *pass = false; }
static inline int onewire_get_batt_sn(u8 *buf) { return -EOPNOTSUPP; }
static inline int onewire_get_manufacturing_date(u8 *buf) { return -EOPNOTSUPP; }
static inline int onewire_get_first_usage_date(u8 *buf) { return -EOPNOTSUPP; }
static inline int onewire_set_first_usage_date(u8 *buf) { return -EOPNOTSUPP; }
static inline int onewire_get_batt_cycle_count(int *count) { return -EOPNOTSUPP; }
static inline int onewire_set_batt_cycle_count(void) { return -EOPNOTSUPP; }
static inline int onewire_reset_batt_cycle_count(void) { return -EOPNOTSUPP; }
static inline int onewire_get_batt_ui_soh(u8 *buf) { return -EOPNOTSUPP; }
static inline int onewire_set_batt_ui_soh(u8 *buf) { return -EOPNOTSUPP; }
static inline int onewire_get_shutdown_uisoc(int *uisoc) { return -EOPNOTSUPP; }
static inline int onewire_set_shutdown_uisoc(int uisoc) { return -EOPNOTSUPP; }
#endif

#endif /* _MCA_HARDWARE_ONEWIRE_AUTH_H_ */
