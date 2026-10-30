// SPDX-License-Identifier: GPL-2.0-only
//! IPA (Intelligent Power Allocation) governor: the arithmetic.
//!
//! gov_power_allocator.c keeps everything that walks the trips, the
//! instances and the cooling devices; the two pieces of maths - the PID
//! controller and the distribution of the power budget - are here.

use core::ffi::{c_int, c_ulong};

/// Fixed point fractional bits, as in the C implementation.
const FRAC_BITS: u32 = 10;

/// Convert an integer to the fixed point representation.
fn int_to_frac(x: i32) -> i32 {
    x << FRAC_BITS
}

/// Multiply two fixed point numbers.
fn mul_frac(x: i64, y: i64) -> i64 {
    (x * y) >> FRAC_BITS
}

/// Divide two fixed point numbers.
fn div_frac(x: i64, y: i64) -> i64 {
    (x << FRAC_BITS) / y
}

/// The result of one PID iteration.
///
/// The individual terms come back as well so that the caller can keep
/// tracing them, and the controller state is handed back for the caller to
/// store.
#[repr(C)]
pub struct RustIpaPidResult {
    /// The power budget for this round, in milliwatts.
    pub power_range: u32,
    /// Accumulated error, to be stored by the caller for the next round.
    pub err_integral: i64,
    /// Error of this round, to be stored by the caller.
    pub prev_err: i32,
    /// The error itself, for tracing.
    pub err: i32,
    /// The proportional term, for tracing.
    pub p: i64,
    /// The integral term, for tracing.
    pub i: i64,
    /// The derivative term, for tracing.
    pub d: i64,
}

/// One iteration of the power allocator PID controller.
///
/// Returns the power budget for this round and the updated controller state.
#[no_mangle]
#[allow(clippy::too_many_arguments)]
pub extern "C" fn rust_ipa_pid(
    control_temp: c_int,
    temperature: c_int,
    k_po: c_int,
    k_pu: c_int,
    k_i: c_int,
    k_d: c_int,
    integral_cutoff: c_int,
    err_integral: i64,
    prev_err: c_int,
    max_allocatable_power: u32,
    sustainable_power: u32,
    passive_delay_ms: u32,
) -> RustIpaPidResult {
    let max_power_frac = max_allocatable_power.wrapping_shl(FRAC_BITS) as i32;

    let err = int_to_frac(control_temp - temperature);

    // Proportional term: the aggressive constant above the control
    // temperature, the passive one below it.
    let p = mul_frac((if err < 0 { k_po } else { k_pu }) as i64, err as i64);

    // Integral term, accumulated only while the error is under the cutoff and
    // as long as that does not push the term past the maximum power.
    let mut i = mul_frac(k_i as i64, err_integral);
    let mut err_integral = err_integral;

    if err < int_to_frac(integral_cutoff) {
        let i_next = i + mul_frac(k_i as i64, err as i64);

        if i_next.abs() < max_power_frac as i64 {
            i = i_next;
            err_integral += err as i64;
        }
    }

    // Derivative term, divided by the sampling period.
    let d = if passive_delay_ms != 0 {
        div_frac(mul_frac(k_d as i64, (err - prev_err) as i64), passive_delay_ms as i64)
    } else {
        0
    };

    let mut power_range = p + i + d;

    // Feed forward the power the zone is known to be able to dissipate.
    power_range = sustainable_power as i64 + (power_range >> FRAC_BITS);
    power_range = power_range.clamp(0, max_allocatable_power as i64);

    RustIpaPidResult {
        power_range: power_range as u32,
        err_integral,
        prev_err: err,
        err,
        p,
        i,
        d,
    }
}

/// Divide a 64 bit value by a 32 bit one, rounding to nearest.
fn div_round_closest_ull(n: u64, d: u64) -> u64 {
    (n + d / 2) / d
}

/// Distribute the power budget among the cooling devices.
///
/// Each actor gets its requested power scaled by the budget; whatever an
/// actor cannot take because it hit its maximum is handed to the others in
/// proportion to how far they are from theirs, so no power is left unused
/// while some actor is starved.
///
/// # Safety
/// `req_power`, `max_power`, `granted_power` and `extra_actor_power` must
/// all point to at least `num_actors` elements.
#[no_mangle]
pub unsafe extern "C" fn rust_ipa_divvy_power(
    req_power: *const u32,
    max_power: *const u32,
    num_actors: c_int,
    total_req_power: u32,
    power_range: u32,
    granted_power: *mut u32,
    extra_actor_power: *mut u32,
) {
    let n = num_actors.max(0) as usize;
    if n == 0 {
        return;
    }

    let req = unsafe { core::slice::from_raw_parts(req_power, n) };
    let max = unsafe { core::slice::from_raw_parts(max_power, n) };
    let granted = unsafe { core::slice::from_raw_parts_mut(granted_power, n) };
    let extra_actor = unsafe { core::slice::from_raw_parts_mut(extra_actor_power, n) };

    // Nobody requested anything: hand out the maximums.
    if total_req_power == 0 {
        granted.copy_from_slice(max);
        return;
    }

    let mut extra_power: u32 = 0;
    let mut capped_extra_power: u32 = 0;

    for idx in 0..n {
        let req_range = req[idx] as u64 * power_range as u64;

        granted[idx] = div_round_closest_ull(req_range, total_req_power as u64) as u32;

        if granted[idx] > max[idx] {
            extra_power += granted[idx] - max[idx];
            granted[idx] = max[idx];
        }

        extra_actor[idx] = max[idx] - granted[idx];
        capped_extra_power += extra_actor[idx];
    }

    if extra_power == 0 {
        return;
    }

    // Hand the reclaimed power out again, again in proportion to the room
    // each actor has left.
    extra_power = extra_power.min(capped_extra_power);
    if capped_extra_power > 0 {
        for idx in 0..n {
            let extra_range = extra_actor[idx] as u64 * extra_power as u64;

            granted[idx] += div_round_closest_ull(extra_range, capped_extra_power as u64) as u32;
        }
    }
}

/// Round a fixed point value to an integer, for the trace points.
#[no_mangle]
pub extern "C" fn rust_ipa_frac_to_int(x: c_ulong) -> c_int {
    (x as i64 >> FRAC_BITS) as c_int
}
