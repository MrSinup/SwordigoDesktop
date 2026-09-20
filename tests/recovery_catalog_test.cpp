// recovery_catalog_test.cpp — Memory Research catalog schema contract.
//
// The embedded recovery DB (src/game/research/embedded_recovery_db.cpp) keys
// every table by TEXT ids and uses snake_case column names
// (struct_id / size_arm64 / summary / canonical_name / type_name /
//  vtable_rva / rel_kind ...), while the RecoveryCatalog public API exposes
// dense int ids and camelCase struct members.  When those two disagree the
// queries fail at sqlite3_prepare_v2 and every getter returns an EMPTY vector
// with no error — the Catalog tab then renders a blank struct list.
//
// This test pins the contract so that failure mode cannot come back silently:
//   * all 17 structs load, each with a non-empty TEXT key
//   * find_struct() / find_struct_by_id() agree
//   * the GameSceneController field offsets the Live Inspector depends on
//     (hero 0xD8, charController 0xE0, health 0xF0, mana 0xF8) survive
//   * fields / relationships / vtables+slots / proto fields all return rows
//   * row counts match the DB build that ships in this tree
//
// Pure RAM test: no guest, no GL, no Qt.  Skips (exit 77, ctest SKIP) when the
// catalog cannot be initialised, e.g. a build without SQLite3.
#include <cstdio>
#include <string>
#include <vector>

#include "game/research/recovery_catalog.h"

using namespace swordfare::research;

static int g_failures = 0;

static void check(bool ok, const char* what) {
    std::printf("  [%s] %s\n", ok ? " OK " : "FAIL", what);
    if (!ok) ++g_failures;
}

