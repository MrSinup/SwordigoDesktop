// elf_sections_test.cpp — sections as the coarse answer to "where is this?".
//
// The section entries in test_real_layout() are the addresses and sizes measures
// straight out of the shipped libswordigo.so 1.4.13 arm64, in the order and with
// the types/flags that image actually has.  The point of the test is the
// consequence: a module-scoped scan for a common 4-byte value produced 12 hits,
// and 8 of them were inside .dynsym and .rela.plt — the emulator's own
// bookkeeping — because the lowest symbol in the image sits at 0x264397, above
// every one of those tables.  That is why a section layer is needed at all.
//
// Pure RAM test.
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "game/research/elf_sections.h"

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

static constexpr uint32_t kPROGBITS = 1, kSTRTAB = 3, kRELA = 4, kNOBITS = 8,
                          kDYNSYM = 11, kGNU_HASH = 0x6ffffff6;
static constexpr uint64_t kW = 0x1, kA = 0x2, kX = 0x4;

static SectionEntry sec(uint64_t a, uint64_t s, uint32_t t, uint64_t f, const char* n) {
    SectionEntry e; e.addr = a; e.size = s; e.type = t; e.flags = f; e.name = n; return e;
}

// The real 1.4.13 arm64 image layout.
static std::vector<SectionEntry> real_layout() {
    return {
        sec(0x2F8,    0x68328,  kDYNSYM,   kA,           ".dynsym"),
        sec(0x8E7B8,  0x109414, kSTRTAB,   kA,           ".dynstr"),
        sec(0x197BD0, 0x740A0,  kRELA,     kA,           ".rela.dyn"),
        sec(0x20BC70, 0x1AC88,  kRELA,     kA,           ".rela.plt"),
        sec(0x259B00, 0x1A0B4,  kPROGBITS, kA,           ".rodata"),
        sec(0x28EF50, 0x92A40,  kPROGBITS, kA,           ".eh_frame"),
        sec(0x321990, 0x2E1278, kPROGBITS, kA | kX,      ".text"),
        sec(0x6189E0, 0x25ED0,  kPROGBITS, kA | kW,      ".data.rel.ro"),
        sec(0x63EBA0, 0x3780,   kPROGBITS, kA | kW,      ".got"),
        sec(0x64F210, 0xB18,    kPROGBITS, kA | kW,      ".data"),
        sec(0x64FD30, 0x3D30,   kNOBITS,   kA | kW,      ".bss"),
        sec(0x0,      0x0,      kGNU_HASH, kA,           ".gnu.hash"),
    };
}

static void test_real_layout() {
    std::printf("\n--- the shipped image's real layout ---\n");
    SectionMap sm;
    sm.set("libswordigo.so", real_layout());

    check(sm.has("libswordigo.so"), "sections indexed");
    check(sm.count("libswordigo.so") == 12, "twelve sections");
    // Five metadata sections (dynsym, dynstr, rela.dyn, rela.plt, gnu.hash) but
    // only four with an extent, so only four excludable ranges — the zero-size one
    // is counted as metadata yet can never be matched, which is the correct split.
    check(sm.metadata_count("libswordigo.so") == 5,
          "five of them are metadata (dynsym/dynstr/rela.dyn/rela.plt/gnu.hash)");

    // Where the actual scan hits landed.
    check_str(sm.describe("libswordigo.so", 0x31058), ".dynsym+0x30D60",
              "a hit inside the symbol table is named as such");
    check_str(sm.describe("libswordigo.so", 0x639C8), ".dynsym+0x636D0",
              "second hit inside the symbol table");
    check_str(sm.describe("libswordigo.so", 0x20DADC), ".rela.plt+0x1E6C",
              "a hit inside the PLT relocations is named as such");
    check_str(sm.describe("libswordigo.so", 0x25AB38), ".rodata+0x1038",
              "a real data hit is named as read-only data");
    check_str(sm.describe("libswordigo.so", 0x267DE0), ".rodata+0xE2E0",
              "a protobuf constant table hit is named as read-only data");

    // The metadata rule is what makes those hits dismissible.
    check(sm.is_metadata("libswordigo.so", 0x31058), "dynsym is metadata");
    check(sm.is_metadata("libswordigo.so", 0x20DADC), "relocations are metadata");
    check(!sm.is_metadata("libswordigo.so", 0x25AB38), "rodata is not metadata, it is real data");
    check(!sm.is_metadata("libswordigo.so", 0x330000), "code is not metadata");

    check(section_kind_of(kPROGBITS, kA | kX, ".text") == SectionKind::Code, ".text is code");
    check(section_kind_of(kPROGBITS, kA | kW, ".data") == SectionKind::WritableData,
          "writable progbits is data");
    check(section_kind_of(kPROGBITS, kA, ".rodata") == SectionKind::ReadOnlyData,
          "read-only progbits is read-only data");
    check(section_kind_of(kNOBITS, kA | kW, ".bss") == SectionKind::Bss, "nobits is bss");
    check(section_kind_of(kSTRTAB, kA, ".strtab") == SectionKind::StringTable,
          "a string table is a string table by name");
    check(section_kind_of(kGNU_HASH, kA, ".gnu.hash") == SectionKind::Hash,
          "a gnu hash table is a hash table");

    // The excludable ranges are exactly the four metadata blocks.
    const auto rs = sm.metadata_ranges("libswordigo.so");
    check(rs.size() == 4, "four excludable ranges");
    check(rva_in_ranges(rs, 0x31058), "a dynsym hit is inside an excludable range");
    check(rva_in_ranges(rs, 0x20DADC), "a rela hit is inside an excludable range");
    check(!rva_in_ranges(rs, 0x25AB38), "a rodata hit is NOT excluded");
    check(!rva_in_ranges(rs, 0x330000), "a code hit is NOT excluded");

    check(sm.covered_end("libswordigo.so") == 0x64FD30 + 0x3D30,
          "covered end spans to the end of .bss");

    // A zero-size section (.gnu.hash here) must never claim to contain anything:
    // membership for a section with no extent would be a fabrication.
    check(sm.find("libswordigo.so", 0x0) == nullptr,
          "a zero-size section never matches");
    check(!sm.is_metadata("libswordigo.so", 0x0), "and is not classified as metadata either");

    check(!sm.has("libother.so"), "an unregistered module has no sections");
    check_str(sm.describe("libother.so", 0x1000), "",
              "no sections -> no description, rather than a guess");
    check(!sm.is_metadata("libother.so", 0x1000), "unknown region is not called metadata");
}

