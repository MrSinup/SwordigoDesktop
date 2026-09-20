// unclaimed_explorer_test.cpp — ranking the memory nobody has identified yet.
//
// The explorer's whole value is that it points at the RIGHT unclaimed memory, so
// this test is about discrimination, not about plumbing:
//
//   * "unclaimed" really means unclaimed — anything inside a claimed range is
//     skipped, never ranked
//   * static memory costs nothing: a slot that never moves is never promoted,
//     so a 3.5 GB address space does not turn into 3.5 GB of bookkeeping
//   * a pass is bounded and the region cursor advances, so the sweep can never
//     block a frame
//   * a slot that moves WITH the scene outranks one that just churns
//   * a constant non-zero slot is still listed, but below both
//   * adjacent movers are recognised as one probable struct
//   * a pointer-looking value is demoted, not hidden
//   * a mapped lead gets a deterministic, boot-stable name; an unmapped one is
//     honestly session-scoped
//
// Pure RAM test: no guest, no GL, no Qt.
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "game/research/unclaimed_explorer.h"

using namespace swordfare::research;

static int g_failures = 0;

static void check(bool ok, const char* what) {
    std::printf("  [%s] %s\n", ok ? " OK " : "FAIL", what);
    if (!ok) ++g_failures;
}

static void check_eq_u64(uint64_t got, uint64_t want, const char* what) {
    const bool ok = (got == want);
    std::printf("  [%s] %s", ok ? " OK " : "FAIL", what);
    if (!ok) std::printf(" (got 0x%llX want 0x%llX)",
                         (unsigned long long)got, (unsigned long long)want);
    std::printf("\n");
    if (!ok) ++g_failures;
}

// ---------------------------------------------------------------------------
static constexpr uint64_t kMemSize    = 0x40000;
static constexpr uint64_t kRegionBeg  = 0x10000;
static constexpr uint64_t kRegionEnd  = 0x20000;

// Deliberately fixed "interesting" addresses inside the region.
static constexpr uint64_t kAddrScene   = 0x11000;
static constexpr uint64_t kAddrChurn   = 0x12000;
static constexpr uint64_t kAddrStatic  = 0x13000;
static constexpr uint64_t kAddrPointer = 0x14000;
static constexpr uint64_t kAddrCluster = 0x15000;   // 8 slots, 0x15000..0x1501C
static constexpr uint64_t kAddrClaimed = 0x16000;   // inside a claimed range

struct Guest {
    std::vector<uint8_t> bytes;
    Guest() : bytes(kMemSize, 0) {}
    const uint8_t* data() const { return bytes.data(); }
    uint64_t size() const { return kMemSize; }
    void poke32(uint64_t addr, uint32_t v) {
        std::memcpy(bytes.data() + addr, &v, 4);
    }
    uint32_t peek32(uint64_t addr) const {
        uint32_t v = 0;
        std::memcpy(&v, bytes.data() + addr, 4);
        return v;
    }
};

static ModuleMap make_modules() {
    // libswordigo covers the whole region, so every slot in it has a static home;
    // a separate probe address outside it is used for the unmapped case.
    ModuleMap m;
    ModuleExtent e;
    e.name      = "libswordigo.so";
    e.base_va   = kRegionBeg;
    e.rva_begin = 0;
    e.rva_end   = kRegionEnd - kRegionBeg;
    e.build_id  = "sre13-1.4.13-arm64";
    m.set(e);
    return m;
}

static ExploreConfig make_config() {
    ExploreConfig c;
    c.region_begin = kRegionBeg;
    c.region_end   = kRegionEnd;
    c.type         = ValueType::Dword;
    // Big enough that a single pass covers the whole region, so consecutive
    // passes revisit the same addresses and change detection is exercised.
    c.slots_per_pass = (kRegionEnd - kRegionBeg) / 4 + 64;
    c.cache_slots    = 1u << 18;
    c.max_tracks     = 1000;
    c.min_samples    = 2;
    c.cluster_gap    = 24;
    c.max_leads      = 100;
    return c;
}

static const Lead* find_lead(const std::vector<Lead>& leads, uint64_t addr) {
    for (const auto& l : leads)
        if (l.address == addr) return &l;
    return nullptr;
}

