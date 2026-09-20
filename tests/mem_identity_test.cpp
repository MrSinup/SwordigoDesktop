// mem_identity_test.cpp — deterministic identity for the live memory inspector.
//
// The whole point of this layer: a researcher's findings must survive a reboot,
// a crash and a scene change.  So identity is derived ONLY from static facts
// (build + module + kind + container + offset + type) and never from a runtime
// address.  Two separate "boots" must produce the byte-identical base name and
// var_id for the same static site, while the live *instances* of that site stay
// distinguishable.
//
// Pure RAM test, no guest, no Qt/GL, no SQLite.
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "game/research/mem_identity.h"

using namespace swordfare::research;

static int g_failures = 0;

static void check(bool ok, const char* what) {
    std::printf("  [%s] %s\n", ok ? " OK " : "FAIL", what);
    if (!ok) ++g_failures;
}

static bool is_var_name(const std::string& s) {
    if (s.rfind("VAR_", 0) != 0) return false;
    if (s.size() != 8) return false;
    for (size_t i = 4; i < s.size(); ++i)
        if (!std::isdigit(static_cast<unsigned char>(s[i]))) return false;
    return true;
}

// A mutable version of the same static site, as seen on a second boot.
static StaticRef make_field(const std::string& recovered_field, uint64_t off) {
    StaticRef r;
    r.build_id       = "sre13-1.4.13-arm64";
    r.module         = "libswordigo.so";
    r.kind           = MemKind::StructField;
    r.struct_name    = "GameSceneController";
    r.container_rva  = 0x4253B4;          // the struct's ctor, from the corpus
    r.offset         = off;
    r.type_name      = "float";
    r.recovered_name = recovered_field;
    r.provenance     = recovered_field.empty() ? Provenance::Unknown : Provenance::Recovered;
    return r;
}

