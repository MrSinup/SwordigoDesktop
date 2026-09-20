// live_memory_engine_test.cpp — Live Object Map, PC/instruction correlation,
// and the .swordfare workspace.
//
// Three things are pinned here, and they are the three claims the whole feature
// rests on:
//
//   1. An object can MOVE in memory while keeping its identity.  The runtime
//      address is the only mutable part of the STATIC→RUNTIME→OBJECT→STRUCT
//      pipeline, and `refresh()` is what rewrites it.
//   2. A memory access can be attributed to a specific static instruction, and
//      that is only ever treated as *evidence* (Observed/Suspected/Correlated),
//      never as proof.
//   3. Research survives a crash.  A rename, a mapping and an observation are
//      keyed by identity, not by address, so reopening after an unclean exit
//      finds the same VAR_750 the researcher was investigating — at whatever
//      new address it now resolves to.
//
// Pure RAM + temp-dir test: no emulator, no Qt, no GPU.
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#include "game/research/live_object_map.h"
#include "game/research/mem_access_trace.h"
#include "game/research/research_workspace.h"

using namespace swordfare::research;
namespace fs = std::filesystem;

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

static void check_str(const std::string& got, const std::string& want, const char* what) {
    const bool ok = (got == want);
    std::printf("  [%s] %s", ok ? " OK " : "FAIL", what);
    if (!ok) std::printf(" (got \"%s\" want \"%s\")", got.c_str(), want.c_str());
    std::printf("\n");
    if (!ok) ++g_failures;
}

// ---------------------------------------------------------------------------
// A synthetic guest: libswordigo at a fixed load base, plus heap above it.
// ---------------------------------------------------------------------------
static constexpr uint64_t kBase    = 0x10000;      // module load base
static constexpr uint64_t kRvaEnd  = 0x800000;     // module image size
static constexpr uint64_t kMemSize = kRvaEnd + kBase;   // flat guest buffer

struct Guest {
    std::vector<uint8_t> bytes;
    Guest() : bytes(kMemSize, 0) {}
    uint8_t* data() { return bytes.data(); }
    uint64_t size() const { return kMemSize; }
    void poke64(uint64_t addr, uint64_t v) { std::memcpy(bytes.data() + addr, &v, 8); }
    void poke32(uint64_t addr, uint32_t v) { std::memcpy(bytes.data() + addr, &v, 4); }
};

static ModuleMap make_modules() {
    ModuleMap m;
    ModuleExtent e;
    e.name      = "libswordigo.so";
    e.base_va   = kBase;
    e.rva_begin = 0;
    e.rva_end   = kRvaEnd;
    e.build_id  = "sre13-1.4.13-arm64";
    m.set(e);
    return m;
}

static ObjectRoot gsc_root() {
    ObjectRoot r;
    r.label       = "GameSceneController";
    r.module      = "libswordigo.so";
    r.pointer_rva = 0x800;          // static slot holding the GSC pointer
    r.byte_size   = 0x400;
    r.kind        = MemKind::HeapInstance;
    r.struct_name = "GameSceneController";
    r.build_id    = "sre13-1.4.13-arm64";
    return r;
}

static std::vector<ObjectField> gsc_fields() {
    std::vector<ObjectField> f;

    ObjectField health;
    health.ref.build_id      = "sre13-1.4.13-arm64";
    health.ref.module        = "libswordigo.so";
    health.ref.kind          = MemKind::StructField;
    health.ref.struct_name   = "GameSceneController";
    health.ref.container_rva = 0x9CD0;
    health.ref.offset        = 0x90;
    health.ref.type_name     = "float";
    health.ref.recovered_name = "currentHealth";
    health.ref.provenance    = Provenance::Recovered;
    health.offset            = 0x90;
    health.width             = 4;
    health.type_name         = "float";
    health.recovered_name    = "currentHealth";
    health.provenance        = Provenance::Recovered;
    f.push_back(health);

    ObjectField unknown;
    unknown.ref.build_id      = "sre13-1.4.13-arm64";
    unknown.ref.module        = "libswordigo.so";
    unknown.ref.kind          = MemKind::StructField;
    unknown.ref.struct_name   = "GameSceneController";
    unknown.ref.container_rva = 0x9CD0;
    unknown.ref.offset        = 0xA0;
    unknown.ref.type_name     = "int32";
    unknown.ref.provenance    = Provenance::Unknown;
    unknown.offset            = 0xA0;
    unknown.width             = 4;
    unknown.type_name         = "int32";
    f.push_back(unknown);

    return f;
}

