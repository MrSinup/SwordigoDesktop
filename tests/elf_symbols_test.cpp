// elf_symbols_test.cpp — the dynamic symbol table as static-site evidence.
//
// Two separate things are being pinned here:
//
//  1. The mangled-name transform.  The expected values in test_brief_names() are
//     not invented: every mangled input is copied from the real
//     libswordigo.so 1.4.13 arm64 .dynsym, so this is a check against ground
//     truth rather than against my own idea of the ABI.
//  2. The lookup semantics — in particular that an address *between* symbols is
//     reported as being between them, never as belonging to one.
//
// Pure RAM test, no ELF file, no guest, no Qt/GL.
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "game/research/elf_symbols.h"
#include "game/research/mem_identity.h"

using namespace swordfare::research;

static int g_failures = 0;

static void check(bool ok, const char* what) {
    std::printf("  [%s] %s\n", ok ? " OK " : "FAIL", what);
    if (!ok) ++g_failures;
}

static void check_str(const std::string& got, const std::string& want, const char* what) {
    const bool ok = (got == want);
    std::printf("  [%s] %s\n", ok ? " OK " : "FAIL", what);
    if (!ok) std::printf("        got  \"%s\"\n        want \"%s\"\n", got.c_str(), want.c_str());
    if (!ok) ++g_failures;
}

// Mangled names taken verbatim from the shipped binary's .dynsym.
static void test_brief_names() {
    std::printf("\n--- mangled name -> column text (real symbols) ---\n");
    struct Case { const char* mangled; const char* brief; };
    const Case cases[] = {
        {"_ZN5Caver29ProjectileControllerComponent19PerformBindedActionEi",
         "Caver::ProjectileControllerComponent::PerformBindedAction"},
        {"_ZN5Caver5Proto9GameState30kMenuButtonFlashingFieldNumberE",
         "Caver::Proto::GameState::kMenuButtonFlashingFieldNumber"},
        {"_ZN5Caver23BindBackgroundGLContextEv",
         "Caver::BindBackgroundGLContext"},
        {"_ZN5Caver7Matrix412PreTranslateERKNS_7Vector3E",
         "Caver::Matrix4::PreTranslate"},
        {"_ZN5Caver27CharAnimControllerComponent22BlendFromAnimationNodeERKN5boost13intrusive_ptrINS_12AnimKeysNodeEEEf",
         "Caver::CharAnimControllerComponent::BlendFromAnimationNode"},
        {"_ZN5Caver5Proto7MapNode17default_instance_E",
         "Caver::Proto::MapNode::default_instance_"},
        {"_ZTSN5Caver22OfflineAchievementViewE",
         "name of Caver::OfflineAchievementView"},
        {"_ZTIN5Caver30ProfileSelectionViewControllerE",
         "typeinfo for Caver::ProfileSelectionViewController"},
        {"_ZTIN5boost6detail17sp_counted_impl_pIN5Caver12MenuItemSlotEEE",
         "typeinfo for boost::detail::sp_counted_impl_p<Caver::MenuItemSlot>"},
        // A plain C symbol must pass through untouched.
        {"memcpy", "memcpy"},
        {"__cxa_atexit", "__cxa_atexit"},
    };
    for (const Case& c : cases) {
        std::string got = brief_symbol_name(c.mangled);
        std::printf("  %-72s -> %s\n", c.mangled, got.c_str());
        check_str(got, c.brief, "brief name");
    }

    // A name it cannot parse must come back unchanged: a wrong name is worse
    // than a mangled one.
    const std::string weird = "_ZN5CaverZZ";
    check(brief_symbol_name(weird) == weird, "unparsable input is returned unchanged");
    check(brief_symbol_name("") == "", "empty input stays empty");
}

