// SPDX-License-Identifier: GPL-2.0-only
//! devfreq simple ondemand governor: the frequency selection policy.
//!
//! governor_simpleondemand.c keeps the devfreq plumbing (statistics update,
//! the overflow shifting of the counters, writing the result back); the
//! arithmetic and the decision are here.

use core::ffi::{c_int, c_uint, c_ulong};

/// `DEVFREQ_MAX_FREQ` from devfreq.h.
const DEVFREQ_MAX_FREQ: c_ulong = c_ulong::MAX;

/// `-EINVAL`.
const EINVAL: c_int = 22;

/// Select the target frequency from the bus utilisation.
///
/// Writes the chosen frequency to `freq` and returns 0, or returns `-EINVAL`
/// when the thresholds are inconsistent. The counters handed in are expected
/// to have been scaled down by the caller if they were too large for the
/// multiplications below.
///
/// # Safety
/// `freq` must be a valid pointer to a `unsigned long`.
#[no_mangle]
pub unsafe extern "C" fn rust_simple_ondemand_target(
    busy_time: c_ulong,
    total_time: c_ulong,
    current_frequency: c_ulong,
    upthreshold: c_uint,
    downdifferential: c_uint,
    freq: *mut c_ulong,
) -> c_int {
    let up = upthreshold as u64;
    let down = downdifferential as u64;

    if upthreshold > 100 || upthreshold < downdifferential {
        return -EINVAL;
    }

    let busy = busy_time as u64;
    let total = total_time as u64;

    // Nothing measured yet, or nothing to scale a frequency from: run at the
    // maximum.
    if total_time == 0 || current_frequency == 0 {
        unsafe { freq.write(DEVFREQ_MAX_FREQ) };
        return 0;
    }

    // Busy enough to justify the maximum frequency.
    if busy * 100 > total * up {
        unsafe { freq.write(DEVFREQ_MAX_FREQ) };
        return 0;
    }

    // Close to the threshold: stay where we are.
    if busy * 100 > total * (up - down) {
        unsafe { freq.write(current_frequency) };
        return 0;
    }

    // Scale the frequency by the load, aiming slightly above the busy share
    // so the device does not sit just below the threshold.
    //
    // u64 is enough here: the caller shifts the counters down to below 2^24
    // before calling, so busy * current_frequency stays well inside 64 bits,
    // and 128 bit arithmetic would pull in __udivti3, which the kernel does
    // not provide.
    let scaled = busy * (current_frequency as u64) / total;
    let target = scaled * 100 / (up - down / 2);

    unsafe { freq.write(target) };
    0
}