int main() {
    // ── 1. Same static site -> same identity, on every boot ─────────────────
    std::printf("--- determinism across boots ---\n");
    StaticRef hp_a = make_field("currentHealth", 0x90);
    StaticRef hp_b = make_field("currentHealth", 0x90);
    Identity id_a = IdentityResolver::resolve(hp_a);
    Identity id_b = IdentityResolver::resolve(hp_b);

    std::printf("  canonical = %s\n  var_id = %016llX  base_name = %s\n",
                id_a.canonical.c_str(),
                (unsigned long long)id_a.var_id, id_a.base_name.c_str());
    check(id_a.var_id == id_b.var_id, "same site -> same var_id across boots");
    check(id_a.base_name == id_b.base_name, "same site -> same base name across boots");
    check(id_a.base_name == "currentHealth", "recovered name used as base name");
    check(id_a.var_id != 0, "var_id is non-zero");

    StaticRef mana = make_field("currentMana", 0x94);
    Identity id_mana = IdentityResolver::resolve(mana);
    check(id_mana.var_id != id_a.var_id, "a different offset is a different identity");
    check(id_mana.base_name == "currentMana", "second recovered field named correctly");

    // ── 2. Discovering a name must NOT change identity ──────────────────────
    std::printf("\n--- discovery does not mutate identity ---\n");
    StaticRef unknown_site = make_field("", 0xB0);
    Identity before = IdentityResolver::resolve(unknown_site);
    unknown_site.recovered_name = "currentEnergy";
    unknown_site.provenance     = Provenance::Recovered;
    Identity after = IdentityResolver::resolve(unknown_site);

    std::printf("  before: %s\n  after : %s (recovered)\n",
                before.base_name.c_str(), after.base_name.c_str());
    check(before.var_id == after.var_id,
          "var_id is unchanged when the name is discovered");
    check(is_var_name(before.base_name), "undiscovered field renders as VAR_<4 digits>");
    check(after.base_name == "currentEnergy", "discovered field picks up its real name");

    // ── 3. Undiscovered naming is deterministic and varied ──────────────────
    std::printf("\n--- undiscovered naming ---\n");
    Identity u1 = IdentityResolver::resolve(make_field("", 0xA0));
    Identity u2 = IdentityResolver::resolve(make_field("", 0xA4));
    Identity u1_again = IdentityResolver::resolve(make_field("", 0xA0));
    std::printf("  +0xA0 -> %s   +0xA4 -> %s\n", u1.base_name.c_str(), u2.base_name.c_str());
    check(u1.base_name == u1_again.base_name, "VAR handle is stable, not random");
    check(u1.base_name != u2.base_name, "distinct offsets get distinct VAR handles");
    check(is_var_name(u1.base_name), "VAR naming shape is VAR_####");

    // Stack locals are named after their function.
    StaticRef stk;
    stk.build_id      = "sre13-1.4.13-arm64";
    stk.module        = "libswordigo.so";
    stk.kind          = MemKind::StackLocal;
    stk.container_rva = 0x156120;
    stk.offset        = 0x28;
    stk.type_name     = "int32";
    Identity stk_id = IdentityResolver::resolve(stk);
    std::printf("  stack -> %s\n", stk_id.base_name.c_str());
    check(stk_id.base_name.rfind("sub_00156120_VAR_", 0) == 0,
          "stack local is named sub_<function RVA>_VAR_####");

    // A global/static site (what a scanner hit in the module range is) must name
    // itself from the module + RVA, and two sites must never collide.
    auto make_global = [](uint64_t rva) {
        StaticRef r;
        r.build_id       = "sre13-1.4.13-arm64";
        r.module         = "libswordigo.so";
        r.kind           = MemKind::Global;
        r.container_rva  = rva;
        r.type_name      = "4 Bytes";
        r.provenance     = Provenance::Observed;
        return r;
    };
    const Identity g1 = IdentityResolver::resolve(make_global(0x31058));
    const Identity g2 = IdentityResolver::resolve(make_global(0x639C8));
    std::printf("  module+0x31058 -> %s   module+0x639C8 -> %s\n",
                g1.base_name.c_str(), g2.base_name.c_str());
    check(g1.base_name.rfind("g_VAR_", 0) == 0, "module site names as g_VAR_####");
    check(g1.base_name != g2.base_name, "two module sites never share a name");
    check(g1.var_id == IdentityResolver::resolve(make_global(0x31058)).var_id,
          "a module site's identity is the same on the next boot");

    // Tier C: no static anchor.  The name must NOT look like a static site,
    // must be stable within a session, and must differ between slots (otherwise
    // every unanchored value in the session would collapse into one row name).
    const std::string c1 = session_scoped_site_name(0x7F42A91320ULL);
    const std::string c2 = session_scoped_site_name(0x7F42A91500ULL);
    std::printf("  unanchored -> %s\n", c1.c_str());
    check(c1.rfind("heap_VAR_", 0) == 0, "unanchored slot is marked as session-scoped");
    check(!is_var_name(c1), "unanchored slot is not presented as a static VAR_#### site");
    check(c1 == session_scoped_site_name(0x7F42A91320ULL),
          "same unanchored slot names identically within a session");
    check(c1 != c2, "two unanchored slots do not collapse into one name");

    // ── 4. Instance tags ────────────────────────────────────────────────────
    std::printf("\n--- instance tags ---\n");
    InstanceTag t;
    t.value = 2726; t.source = TagSource::Session;
    check(IdentityResolver::instance_name("VAR_750", t) == "VAR_750_2726",
          "instance name composes base + _tag");
    InstanceTag none;
    check(IdentityResolver::instance_name("VAR_750", none) == "VAR_750",
          "no tag -> bare base name");
    check(t.suffix_str() == "2726", "tag renders as a 4-digit suffix");

    uint32_t tag_goblin = IdentityResolver::stable_tag_value("goblin_03");
    uint32_t tag_bat    = IdentityResolver::stable_tag_value("bat_07");
    check(tag_goblin == IdentityResolver::stable_tag_value("goblin_03"),
          "provenance-anchored tag is deterministic across boots");
    check(tag_goblin != tag_bat, "different anchors get different tags");
    check(tag_goblin >= 1000 && tag_goblin <= 9999, "anchor tag is in the 4-digit range");

    // ── 5. Multi-instance: one static field, many live copies ───────────────
    std::printf("\n--- multi-instance ---\n");
    LiveMemoryIndex li;
    li.reset_session(0xABCDEF);
    StaticRef vec_x;
    vec_x.build_id      = "sre13-1.4.13-arm64";
    vec_x.module        = "libswordigo.so";
    vec_x.kind          = MemKind::StructField;
    vec_x.struct_name   = "SceneObject";
    vec_x.container_rva = 0x100;
    vec_x.offset        = 0x80;
    vec_x.type_name     = "Vector2f";
    vec_x.provenance    = Provenance::Inferred;
    Identity vec_id = IdentityResolver::resolve(vec_x);

    std::string n1 = li.bind(0x40000000, vec_x, 1);
    std::string n2 = li.bind(0x41000000, vec_x, 1);
    std::string n3 = li.bind(0x42000000, vec_x, 1);
    std::printf("  3 live copies -> %s | %s | %s\n", n1.c_str(), n2.c_str(), n3.c_str());
    check(li.instance_count_of(vec_id.var_id) == 3, "three instances of one static site");
    check(n1 != n2 && n2 != n3 && n1 != n3, "instances are distinguishable");
    check(n1.rfind(vec_id.base_name + "_", 0) == 0, "instance name extends the base name");
    check(li.all_instances_of(vec_id.var_id).size() == 3, "all_instances_of lists them");
    auto insts = li.all_instances_of(vec_id.var_id);
    check(insts[0].runtime_va < insts[1].runtime_va &&
          insts[1].runtime_va < insts[2].runtime_va, "instances sorted by runtime address");

    // Same runtime address rebinds to the same display name (session-stable).
    check(li.bind(0x40000000, vec_x, 2) == n1, "rebinding the same VA keeps its tag");

    // A provenance-anchored instance is stable across sessions...
    LiveMemoryIndex li2;
    li2.reset_session(0x999999);
    std::string a = li.bind(0x50000000, vec_x, 3, "goblin_03");
    std::string b = li2.bind(0x5A000000, vec_x, 3, "goblin_03");
    std::printf("  anchored: %s vs %s (different session, different address)\n",
                a.c_str(), b.c_str());
    check(a == b, "provenance-anchored instance name survives a new session");
    const LiveMemoryEntry* e = li.find_by_va(0x50000000);
    check(e && e->tag.source == TagSource::Provenance, "anchored tag marked as provenance");

    // ── 6. Session reset: tags change, identity does not ────────────────────
    std::printf("\n--- session reset ---\n");
    std::string before_reset = n1;
    li.reset_session(0x11111111);
    std::string after_reset = li.bind(0x40000000, vec_x, 4);
    std::printf("  %s -> %s\n", before_reset.c_str(), after_reset.c_str());
    check(after_reset != before_reset, "a new session re-tags unanchored instances");
    check(after_reset.rfind(vec_id.base_name + "_", 0) == 0,
          "base identity is untouched by a session reset");

    // ── 7. Lifecycle ────────────────────────────────────────────────────────
    std::printf("\n--- ageing ---\n");
    size_t aged = li.age_out(1000, 100);
    check(aged > 0, "unseen entries age out");
    size_t pruned = li.prune_stale();
    check(pruned == aged, "prune removes exactly the stale entries");
    check(li.find_by_va(0x41000000) == nullptr, "pruned address is gone");

    // ── 8. Persistence across a restart ─────────────────────────────────────
    std::printf("\n--- persistence ---\n");
    const std::string path = "/tmp/swordfare_mem_identity_test/store.tsv";
    std::system("mkdir -p /tmp/swordfare_mem_identity_test");
    {
        IdentityStore st;
        st.remember(before, 1);                 // the undiscovered site, boot 1
        check(st.size() == 1, "identity remembered");
        check(st.rename(before.var_id, "currentEnergy"), "researcher renames the identity");
        check(st.promote(before.var_id, Provenance::Confirmed), "provenance promoted");
        check(!st.promote(before.var_id, Provenance::Observed),
              "provenance never demotes");
        check(st.save(path), "store saved");
    }
    {
        IdentityStore st2;
        check(st2.load(path), "store reloaded after a restart");
        auto rec = st2.find(before.var_id);
        check(rec.has_value(), "identity survived the restart");
        check(rec && rec->user_name == "currentEnergy", "researcher's name survived");
        check(rec && rec->provenance == Provenance::Confirmed, "provenance survived");
        check(st2.effective_base_name(before.var_id) == "currentEnergy",
              "display name prefers the researcher's name");
        check(!st2.find(0xDEADBEEFDEADBEEFULL).has_value(), "unknown id not found");
    }

    std::printf("\n%s (%d failures)\n", g_failures ? "FAILED" : "PASSED", g_failures);
    return g_failures ? 1 : 0;
}
