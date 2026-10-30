// SPDX-License-Identifier: GPL-2.0-only
//! addrtype match: the decision logic.
//!
//! xt_addrtype.c keeps the parts that need the networking stack - the
//! address classification and the IPv6 route lookups - and asks here whether
//! a classified address satisfies the rule, how the source and destination
//! results combine, and whether the rule itself is valid.

use core::ffi::c_int;

/// Address type bits, `enum` in xt_addrtype.h.
const UNSPEC: u16 = 1 << 0;
const UNICAST: u16 = 1 << 1;
const BROADCAST: u16 = 1 << 3;
const BLACKHOLE: u16 = 1 << 6;
const PROHIBIT: u16 = 1 << 8;

/// The types that need a route lookup on IPv6.
const NEEDS_LOOKUP: u16 = (1 << 2) | (1 << 4) | (1 << 7); // LOCAL | ANYCAST | UNREACHABLE
const LOCAL: u16 = 1 << 2;
const ANYCAST: u16 = 1 << 4;
const UNREACHABLE: u16 = 1 << 7;

/// Flag bits, `enum` in xt_addrtype.h. The invert bits are tested by the
/// caller, which hands the resulting booleans to `rust_addrtype_combine`.
const LIMIT_IFACE_IN: u32 = 0x0004;
const LIMIT_IFACE_OUT: u32 = 0x0008;

/// `IPV6_ADDR_*` from net/ipv6.h.
const IPV6_ADDR_ANY: u32 = 0x0000;
const IPV6_ADDR_UNICAST: u32 = 0x0001;
const IPV6_ADDR_MULTICAST: u32 = 0x0002;

/// Netfilter hooks, from netfilter.h.
const NF_INET_PRE_ROUTING: u32 = 0;
const NF_INET_LOCAL_IN: u32 = 1;
const NF_INET_LOCAL_OUT: u32 = 3;
const NF_INET_POST_ROUTING: u32 = 4;

/// Family to tell IPv6 rules apart.
const NFPROTO_IPV6: u32 = 10;

/// Outcome of the IPv6 bit test when it is not conclusive.
const V6_NEED_LOOKUP: c_int = -1;

/// Does an IPv4 address of type `addr_type` satisfy `mask`?
///
/// This is the mask test the match is really about: the address type is a
/// small enum, and the rule is a bitmask of the types it accepts.
#[no_mangle]
pub extern "C" fn rust_addrtype_type_hit(addr_type: u32, mask: u16) -> bool {
    // `inet_dev_addr_type()` returns an `enum rt_scope_t`-like value below the
    // width of the mask, as the C version assumes when it shifts a u16 by it.
    let bit = 1u16.checked_shl(addr_type).unwrap_or(0);

    mask & bit != 0
}

/// Test the type bits of an IPv6 address against `mask`.
///
/// Returns 1 for a match, 0 for a mismatch, and -1 when the mask asks for a
/// type that only a route lookup can answer, in which case the caller has to
/// do the lookup and combine the result itself.
#[no_mangle]
pub extern "C" fn rust_addrtype_v6_bits(addr_type: u32, mask: u16) -> c_int {
    if mask & (1 << 5) != 0 && addr_type & IPV6_ADDR_MULTICAST == 0 {
        return 0;
    }
    if mask & UNICAST != 0 && addr_type & IPV6_ADDR_UNICAST == 0 {
        return 0;
    }
    if mask & UNSPEC != 0 && addr_type != IPV6_ADDR_ANY {
        return 0;
    }

    if mask & NEEDS_LOOKUP != 0 {
        return V6_NEED_LOOKUP;
    }

    1
}

/// Which of the types a route lookup reported satisfy the mask?
///
/// `lookup` holds the XT_ADDRTYPE_* bits the caller found by looking at the
/// routing table and at the interface addresses.
#[no_mangle]
pub extern "C" fn rust_addrtype_v6_lookup_hit(lookup: u32, mask: u16) -> bool {
    mask as u32 & lookup != 0
}

/// Combine the source and destination results.
///
/// A zero mask means the rule does not constrain that side. The invert flag
/// flips the corresponding result, which is how "not a local address" style
/// rules are expressed.
#[no_mangle]
pub extern "C" fn rust_addrtype_combine(
    source: u16,
    dest: u16,
    invert_source: bool,
    invert_dest: bool,
    src_hit: bool,
    dst_hit: bool,
) -> bool {
    let mut ret = true;

    if source != 0 {
        ret &= src_hit ^ invert_source;
    }
    if dest != 0 {
        ret &= dst_hit ^ invert_dest;
    }

    ret
}

/// Why a rule was rejected; the caller turns this into a message.
#[repr(C)]
pub enum RustAddrtypeRuleError {
    /// The rule is valid.
    None = 0,
    /// Both interface limitations selected.
    BothIfaces = 1,
    /// An output interface limit in a hook that has no output interface.
    OutIfaceWrongHook = 2,
    /// An input interface limit in a hook that has no input interface.
    InIfaceWrongHook = 3,
    /// Blackhole matching, unsupported on IPv6.
    V6Blackhole = 4,
    /// Prohibit and above, unsupported on IPv6.
    V6Prohibit = 5,
    /// Broadcast matching, unsupported on IPv6.
    V6Broadcast = 6,
}

/// Validate a revision 1 rule before it is used.
#[no_mangle]
pub extern "C" fn rust_addrtype_checkentry(
    flags: u32,
    hook_mask: u32,
    family: u32,
    source: u16,
    dest: u16,
    ipv6_enabled: bool,
) -> RustAddrtypeRuleError {
    if flags & LIMIT_IFACE_IN != 0 && flags & LIMIT_IFACE_OUT != 0 {
        return RustAddrtypeRuleError::BothIfaces;
    }

    let has_in = hook_mask
        & ((1 << NF_INET_PRE_ROUTING) | (1 << NF_INET_LOCAL_IN))
        != 0;
    if has_in && flags & LIMIT_IFACE_OUT != 0 {
        return RustAddrtypeRuleError::OutIfaceWrongHook;
    }

    let has_out = hook_mask
        & ((1 << NF_INET_POST_ROUTING) | (1 << NF_INET_LOCAL_OUT))
        != 0;
    if has_out && flags & LIMIT_IFACE_IN != 0 {
        return RustAddrtypeRuleError::InIfaceWrongHook;
    }

    if ipv6_enabled && family == NFPROTO_IPV6 {
        let both = source | dest;

        if both & BLACKHOLE != 0 {
            return RustAddrtypeRuleError::V6Blackhole;
        }
        if both >= PROHIBIT {
            return RustAddrtypeRuleError::V6Prohibit;
        }
        if both & BROADCAST != 0 {
            return RustAddrtypeRuleError::V6Broadcast;
        }
    }

    RustAddrtypeRuleError::None
}

/// The XT_ADDRTYPE_LOCAL and XT_ADDRTYPE_ANYCAST bits, for the caller to
/// build the lookup result.
#[no_mangle]
pub extern "C" fn rust_addrtype_local_bit() -> u16 {
    LOCAL
}

/// The XT_ADDRTYPE_ANYCAST bit.
#[no_mangle]
pub extern "C" fn rust_addrtype_anycast_bit() -> u16 {
    ANYCAST
}

/// The XT_ADDRTYPE_UNREACHABLE bit.
#[no_mangle]
pub extern "C" fn rust_addrtype_unreachable_bit() -> u16 {
    UNREACHABLE
}

/// Whether the mask constrains anything at all.
#[no_mangle]
pub extern "C" fn rust_addrtype_mask_is_empty(mask: u16) -> bool {
    mask == 0
}
