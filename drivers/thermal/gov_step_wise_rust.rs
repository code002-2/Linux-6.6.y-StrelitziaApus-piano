// SPDX-License-Identifier: GPL-2.0-only
//! step_wise thermal governor: the throttling policy.
//!
//! Walking the instance list, talking to cooling devices and keeping the
//! passive counters needs the internal thermal structures, so that part stays
//! in C (gov_step_wise.c). The decision itself - how far to step the cooling
//! state for a given trip point - is here.

use core::ffi::{c_int, c_ulong};

/// `THERMAL_NO_TARGET` from thermal_core.h.
const THERMAL_NO_TARGET: c_ulong = c_ulong::MAX;

/// `enum thermal_trend` from thermal.h.
const TREND_RAISING: c_int = 1;
const TREND_DROPPING: c_int = 2;

/// `clamp()` from minmax.h, for the cooling state range of an instance.
fn clamp_cooling(value: c_ulong, lower: c_ulong, upper: c_ulong) -> c_ulong {
    if value < lower {
        lower
    } else if value > upper {
        upper
    } else {
        value
    }
}

/// Decide the next target cooling state for one thermal instance.
///
/// Above a trip point, step the cooling state up when the zone is heating up
/// and leave it alone otherwise; below a trip point, step it down when the
/// zone is cooling and deactivate the instance once it reaches the lower
/// limit. Returns `THERMAL_NO_TARGET` when the instance should be inactive.
#[no_mangle]
pub extern "C" fn rust_step_wise_next_target(
    initialized: bool,
    throttle: bool,
    trend: c_int,
    cur_state: c_ulong,
    lower: c_ulong,
    upper: c_ulong,
    target: c_ulong,
) -> c_ulong {
    // First evaluation of this instance: only act when throttling is asked
    // for, otherwise leave it without a target.
    if !initialized {
        return if throttle {
            clamp_cooling(cur_state.saturating_add(1), lower, upper)
        } else {
            THERMAL_NO_TARGET
        };
    }

    if throttle {
        if trend == TREND_RAISING {
            return clamp_cooling(cur_state.saturating_add(1), lower, upper);
        }
    } else if trend == TREND_DROPPING {
        return if cur_state <= lower {
            THERMAL_NO_TARGET
        } else {
            clamp_cooling(cur_state.saturating_sub(1), lower, upper)
        };
    }

    target
}
