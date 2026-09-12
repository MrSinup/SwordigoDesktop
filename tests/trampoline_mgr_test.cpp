#include "platform/trampoline_mgr.h"

#include <cassert>
#include <cstdint>
#include <vector>

int main() {
    constexpr uint64_t target = 0x1000;
    constexpr uint64_t replacement = 0x2000;
    constexpr uint64_t orig_slot = 0x3000;
    constexpr uint32_t original_insn = 0xa9bf7bfd; // stp x29, x30, [sp, #-16]!

    std::vector<uint8_t> memory(TrampolineMgr::ARENA_END, 0);
    *reinterpret_cast<uint32_t*>(memory.data() + target) = original_insn;

    auto& mgr = TrampolineMgr::instance();
    mgr.init(memory.data(), 0);
    assert(mgr.install_hook("relay_order_regression", target, replacement, orig_slot));

    const uint64_t cave = *reinterpret_cast<uint64_t*>(memory.data() + orig_slot);
    assert(cave == TrampolineMgr::ARENA_BASE);
    assert(*reinterpret_cast<uint32_t*>(memory.data() + cave) == original_insn);
    assert(*reinterpret_cast<uint32_t*>(memory.data() + target) != original_insn);
    assert(*reinterpret_cast<uint32_t*>(memory.data() + cave) != 0xd4400000u);

    // A relay-only install must also capture the target before writing its cave.
    constexpr uint64_t relay_target = 0x6000;
    constexpr uint64_t relay_orig_slot = 0x7000;
    *reinterpret_cast<uint32_t*>(memory.data() + relay_target) = original_insn;
    assert(mgr.install_relay("relay_only_order_regression", relay_target, relay_orig_slot));
    const uint64_t relay_cave = *reinterpret_cast<uint64_t*>(memory.data() + relay_orig_slot);
    assert(*reinterpret_cast<uint32_t*>(memory.data() + relay_cave) == original_insn);
    assert(*reinterpret_cast<uint32_t*>(memory.data() + relay_target) == original_insn);

    // Reinitializing the same guest image must preserve hook ownership, and an
    // idempotent install must retain the original relay rather than chaining.
    mgr.init(memory.data(), 0);
    assert(mgr.install_hook("relay_order_regression_again", target, replacement, orig_slot));
    assert(*reinterpret_cast<uint64_t*>(memory.data() + orig_slot) == cave);
    assert(!mgr.install_hook("conflicting_replacement", target, replacement + 4));

    // AArch64 B has a signed 26-bit word displacement. Out-of-range and
    // misaligned targets must fail without changing the original instruction.
    constexpr uint64_t far_target = 0x4000;
    *reinterpret_cast<uint32_t*>(memory.data() + far_target) = original_insn;
    assert(!mgr.install_hook("out_of_range", far_target, far_target + 0x08000000ULL));
    assert(*reinterpret_cast<uint32_t*>(memory.data() + far_target) == original_insn);
    assert(!mgr.install_hook("misaligned", far_target + 2, replacement));
    assert(!mgr.install_hook("relay_slot_overflow", far_target, replacement, 0, 16));
    *reinterpret_cast<uint64_t*>(memory.data() + relay_orig_slot) = 0;
    assert(!mgr.install_relay("relay_only_slot_overflow", far_target,
                              relay_orig_slot, 16));
    assert(*reinterpret_cast<uint64_t*>(memory.data() + relay_orig_slot) == 0);

    // NOP patches are tracked and fully restorable.
    constexpr uint64_t noop_target = 0x5000;
    *reinterpret_cast<uint32_t*>(memory.data() + noop_target) = original_insn;
    *reinterpret_cast<uint32_t*>(memory.data() + noop_target + 4) = 0xd65f03c0u;
    assert(mgr.install_hook_noop("noop_restore", noop_target, 2));
    assert(*reinterpret_cast<uint32_t*>(memory.data() + noop_target) == 0xd503201fu);
    assert(mgr.uninstall_hook(noop_target));
    assert(*reinterpret_cast<uint32_t*>(memory.data() + noop_target) == original_insn);
    assert(*reinterpret_cast<uint32_t*>(memory.data() + noop_target + 4) == 0xd65f03c0u);

    assert(mgr.uninstall_hook(target));
    assert(*reinterpret_cast<uint32_t*>(memory.data() + target) == original_insn);
    return 0;
}
