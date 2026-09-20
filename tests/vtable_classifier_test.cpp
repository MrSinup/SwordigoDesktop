// vtable_classifier_test.cpp — object classification address-space contract.
//
// Regression test for the RVA/VA mismatch: an object's vptr is a RELOCATED VA
// (libswordigo is mapped at 0x1000000) while every known-vtable key — the
// compiled-in seeds and the recovery catalog's vtables.vtable_rva — is a
// module-relative RVA.  classify() used to compare the two directly, so no
// object could ever be classified.  The test builds synthetic guest memory in
// which a relocated vptr must match an RVA-keyed table entry.
//
// It also covers the RTTI strategy, which classifies an object from its
// type_info name with no table entry at all, and the bounded-read / rejection
// paths.
//
// Pure RAM test: no guest, no GL, no Qt.  Skips (exit 77) without SQLite,
// because RecoveryCatalog is compiled in.
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "game/research/vtable_classifier.h"

using namespace swordfare::research;

static int g_failures = 0;

static void check(bool ok, const char* what) {
    std::printf("  [%s] %s\n", ok ? " OK " : "FAIL", what);
    if (!ok) ++g_failures;
}

// 48 MiB of guest memory is enough to span the libswordigo window the
// classifier reasons about ([0x1000000, 0x3000000)).
static constexpr uint64_t kMemSize = 0x3000000;
static constexpr uint64_t kBase    = 0x1000000;

static std::vector<uint8_t> g_mem;

static void w64(uint64_t va, uint64_t v) {
    std::memcpy(g_mem.data() + va, &v, 8);
}
static void wstr(uint64_t va, const char* s) {
    std::memcpy(g_mem.data() + va, s, std::strlen(s) + 1);
}

// Lay out a plausible Itanium-ABI object:
//   object @ obj_va  ->  vptr == vtable_va
//   vtable_va - 8    ->  type_info*
//   type_info + 8    ->  mangled name string
// Both the vtable and the typeinfo live in the module image.
static void make_object(uint64_t obj_va, uint64_t vtable_rva,
                        const char* mangled_name) {
    const uint64_t vtable_va = kBase + vtable_rva;
    w64(vtable_va, kBase + 0x3000);          // slot 0: some code pointer
    const uint64_t ti_va = kBase + vtable_rva + 0x10000;
    w64(vtable_va - 8, ti_va);               // RTTI pointer before the vptr
    const uint64_t name_va = ti_va + 0x1000;
    wstr(name_va, mangled_name);
    w64(ti_va + 8, name_va);                 // type_info::__type_name
    w64(obj_va, vtable_va);                  // the object's vptr
}