// ---------------------------------------------------------------------------
static void test_module_map() {
    std::printf("\n[module map: RVA is the identity space]\n");
    ModuleMap m = make_modules();

    std::string mod;
    uint64_t rva = 0;
    check(m.rva_of(kBase + 0x1234, &mod, &rva), "runtime VA normalises");
    check_str(mod, "libswordigo.so", "module resolved");
    check_eq_u64(rva, 0x1234, "RVA recovered");

    uint64_t va = 0;
    check(m.va_of("libswordigo.so", 0x1234, &va), "RVA re-materialises");
    check_eq_u64(va, kBase + 0x1234, "runtime VA recovered");

    // A heap address is not in the image and must say so rather than guess.
    check(!m.rva_of(kBase + kRvaEnd + 0x1000, &mod, &rva), "heap address is not in a module");
    check(!m.va_of("nosuch.so", 0x10, &va), "unknown module refused");
    check(m.describe(kBase + 0x40).rfind("libswordigo.so+", 0) == 0,
          "describe() names the static location");
}

static void test_pointer_path() {
    std::printf("\n[pointer paths: multi-level dereference]\n");
    Guest g;
    g.poke64(kBase + 0x800, 0x21000);        // root pointer
    g.poke64(0x21010, 0x22000);              // level 2

    PointerPath p;
    p.module   = "libswordigo.so";
    p.root_rva = 0x800;
    p.offsets  = {0x10, 0x90};

    const PointerResolution r = resolve_pointer_path(p, make_modules(), g.data(), g.size());
    check(r.ok, "path resolved");
    check_eq_u64(r.address, 0x22090, "final address is base2 + 0x90");
    check_eq_u64(r.hops.size(), 2, "both hops recorded for the UI");

    // A null in the chain is a normal, expected outcome — not a crash.
    g.poke64(0x21010, 0);
    const PointerResolution r2 = resolve_pointer_path(p, make_modules(), g.data(), g.size());
    check(!r2.ok, "null pointer reported as unresolved");
    check(!r2.error.empty(), "and it explains why");

    PointerPath bad = p;
    bad.module = "nosuch.so";
    check(!resolve_pointer_path(bad, make_modules(), g.data(), g.size()).ok,
          "unknown module refused");
}

static void test_object_movement_keeps_identity() {
    std::printf("\n[live object map: an object moves, its identity does not]\n");
    Guest g;
    LiveObjectMap map;
    map.set_modules(make_modules());
    const uint64_t id = map.add_root(gsc_root());
    map.set_fields(id, gsc_fields());
    check(id != 0, "root registered");

    // Frame 1: the game allocates the controller at 0x21000.
    g.poke64(kBase + 0x800, 0x21000);
    g.poke32(0x21090, 0x42C80000);            // 100.0f
    g.poke32(0x210A0, 7);
    check_eq_u64(map.refresh(g.data(), g.size(), 1), 0, "first refresh is not a move");

    const LiveObject* o = map.find(id);
    check(o != nullptr && o->alive, "object is alive");
    if (!o) return;
    check_eq_u64(o->runtime_va, 0x21000, "runtime address resolved from the static slot");
    const uint64_t var_id_before = o->identity.var_id;
    const std::string name_before = o->display_name();
    check(!name_before.empty(), "object has a deterministic name");

    check_eq_u64(map.sample_fields(g.data(), g.size()), 2, "both known fields sampled");
    o = map.find(id);
    check(o->fields[0].display_name() == "currentHealth",
          "recovered field shows its recovered name");
    check(o->fields[1].display_name().rfind("VAR_", 0) == 0,
          "unrecovered field shows a deterministic VAR_ name");
    check(o->fields[1].display_name() != o->fields[0].display_name(),
          "the two fields are not collapsed together");

    // Frame 2: the scene reloads and the controller is rebuilt elsewhere.
    g.poke64(kBase + 0x800, 0x2F000);
    g.poke32(0x2F090, 0x41200000);            // 10.0f
    check_eq_u64(map.refresh(g.data(), g.size(), 2), 1, "the move is detected");

    o = map.find(id);
    check_eq_u64(o->runtime_va, 0x2F000, "runtime address updated");
    check_eq_u64(o->previous_va, 0x21000, "previous address remembered");
    check(o->moved(), "moved() reports the re-resolution");
    check_eq_u64(o->move_count, 1, "one move counted");
    check_eq_u64(o->identity.var_id, var_id_before, "identity unchanged by the move");
    check_str(o->display_name(), name_before, "name unchanged by the move");

    // Fields follow the object, not the address.
    check_eq_u64(map.sample_fields(g.data(), g.size()), 2, "fields re-sampled at the new address");
    o = map.find(id);
    check(o->fields[0].last_value.rfind("10", 0) == 0, "value read from the new location");

    check_eq_u64(map.total_moves(), 1, "map-level move counter");
    check(map.summary().find("1 have moved") != std::string::npos, "summary reports the move");
}

