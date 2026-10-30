// SPDX-License-Identifier: GPL-2.0-only
//! bang_bang thermal governor: the two point hysteresis policy.
//!
//! gov_bang_bang.c keeps the instance walk and the cooling device updates;
//! the state machine for one instance is here.

use core::ffi::{c_int, c_ulong};

/// `THERMAL_NO_TARGET` from thermal_core.h.
const THERMAL_NO_TARGET: c_ulong = c_ulong::MAX;

/// Whether the target a caller handed in is one this governor can work with.
#[no_mangle]
pub extern "C" fn rust_bang_bang_target_is_valid(target: c_ulong) -> bool {
    target == 0 || target == 1 || target == THERMAL_NO_TARGET
}

/// Decide the next target state of one thermal instance.
///
/// Turn the cooling device on once the zone reaches the trip point, and off
/// again only after it drops below the trip point by the hysteresis, so a
/// fan does not toggle around the threshold.
///
/// Returns 0 (off) or 1 (on).
#[no_mangle]
pub extern "C" fn rust_bang_bang_next_target(
    target: c_ulong,
    temperature: c_int,
    trip_temperature: c_int,
    hysteresis: c_int,
) -> c_ulong {
    // A device that was never given a target starts switched off, and one in
    // an unexpected state is treated as on.
    let mut state = match target {
        THERMAL_NO_TARGET => 0,
        0 => 0,
        1 => 1,
        _ => 1,
    };

    if state == 0 {
        if temperature >= trip_temperature {
            state = 1;
        }
    } else if temperature <= trip_temperature.saturating_sub(hysteresis) {
        state = 0;
    }

    state
}