int main() {
    auto& cat = RecoveryCatalog::instance();
    cat.init("/tmp/swordfare_vtable_classifier_test");
    if (!cat.is_ready()) {
        std::printf("SKIP: recovery catalog not available (no SQLite3?)\n");
        return 77;
    }

    auto& vc = VtableClassifier::instance();
    vc.init();                                  // rebuild against the live catalog
    vc.set_module_window(kBase, kBase + 0x2000000);

    g_mem.assign(kMemSize, 0);

    auto scene_obj = cat.find_struct("SceneObject");
    auto health    = cat.find_struct("HealthComponent");
    if (!scene_obj || !health) {
        std::printf("FAIL: catalog is missing SceneObject/HealthComponent\n");
        return 1;
    }

    // ── 1. Relocated vptr + RVA-keyed table entry (the regression) ──────────
    std::printf("--- relocated vptr vs RVA key ---\n");
    const uint64_t obj1 = 0x2000000;
    make_object(obj1, 0x20000, "N5Caver11SceneObjectE");   // NB: length-prefixed
    vc.register_vtable(0x20000, "SceneObject", scene_obj->id);

    ClassificationResult r1 = vc.classify(obj1, g_mem.data(), kMemSize);
    std::printf("  obj=0x%llX vptr_va=0x%llX vptr_rva=0x%llX -> %s (%s)\n",
                (unsigned long long)obj1,
                (unsigned long long)r1.vptr_va,
                (unsigned long long)r1.vptr_rva,
                r1.struct_name.c_str(), r1.confidence.c_str());
    check(r1.matched, "classify matched");
    check(r1.struct_name == "SceneObject", "classified as SceneObject");
    check(r1.catalog_id == scene_obj->id, "resolved against the recovery catalog");
    check(r1.vptr_rva == 0x20000, "vptr normalized into RVA space");
    check(r1.confidence == "RTTI", "RTTI strategy used");

    // ── 2. Table-only match: vptr in VA space, no usable RTTI ───────────────
    std::printf("\n--- VTable table match without RTTI ---\n");
    const uint64_t obj2 = 0x2100000;
    vc.register_vtable(0x60000, "GameState", -1);
    w64(obj2, kBase + 0x60000);                 // relocated vptr, no RTTI slot
    ClassificationResult r2 = vc.classify(obj2, g_mem.data(), kMemSize);
    std::printf("  vptr_va=0x%llX -> %s (%s)\n",
                (unsigned long long)r2.vptr_va,
                r2.struct_name.c_str(), r2.confidence.c_str());
    check(r2.matched, "VA vptr matched an RVA-keyed entry");
    check(r2.struct_name == "GameState", "classified as GameState");
    check(r2.confidence == "KNOWN", "table strategy used");

    // ── 3. Decrypts an un-relocated (already RVA) vptr too ──────────────────
    const uint64_t obj3 = 0x2200000;
    w64(obj3, 0x60000);                         // raw RVA, no RTTI
    ClassificationResult r3 = vc.classify(obj3, g_mem.data(), kMemSize);
    check(r3.matched && r3.struct_name == "GameState", "raw RVA vptr still matches");

    // ── 4. RTTI classifies with NO table entry (catalog-data independent) ───
    std::printf("\n--- RTTI only (no table entry) ---\n");
    const uint64_t obj4 = 0x2300000;
    make_object(obj4, 0x40000, "N5Caver15HealthComponentE");
    ClassificationResult r4 = vc.classify(obj4, g_mem.data(), kMemSize);
    std::printf("  -> %s (%s) catalog_id=%d\n", r4.struct_name.c_str(),
                r4.confidence.c_str(), r4.catalog_id);
    check(r4.matched, "unregistered vtable classified via RTTI");
    check(r4.struct_name == "HealthComponent", "RTTI name parsed and demangled");
    check(r4.catalog_id == health->id, "RTTI name matched to catalog struct");

    // ── 5. Unknown object is reported as unmatched ──────────────────────────
    std::printf("\n--- unknown ---\n");
    const uint64_t obj5 = 0x2400000;
    w64(obj5, kBase + 0x1F0000);                // a pointer nobody registered
    ClassificationResult r5 = vc.classify(obj5, g_mem.data(), kMemSize);
    check(!r5.matched, "unregistered vptr does not match");
    check(r5.confidence == "UNKNOWN", "confidence UNKNOWN on a miss");
    check(r5.vptr_rva == 0x1F0000, "miss still reports the observed RVA");

    // ── 6. Rejections / bounded reads ───────────────────────────────────────
    std::printf("\n--- bounds ---\n");
    check(!vc.classify(0, g_mem.data(), kMemSize).matched, "null object rejected");
    check(!vc.classify(obj1 + 4, g_mem.data(), kMemSize).matched,
          "misaligned object rejected");
    check(!vc.classify(kMemSize - 4, g_mem.data(), kMemSize).matched,
          "object past the end of memory rejected");
    check(!vc.classify(obj1, nullptr, kMemSize).matched, "null memory rejected");
    // A vptr pointing at the very end of memory must not read out of bounds.
    const uint64_t obj6 = 0x2500000;
    w64(obj6, kMemSize - 1);
    check(!vc.classify(obj6, g_mem.data(), kMemSize).matched,
          "wild vptr near the memory end handled safely");

    // ── 7. Diagnostics ──────────────────────────────────────────────────────
    std::printf("\n--- probe_summary ---\n  %s\n", vc.probe_summary().c_str());
    check(vc.probe_summary().find("vtable keys") != std::string::npos,
          "probe_summary reports the vtable-key sweep");

    std::printf("\n%s (%d failures)\n", g_failures ? "FAILED" : "PASSED", g_failures);
    return g_failures ? 1 : 0;
}