static std::vector<SymbolEntry> make_syms() {
    std::vector<SymbolEntry> v;
    auto add = [&](uint64_t rva, uint64_t size, uint8_t type,
                   const char* name, const char* brief) {
        SymbolEntry e;
        e.rva = rva; e.size = size; e.type = type;
        e.name = name; e.brief = brief;
        v.push_back(e);
    };
    add(0x1000, 0x40, 2, "_ZN5Caver12UpdateGlobalsEv", "Caver::UpdateGlobals");
    add(0x1040, 0x00, 1, "_ZN5Caver5g_FooE",         "Caver::g_Foo");       // no extent
    add(0x1100, 0x80, 2, "_ZN5Caver6Render3DEv",     "Caver::Render3D");
    return v;
}

static void test_lookup() {
    std::printf("\n--- floor / ceil lookup ---\n");
    SymbolTable st;
    st.set("libswordigo.so", make_syms());

    check(st.has("libswordigo.so"), "module is indexed");
    check(st.count("libswordigo.so") == 3, "three symbols indexed");
    check(st.func_count("libswordigo.so") == 2, "two of them are functions");
    check(!st.has("libother.so"), "an unloaded module has no symbols");
    check(st.floor("libother.so", 0x1000) == nullptr, "lookup in an empty module is null");

    // Inside a sized symbol: the floor must be that symbol and it must cover.
    const SymbolEntry* f = st.floor("libswordigo.so", 0x1010);
    check(f && f->brief == "Caver::UpdateGlobals", "floor picks the symbol below");
    check(f && SymbolTable::covers(*f, 0x1010), "an address inside the extent is covered");
    check(f && !SymbolTable::covers(*f, 0x1030 + 0x20),
          "an address past the extent is not covered");

    // The zero-sized symbol: still the nearest below, but never "inside".
    const SymbolEntry* z = st.floor("libswordigo.so", 0x1060);
    check(z && z->brief == "Caver::g_Foo", "a zero-sized data symbol is the floor");
    check(z && !SymbolTable::covers(*z, 0x1060), "a zero-sized symbol covers nothing");

    // Gap between g_Foo (0x1040) and Render3D (0x1100).
    check_str(st.describe_floor("libswordigo.so", 0x1090), "Caver::g_Foo+0x50",
              "static site reads as the last dynamic symbol + offset");
    check_str(st.describe_between("libswordigo.so", 0x1090),
              "Caver::g_Foo+0x50  ..  Caver::Render3D",
              "a gap is reported as being between the two symbols");
    check_str(st.describe_between("libswordigo.so", 0x1010),
              "inside Caver::UpdateGlobals  (0x10 of 0x40)",
              "an address really inside a symbol says so");

    // Edges: before everything, and past everything.
    check(st.floor("libswordigo.so", 0x800) == nullptr,
          "an address below the first symbol has no floor");
    check_str(st.describe_between("libswordigo.so", 0x800),
              "before Caver::UpdateGlobals", "before the first symbol");
    check_str(st.describe_between("libswordigo.so", 0x9000),
              "after Caver::Render3D", "past the last symbol");
    check(st.ceil("libswordigo.so", 0x9000) == nullptr, "no ceil past the end");
    check(st.ceil("libswordigo.so", 0x1000) != nullptr, "ceil above the first symbol");
    check(st.describe_floor("libother.so", 0x1000).empty(),
          "no symbols -> no description, rather than a guess");
}

