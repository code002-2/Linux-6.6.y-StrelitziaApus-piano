// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * NTFS block device I/O.
 *
 * Copyright (c) 2026 LG Electronics Co., Ltd.
 */

#include <linux/blkdev.h>

#include "ntfs.h"

/*
 * ntfs_bdev_read - Read data directly from block device using bio
 * @bdev:	block device to read from
 * @data:	destination buffer
 * @start:	starting byte offset on the block device
 * @size:	number of bytes to read
 *
 * Reads @size bytes starting from byte offset @start directly from the block
 * device using one or more BIOs. This function bypasses the page cache
 * completely and performs synchronous I/O with REQ_META | REQ_SYNC flags set.
 *
 * The @start offset must be sector-aligned (512 bytes). If it is not aligned,
 * the function will return -EINVAL.
 *
 * If the destination buffer @data is not a vmalloc address, it falls back
 * to the more efficient bdev_rw_virt() helper.
 *
 * Return: 0 on success, negative error code on failure.
 */
int ntfs_bdev_read(struct block_device *bdev, char *data, loff_t start, size_t size)
{
	struct address_space *mapping = bdev->bd_inode->i_mapping;
	pgoff_t idx = start >> PAGE_SHIFT;
	unsigned int from = start & ~PAGE_MASK;
	size_t done = 0;

	if (start & (SECTOR_SIZE - 1))
		return -EINVAL;

	/*
	 * 6.6 has neither bdev_rw_virt() nor bio_add_vmalloc_chunk(), so go
	 * through the block device page cache, the same way writes already do.
	 */
	while (done < size) {
		struct folio *folio = read_mapping_folio(mapping, idx, NULL);
		size_t chunk;

		if (IS_ERR(folio))
			return PTR_ERR(folio);

		chunk = min(size - done, folio_size(folio) - from);
		memcpy_from_folio(data + done, folio, from, chunk);
		folio_put(folio);

		done += chunk;
		from = 0;
		idx++;
	}

	return 0;
}

/*
 * ntfs_bdev_write - Update block device contents via page cache
 * @sb:		super block of the mounted NTFS filesystem
 * @buf:	source buffer containing data to write
 * @start:	starting byte offset on the block device
 * @size:	number of bytes to write
 *
 * Writes @size bytes from @buf to the block device (sb->s_bdev) starting
 * at byte offset @start. The write is performed entirely through the page
 * cache of the block device's address space.
 */
int ntfs_bdev_write(struct super_block *sb, void *buf, loff_t start, size_t size)
{
	pgoff_t idx, idx_end;
	loff_t offset, end = start + size;
	u32 from, to, buf_off = 0;
	struct folio *folio;

	idx = start >> PAGE_SHIFT;
	idx_end = end >> PAGE_SHIFT;
	from = start & ~PAGE_MASK;

	if (idx == idx_end)
		idx_end++;

	for (; idx < idx_end; idx++, from = 0) {
		u32 len;

		folio = read_mapping_folio(sb->s_bdev->bd_inode->i_mapping, idx, NULL);
		if (IS_ERR(folio)) {
			ntfs_error(sb, "Unable to read %ld page", idx);
			return PTR_ERR(folio);
		}

		offset = (loff_t)idx << PAGE_SHIFT;
		to = min_t(u32, end - offset, PAGE_SIZE);
		len = to - from;

		memcpy_to_folio(folio, from, buf + buf_off, len);
		buf_off += len;
		folio_mark_uptodate(folio);
		folio_mark_dirty(folio);
		folio_put(folio);
	}

	return 0;
}
