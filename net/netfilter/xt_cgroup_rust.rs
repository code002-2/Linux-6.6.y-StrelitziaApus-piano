// SPDX-License-Identifier: GPL-2.0-only
//! cgroup match: the rule validation and the comparison.
//!
//! xt_cgroup.c keeps the socket lookup, the cgroup ancestry walk and the
//! reference counting; deciding whether a rule is valid, and turning a hit
//! into a verdict, happens here.

/// Why a rule was rejected. The caller maps this to a message and an errno.
#[repr(C)]
pub enum RustCgroupRuleError {
    /// The rule is valid.
    None = 0,
    /// An invert flag has bits outside the single supported bit.
    BadInvert = 1,
    /// Neither a path nor a classid was given.
    NothingToMatch = 2,
    /// Both a path and a classid were given.
    Ambiguous = 3,
    /// A classid was given but net_cls support is not compiled in.
    NoClassidSupport = 4,
    /// The path is not NUL terminated within the buffer.
    PathTooLong = 5,
}

/// Validate a revision 0 rule.
#[no_mangle]
pub extern "C" fn rust_cgroup_check_v0(invert: u32, classid_enabled: bool) -> RustCgroupRuleError {
    if invert & !1 != 0 {
        return RustCgroupRuleError::BadInvert;
    }

    if !classid_enabled {
        return RustCgroupRuleError::NoClassidSupport;
    }

    RustCgroupRuleError::None
}

/// Validate the parts of a revision 1 or 2 rule that do not need the cgroup
/// hierarchy; the path lookup itself stays with the caller.
#[no_mangle]
pub extern "C" fn rust_cgroup_check_v1(
    invert_path: u32,
    invert_classid: u32,
    has_path: bool,
    has_classid: bool,
    classid_enabled: bool,
    path_terminated: bool,
) -> RustCgroupRuleError {
    if invert_path & !1 != 0 || invert_classid & !1 != 0 {
        return RustCgroupRuleError::BadInvert;
    }

    match (has_path, has_classid) {
        (false, false) => return RustCgroupRuleError::NothingToMatch,
        (true, true) => return RustCgroupRuleError::Ambiguous,
        _ => {}
    }

    if has_classid && !classid_enabled {
        return RustCgroupRuleError::NoClassidSupport;
    }

    if has_path && !path_terminated {
        return RustCgroupRuleError::PathTooLong;
    }

    RustCgroupRuleError::None
}

/// Turn a hit into a verdict, honouring the invert flag.
#[no_mangle]
pub extern "C" fn rust_cgroup_combine(hit: bool, invert: bool) -> bool {
    hit ^ invert
}