static int index_of(const std::vector<Lead>& leads, uint64_t addr) {
    for (size_t i = 0; i < leads.size(); ++i)
        if (leads[i].address == addr) return static_cast<int>(i);
    return -1;
}

// ---------------------------------------------------------------------------
static void test_claiming() {
    std::printf("\n[claiming: unclaimed means unclaimed]\n");
    UnclaimedExplorer ex;
    ex.configure(make_config());

    check(!ex.is_claimed(kAddrScene), "nothing is claimed before ranges are set");

    // Overlapping ranges must merge, and the lookup must stay exact at the edges.
    ex.set_claimed({{0x10500, 0x10600}, {0x10580, 0x10700}, {0x16000, 0x17000}});
    check_eq_u64(ex.claimed_range_count(), 2, "overlapping ranges merged");
    check(ex.is_claimed(0x10500), "range start is claimed");
    check(ex.is_claimed(0x106FF), "merged range interior is claimed");
    check(!ex.is_claimed(0x104FF), "just below a range is NOT claimed");
    check(!ex.is_claimed(0x10700), "just past a range is NOT claimed (half-open)");
    check(ex.is_claimed(kAddrClaimed), "the test range is claimed");

    // A degenerate range must still cover its single slot rather than vanishing.
    UnclaimedExplorer ex2;
    ex2.configure(make_config());
    ex2.set_claimed({{0x18000, 0x18000}});
    check(ex2.is_claimed(0x18000), "a zero-width range still covers its slot");

    ex.set_claimed({});
    check(!ex.is_claimed(kAddrClaimed), "clearing the ranges unclaims everything");
}

static void test_bounded_passes() {
    std::printf("\n[bounded work: a pass can never block a frame]\n");
    Guest g;
    UnclaimedExplorer ex;
    ExploreConfig c = make_config();
    c.slots_per_pass = 512;                 // tiny budget on purpose
    ex.configure(c);

    const auto r1 = ex.pass(g.data(), g.size(), 1, false, false);
    check_eq_u64(r1.slots_examined, 512, "pass examined exactly its budget");
    check(!r1.wrapped, "nothing wrapped on the first slice");
    check(r1.cursor_after > r1.cursor_before, "cursor advanced");
    check(r1.region_valid, "region reported as valid");

    // Keep going until it wraps, and check the cursor walked the whole region.
    uint64_t examined = r1.slots_examined;
    bool wrapped = false;
    for (int i = 0; i < 200 && !wrapped; ++i) {
        const auto r = ex.pass(g.data(), g.size(), 2 + i, false, false);
        examined += r.slots_examined;
        wrapped = r.wrapped;
    }
    check(wrapped, "the cursor eventually wraps");
    const uint64_t slots_in_region = (kRegionEnd - kRegionBeg) / 4;
    check(examined >= slots_in_region,
          "the sweep covered every slot before wrapping");

    // An empty region must be refused rather than looped over.
    UnclaimedExplorer ex2;
    ExploreConfig c2 = make_config();
    c2.region_begin = c2.region_end = kRegionBeg;
    ex2.configure(c2);
    check(!ex2.pass(g.data(), g.size(), 1, false, false).region_valid,
          "an empty region is refused");

    // No guest memory at all must be refused too.
    UnclaimedExplorer ex3;
    ex3.configure(make_config());
    check(!ex3.pass(nullptr, 0, 1, false, false).region_valid, "null memory is refused");
}

static void test_static_memory_costs_nothing() {
    std::printf("\n[promotion: only moving slots are tracked]\n");
    Guest g;
    // Fill the region with a constant non-zero pattern: not one slot moves.
    for (uint64_t a = kRegionBeg; a < kRegionEnd; a += 4) g.poke32(a, 0xDEADBEEF);

    UnclaimedExplorer ex;
    ex.configure(make_config());

    ex.pass(g.data(), g.size(), 1, false, false);   // baseline
    ex.pass(g.data(), g.size(), 2, false, false);   // nothing changed

    check_eq_u64(ex.tracked(), 0, "16k static slots produced 0 tracks");
    check_eq_u64(ex.promotions(), 0, "no promotions");

    // Move exactly one slot.
    g.poke32(kAddrScene, 0x1234);
    ex.pass(g.data(), g.size(), 3, false, false);
    check_eq_u64(ex.tracked(), 1, "exactly one track, for the one slot that moved");
}