static void test_identity_is_address_independent() {
    std::printf("\n[live object map: identity survives a different load base]\n");
    // Same static site, different boot: a different module load base AND a
    // different heap address (the guest buffer is one address space, so the
    // second boot's image is simply mapped elsewhere inside it).  The identity
    // must be byte-identical.
    static constexpr uint64_t kBase2 = 0x200000;   // second boot's load base
    Guest g1, g3;
    LiveObjectMap m1, m2;

    ModuleMap mods1 = make_modules();
    m1.set_modules(mods1);
    const uint64_t a = m1.add_root(gsc_root());
    m1.set_fields(a, gsc_fields());
    g1.poke64(kBase + 0x800, 0x21000);
    m1.refresh(g1.data(), g1.size(), 1);

    ModuleMap mods2;
    ModuleExtent e;
    e.name = "libswordigo.so"; e.base_va = kBase2; e.rva_begin = 0; e.rva_end = kRvaEnd;
    mods2.set(e);
    m2.set_modules(mods2);
    const uint64_t b = m2.add_root(gsc_root());
    m2.set_fields(b, gsc_fields());
    // Different boot: the image moved, so the slot and the object moved too.
    g3.poke64(kBase2 + 0x800, 0x400000);
    m2.refresh(g3.data(), g3.size(), 1);

    const LiveObject* o1 = m1.find(a);
    const LiveObject* o2 = m2.find(b);
    check(o1 && o2, "both objects resolved");
    if (!o1 || !o2) return;
    check(o1->alive && o2->alive, "both objects alive");
    check_eq_u64(o1->runtime_va, 0x21000, "boot 1 address");
    check_eq_u64(o2->runtime_va, 0x400000, "boot 2 address");
    check(o1->runtime_va != o2->runtime_va, "the two boots really used different addresses");
    check_eq_u64(o1->identity.var_id, o2->identity.var_id,
                 "object identity is identical across boots");
    check_str(o1->display_name(), o2->display_name(),
              "object name is identical across boots");
    check_str(o1->fields[1].display_name(), o2->fields[1].display_name(),
              "unrecovered FIELD name is identical across boots");
}