// The raw-array path is what the loader's pointers feed, so build it the way the
// GUI bridge will: a synthetic 64-bit Elf64_Sym array plus a string table.
static void test_raw_build() {
    std::printf("\n--- building from a raw dynsym array ---\n");
    const char names[] = "\0Caver_A\0Caver_B\0imported_fn\0";
    //                idx: 0        1        2
    const size_t n_a = 1, n_b = 9, n_i = 17, strsz = 29;

    struct Sym64 { uint32_t name; uint8_t info; uint8_t other; uint16_t shndx;
                   uint64_t value; uint64_t size; };
    std::vector<Sym64> syms(4);
    std::memset(syms.data(), 0, syms.size() * sizeof(Sym64));
    syms[0].name = 0;    syms[0].value = 0;        // the mandatory null symbol
    syms[1].name = n_a;  syms[1].value = 0x2000; syms[1].size = 0x30; syms[1].info = 2;
    syms[2].name = n_b;  syms[2].value = 0x1000; syms[2].size = 0x10; syms[2].info = 2;
    syms[3].name = n_i;  syms[3].value = 0;        // undefined/imported
    syms[3].info = 2;

    SymbolTable st;
    check(st.build("libswordigo.so", syms.data(), syms.size(), names, strsz, true),
          "raw table built");

    // The array is deliberately unsorted (0x2000 before 0x1000) so the sort is
    // actually exercised.
    const std::vector<SymbolEntry>* v = st.entries("libswordigo.so");
    check(v && v->size() == 2, "the null and undefined symbols are dropped");
    check(v && (*v)[0].rva == 0x1000 && (*v)[1].rva == 0x2000, "entries are sorted by address");
    check_str(st.describe_floor("libswordigo.so", 0x2008), "Caver_A+0x8",
              "raw-built table describes a site");

    // A null/zero-length string table must be refused, so the table stays empty
    // instead of producing symbols at bogus offsets.
    SymbolTable bad;
    check(!bad.build("x", syms.data(), syms.size(), names, 0, true),
          "an empty string table is refused");
    check(!bad.build("x", nullptr, 4, names, strsz, true),
          "a null symtab is refused");
    check(bad.summary().find("no symbol table") != std::string::npos,
          "an empty table says so in its summary");
}

// The single most important property: symbols are annotation.  Adding, changing
// or removing them must leave every identity exactly as it was.
static void test_identity_is_untouched() {
    std::printf("\n--- symbols must not move any identity ---\n");
    StaticRef ref;
    ref.build_id      = "sre13-1.4.13-arm64";
    ref.module        = "libswordigo.so";
    ref.kind          = MemKind::Global;
    ref.container_rva = 0x31058;
    ref.type_name     = "4 Bytes";
    ref.provenance    = Provenance::Observed;

    const Identity before = IdentityResolver::resolve(ref);

    SymbolTable st;
    std::vector<SymbolEntry> syms;
    SymbolEntry e;
    e.rva = 0x31000; e.size = 0x100; e.type = 2; e.brief = "Caver::SomethingHuge";
    e.name = "_ZN5Caver15SomethingHugeEv";
    syms.push_back(e);
    st.set("libswordigo.so", syms);

    // The symbol covering the site exists now, and the site is described by it...
    check_str(st.describe_between("libswordigo.so", 0x31058),
              "inside Caver::SomethingHuge  (0x58 of 0x100)",
              "the symbol layer describes the site");
    // ...while the identity is byte-identical, because the symbol never reached
    // the key.  A long, build-dependent name in the identity would make the
    // researcher's handle unstable, which is the one thing it must never be.
    const Identity after = IdentityResolver::resolve(ref);
    check(after.var_id == before.var_id, "var_id is unchanged by symbols existing");
    check(after.base_name == before.base_name, "base name is unchanged by symbols existing");
    check(after.base_name.rfind("g_VAR_", 0) == 0, "the handle stays short and stable");

    // And the canonical key itself must not contain a symbol name.
    check(before.canonical.find("Caver") == std::string::npos,
          "the identity key contains no symbol name");
}

int main() {
    std::printf("=== elf_symbols (dynamic symbol evidence) ===\n");
    test_brief_names();
    test_lookup();
    test_raw_build();
    test_identity_is_untouched();

    std::printf("\n%s (%d failure%s)\n",
                g_failures ? "FAILED" : "PASSED", g_failures, g_failures == 1 ? "" : "s");
    return g_failures ? 1 : 0;
}