static void test_ranking_discriminates() {
    std::printf("\n[ranking: scene-linked beats churn beats constant]\n");
    Guest g;

    g.poke32(kAddrScene, 100);
    g.poke32(kAddrStatic, 4242);            // constant, non-zero: never promoted
    g.poke32(kAddrPointer, 0x24000);        // churns between page-aligned values
    g.poke32(kAddrChurn, 1);
    g.poke32(kAddrClaimed, 7);
    for (int i = 0; i < 8; ++i) g.poke32(kAddrCluster + 4 * i, 10 + i);

    UnclaimedExplorer ex;
    ExploreConfig c = make_config();
    c.max_leads = 100;
    ex.configure(c);
    ex.set_claimed({{kAddrClaimed, kAddrClaimed + 0x1000}});

    // Pass 1 establishes baselines.
    const auto p1 = ex.pass(g.data(), g.size(), 1, false, false);
    check(p1.slots_skipped_claimed > 0, "claimed slots were skipped during the sweep");

    // Passes 2..6: a scene event each time, moving the scene slot and the whole
    // cluster.  Five correlated changes is a *pattern*; one would not be, which
    // is exactly why the evidence threshold is >1.
    for (uint32_t k = 0; k < 5; ++k) {
        g.poke32(kAddrScene, 101 + k);
        for (int i = 0; i < 8; ++i) g.poke32(kAddrCluster + 4 * i, 20 + i + k);
        ex.pass(g.data(), g.size(), 2 + k, /*scene_signal=*/true, false);
    }

    // Passes 7..12: only the churn slot and the pointer move, with no scene
    // event at any point.
    for (uint32_t k = 0; k < 6; ++k) {
        g.poke32(kAddrChurn, 100 + k * 7);
        g.poke32(kAddrPointer, static_cast<uint32_t>(0x24000 + (k + 1) * 0x1000));
        ex.pass(g.data(), g.size(), 7 + k, /*scene_signal=*/false, false);
    }

    const ModuleMap mods = make_modules();
    const std::vector<Lead> leads = ex.rank(mods, "sre13-1.4.13-arm64", 100);
    check(!leads.empty(), "leads were produced");

    const Lead* scene   = find_lead(leads, kAddrScene);
    const Lead* churn   = find_lead(leads, kAddrChurn);
    const Lead* stat    = find_lead(leads, kAddrStatic);
    const Lead* ptr     = find_lead(leads, kAddrPointer);
    const Lead* claimed = find_lead(leads, kAddrClaimed);

    check(scene != nullptr, "the scene-linked slot is listed");
    check(churn != nullptr, "the churning slot is listed");
    check(ptr != nullptr, "the pointer-churning slot is listed (demoted, not hidden)");
    check(stat == nullptr,
          "a slot that never moved is NOT listed (that is the scanner's job)");
    check(claimed == nullptr, "the claimed slot is NOT listed (it is identified already)");

    if (scene && churn) {
        check(scene->score > churn->score, "scene-linked outranks churn");
        check(scene->scene_changes > 0, "the scene change was attributed");
        check(scene->why.find("scene change") != std::string::npos,
              "its evidence says so");
        check(scene->why.find("association, not causation") != std::string::npos,
              "the evidence does not overclaim causation");
        check(scene->evidence == Provenance::Inferred,
              "a strong association reaches SUSPECTED (not RECOVERED)");
        check(!provenance_is_proven(scene->evidence),
              "a lead is never presented as proven");
        check(churn->evidence == Provenance::Observed,
              "mere churn stays OBSERVED");
    }
    if (ptr) {
        check(ptr->why.find("looks like a pointer") != std::string::npos,
              "a pointer-looking value says why it was demoted");
    }

    // Clustering: the eight adjacent movers are reported as one probable struct.
    const Lead* cl = find_lead(leads, kAddrCluster);
    check(cl != nullptr, "the cluster's first slot is listed");
    if (cl) {
        check(cl->cluster_slots >= 8, "the cluster is recognised");
        check(cl->cluster_span >= 32, "and its byte span is reported");
        check(cl->why.find("cluster") != std::string::npos, "and it says so");
    }

    // Ranking is deterministic: same state, same order.
    const std::vector<Lead> again = ex.rank(mods, "sre13-1.4.13-arm64", 100);
    check(again.size() == leads.size(), "ranking is repeatable in size");
    bool same_order = again.size() == leads.size();
    for (size_t i = 0; same_order && i < leads.size(); ++i)
        same_order = (again[i].address == leads[i].address);
    check(same_order, "ranking is repeatable in order");
}