// ---------------------------------------------------------------------------
static void test_arm64_decoder() {
    std::printf("\n[aarch64 decoder: what instruction touched this?]\n");

    // STR w1, [x0, #0x90]
    const DecodedAccess st = decode_arm64_access(0xB9009001u);
    check(st.ok, "STR W, unsigned offset decoded");
    check(st.is_store && !st.is_load, "recognised as a store");
    check_eq_u64(st.offset, 0x90, "offset 0x90");
    check_eq_u64(st.width, 4, "4 bytes wide");
    check_eq_u64(st.base_reg, 0, "base register x0");
    check(st.mnemonic.find("[x0,#0x90]") != std::string::npos, "mnemonic names the site");

    // LDR x2, [x0, #0x90]
    const DecodedAccess ld = decode_arm64_access(0xF9404802u);
    check(ld.ok && ld.is_load, "LDR X decoded");
    check_eq_u64(ld.offset, 0x90, "scaled offset 0x90 for a 64-bit load");
    check_eq_u64(ld.width, 8, "8 bytes wide");

    // LDRSW x1, [x0, #0x90]
    const DecodedAccess sw = decode_arm64_access(0xB9809001u);
    check(sw.ok && sw.is_load, "LDRSW decoded");
    check_eq_u64(sw.offset, 0x90, "LDRSW offset");

    // STP x29, x30, [sp, #-0x10]!  — the prologue form.
    const DecodedAccess stp = decode_arm64_access(0xA9BF7BFDu);
    check(stp.ok && stp.is_store, "STP decoded");
    check(stp.pre_index, "pre-index recognised");
    check(stp.pair, "pair form flagged");
    check_eq_u64(stp.offset, (uint64_t)-16, "signed offset -16 (two's complement)");
    check_eq_u64(stp.width, 16, "pair transfers 16 bytes");

    // LDR w0, [x1, #-4] — unscaled (LDUR), negative immediate.
    const DecodedAccess unscaled = decode_arm64_access(0xB85FC020u);
    check(unscaled.ok, "unscaled load decoded");
    check(!unscaled.pre_index && !unscaled.post_index, "plain unscaled has no writeback");
    check_eq_u64(unscaled.offset, (uint64_t)-4, "negative unscaled offset preserved");

    // A non-memory instruction must not be guessed at.
    check(!decode_arm64_access(0x91024000u).ok, "ADD is not reported as an access");
    check(!decode_arm64_access(0xD503201Fu).ok, "NOP is not reported as an access");

    // Base-register setup sequence.
    uint8_t reg = 0xFF;
    bool is_move = false;
    check(decode_arm64_setup(0x91024000u, &reg, &is_move), "ADD immediate is a setup");
    check_eq_u64(reg, 0, "ADD writes x0");
    check(!is_move, "ADD is not a move");
    check(decode_arm64_setup(0xAA0103E0u, &reg, &is_move), "MOV register is a setup");
    check_eq_u64(reg, 0, "MOV writes x0");
    check(is_move, "MOV flagged as a move");
    check(decode_arm64_setup(0xD2800200u, &reg, &is_move), "MOVZ is a setup");
    check(!decode_arm64_setup(0x9B000000u, &reg, &is_move), "MUL is not a setup");
}

static void test_access_trace_correlation() {
    std::printf("\n[access trace: instruction evidence, never proof]\n");
    ModuleMap mods = make_modules();
    AccessTrace trace;
    TraceConfig c1;
    c1.max_events = 64; c1.max_candidates = 16; c1.hotspot_minimum = 4;
    trace.configure(c1);
    trace.set_modules(&mods);

    // A static store site: libswordigo.so+0x1840, "STR w1, [x0,#0x90]".
    const uint64_t pc = kBase + 0x1840;
    const DecodedAccess d = decode_arm64_access(0xB9009001u);
    check(d.ok, "instruction decoded for the trace");

    // Three different GameSceneControllers all write the same field.
    const uint64_t bases[3] = {0x21000, 0x2F000, 0x33000};
    for (int round = 0; round < 20; ++round) {
        for (uint64_t b : bases) {
            trace.record(pc, b + d.offset, d.width, AccessKind::Write,
                         b, d.offset, d.base_reg, static_cast<uint64_t>(round + 1),
                         d.mnemonic);
        }
    }
    check_eq_u64(trace.total_events(), 60, "60 accesses recorded");
    check_eq_u64(trace.unmapped_events(), 0, "all PCs were inside a known module");

    check_eq_u64(trace.fold(), 1, "folded into exactly one code site");
    const auto& cands = trace.candidates();
    check(!cands.empty(), "candidate present");
    if (cands.empty()) return;
    check_eq_u64(cands[0].offset, 0x90, "candidate field offset is 0x90");
    check_eq_u64(cands[0].width, 4, "candidate field width is 4");
    check(cands[0].kind == AccessKind::Write, "candidate is a write site");
    check_eq_u64(cands[0].hits, 60, "every access counted");
    check_eq_u64(cands[0].distinct_bases, 3, "three distinct objects observed");
    check(cands[0].evidence == Provenance::Correlated,
          "repeated writes across several objects reached CORRELATED");
    check(!provenance_is_proven(cands[0].evidence),
          "instruction evidence is NOT treated as proven");
    check_eq_u64(cands[0].site_id, code_site_id("libswordigo.so", 0x1840),
                 "site id is a pure function of module + RVA");
    check(cands[0].sample_mnemonic.find("x0,#0x90") != std::string::npos,
          "the decoded instruction is kept as evidence");
    check(cands[0].describe().find("x60") != std::string::npos, "description states the count");

    // A PC outside any module carries no identity, so it is dropped — counted,
    // not silently treated as evidence for something.
    const uint64_t before_unmapped = trace.unmapped_events();
    trace.record(0xDEADBEEF, 0x1234, 4, AccessKind::Write, 0, 0, 0, 1, "");
    check_eq_u64(trace.unmapped_events(), before_unmapped + 1,
                 "unmapped PC counted, not guessed at");

    // Evidence ladder: a single observation stays merely OBSERVED.
    ModuleMap m2 = make_modules();
    AccessTrace t2;
    TraceConfig c2;
    c2.max_events = 16; c2.max_candidates = 8; c2.hotspot_minimum = 4;
    t2.configure(c2);
    t2.set_modules(&m2);
    t2.record(kBase + 0x2000, 0x2100, 4, AccessKind::Read, 0x2000, 0, 0, 1, "LDR");
    t2.fold();
    check(t2.candidates().size() == 1, "one candidate");
    check(t2.candidates()[0].evidence == Provenance::Observed,
          "a single access is only OBSERVED");

    // The ring buffer is bounded.
    AccessTrace t3;
    TraceConfig c3;
    c3.max_events = 8; c3.max_candidates = 4; c3.hotspot_minimum = 2;
    t3.configure(c3);
    t3.set_modules(&m2);
    for (int i = 0; i < 40; ++i)
        t3.record(kBase + 0x3000 + i * 4, 0x4000, 4, AccessKind::Write, 0x4000, 0, 0, 1, "");
    check_eq_u64(t3.events().size(), 8, "event ring stays bounded");
    check_eq_u64(t3.total_events(), 40, "but the total is still reported");

    // The hook installer must refuse rather than fabricate.
    std::string why;
    check(!install_memory_access_hooks(nullptr, &why), "hooks are NOT silently installed");
    check(why.find("per-instruction") != std::string::npos, "and it says exactly why");
}

