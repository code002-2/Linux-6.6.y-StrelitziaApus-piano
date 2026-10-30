// SPDX-License-Identifier: GPL-2.0-only
//! ISO 9660 Rock Ridge parsing: the bounds arithmetic.
//!
//! Everything here works on numbers taken straight from the medium, which is
//! not trusted: rock.c keeps the buffer handling and the I/O, and asks these
//! functions whether a record fits before looking at it, whether a
//! continuation area is inside the volume, and where the Rock Ridge area of a
//! directory record actually starts.

use core::ffi::c_int;

/// `-EIO`, the error the caller reports for a corrupt directory entry.
const EIO: c_int = 5;

/// Size of the shared Rock Ridge header: signature, length, version.
const RR_HEADER: i32 = 4;

/// `sizeof(struct iso_directory_record)`, the fixed part of a directory
/// record. The name, and the Rock Ridge area after it, follow this.
const ISO_DIR_RECORD_SIZE: i32 = 33;

/// `offsetof(struct rock_ridge, u)`, the smallest possible entry.
pub const RR_MIN_DE_SIZE: i32 = 4;

/// Signature helper, matching `SIG()` in rock.c.
const fn sig(a: u8, b: u8) -> u32 {
    (a as u32) | ((b as u32) << 8)
}

/// Why a continuation area was rejected.
#[repr(C)]
pub enum RustRockCeError {
    /// The continuation area is usable.
    None = 0,
    /// It does not lie within one block.
    OutOfBlock = 1,
    /// Its extent is past the end of the volume.
    PastVolume = 2,
}

/// The size a Rock Ridge entry of this signature needs, header included.
///
/// The sizes come from the on-disk structures in rock.h, which are packed, so
/// they are exactly the sum of their fields:
///
///   SP  magic[2] + skip                         3
///   CE  extent[8] + offset[8] + size[8]        24
///   ER  len_id + len_des + len_src + ext_ver    4
///   RR  flags[1]                                1
///   PX  mode[8] + n_links[8] + uid[8] + gid[8] 32
///   PN  dev_high[8] + dev_low[8]               16
///   SL  flags + component{flags, len}           3
///   NM  flags[1]                                1
///   CL  location[8]                             8
///   PL  location[8]                             8
///   TF  flags[1]                                1
///   ZF  algorithm[2] + parms[2] + real_size[8] 12
///
/// A signature the parser does not know needs only the header.
const fn entry_size(remaining_sig: u32) -> i32 {
    match remaining_sig {
        s if s == sig(b'S', b'P') => 3,
        s if s == sig(b'C', b'E') => 24,
        s if s == sig(b'E', b'R') => 4,
        s if s == sig(b'R', b'R') => 1,
        s if s == sig(b'P', b'X') => 32,
        s if s == sig(b'P', b'N') => 16,
        s if s == sig(b'S', b'L') => 3,
        s if s == sig(b'N', b'M') => 1,
        s if s == sig(b'C', b'L') => 8,
        s if s == sig(b'P', b'L') => 8,
        s if s == sig(b'T', b'F') => 1,
        s if s == sig(b'Z', b'F') => 12,
        _ => 0,
    }
}

/// The result of the fit check, with the size the record needs so that the
/// caller can report what went wrong.
#[repr(C)]
pub struct RustRockOverflow {
    /// Zero when the record fits, negative when it does not.
    pub error: c_int,
    /// Bytes the record needs, its header included.
    pub needed: c_int,
}

/// Does an entry of signature `sig` fit in the `remaining` bytes left?
///
/// The sizes come from the medium-independent layout of the records, but the
/// remaining length comes from the medium, so this is where a directory entry
/// that claims more space than it has is caught.
#[no_mangle]
pub extern "C" fn rust_rock_check_overflow(sig: u32, remaining: c_int) -> RustRockOverflow {
    let needed = entry_size(sig) + RR_HEADER;

    RustRockOverflow {
        error: if needed > remaining { -EIO } else { 0 },
        needed,
    }
}

/// Check that a continuation area can be read.
///
/// The three fields come from the medium, so they are checked against the
/// block size and against the number of zones in the volume before anything is
/// read or allocated.
#[no_mangle]
pub extern "C" fn rust_rock_check_ce(
    cont_offset: c_int,
    cont_size: c_int,
    blocksize: c_int,
    cont_extent: c_int,
    nzones: u64,
) -> RustRockCeError {
    let offset = cont_offset as u32;
    let size = cont_size as u32;

    // The offset and the size have to stay inside one block, and the record
    // itself has to be able to fit after the offset.
    if offset > (blocksize as u32).wrapping_sub(RR_MIN_DE_SIZE as u32)
        || size > blocksize as u32
        || offset.wrapping_add(size) > blocksize as u32
    {
        return RustRockCeError::OutOfBlock;
    }

    if (cont_extent as u32) as u64 >= nzones {
        return RustRockCeError::PastVolume;
    }

    RustRockCeError::None
}

/// Where the Rock Ridge entries of a directory record start, and how much
/// room they have.
#[repr(C)]
pub struct RustRockSetup {
    /// Bytes from the start of the directory record to the Rock Ridge area.
    pub chr_offset: i32,
    /// Bytes still available for Rock Ridge entries.
    pub len: i32,
}

/// Work out the Rock Ridge area of a directory record.
///
/// The record starts with a fixed header and the file name, padded to an even
/// length; the system use area, which is where Rock Ridge lives, follows. Both
/// the record length and the name length come from the medium.
#[no_mangle]
pub extern "C" fn rust_rock_setup(de_len: u8, name_len: u8, rock_offset: c_int) -> RustRockSetup {
    let mut chr_offset = ISO_DIR_RECORD_SIZE + name_len as i32;
    if chr_offset & 1 != 0 {
        chr_offset += 1;
    }

    let mut len = de_len as i32 - chr_offset;
    if len < 0 {
        len = 0;
    }

    // A skip value from a preceding SP entry moves the start of the area.
    if rock_offset >= 0 {
        chr_offset += rock_offset;
        len -= rock_offset;
        if len < 0 {
            len = 0;
        }
    }

    RustRockSetup { chr_offset, len }
}