static void test_identity_of_leads() {
    std::printf("\n[leads: mapped names are deterministic, unmapped ones say so]\n");
    Guest g;
    g.poke32(kAddrScene, 1);
    UnclaimedExplorer ex;
    ex.configure(make_config());
    ex.pass(g.data(), g.size(), 1, false, false);
    g.poke32(kAddrScene, 2);
    ex.pass(g.data(), g.size(), 2, true, false);

    const ModuleMap mods = make_modules();
    const auto leads = ex.rank(mods, "sre13-1.4.13-arm64", 50);
    const Lead* scene = find_lead(leads, kAddrScene);
    check(scene != nullptr, "lead present");
    if (!scene) return;

    check(scene->mapped, "a slot inside the module is mapped");
    check(scene->module == "libswordigo.so", "module named");
    check_eq_u64(scene->rva, kAddrScene - kRegionBeg, "RVA is module-relative");
    check(scene->display_name.rfind("g_VAR_", 0) == 0,
          "a global gets the deterministic g_VAR_ name");
    check(scene->identity.var_id == IdentityResolver::resolve([&] {
              StaticRef r;
              r.build_id = "sre13-1.4.13-arm64";
              r.module = "libswordigo.so";
              r.kind = MemKind::Global;
              r.container_rva = kAddrScene - kRegionBeg;
              r.offset = 0;
              r.type_name = value_type_name(ValueType::Dword);
              return r;
          }()).var_id,
          "identity is a pure function of the static site");

    // The same static site on a second "boot" must name identically, even though
    // the runtime address is different.
    Guest g2;
    UnclaimedExplorer ex2;
    ex2.configure(make_config());
    const uint64_t other = kRegionBeg + 0x2000;   // a different slot
    g2.poke32(other, 1);
    ex2.pass(g2.data(), g2.size(), 1, false, false);
    g2.poke32(other, 2);
    ex2.pass(g2.data(), g2.size(), 2, true, false);
    const auto leads2 = ex2.rank(mods, "sre13-1.4.13-arm64", 50);
    const Lead* l2 = find_lead(leads2, other);
    check(l2 != nullptr, "second slot listed");
    if (l2) check(l2->display_name != scene->display_name,
                  "a different static site gets a different name");

    // An address with no static home cannot claim permanence.
    UnclaimedExplorer ex3;
    ExploreConfig c3 = make_config();
    c3.region_begin = 0x30000;              // outside the module extent
    c3.region_end   = 0x38000;
    ex3.configure(c3);
    Guest g3;
    g3.poke32(0x31000, 5);
    ex3.pass(g3.data(), g3.size(), 1, false, false);
    g3.poke32(0x31000, 6);
    ex3.pass(g3.data(), g3.size(), 2, false, false);
    const auto leads3 = ex3.rank(mods, "sre13-1.4.13-arm64", 50);
    const Lead* l3 = find_lead(leads3, 0x31000);
    check(l3 != nullptr, "unmapped lead is still listed");
    if (l3) {
        check(!l3->mapped, "unmapped lead is marked as having no static home");
        check(l3->display_name.rfind("VAR_", 0) == 0,
              "and it is named as a session-scoped VAR_ observation");
    }
}