// ---------------------------------------------------------------------------
static void test_workspace_persistence() {
    std::printf("\n[workspace: research survives a crash]\n");
    const fs::path root = fs::temp_directory_path() /
                          ("swordfare_ws_test_" + std::to_string((unsigned)std::time(nullptr)));
    fs::remove_all(root);

    uint64_t kept_var_id = 0;

    {
        ResearchWorkspace ws;
        std::string err;
        check(ws.open(root.string(), &err), "workspace opened");
        check(fs::exists(root / "builds"), "builds/ created");
        check(fs::exists(root / "research"), "research/ created");
        check(fs::exists(root / "sessions"), "sessions/ created");

        // A researcher names an undiscovered field.
        StaticRef ref;
        ref.build_id      = "sre13-1.4.13-arm64";
        ref.module        = "libswordigo.so";
        ref.kind          = MemKind::StructField;
        ref.struct_name   = "GameSceneController";
        ref.container_rva = 0x9CD0;
        ref.offset        = 0xA0;
        ref.type_name     = "int32";
        const Identity id = IdentityResolver::resolve(ref);
        kept_var_id = id.var_id;

        ws.symbols().remember(id, 1);
        check(ws.symbols().rename(id.var_id, "currentEnergy"), "rename recorded");
        ws.symbols().promote(id.var_id, Provenance::Observed);

        Mapping m;
        m.var_id    = id.var_id;
        m.module    = "libswordigo.so";
        m.rva       = 0x9CD0;
        m.offset    = 0xA0;
        m.width     = 4;
        m.type_name = "int32";
        m.kind      = MemKind::StructField;
        m.container = "GameSceneController";
        ws.set_mapping(m);

        Observation o;
        o.var_id     = id.var_id;
        o.seen_at    = 0;
        o.state      = "OBSERVED";
        o.confidence = 0.63;
        o.what       = "value changes when a chest is opened";
        o.evidence   = "write at libswordigo.so+0x1840";
        ws.add_observation(o);

        check(ws.save_research(&err), "research saved");
        check(fs::exists(root / "research" / "symbols.json"), "symbols.json written");
        check(fs::exists(root / "research" / "mappings.json"), "mappings.json written");
        check(fs::exists(root / "research" / "observations.json"), "observations.json written");

        BuildProfile p;
        p.build_id     = "swordigo_1.4.13_arm64";
        p.game_version = "1.4.13";
        p.abi          = "arm64";
        p.text_size    = 0x6149E0;
        p.notes        = "SRE13";
        BuildProfileModule bm;
        bm.name = "libswordigo.so";
        bm.base_va = kBase; bm.rva_begin = 0; bm.rva_end = 0x6149E0;
        p.modules.push_back(bm);
        check(ws.save_build_profile(p, &err), "build profile saved");

        // Session starts, tracks a row, then the process is killed: no
        // end_session() call, which is exactly the crash case.
        check(ws.begin_session(0xCAFE, &err), "session started");
        AddressList list;
        const uint64_t row = list.add(ref, 0x210A0, ValueType::Dword, "GameSceneController");
        list.set_label(row, "currentEnergy");
        check(ws.checkpoint(list.snapshot(), {}, &err), "checkpoint written");
        check(ws.has_open_session(), "session still open");
    }

    // ── "restart after a crash" ────────────────────────────────────────────
    {
        ResearchWorkspace ws;
        std::string err;
        check(ws.open(root.string(), &err), "workspace reopened");

        check_eq_u64(ws.symbols().size(), 1, "the identity was reloaded");
        const auto rec = ws.symbols().find(kept_var_id);
        check(rec.has_value(), "found by the SAME var_id (not by address)");
        if (rec) {
            check(rec->user_name == "currentEnergy", "researcher's name survived the crash");
            check(rec->provenance == Provenance::Observed, "provenance survived the crash");
            check(rec->base_name.rfind("VAR_", 0) == 0,
                  "the original deterministic identity is preserved alongside the rename");
        }

        const auto mp = ws.mapping(kept_var_id);
        check(mp.has_value(), "mapping survived");
        if (mp) {
            check_eq_u64(mp->rva, 0x9CD0, "static RVA survived");
            check_eq_u64(mp->offset, 0xA0, "field offset survived");
            check(mp->container == "GameSceneController", "container survived");
        }

        const auto obs = ws.observations_for(kept_var_id);
        check_eq_u64(obs.size(), 1, "observation survived");
        if (!obs.empty())
            check(obs[0].what == "value changes when a chest is opened",
                  "observation text survived");

        std::string why = ws.explain(kept_var_id);
        check(why.find("currentEnergy") != std::string::npos, "explain() shows the rename");
        check(why.find("identity unchanged") != std::string::npos,
              "explain() states the identity is untouched by the rename");
        check(why.find("chest") != std::string::npos, "explain() shows the evidence");

        const auto prof = ws.load_build_profile("swordigo_1.4.13_arm64", &err);
        check(prof.has_value(), "build profile reloaded");
        if (prof) {
            check(prof->abi == "arm64", "abi survived");
            check_eq_u64(prof->text_size, 0x6149E0, "text size survived");
            check_eq_u64(prof->modules.size(), 1, "module list survived");
        }

        check(ws.crash_recovered(), "an unclean shutdown is detected");
        const std::string sum = ws.latest_session_summary();
        check(sum.find("did NOT shut down cleanly") != std::string::npos,
              "the summary says the session was not closed cleanly");
        check(sum.find("currentEnergy") != std::string::npos,
              "and it names what the researcher was looking at");
        check(sum.find("1 tracked address") != std::string::npos,
              "and how many addresses were being tracked");
    }

    // ── clean shutdown, then reopen ────────────────────────────────────────
    {
        ResearchWorkspace ws;
        std::string err;
        ws.open(root.string(), &err);
        ws.begin_session(0x1, &err);
        AddressList empty;
        ws.checkpoint(empty.snapshot(), {}, &err);
        check(ws.end_session("closed", &err), "session closed cleanly");
        check(!ws.crash_recovered(), "a clean shutdown is not reported as a crash");
    }

    std::error_code ec;
    fs::remove_all(root, ec);
}

int main() {
    std::printf("=== live memory engine (object map / access trace / workspace) ===\n");
    test_module_map();
    test_pointer_path();
    test_object_movement_keeps_identity();
    test_identity_is_address_independent();
    test_arm64_decoder();
    test_access_trace_correlation();
    test_workspace_persistence();

    std::printf("\n%s (%d failure%s)\n",
                g_failures ? "FAILED" : "PASSED", g_failures, g_failures == 1 ? "" : "s");
    return g_failures ? 1 : 0;
}
