// SPDX-License-Identifier: GPL-2.0-only
//! exFAT: the arithmetic done on values taken from the medium.
//!
//! The checksum that guards every directory entry and the boot region, the
//! trailing-dot trimming done on names before they are hashed, and the state
//! machine that decodes the upcase table all read data the filesystem does not
//! control. They live here so that the bounds and the wrapping are checked by
//! construction; the buffer handling and the I/O stay in the C files.

use core::ffi::{c_int, c_uint};

/// The upcase table covers the BMP.
pub const UTBL_INDEX_LIMIT: u32 = 0xFFFF;

/// The bytes the boot sector checksum skips: the volume flags and the
/// percent-in-use, which change as the filesystem is used.
const BOOT_SECTOR_SKIP: [usize; 3] = [106, 107, 112];

/// The rotating checksum exFAT uses for directory entries, the upcase table
/// and the boot region.
///
/// Each byte is added to the checksum rotated right by one bit, which is the
/// `(chksum << 31) | (chksum >> 1)` of the C version expressed as a rotation.
/// `boot_sector` selects the variant that skips the fields that are expected
/// to change.
///
/// # Safety
/// `data` must point to at least `len` readable bytes.
#[no_mangle]
pub unsafe extern "C" fn rust_exfat_calc_chksum32(
    data: *const u8,
    len: c_int,
    chksum: u32,
    boot_sector: bool,
) -> u32 {
    if len <= 0 || data.is_null() {
        return chksum;
    }

    let bytes = unsafe { core::slice::from_raw_parts(data, len as usize) };
    let mut sum = chksum;

    for (i, &byte) in bytes.iter().enumerate() {
        if boot_sector && BOOT_SECTOR_SKIP.contains(&i) {
            continue;
        }

        sum = sum.rotate_right(1).wrapping_add(byte as u32);
    }

    sum
}

/// Length of a name once trailing dots are dropped.
///
/// exFAT stores names with their dots, but lookups ignore trailing ones, so
/// both sides of a comparison have to be trimmed the same way.
///
/// # Safety
/// `name` must point to at least `len` readable bytes.
#[no_mangle]
pub unsafe extern "C" fn rust_exfat_striptail_len(
    len: c_uint,
    name: *const u8,
    keep_last_dots: bool,
) -> c_uint {
    if keep_last_dots || len == 0 || name.is_null() {
        return len;
    }

    let bytes = unsafe { core::slice::from_raw_parts(name, len as usize) };
    let mut trimmed = len;

    while trimmed > 0 && bytes[(trimmed - 1) as usize] == b'.' {
        trimmed -= 1;
    }

    trimmed
}

/// Where the upcase table decoder stands between sectors.
#[repr(C)]
pub struct RustUpcaseState {
    /// The code point being filled in.
    pub index: u32,
    /// Set after a 0xFFFF marker, which skips the next delta.
    pub skip: bool,
    /// Bytes of the sector that were consumed.
    pub consumed: u32,
}

/// Decode one sector of the upcase table.
///
/// The table is stored run-length encoded: a value equal to the index it
/// would have is implicit, 0xFFFF introduces a jump of the amount in the
/// following value, and anything else is an explicit mapping. Decoding stops
/// as soon as the table is complete, and the caller only checksums the bytes
/// that were consumed, so the consumed count comes back here.
///
/// # Safety
/// `src` must point to `byte_len` readable bytes and `table` to at least
/// `table_len` writable entries.
#[no_mangle]
pub unsafe extern "C" fn rust_exfat_upcase_feed(
    src: *const u8,
    byte_len: u32,
    table: *mut u16,
    table_len: u32,
    index: u32,
    skip: bool,
) -> RustUpcaseState {
    let mut index = index;
    let mut skip = skip;
    let mut consumed = 0u32;

    if src.is_null() || table.is_null() {
        return RustUpcaseState { index, skip, consumed };
    }

    let src = unsafe { core::slice::from_raw_parts(src, byte_len as usize) };
    let table = unsafe { core::slice::from_raw_parts_mut(table, table_len as usize) };

    while consumed + 2 <= byte_len && index <= UTBL_INDEX_LIMIT {
        let off = consumed as usize;
        let uni = u16::from_le_bytes([src[off], src[off + 1]]);

        if skip {
            index = index.saturating_add(uni as u32);
            skip = false;
        } else if uni as u32 == index {
            index += 1;
        } else if uni == 0xFFFF {
            skip = true;
        } else {
            if let Some(slot) = table.get_mut(index as usize) {
                *slot = uni;
            }
            index += 1;
        }

        consumed += 2;
    }

    RustUpcaseState { index, skip, consumed }
}