static void test_no_names() {
    std::printf("\n--- an image that kept no section names ---\n");
    SectionMap sm;
    std::vector<SectionEntry> v;
    v.push_back(sec(0x1000, 0x200, kPROGBITS, kA | kX, ""));
    v.push_back(sec(0x2000, 0x100, kDYNSYM,   kA,      ""));
    sm.set("nostrings.so", v);

    // Without names the description falls back to the kind, which is still
    // honest — and classification still catches the metadata.
    check_str(sm.describe("nostrings.so", 0x1010), "code+0x10",
              "an unnamed code section describes by kind");
    check_str(sm.describe("nostrings.so", 0x2010), "symbol table+0x10",
              "an unnamed symbol table describes by kind");
    check(sm.is_metadata("nostrings.so", 0x2010),
          "metadata is still detected without section names");
    check(!sm.is_metadata("nostrings.so", 0x1010), "code is still not metadata");
}

static void test_raw_build() {
    std::printf("\n--- building from a raw shdr array ---\n");
    // Elf64_Shdr: name(4) type(4) flags(8) addr(8) offset(8) size(8) link(4)...=64B
    struct Shdr64 { uint32_t name, type; uint64_t flags, addr, offset, size;
                    uint32_t link, info; uint64_t align, entsize; };
    const char names[] = "\0.text\0.dynsym\0";
    const size_t n_text = 1, n_sym = 7, names_size = 15;

    std::vector<Shdr64> sh(2);
    std::memset(sh.data(), 0, sh.size() * sizeof(Shdr64));
    sh[0].name = n_text; sh[0].type = kPROGBITS; sh[0].flags = kA | kX;
    sh[0].addr = 0x1000; sh[0].size = 0x400;
    sh[1].name = n_sym;  sh[1].type = kDYNSYM;   sh[1].flags = kA;
    sh[1].addr = 0x3000; sh[1].size = 0x200;

    SectionMap sm;
    check(sm.build("raw.so", sh.data(), sh.size(), names, names_size, true),
          "raw section array built");
    check_str(sm.describe("raw.so", 0x1010), ".text+0x10", "raw-built code section named");
    check_str(sm.describe("raw.so", 0x3010), ".dynsym+0x10", "raw-built dynsym named");
    check(sm.is_metadata("raw.so", 0x3010), "raw-built metadata detected");

    // Names may legitimately be absent (a stripped-of-debug image, a loader that
    // dropped them): the section layer must still work.
    SectionMap noname;
    check(noname.build("raw.so", sh.data(), sh.size(), nullptr, 0, true),
          "builds without a name table");
    check_str(noname.describe("raw.so", 0x3010), "symbol table+0x10",
              "falls back to the kind without names");
    check(noname.is_metadata("raw.so", 0x3010), "metadata detected without names");

    SectionMap bad;
    check(!bad.build("x", nullptr, 2, names, names_size, true), "a null shdr array is refused");
    check(!bad.build("x", sh.data(), 0, names, names_size, true), "a zero section count is refused");
    check(bad.summary().find("no sections") != std::string::npos,
          "an empty map says so in its summary");
}

int main() {
    std::printf("=== elf_sections (where is this address, coarsely) ===\n");
    test_real_layout();
    test_no_names();
    test_raw_build();

    std::printf("\n%s (%d failure%s)\n",
                g_failures ? "FAILED" : "PASSED", g_failures, g_failures == 1 ? "" : "s");
    return g_failures ? 1 : 0;
}
