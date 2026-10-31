// SPDX-License-Identifier: GPL-2.0
/*
 * Touchpad palm rejection for the piano keyboard.
 *
 * A contact that lands in one of the edge areas from the device tree is
 * hidden from the input layer until it proves to be a finger: it moves far
 * enough on its own, or together with another finger for a two-finger
 * gesture. A palm resting on the edge stays hidden until it lifts.
 */

#include <linux/module.h>
#include <linux/of.h>
#include <linux/spinlock.h>
#include <asm/unaligned.h>
#include "nano_macro.h"

#define CONTACTS 3
#define CONTACT_SIZE 8
#define CONTACT_BASE 2
#define COUNT_OFFSET (CONTACT_BASE + CONTACTS * CONTACT_SIZE)

#define FLAG_TIP BIT(0)
#define FLAG_IN_RANGE BIT(1)
#define FLAG_VALID BIT(2)

static bool edge_filter = true;
module_param(edge_filter, bool, 0644);
MODULE_PARM_DESC(edge_filter, "Hide touchpad contacts that start on an edge");

struct edge_area {
	u32 min, max;
};

static struct {
	struct edge_area left, right, top, bottom;
	u32 one_swipe;	/* squared distance a lone edge contact must move */
	u32 two_swipe;	/* squared distance with another finger down */
	bool ready;
} cfg;

enum slot_state { SLOT_FREE, SLOT_NORMAL, SLOT_SHIELDED };

static struct {
	enum slot_state state;
	u8 id;
	u16 x0, y0;
} slots[CONTACTS];

static DEFINE_SPINLOCK(filter_lock);

static void read_area(struct device_node *np, const char *side,
		      struct edge_area *area)
{
	char name[48];

	snprintf(name, sizeof(name), "touchpad,edge-area-%s-min", side);
	of_property_read_u32(np, name, &area->min);
	snprintf(name, sizeof(name), "touchpad,edge-area-%s-max", side);
	of_property_read_u32(np, name, &area->max);
}

/* Takes the first touchpad config; the piano keyboards share edge widths. */
void Nanosic_edge_filter_init(struct device_node *node)
{
	struct device_node *configs, *np;

	configs = of_get_child_by_name(node, "touchpad-configs");
	if (!configs)
		return;
	np = of_get_next_available_child(configs, NULL);
	of_node_put(configs);
	if (!np)
		return;

	read_area(np, "left", &cfg.left);
	read_area(np, "right", &cfg.right);
	read_area(np, "top", &cfg.top);
	read_area(np, "bottom", &cfg.bottom);
	cfg.one_swipe = 40000;
	cfg.two_swipe = 2500;
	of_property_read_u32(np, "touchpad,one-swipe-dis-limit", &cfg.one_swipe);
	of_property_read_u32(np, "touchpad,two-swipe-dis-limit", &cfg.two_swipe);
	of_node_put(np);
	cfg.ready = true;
}

static bool in_area(const struct edge_area *area, u16 v)
{
	return area->max > area->min && v >= area->min && v <= area->max;
}

static bool on_edge(u16 x, u16 y)
{
	return in_area(&cfg.left, x) || in_area(&cfg.right, x) ||
	       in_area(&cfg.top, y) || in_area(&cfg.bottom, y);
}

static u32 moved(int slot, u16 x, u16 y)
{
	int dx = x - slots[slot].x0, dy = y - slots[slot].y0;

	return dx * dx + dy * dy;
}

static int find_slot(u8 id)
{
	int i;

	for (i = 0; i < CONTACTS; i++)
		if (slots[i].state != SLOT_FREE && slots[i].id == id)
			return i;
	for (i = 0; i < CONTACTS; i++)
		if (slots[i].state == SLOT_FREE)
			return i;
	return -1;
}

void Nanosic_edge_filter(u8 *report, size_t len)
{
	bool touching[CONTACTS] = { false };
	int slot_of[CONTACTS];
	int i, fingers = 0, hidden = 0;
	unsigned long flags;

	if (!edge_filter || !cfg.ready || len < COUNT_OFFSET + 1)
		return;

	spin_lock_irqsave(&filter_lock, flags);

	for (i = 0; i < CONTACTS; i++) {
		u8 *c = report + CONTACT_BASE + i * CONTACT_SIZE;
		u16 x = get_unaligned_le16(c + 4), y = get_unaligned_le16(c + 6);
		int s;

		slot_of[i] = -1;
		if (!(c[0] & FLAG_TIP))
			continue;
		s = find_slot(c[1]);
		if (s < 0)
			continue;
		if (slots[s].state == SLOT_FREE) {
			slots[s].id = c[1];
			slots[s].x0 = x;
			slots[s].y0 = y;
			slots[s].state = on_edge(x, y) ? SLOT_SHIELDED :
							   SLOT_NORMAL;
		}
		slot_of[i] = s;
		touching[s] = true;
		if (slots[s].state == SLOT_NORMAL)
			fingers++;
	}

	/* Lifted contacts free their slot. */
	for (i = 0; i < CONTACTS; i++)
		if (!touching[i])
			slots[i].state = SLOT_FREE;

	for (i = 0; i < CONTACTS; i++) {
		u8 *c = report + CONTACT_BASE + i * CONTACT_SIZE;
		int s = slot_of[i];

		if (s < 0 || slots[s].state != SLOT_SHIELDED)
			continue;
		if (moved(s, get_unaligned_le16(c + 4),
			  get_unaligned_le16(c + 6)) >
		    (fingers ? cfg.two_swipe : cfg.one_swipe)) {
			slots[s].state = SLOT_NORMAL;
			continue;
		}
		c[0] &= ~(FLAG_TIP | FLAG_IN_RANGE | FLAG_VALID);
		hidden++;
	}

	if (hidden && report[COUNT_OFFSET] >= hidden)
		report[COUNT_OFFSET] -= hidden;

	spin_unlock_irqrestore(&filter_lock, flags);
}