int main() {
    auto& cat = RecoveryCatalog::instance();
    cat.init("/tmp/swordfare_recovery_catalog_test");

    if (!cat.is_ready()) {
        std::printf("SKIP: recovery catalog not available (no SQLite3?)\n");
        return 77;
    }

    std::printf("--- status ---\n%s\n", cat.status_summary().c_str());

    // ── structs ──────────────────────────────────────────────────────────────
    std::printf("\n--- structs ---\n");
    std::vector<CatalogStruct> structs = cat.all_structs();
    std::printf("all_structs() = %zu\n", structs.size());
    check(structs.size() == 17, "17 structs present");

    bool all_keys = true;
    bool ids_dense = true;
    for (size_t i = 0; i < structs.size(); ++i) {
        if (structs[i].key.empty()) all_keys = false;
        if (structs[i].id != static_cast<int>(i)) ids_dense = false;
    }
    check(all_keys, "every struct carries its TEXT key");
    check(ids_dense, "int ids are dense and match all_structs() order");

    for (const auto& s : structs)
        std::printf("  id=%2d %-26s %4u B  %s\n", s.id, s.key.c_str(),
                    s.size_bytes, s.build.c_str());

    // ── lookup round-trip ────────────────────────────────────────────────────
    std::printf("\n--- lookup ---\n");
    auto gsc = cat.find_struct("GameSceneController");
    check(gsc.has_value(), "find_struct(GameSceneController)");
    if (!gsc) return 1;

    auto by_id = cat.find_struct_by_id(gsc->id);
    check(by_id.has_value() && by_id->key == gsc->key && by_id->name == gsc->name,
          "find_struct_by_id round-trips");
    check(!gsc->source_file.empty(), "source provenance resolved from recovery_sources");
    check(!gsc->notes.empty(), "summary mapped into notes");
    check(cat.find_struct("NoSuchStructAtAll").has_value() == false,
          "unknown struct returns nullopt");

    // ── fields, including the offsets the Live Inspector reads ───────────────
    std::printf("\n--- GSC fields ---\n");
    std::vector<CatalogField> fields = cat.fields_for_struct(gsc->id);
    std::printf("fields_for_struct(GSC) = %zu\n", fields.size());
    check(fields.size() == 22, "GSC has 22 field rows");
    check(fields.size() > 0 && fields[0].field_name == "vtable",
          "fields ordered by offset_arm64 (vtable first)");

    struct { const char* name; uint32_t off; } kLiveRootOffsets[] = {
        { "hero",             0xD8 },
        { "charController",   0xE0 },
        { "health",           0xF0 },
        { "mana",             0xF8 },
    };
    for (const auto& e : kLiveRootOffsets) {
        char msg[96];
        std::snprintf(msg, sizeof(msg),
                      "field_offset64(GSC,%s) == 0x%X", e.name, e.off);
        auto off = cat.field_offset64("GameSceneController", e.name);
        check(off.has_value() && *off == e.off, msg);
    }

    // ── relationships ────────────────────────────────────────────────────────
    std::printf("\n--- relationships ---\n");
    std::vector<CatalogRelationship> rels = cat.relationships_from(gsc->id);
    std::printf("relationships_from(GSC) = %zu\n", rels.size());
    check(!rels.empty(), "GSC relationships_from returns rows");
    bool names_resolved = true;
    for (const auto& r : rels) {
        if (r.from_name.empty() || r.to_name.empty()) names_resolved = false;
        std::printf("  +0x%03X [%s] %s -> %s\n", r.offset_arm64, r.kind.c_str(),
                    r.from_name.c_str(), r.to_name.c_str());
    }
    check(names_resolved, "relationship endpoints resolve to struct names");
    if (!rels.empty())
        check(!cat.relationships_to(rels[0].to_id).empty(),
              "relationships_to returns rows for a reverse lookup");

    // ── vtables ──────────────────────────────────────────────────────────────
    std::printf("\n--- vtable ---\n");
    auto vt = cat.vtable_for_struct(gsc->id, true);
    check(vt.has_value(), "vtable_for_struct(GSC)");
    if (vt) {
        std::printf("  rva=0x%llX slots=%d iface=%s\n",
                    (unsigned long long)vt->vptr_arm64, vt->slot_count,
                    vt->notes.c_str());
        check(!vt->slots.empty(), "vtable slots loaded");
        check(vt->slot_count == static_cast<int>(vt->slots.size()),
              "slot_count agrees with the slot rows");
        if (!vt->slots.empty())
            check(!vt->slots[0].symbol_name.empty(), "slot symbol names loaded");
    }

    // ── proto fields ─────────────────────────────────────────────────────────
    std::printf("\n--- proto fields ---\n");
    auto cs = cat.find_struct("CharacterState");
    check(cs.has_value(), "find_struct(CharacterState)");
    if (cs) {
        std::vector<CatalogProtoField> proto = cat.proto_fields_for_struct(cs->id);
        std::printf("proto_fields(CharacterState) = %zu\n", proto.size());
        check(!proto.empty(), "proto fields loaded");
        if (!proto.empty()) {
            check(proto[0].tag != 0, "proto tag parsed");
            check(!proto[0].proto_name.empty(), "proto type string preserved");
            std::printf("  tag=%d wire=%s cpp_field=%s (%s)\n",
                        proto[0].tag, proto[0].wire_type.c_str(),
                        proto[0].cpp_field.c_str(), proto[0].proto_name.c_str());
        }
    }

    // ── unsigned / absent data must not masquerade as a query failure ────────
    // GameViewController etc. genuinely have no struct_fields rows.  Those must
    // still resolve as structs (so the browser lists them) with 0 fields.
    std::printf("\n--- structs without field rows ---\n");
    auto gvc = cat.find_struct("GameViewController");
    check(gvc.has_value(), "GameViewController still listed in the browser");
    if (gvc)
        check(cat.fields_for_struct(gvc->id).empty(),
              "GameViewController legitimately has 0 field rows");

    // ── Address-list seeding contract ───────────────────────────────────────
    // The Address List panel auto-populates from the recovered layout of the
    // live roots, keyed by the STRUCT name.  This is exactly the code path that
    // silently produced an empty panel when it looked up the UI labels
    // ("HealthComponent (Hero)") instead of the struct names, and again when
    // ManaComponent was assumed to exist.  Pin both facts here.
    std::printf("\n--- address-list seeding contract ---\n");
    struct RootContract { const char* struct_name; bool has_layout; };
    const RootContract roots[] = {
        {"GameSceneController",     true},
        {"SceneObject",             true},   // the Hero root
        {"HealthComponent",         true},
        {"ManaComponent",           false},  // genuinely absent from the DB
        {"CharControllerComponent", true},
    };
    for (const auto& r : roots) {
        const auto st = cat.find_struct(r.struct_name);
        char msg[192];
        if (!r.has_layout) {
            std::snprintf(msg, sizeof(msg),
                          "%s is absent from the DB (documented recovery gap)", r.struct_name);
            check(!st.has_value(), msg);
            continue;
        }
        std::snprintf(msg, sizeof(msg), "%s resolves by struct name", r.struct_name);
        check(st.has_value(), msg);
        if (!st) continue;

        const auto fields = cat.fields_for_struct(st->id);
        std::snprintf(msg, sizeof(msg), "%s has a seedable layout (%zu field(s))",
                      r.struct_name, fields.size());
        check(!fields.empty(), msg);

        // A field is only seedable if it has a real offset and a known width;
        // otherwise the seeder skips it and the panel looks emptier than it is.
        size_t seedable = 0;
        for (const auto& f : fields) {
            if (f.offset_arm64 == 0) continue;            // vptr slot
            if (f.size_bytes == 0 || f.size_bytes > 8) continue;
            ++seedable;
        }
        std::snprintf(msg, sizeof(msg), "%s yields %zu seedable field(s)",
                      r.struct_name, seedable);
        check(seedable > 0, msg);
    }

    // ── diagnostics ──────────────────────────────────────────────────────────
    std::printf("\n--- table_stats ---\n");
    std::vector<RecoveryCatalog::TableStat> stats = cat.table_stats();
    check(stats.size() == 9, "9 embedded tables enumerated");
    bool counts_ok = true;
    for (const auto& t : stats) {
        std::printf("  %-20s %d\n", t.name.c_str(), t.row_count);
        if (t.name == "structs" && t.row_count != 17) counts_ok = false;
        if (t.name == "struct_fields" && t.row_count != 144) counts_ok = false;
    }
    check(counts_ok, "row counts match the shipped DB build");

    std::printf("\n%s (%d failures)\n",
                g_failures ? "FAILED" : "PASSED", g_failures);
    return g_failures ? 1 : 0;
}