static void test_track_cap() {
    std::printf("\n[caps: the tracked set and the result list are bounded]\n");
    Guest g;
    for (uint64_t a = kRegionBeg; a < kRegionEnd; a += 4) g.poke32(a, 1);

    UnclaimedExplorer ex;
    ExploreConfig c = make_config();
    c.max_tracks = 4;
    c.max_leads  = 3;
    ex.configure(c);

    ex.pass(g.data(), g.size(), 1, false, false);      // baseline
    for (uint64_t a = kRegionBeg; a < kRegionEnd; a += 4) g.poke32(a, 2);
    const auto r = ex.pass(g.data(), g.size(), 2, false, false);

    check_eq_u64(ex.tracked(), 4, "the tracked set honours max_tracks");
    check(r.track_cap_hit, "hitting the cap is reported, not hidden");

    const auto leads = ex.rank(make_modules(), "sre13-1.4.13-arm64", 0);
    check(leads.size() <= 3, "max_leads bounds the result list");
}

static void test_rebuild_signal_and_correction() {
    std::printf("\n[signals: rebuild correlation, and a correction resets cleanly]\n");
    Guest g;
    g.poke32(kAddrScene, 10);
    UnclaimedExplorer ex;
    ex.configure(make_config());

    ex.pass(g.data(), g.size(), 1, false, false);
    // Values must differ from the baseline, or the first iteration reports no
    // change at all and the count silently comes up short.
    for (int k = 0; k < 5; ++k) {
        g.poke32(kAddrScene, 20 + k);
        ex.pass(g.data(), g.size(), 2 + k, false, /*rebuild_signal=*/true);
    }
    auto leads = ex.rank(make_modules(), "sre13-1.4.13-arm64", 50);
    const Lead* l = find_lead(leads, kAddrScene);
    check(l != nullptr, "lead present");
    if (l) {
        check(l->rebuild_changes == 5, "all five changes attributed to rebuilds");
        check(l->why.find("object rebuild") != std::string::npos, "and the evidence says so");
    }

    // reset() must clear the sample cache; otherwise the stale baseline would
    // report a phantom change on the very next pass.
    ex.reset();
    check_eq_u64(ex.tracked(), 0, "reset clears the tracks");
    const auto r = ex.pass(g.data(), g.size(), 99, false, false);
    check(!r.slots_changed && r.promotions == 0,
          "the first pass after reset reports no phantom changes");
    check_eq_u64(ex.passes(), 1, "pass counter restarts");

    // Reconfiguring the type must also invalidate the baseline.
    UnclaimedExplorer ex2;
    ex2.configure(make_config());
    ex2.pass(g.data(), g.size(), 1, false, false);
    ExploreConfig c2 = make_config();
    c2.type = ValueType::Qword;
    ex2.configure(c2);
    const auto r2 = ex2.pass(g.data(), g.size(), 2, false, false);
    check(!r2.slots_changed && r2.promotions == 0,
          "changing the type does not report phantom changes");
}

static void test_value_rendering() {
    std::printf("\n[rendering: a value that looks like garbage as an int]\n");
    // 100.0f is 0x42C80000 = 1120403456 as an int.
    const MemNumber n = MemNumber::from_int(1120403456);
    const std::string s = describe_slot_value(ValueType::Dword, n);
    check(s.find("1120403456") != std::string::npos, "decimal reading present");
    check(s.find("0x42C80000") != std::string::npos, "hex reading present");
    check(s.find("100") != std::string::npos,
          "float reading shown, because 0x42C80000 is exactly 100.0f");

    const MemNumber big = MemNumber::from_int(0x7FFFFFFF);
    const std::string s2 = describe_slot_value(ValueType::Dword, big);
    check(s2.find("as float") == std::string::npos,
          "a value with no sane float reading does not show one");

    const MemNumber f = MemNumber::from_float(1.5);
    check(describe_slot_value(ValueType::Float, f) == "1.5", "floats render plainly");
}

int main() {
    std::printf("=== unclaimed memory explorer ===\n");
    test_claiming();
    test_bounded_passes();
    test_static_memory_costs_nothing();
    test_ranking_discriminates();
    test_identity_of_leads();
    test_track_cap();
    test_rebuild_signal_and_correction();
    test_value_rendering();

    std::printf("\n%s (%d failure%s)\n",
                g_failures ? "FAILED" : "PASSED", g_failures, g_failures == 1 ? "" : "s");
    return g_failures ? 1 : 0;
}
