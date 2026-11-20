/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Compatibility shims so the fs/ntfs driver taken from ACK android18-7.3
 * (Linux 7.3 based) builds against our 6.6 LTS tree.
 *
 * Everything here is either a helper that only appeared after 6.6, or an
 * accessor that replaced a direct field access. Keep this file small and
 * obvious; the real adaptations live next to the code that needs them.
 */
#ifndef _NTFS_COMPAT_H
#define _NTFS_COMPAT_H

#include <linux/cleanup.h>
#include <linux/err.h>
#include <linux/highmem.h>
#include <linux/pagemap.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/writeback.h>

/*
 * 7.x reads inode->i_state through an accessor. 6.6 still has the plain
 * field; reading it under RCU-ish rules with READ_ONCE matches what the
 * accessor does.
 */
#define inode_state_read_once(inode)	READ_ONCE((inode)->i_state)

/*
 * Typed allocation helpers (6.15+). In 6.6 the type is spelled out with
 * sizeof() instead; both call styles used by the driver (a dereferenced
 * pointer and a bare type) work with sizeof().
 */
#define _ntfs_obj1(f, t)		f(sizeof(t), GFP_KERNEL)
#define _ntfs_obj2(f, t, g)		f(sizeof(t), (g))
#define _ntfs_objs2(f, t, n)		f((n), sizeof(t), GFP_KERNEL)
#define _ntfs_objs3(f, t, n, g)		f((n), sizeof(t), (g))
#define _ntfs_pick2(a, b, NAME, ...)	NAME
#define _ntfs_pick3(a, b, c, NAME, ...)	NAME

#define kmalloc_obj(...) \
	_ntfs_pick2(__VA_ARGS__, _ntfs_obj2, _ntfs_obj1)(kmalloc, __VA_ARGS__)
#define kzalloc_obj(...) \
	_ntfs_pick2(__VA_ARGS__, _ntfs_obj2, _ntfs_obj1)(kzalloc, __VA_ARGS__)
#define kmalloc_objs(...) \
	_ntfs_pick3(__VA_ARGS__, _ntfs_objs3, _ntfs_objs2)(kmalloc_array, __VA_ARGS__)
#define kzalloc_objs(...) \
	_ntfs_pick3(__VA_ARGS__, _ntfs_objs3, _ntfs_objs2)(kcalloc, __VA_ARGS__)
#define kvzalloc_obj(...) \
	_ntfs_pick2(__VA_ARGS__, _ntfs_obj2, _ntfs_obj1)(kvzalloc, __VA_ARGS__)
#define kvzalloc_objs(...) \
	_ntfs_pick3(__VA_ARGS__, _ntfs_objs3, _ntfs_objs2)(kvcalloc, __VA_ARGS__)

/* 7.x sets i_state through an accessor; 6.6 still has the plain field. */
#define inode_state_set(inode, state)	((inode)->i_state = (state))

/*
 * Zeroing / vmalloc typed allocators, same deal as kmalloc_obj above.
 */
/*
 * Drop-in for the inode drop callback rename.
 */
#define inode_generic_drop(inode)	generic_drop_inode(inode)

/*
 * Little helpers from lib/vsprintf.c and kernel.h that 6.6 does not have.
 */
#define str_plural(n)			((n) == 1 ? "" : "s")
#define cmp_int(a, b)			(((a) > (b)) - ((a) < (b)))

/*
 * folio_fill_tail(): copy @len bytes from @src at @offset into @folio and
 * zero the rest of it. Only used while a folio is being brought uptodate
 * from resident attribute data, so a plain map/copy/memset is enough.
 */
static inline void folio_fill_tail(struct folio *folio, size_t offset,
				   const void *src, size_t len)
{
	void *addr = kmap_local_folio(folio, 0);

	memcpy(addr + offset, src, len);
	if (offset + len < folio_size(folio))
		memset(addr + offset + len, 0, folio_size(folio) - offset - len);
	kunmap_local(addr);
}

/*
 * 6.6 still spells this in terms of struct page and hooks it as
 * ->error_remove_page; keep the driver's folio spelling.
 */
#define generic_error_remove_folio(mapping, folio) \
	generic_error_remove_page((mapping), &(folio)->page)
#undef error_remove_folio


/* 7.x renamed the block device freeze helpers. */
#define bdev_freeze(bdev)	freeze_bdev(bdev)
#define bdev_thaw(bdev)		thaw_bdev(bdev)

/*
 * FS_IOC_SHUTDOWN and its flag set arrived after 6.6. Pin the same numbers
 * the upstream ioctl uses so userspace keeps working.
 */
#ifndef FS_IOC_SHUTDOWN
#define FS_IOC_SHUTDOWN				_IOR('X', 125, __u32)
#define FS_SHUTDOWN_FLAGS_LOGFLUSH		0x1
#define FS_SHUTDOWN_FLAGS_NOLOGFLUSH		0x2
#define FS_SHUTDOWN_FLAGS_DEFAULT		0x0
#endif

/* Every 6.6 workqueue is per-cpu unless WQ_UNBOUND is set. */
#ifndef WQ_PERCPU
#define WQ_PERCPU				0
#endif

/*
 * 7.x gave the default fallocate mode a name. In 6.6 allocate is simply
 * mode 0, and the mode field is the low flag bits.
 */
#ifndef FALLOC_FL_ALLOCATE_RANGE
#define FALLOC_FL_ALLOCATE_RANGE		0x00
#endif
#ifndef FALLOC_FL_MODE_MASK
#define FALLOC_FL_MODE_MASK					\
	(FALLOC_FL_PUNCH_HOLE | FALLOC_FL_COLLAPSE_RANGE |	\
	 FALLOC_FL_ZERO_RANGE | FALLOC_FL_INSERT_RANGE)
#endif

/* libfs-only errno that 6.6 keeps private to jbd2. */
#ifndef EFSCORRUPTED
#define EFSCORRUPTED		EUCLEAN
#endif

#endif /* _NTFS_COMPAT_H */
