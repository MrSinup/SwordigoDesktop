// mem_scanner_test.cpp — the CE/GG-class scan engine + identity-routed address list.
//
// The scanner is the "find it" half of a memory editor; the address list is the
// "keep it, edit it, freeze it" half.  Both run against a flat synthetic guest
// buffer here (the real one is g_guest_memory), so this is a pure RAM test with
// no emulator, no Qt, no SQLite.
//
// What is actually being pinned:
//   * exact / changed / increased-by / between / AoB / string scanning
//   * Unknown Initial scan seeding, then narrowing by next_scan
//   * every read and write is bounds-checked (nothing walks off the buffer)
//   * the address list refuses a write that would spill into the next field
//   * a rename does NOT change identity (the researcher's label is a label)
//   * a frozen row is restored exactly, and rebinding a moved object keeps
//     identity while changing only the address
//
// Note A(): the scanner deliberately refuses to consider addresses below the
// null page / ELF header, so every test address is biased into the real range.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "game/research/mem_address_list.h"
#include "game/research/mem_identity.h"
#include "game/research/mem_scanner.h"

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

// A synthetic guest address space: sized like a small module image, byte
// addressed exactly like the flat g_guest_memory buffer.
static constexpr uint64_t kMemSize = 1ull << 21;   // 2 MiB
static constexpr uint64_t kBase    = 0x10000;      // policy: skip the null page

static constexpr uint64_t A(uint64_t logical) { return kBase + logical; }

struct Guest {
    std::vector<uint8_t> bytes;

    Guest() : bytes(kMemSize, 0) {}

    uint8_t* data() { return bytes.data(); }
    uint64_t size() const { return kMemSize; }

    template <typename T>
    void poke(uint64_t logical, T v) {
        std::memcpy(bytes.data() + A(logical), &v, sizeof(T));
    }
    template <typename T>
    T peek(uint64_t logical) const {
        T v{};
        std::memcpy(&v, bytes.data() + A(logical), sizeof(T));
        return v;
    }
    void poke_bytes(uint64_t logical, const std::vector<uint8_t>& b) {
        std::memcpy(bytes.data() + A(logical), b.data(), b.size());
    }
};

// Address of the i-th candidate, or 0 when the set is smaller than i+1.
static uint64_t candidate_addr(const MemScanner& sc, size_t i) {
    return i < sc.candidate_count() ? sc.candidates()[i].address : 0;
}

// A deterministic static site in the "module" — used to give address-list rows
// real identities instead of synthetic ones.
static StaticRef field_ref(const char* name, uint64_t off, const char* type) {
    StaticRef r;
    r.build_id       = "sre13-1.4.13-arm64";
    r.module         = "libswordigo.so";
    r.kind           = MemKind::StructField;
    r.struct_name    = "GameSceneController";
    r.container_rva  = 0x4253B4;
    r.offset         = off;
    r.type_name      = type;
    r.recovered_name = name ? name : "";
    // A field only displays its recovered name once it is actually proven.
    r.provenance     = name ? Provenance::Recovered : Provenance::Unknown;
    return r;
}

// A heap object of a shared struct layout — the data-driven Swordigo case where
// one static layout has many simultaneous live copies.
static StaticRef heap_instance_ref() {
    StaticRef r;
    r.build_id      = "sre13-1.4.13-arm64";
    r.module        = "libswordigo.so";
    r.kind          = MemKind::HeapInstance;
    r.container_rva = 0x4253B4;
    r.offset        = 0xA0;
    r.type_name     = "float";
    return r;
}

// ---------------------------------------------------------------------------
static void test_typed_read_write() {
    std::printf("\n[typed accessors]\n");
    Guest g;
    g.poke<uint32_t>(0x100, 0xDEADBEEF);

    MemNumber n;
    check(mem_read_number(g.data(), g.size(), A(0x100), ValueType::Dword, &n), "read dword ok");
    check_eq_u64((uint64_t)n.i, 0xDEADBEEFu, "dword value");
    check(mem_read_number(g.data(), g.size(), A(0x100), ValueType::Byte, &n), "read byte ok");
    check_eq_u64((uint64_t)n.i, 0xEFu, "byte is little-endian low");

    // Out of bounds / truncated reads must fail, not read adjacent memory.
    check(!mem_read_number(g.data(), g.size(), A(kMemSize - 2), ValueType::Dword, &n),
          "read straddling the end is refused");
    check(!mem_read_number(g.data(), g.size(), A(kMemSize), ValueType::Byte, &n),
          "read at exactly end is refused");
    check(!mem_read_number(g.data(), g.size(), 0x40, ValueType::Byte, &n),
          "read below the null-page guard is refused");

    // Type size + float flag plumbing.
    check_eq_u64(value_type_size(ValueType::Qword), 8, "qword size");
    check_eq_u64(value_type_size(ValueType::Str), 0, "string has no fixed size");
    check(value_type_is_float(ValueType::Float), "float flagged as float");
    check(!value_type_is_float(ValueType::Dword), "dword not float");
    check(value_type_is_numeric(ValueType::Dword), "dword numeric");
    check(!value_type_is_numeric(ValueType::Str), "string not numeric");
}

static void test_write_validation() {
    std::printf("\n[transactional writes]\n");
    Guest g;
    std::string err;

    check(mem_write_typed(g.data(), g.size(), A(0x40), ValueType::Dword, "1234", &err),
          "typed write accepted");
    check_eq_u64(g.peek<uint32_t>(0x40), 1234u, "typed write landed");

    err.clear();
    check(mem_write_typed(g.data(), g.size(), A(0x40), ValueType::Dword, "0x1F", &err),
          "hex text accepted");
    check_eq_u64(g.peek<uint32_t>(0x40), 0x1Fu, "hex write landed");

    err.clear();
    check(!mem_write_typed(g.data(), g.size(), A(0x40), ValueType::Dword, "not-a-number", &err),
          "garbage refused");
    check(!err.empty(), "refusal explains itself");
    check_eq_u64(g.peek<uint32_t>(0x40), 0x1Fu, "refused write did not modify memory");

    err.clear();
    check(!mem_write_typed(g.data(), g.size(), A(kMemSize - 1), ValueType::Dword, "1", &err),
          "write off the end refused");

    err.clear();
    check(!mem_write_bytes(g.data(), g.size(), A(kMemSize - 2),
                           std::vector<uint8_t>{1, 2, 3, 4}, &err),
          "raw write off the end refused");

    err.clear();
    check(!mem_write_bytes(g.data(), g.size(), 0x20, std::vector<uint8_t>{1}, &err),
          "write below the null-page guard refused");

    // Floats round-trip through the typed writer.
    err.clear();
    check(mem_write_typed(g.data(), g.size(), A(0x80), ValueType::Float, "1.5", &err),
          "float write accepted");
    const float f = g.peek<float>(0x80);
    check(f > 1.49f && f < 1.51f, "float write landed");
}

static void test_exact_and_narrowing() {
    std::printf("\n[scan: exact + next_scan narrowing]\n");
    Guest g;
    const uint32_t kTarget = 4242;
    g.poke<uint32_t>(0x200, kTarget);
    g.poke<uint32_t>(0x300, kTarget);
    g.poke<uint32_t>(0x400, 1234);

    MemScanner sc;
    ScanValue v;
    v.num = MemNumber::from_int(kTarget);
    ScanOptions o;
    o.type = ValueType::Dword;
    o.scan = ScanType::Exact;

    ScanStats st;
    check_eq_u64(sc.first_scan(g.data(), g.size(), v, o, &st), 2, "exact scan found both copies");
    check_eq_u64(st.bytes_scanned, kMemSize - 0x1000, "scan covered the buffer from the guard up");
    check_eq_u64(st.compared, (kMemSize - 0x1000) / 4, "natural 4-byte alignment scanned every slot");

    // Guest changes one copy; an exact re-scan keeps only the other.
    g.poke<uint32_t>(0x300, 999);
    check_eq_u64(sc.next_scan(g.data(), g.size(), v, o, &st), 1, "next_scan dropped the changed copy");
    check_eq_u64(candidate_addr(sc, 0), A(0x200), "survivor is the untouched address");
}

static void test_changed_and_increased() {
    std::printf("\n[scan: changed / unchanged / increased-by]\n");
    Guest g;
    g.poke<uint32_t>(0x1000, 10);
    g.poke<uint32_t>(0x1100, 10);
    g.poke<uint32_t>(0x1200, 10);

    MemScanner sc;
    ScanValue v;
    v.num = MemNumber::from_int(10);
    ScanOptions o;
    o.type = ValueType::Dword;
    o.scan = ScanType::Exact;
    check_eq_u64(sc.first_scan(g.data(), g.size(), v, o), 3, "seeded all three copies");

    // 0x1000 stays, 0x1100 +5, 0x1200 -5.
    g.poke<uint32_t>(0x1000, 10);
    g.poke<uint32_t>(0x1100, 15);
    g.poke<uint32_t>(0x1200, 5);

    ScanValue d; d.num = MemNumber::from_int(5);
    ScanOptions by = o; by.scan = ScanType::IncreasedBy;
    check_eq_u64(sc.next_scan(g.data(), g.size(), d, by), 1, "increased-by 5 keeps exactly one");
    check_eq_u64(candidate_addr(sc, 0), A(0x1100), "increased-by found the +5 slot");

    // Unchanged: re-seed with everything equal, then move it all.
    MemScanner su;
    g.poke<uint32_t>(0x1000, 10); g.poke<uint32_t>(0x1100, 10); g.poke<uint32_t>(0x1200, 10);
    su.first_scan(g.data(), g.size(), v, o);
    g.poke<uint32_t>(0x1000, 11); g.poke<uint32_t>(0x1100, 15); g.poke<uint32_t>(0x1200, 5);
    ScanOptions un = o; un.scan = ScanType::Unchanged;
    check_eq_u64(su.next_scan(g.data(), g.size(), v, un), 0, "unchanged kept none (all moved)");

    // Changed: same start, now all three moved.
    MemScanner sc2;
    g.poke<uint32_t>(0x1000, 10); g.poke<uint32_t>(0x1100, 10); g.poke<uint32_t>(0x1200, 10);
    sc2.first_scan(g.data(), g.size(), v, o);
    g.poke<uint32_t>(0x1000, 11); g.poke<uint32_t>(0x1100, 15); g.poke<uint32_t>(0x1200, 5);
    ScanOptions ch = o; ch.scan = ScanType::Changed;
    check_eq_u64(sc2.next_scan(g.data(), g.size(), v, ch), 3, "changed kept all three");
}

static void test_unknown_initial() {
    std::printf("\n[scan: unknown initial + decreased]\n");
    Guest g;
    g.poke<uint32_t>(0x40, 7);
    g.poke<uint32_t>(0x80, 7);

    MemScanner sc;
    ScanValue v;                       // no target needed
    ScanOptions o;
    o.type = ValueType::Dword;
    o.scan = ScanType::UnknownInitial;
    check_eq_u64(sc.first_scan(g.data(), g.size(), v, o), (kMemSize - 0x1000) / 4,
                 "unknown-initial seeded every dword slot in range");

    // Everything is 0 except the two 7s; one of them drops to 6.
    g.poke<uint32_t>(0x80, 6);
    ScanOptions dec = o; dec.scan = ScanType::Decreased;
    check_eq_u64(sc.next_scan(g.data(), g.size(), v, dec), 1,
                 "decreased narrowed to the one slot that dropped");
    check_eq_u64(candidate_addr(sc, 0), A(0x80), "and it is the right address");
}

static void test_between_and_bounds() {
    std::printf("\n[scan: between / bigger / smaller]\n");
    Guest g;
    for (int i = 0; i < 10; ++i) g.poke<uint32_t>(0x1000 + 0x10 * i, 100 + i * 10);  // 100..190

    MemScanner sc;
    ScanValue v; v.num = MemNumber::from_int(120); v.num2 = MemNumber::from_int(150);
    ScanOptions o;
    o.type = ValueType::Dword;
    o.scan = ScanType::Between;
    check_eq_u64(sc.first_scan(g.data(), g.size(), v, o), 4, "between 120..150 inclusive = 4");

    ScanValue b; b.num = MemNumber::from_int(170);
    MemScanner s2;
    ScanOptions o2; o2.type = ValueType::Dword; o2.scan = ScanType::BiggerThan;
    check_eq_u64(s2.first_scan(g.data(), g.size(), b, o2), 2, "bigger than 170 = 2");

    // Natural alignment means a 4-byte value is only ever seen at a 4-byte
    // aligned address: no window overlapping it from below may report a match.
    MemScanner s3;
    ScanOptions o3; o3.type = ValueType::Dword; o3.scan = ScanType::BiggerThan;
    o3.range_begin = A(0x1070); o3.range_end = A(0x1090);
    check_eq_u64(s3.first_scan(g.data(), g.size(), b, o3), 1,
                 "a small range still yields no off-lattice windows");

    // A restricted range must only ever look inside it: cut off the last sample.
    MemScanner s4;
    ScanOptions o4; o4.type = ValueType::Dword; o4.scan = ScanType::BiggerThan;
    o4.range_begin = A(0x1000); o4.range_end = A(0x1090);
    size_t in_range = 0;
    const size_t n4 = s4.first_scan(g.data(), g.size(), b, o4);
    for (size_t i = 0; i < s4.candidate_count(); ++i) {
        const uint64_t a = candidate_addr(s4, i);
        if (a >= A(0x1000) && a < A(0x1090)) ++in_range;
    }
    check(in_range == n4, "every hit lies inside the requested range");
    check(n4 > 0 && n4 <= 2, "range-restricted scan still finds the in-range sample");
}

static void test_string_and_aob() {
    std::printf("\n[scan: string + array-of-bytes]\n");
    Guest g;
    const char* name1 = "goblin_03";
    const char* name2 = "bat_07";
    std::memcpy(g.data() + A(0x800), name1, std::strlen(name1) + 1);
    std::memcpy(g.data() + A(0x900), name2, std::strlen(name2) + 1);

    MemScanner sc;
    ScanValue v; v.text = "goblin_03"; v.case_sensitive = true;
    ScanOptions o; o.type = ValueType::Str; o.scan = ScanType::Exact;
    check_eq_u64(sc.first_scan(g.data(), g.size(), v, o), 1, "string scan found the entity name");
    check_eq_u64(candidate_addr(sc, 0), A(0x800), "string hit at the right address");

    // AoB with a wildcard, CE style ("4D 5A ?? ?? 01").
    Guest h;
    h.poke_bytes(0x20, {0x4D, 0x5A, 0x11, 0x22, 0x01});
    h.poke_bytes(0x60, {0x4D, 0x5A, 0x33, 0x44, 0x01});

    ScanValue a;
    a.bytes = {0x4D, 0x5A, 0x00, 0x00, 0x01};
    a.mask  = {1, 1, 0, 0, 1};                 // 0 = wildcard
    ScanOptions o2; o2.type = ValueType::AoB; o2.scan = ScanType::Exact;
    MemScanner s2;
    check_eq_u64(s2.first_scan(h.data(), h.size(), a, o2), 2,
                 "AoB with wildcards matched both (mask skipped the varying bytes)");

    ScanValue a2;
    a2.bytes = {0x4D, 0x5A, 0x11, 0x22, 0x01};   // no mask = all exact
    ScanOptions o3; o3.type = ValueType::AoB; o3.scan = ScanType::Exact;
    MemScanner s3;
    check_eq_u64(s3.first_scan(h.data(), h.size(), a2, o3), 1, "all-exact AoB matched only one");
    check_eq_u64(candidate_addr(s3, 0), A(0x20), "and at the right address");
}

static void test_float_tolerance() {
    std::printf("\n[scan: float tolerance]\n");
    Guest g;
    g.poke<float>(0x200, 1.0f);
    g.poke<float>(0x300, 1.0f + 1e-7f);
    g.poke<float>(0x400, 5.0f);

    MemScanner sc;
    ScanValue v; v.num = MemNumber::from_float(1.0);
    ScanOptions o; o.type = ValueType::Float; o.scan = ScanType::Exact;
    check_eq_u64(sc.first_scan(g.data(), g.size(), v, o), 2,
                 "near-identical floats both matched within tolerance");

    MemScanner s2;
    ScanValue v2; v2.num = MemNumber::from_float(1.0); v2.tolerance = 0.5;
    check_eq_u64(s2.first_scan(g.data(), g.size(), v2, o), 2,
                 "explicit tolerance did not swallow the distant float");
}

static void test_max_results_and_alignment() {
    std::printf("\n[scan: limits + alignment]\n");
    Guest g;
    for (int i = 0; i < 64; ++i) g.poke<uint32_t>(0x1000 + 4 * i, 77);

    MemScanner sc;
    ScanValue v; v.num = MemNumber::from_int(77);
    ScanOptions o; o.type = ValueType::Dword; o.scan = ScanType::Exact;
    o.max_results = 10;
    ScanStats st;
    check_eq_u64(sc.first_scan(g.data(), g.size(), v, o, &st), 10, "max_results caps the result set");
    check(st.truncated, "truncation is reported, not silent");

    // Alignment 8 must only consider addresses that are multiples of 8: the
    // 55 at 0x1004 is invisible, the 55 at 0x1008 is not.
    Guest h;
    h.poke<uint32_t>(0x1004, 55);   // not 8-aligned
    h.poke<uint32_t>(0x1008, 55);   // 8-aligned
    ScanValue v55; v55.num = MemNumber::from_int(55);
    ScanOptions o2; o2.type = ValueType::Dword; o2.scan = ScanType::Exact; o2.alignment = 8;
    MemScanner s2;
    check_eq_u64(s2.first_scan(h.data(), h.size(), v55, o2), 1, "8-alignment skipped the unaligned hit");
    check_eq_u64(candidate_addr(s2, 0), A(0x1008), "aligned hit retained");
}

static void test_format_and_parse() {
    std::printf("\n[scan: value formatting + text parsing]\n");
    Guest g;
    g.poke<uint32_t>(0x10, 1234);
    g.poke<float>(0x20, 2.5f);
    std::memcpy(g.data() + A(0x30), "hello", 6);

    check_eq_u64(std::strtoull(
                     MemScanner::format_value(g.data(), g.size(), A(0x10), ValueType::Dword).c_str(),
                     nullptr, 10),
                 1234, "dword formatted as decimal");

    const std::string fs = MemScanner::format_value(g.data(), g.size(), A(0x20), ValueType::Float);
    check(fs.rfind("2.5", 0) == 0, "float formatted with a decimal point");

    const std::string ss = MemScanner::format_value(g.data(), g.size(), A(0x30), ValueType::Str);
    check(ss == "\"hello\"", "string formatted quoted (so whitespace is visible)");

    ScanValue out;
    check(MemScanner::parse_typed_text(ValueType::Dword, "-42", &out), "parse negative int");
    check_eq_u64((uint64_t)out.num.i, (uint64_t)(int64_t)-42, "negative int preserved exactly");
    check(MemScanner::parse_typed_text(ValueType::Qword, "0xFFFFFFFFFFFFFFFF", &out), "parse 64-bit hex");
    check((uint64_t)out.num.i == 0xFFFFFFFFFFFFFFFFull, "64-bit value not truncated");
    check(MemScanner::parse_typed_text(ValueType::Float, "1.25", &out), "parse float");
    check(out.num.is_float, "parsed float stays in float domain");
    check(!MemScanner::parse_typed_text(ValueType::Dword, "abc", &out), "garbage rejected");
}

// ---------------------------------------------------------------------------
// The edit round-trip.  This exists because the first version of the edit box
// seeded itself from read_value(), which is a RENDERING ("32 (0x20)"), and the
// parser then refused that same string — so every edit silently failed and the
// cell redisplayed the old value, which reads exactly like "writes don't work".
// ---------------------------------------------------------------------------
static void test_edit_round_trip() {
    std::printf("\n[edit: the value in the box must parse back]\n");
    Guest g;
    g.poke<uint32_t>(0x10, 32);

    // A rendering is explicitly NOT round-trippable-looking: it carries two
    // numbers and a suffix, which is fine to read and impossible to guess at.
    const std::string rendered =
        MemScanner::format_value(g.data(), g.size(), A(0x10), ValueType::Dword);
    check(rendered == "32 (0x20)", "format_value renders decimal + hex (a display form)");

    // ...and yet pasting it back must still work, because it is what the table
    // shows and copying it is the operator's first instinct.
    ScanValue out;
    check(MemScanner::parse_typed_text(ValueType::Dword, rendered, &out),
          "a value copied out of the table parses back (suffix tolerated)");
    check_eq_u64((uint64_t)out.num.i, 32, "and parses to the right number");

    // The editable form is the bare number, and it round-trips through the
    // parser by construction.
    const std::string editable = MemScanner::format_editable(
        g.data(), g.size(), A(0x10), ValueType::Dword, ValueDomain::Decimal);
    check(editable == "32", "format_editable is bare (no suffix, no hex twin)");
    check(MemScanner::parse_typed_text(ValueType::Dword, editable, &out),
          "the editable form parses back");

    const std::string as_hex = MemScanner::format_editable(
        g.data(), g.size(), A(0x10), ValueType::Dword, ValueDomain::Hex);
    check(as_hex == "0x20", "the same cell reads as 0x20 in hex mode");
    check(MemScanner::parse_typed_text(ValueType::Dword, as_hex, &out) &&
              (uint64_t)out.num.i == 32,
          "and 0x20 parses back to 32");

    // A byte with the top bit set must not read as a sign-extended 64-bit mess.
    g.poke<uint8_t>(0x40, 0x8C);
    check(MemScanner::format_editable(g.data(), g.size(), A(0x40), ValueType::Byte,
                                      ValueDomain::Hex) == "0x8C",
          "hex byte is masked to the field width (0x8C, not 0xFFFFFFFFFFFFFF8C)");
    check(MemScanner::parse_typed_text(ValueType::Byte, "0xFF", &out), "byte accepts 0xFF");
    check(MemScanner::parse_typed_text(ValueType::Byte, "255", &out), "byte accepts 255");
    check(MemScanner::parse_typed_text(ValueType::Byte, "-1", &out),
          "byte accepts -1 (two's complement is what a freeze actually writes)");

    // One parse, many widths: the point of the fit mask is that a refusal can
    // name what WOULD have worked instead of just saying no.
    const LiteralParse p255 = parse_literal("255");
    check(p255.ok, "255 parses");
    check(p255.fits_type(ValueType::Byte) && p255.fits_type(ValueType::Dword),
          "255 fits byte, word, dword and qword");
    check(p255.narrowest == ValueType::Byte, "narrowest fit is byte");

    const LiteralParse p300 = parse_literal("300");
    check(!p300.fits_type(ValueType::Byte) && p300.fits_type(ValueType::Word),
          "300 does not fit a byte but does fit a word");
    check(p300.fits_desc().find(value_type_name(ValueType::Word)) != std::string::npos,
          "the refusal text names the widths that would have worked");

    // 70000 is a perfectly good Dword — refusing it would be a bug, not a
    // safety feature.  Only a value genuinely wider than the field is refused.
    const LiteralParse p70k = parse_literal("70000");
    check(p70k.fits_type(ValueType::Dword), "70000 fits a dword");
    const LiteralParse pbig = parse_literal("9000000000");
    check(!pbig.fits_type(ValueType::Dword) && pbig.fits_type(ValueType::Qword),
          "9000000000 does not fit a dword but does fit a qword");

    // A float field accepts an integer literal — that is how people write 100.0.
    check(parse_literal("100").fits_as(ValueType::Float),
          "an integer literal is a valid float");

    const LiteralParse pneg = parse_literal("-200");
    check(!pneg.fits_type(ValueType::Byte) && pneg.fits_type(ValueType::Word),
          "-200 does not fit a byte, but does fit a word");

    // The live counterpart shown beside the box.
    check(literal_counterpart("32", ValueDomain::Decimal, ValueType::Dword) == "0x20",
          "decimal input shows its hex counterpart");
    check(literal_counterpart("20", ValueDomain::Hex, ValueType::Dword) == "32",
          "hex input shows its decimal counterpart");
    check(literal_counterpart("nonsense", ValueDomain::Decimal, ValueType::Dword).empty(),
          "unparseable input yields no counterpart (rather than a wrong one)");

    // Auto honours prefixes; an explicit domain does not second-guess you.
    check(parse_literal("0x1F").num.i == 31, "auto honours the 0x prefix");
    check(parse_literal("0b1010").num.i == 10, "auto honours the 0b prefix");
    check(parse_literal("0o17").num.i == 15, "auto honours the 0o prefix");
    check(parse_literal("20", ValueDomain::Hex).num.i == 32,
          "hex domain reads bare digits as hex");
    check(parse_literal("12.5").num.is_float, "a fractional literal stays a float");
    check(!parse_literal("12.5").fits_type(ValueType::Dword),
          "12.5 is never silently truncated to an integer width");
}

// ---------------------------------------------------------------------------
// Scan-combination validity.  The behaviour being pinned is that an unusable
// filter is VISIBLE rather than silently swapped for a different one.
// ---------------------------------------------------------------------------
static void test_scan_combination_validity() {
    std::printf("\n[scan: unusable filter combinations are refused, not swapped]\n");

    check(scan_type_supported(ScanType::Exact, ValueType::Dword, true),
          "Exact is valid on a first scan");
    check(scan_type_supported(ScanType::UnknownInitial, ValueType::Dword, true),
          "Unknown Initial is valid on a first scan");
    check(!scan_type_supported(ScanType::Increased, ValueType::Dword, true),
          "Increased is NOT valid on a first scan (no previous value)");
    check(scan_type_supported(ScanType::Increased, ValueType::Dword, false),
          "Increased IS valid on a narrowing pass");
    check(scan_type_unsupported_reason(ScanType::Changed, ValueType::Dword, true) != nullptr,
          "and there is a reason to show the operator");
    check(scan_type_unsupported_reason(ScanType::Exact, ValueType::Dword, true) == nullptr,
          "a supported filter has no reason attached");

    // Variable-length targets: equality is the only meaningful relation.
    check(scan_type_supported(ScanType::Exact, ValueType::Str, true),
          "a string scan can match exactly");
    check(!scan_type_supported(ScanType::Unchanged, ValueType::Str, false),
          "a string scan cannot be 'unchanged' — there is no ordering or arithmetic");
    check(!scan_type_supported(ScanType::DecreasedBy, ValueType::AoB, false),
          "a byte pattern cannot be 'decreased by'");

    // "All" is a seeding mode, not a persistent type.
    check(scan_type_supported(ScanType::Exact, ValueType::All, true),
          "'All' is meaningful on a first scan");
    check(!scan_type_supported(ScanType::Exact, ValueType::All, false),
          "'All' is not meaningful on a narrowing pass");

    check(scan_type_needs_previous(ScanType::IncreasedBy) &&
              scan_type_needs_previous(ScanType::Unchanged),
          "delta and change filters need a previous value");
    check(!scan_type_needs_previous(ScanType::Exact) &&
              !scan_type_needs_previous(ScanType::Between),
          "equality and range do not");
    check(scan_type_needs_second_value(ScanType::Between) &&
              !scan_type_needs_second_value(ScanType::Exact),
          "only 'Between' takes an upper bound");

    // And the engine says so in its stats, instead of quietly seeding.
    Guest g;
    g.poke<uint32_t>(0x10, 7);
    g.poke<uint32_t>(0x20, 7);
    MemScanner sc;
    ScanOptions opts;
    opts.type  = ValueType::Dword;
    opts.scan  = ScanType::Increased;
    opts.range_begin = A(0);
    opts.range_end   = A(0x100);
    ScanStats st;
    sc.first_scan(g.data(), g.size(), ScanValue{}, opts, &st);
    check(st.filter_ignored, "a first scan reports that its filter was dropped");
    check(st.requested == ScanType::Increased && st.applied == ScanType::UnknownInitial,
          "and names both what was asked for and what ran");

    // A narrowing pass must not report a drop.
    ScanStats st2;
    sc.next_scan(g.data(), g.size(), ScanValue{}, opts, &st2);
    check(!st2.filter_ignored && st2.applied == ScanType::Increased,
          "a narrowing pass runs the filter it was given");
}

// ---------------------------------------------------------------------------
// The whole complaint, end to end: add a row, click edit, type a new value,
// press set, and the guest memory must actually change.
// ---------------------------------------------------------------------------
static void test_address_list_edit_lands() {
    std::printf("\n[address list: the edit actually writes guest memory]\n");
    Guest g;
    g.poke<uint32_t>(0x10, 32);

    AddressList list;
    const uint64_t id = list.add(field_ref("coins", 0x10, "uint32"), A(0x10),
                                 ValueType::Dword, "GameSceneController");

    // Seeding the box from read_value() is the bug; read_editable() is the fix.
    const std::string seeded = list.read_editable(id, g.data(), g.size(),
                                                  ValueDomain::Decimal);
    check(seeded == "32", "the edit box seeds with a bare number, not '32 (0x20)'");

    std::string err;
    check(list.validate_value(id, seeded, ValueDomain::Decimal, &err),
          "the seeded text validates clean");

    check(list.set_value(id, "99", g.data(), g.size(), &err, ValueDomain::Decimal),
          "writing a new decimal value is accepted");
    check_eq_u64(g.peek<uint32_t>(0x10), 99, "guest memory actually changed");
    check(list.read_editable(id, g.data(), g.size(), ValueDomain::Decimal) == "99",
          "and the row reads back the new value");

    // Hex domain write.
    check(list.set_value(id, "0x20", g.data(), g.size(), &err, ValueDomain::Hex),
          "writing 0x20 in hex mode is accepted");
    check_eq_u64(g.peek<uint32_t>(0x10), 32, "hex write lands as 32");

    // The per-row representation sticks, on the read path too.
    list.set_domain(id, ValueDomain::Hex);
    check(list.read_value(id, g.data(), g.size()) == "0x20",
          "a hex-mode row also READS as hex");
    list.set_domain(id, ValueDomain::Decimal);
    check(list.read_value(id, g.data(), g.size()).find("32") != std::string::npos,
          "switching back to decimal reads decimal again");

    // A refused write must leave memory untouched and say why.
    err.clear();
    check(!list.validate_value(id, "9000000000", ValueDomain::Decimal, &err),
          "a value wider than the field is refused");
    check(err.find("fits") != std::string::npos,
          "and the refusal names the widths that would have worked");
    const uint32_t before = g.peek<uint32_t>(0x10);
    check(!list.set_value(id, "9000000000", g.data(), g.size(), &err, ValueDomain::Decimal),
          "the refused write does not execute");
    check_eq_u64(g.peek<uint32_t>(0x10), before, "and memory is provably untouched");

    // The old failure mode, exactly as the operator hit it: paste the rendered
    // cell back in and commit.
    err.clear();
    const std::string pasted = list.read_value(id, g.data(), g.size());
    check(list.set_value(id, pasted, g.data(), g.size(), &err, ValueDomain::Decimal),
          "pasting the table's own rendering ('32 (0x20)') still writes");
    check_eq_u64(g.peek<uint32_t>(0x10), 32, "and lands on the right value");
}

// ---------------------------------------------------------------------------
static void test_address_list_identity() {
    std::printf("\n[address list: identity is not the address]\n");
    AddressList list;

    const uint64_t id = list.add(field_ref("currentHealth", 0x90, "float"), 0x71A50090,
                                 ValueType::Float, "GameSceneController");
    check(id != 0, "row added");
    check_eq_u64(list.size(), 1, "one row");

    const AddressEntry* e = list.find(id);
    check(e != nullptr, "row findable");
    if (!e) return;
    check(e->display_name.rfind("currentHealth", 0) == 0, "known field shows its recovered name");
    check(e->is_known(), "recovered field is proven-tier");
    check_eq_u64(e->runtime_va, 0x71A50090, "address recorded");

    const uint64_t    var_id_before = e->identity.var_id;
    const std::string base_before   = e->identity.base_name;

    // Rename — identity must survive intact (this is the whole point).
    check(list.set_label(id, "player HP"), "rename accepted");
    e = list.find(id);
    check(e->label() == "player HP", "label is what the researcher typed");
    check_eq_u64(e->identity.var_id, var_id_before, "var_id unchanged by rename");
    check(e->identity.base_name == base_before, "underlying base name unchanged by rename");
    check(e->display_name.rfind("currentHealth", 0) == 0, "resolved name still available");

    // The object moves; only the address changes.
    check(list.rebind(id, 0x71C80090), "rebind accepted");
    e = list.find(id);
    check_eq_u64(e->runtime_va, 0x71C80090, "address updated");
    check_eq_u64(e->identity.var_id, var_id_before, "var_id still identical after move");
    check(e->label() == "player HP", "label survived the move");

    // An unrecovered field gets a deterministic VAR_ base name, not a random one.
    const uint64_t u1 = list.add(field_ref(nullptr, 0xA0, "float"), 0x500, ValueType::Float);
    const uint64_t u2 = list.add(field_ref(nullptr, 0xA0, "float"), 0x900, ValueType::Float);
    const AddressEntry* a = list.find(u1);
    const AddressEntry* b = list.find(u2);
    check(a && b, "both unrecovered rows findable");
    if (!a || !b) return;
    check(a->identity.base_name.rfind("VAR_", 0) == 0, "unrecovered field gets a VAR_ base");
    check(a->identity.base_name == b->identity.base_name,
          "same static site -> identical base name everywhere it appears");
    check_eq_u64(a->identity.var_id, b->identity.var_id, "same static site -> same var_id");
    check(!a->is_known(), "unrecovered field is not marked as recovered");

    // The data-driven case: one shared struct layout, many live copies.  The
    // instance tag comes from the identity index, which is the component that
    // knows whether a copy has a provenance anchor or only a session tag.
    LiveMemoryIndex idx;
    const StaticRef  inst = heap_instance_ref();
    const std::string n1 = idx.bind(A(0x300), inst, 1);
    const std::string n2 = idx.bind(A(0x400), inst, 1);
    check(!n1.empty() && !n2.empty(), "both live copies bound to an identity");
    check(n1 != n2, "two live copies of one shared layout are distinguishable");
    check(idx.bind(A(0x300), inst, 2) == n1, "a copy keeps its name across frames");

    const LiveMemoryEntry* e1 = idx.find_by_va(A(0x300));
    const LiveMemoryEntry* e2 = idx.find_by_va(A(0x400));
    check(e1 && e2, "both copies resolvable by address");
    if (!e1 || !e2) return;
    check_eq_u64(e1->identity.var_id, e2->identity.var_id,
                 "same static site -> same identity for both copies");

    // The address list takes the tag from that index, so the two rows are
    // distinguishable while still sharing one deterministic identity.
    const uint64_t h1 = list.add(inst, A(0x300), ValueType::Float, "SceneObject", 0,
                                 e1->tag);
    const uint64_t h2 = list.add(inst, A(0x400), ValueType::Float, "SceneObject", 0,
                                 e2->tag);
    const AddressEntry* p = list.find(h1);
    const AddressEntry* q = list.find(h2);
    check(p && q, "both live copies findable in the address list");
    if (!p || !q) return;
    check_eq_u64(p->identity.var_id, q->identity.var_id, "rows share one identity");
    check(p->display_name != q->display_name, "rows display as distinct instances");
    check(p->display_name == n1 || p->display_name.rfind(n1, 0) == 0,
          "row name matches the name the index assigned");
}

static void test_address_list_write_safety() {
    std::printf("\n[address list: field-spill protection]\n");
    Guest g;
    AddressList list;

    // A float at struct+0x90; the neighbouring field starts at +0x94.
    const uint64_t id = list.add(field_ref("currentHealth", 0x90, "float"), A(0x90),
                                 ValueType::Float, "GameSceneController", A(0x94));
    std::string err;
    check(list.set_value(id, "42.5", g.data(), g.size(), &err), "in-field write allowed");
    check(g.peek<float>(0x90) > 42.4f && g.peek<float>(0x90) < 42.6f, "write landed");

    // A qword write at the same address is 8 bytes wide but the field is only
    // 4 bytes (it ends at +0x94), so it would clobber the neighbouring field and
    // must be refused.
    const uint64_t wide = list.add(field_ref("currentHealth", 0x90, "float"), A(0x90),
                                   ValueType::Qword, "GameSceneController", A(0x94));
    err.clear();
    check(!list.set_value(wide, "1", g.data(), g.size(), &err), "spilling write refused");
    check(!err.empty(), "spill refusal explains itself");

    // Out-of-bounds address is refused regardless of field bounds.
    const uint64_t oob = list.add(field_ref(nullptr, 0x10, "u32"), A(kMemSize - 1),
                                  ValueType::Dword);
    err.clear();
    check(!list.set_value(oob, "1", g.data(), g.size(), &err), "out-of-bounds write refused");
}

static void test_freeze_and_apply() {
    std::printf("\n[address list: freeze applies from the emulator tick]\n");
    Guest g;
    AddressList list;
    const uint64_t id = list.add(field_ref("currentHealth", 0x90, "float"), A(0x90),
                                 ValueType::Float, "GameSceneController");

    std::string err;
    g.poke<float>(0x90, 100.0f);
    check(list.set_frozen(id, true, g.data(), g.size(), "100", &err), "freeze accepted");
    // The guest overwrites the value between ticks…
    g.poke<float>(0x90, 12.0f);
    check_eq_u64(list.apply_freeze(g.data(), g.size()), 1, "freeze restored one row");
    check(g.peek<float>(0x90) > 99.9f && g.peek<float>(0x90) < 100.1f, "frozen value restored");

    // Unfreezing stops the restoration.
    check(list.set_frozen(id, false, g.data(), g.size(), "", &err), "unfreeze accepted");
    g.poke<float>(0x90, 12.0f);
    check_eq_u64(list.apply_freeze(g.data(), g.size()), 0, "unfrozen row is not written");
    check(g.peek<float>(0x90) > 11.9f, "guest value left alone after unfreeze");
}

static void test_filtering_and_groups() {
    std::printf("\n[address list: categorisation]\n");
    AddressList list;
    const uint64_t known = list.add(field_ref("currentHealth", 0x90, "float"), A(0x90),
                                    ValueType::Float, "GameSceneController");
    list.add(field_ref(nullptr, 0xA0, "float"), A(0xA0), ValueType::Float, "GameSceneController");
    list.add(field_ref(nullptr, 0x10, "u32"), A(0x10), ValueType::Dword, "SceneObject");
    list.set_provenance(known, Provenance::Confirmed);

    AddressFilter only_unknown;
    only_unknown.unresolved_only = true;
    check_eq_u64(list.snapshot(only_unknown).size(), 2, "unresolved_only filters to tier B/C");

    AddressFilter group;
    group.group = "SceneObject"; group.has_group = true;
    check_eq_u64(list.snapshot(group).size(), 1, "filter by owning struct");

    AddressFilter typed;
    typed.type = ValueType::Float;
    check_eq_u64(list.snapshot(typed).size(), 2, "filter by value type");

    AddressFilter proven;
    proven.min_provenance = Provenance::Recovered;
    check_eq_u64(list.snapshot(proven).size(), 1, "filter by proven-tier only");

    check_eq_u64(list.groups().size(), 2, "two groups reported");

    check(list.remove(known), "remove works");
    check_eq_u64(list.size(), 2, "row removed");
}

// ---------------------------------------------------------------------------
// The GameGuardian-style workflow: save a shot's offsets, do another search,
// narrow, then walk back to an earlier shot and branch from it.
// ---------------------------------------------------------------------------
static void test_saved_shots() {
    std::printf("\n--- saved shots (multi-shot narrowing) ---\n");
    std::vector<uint8_t> mem(kMemSize, 0);
    auto put = [&](uint64_t a, uint32_t v) {
        std::memcpy(mem.data() + a, &v, sizeof(v));
    };

    // Twelve slots that all read 14, exactly like a "coins" search hitting
    // several unrelated places before the value changes.
    for (int i = 0; i < 12; ++i) put(A(0x100) + i * 8, 14);

    MemScanner sc;
    ScanValue v;
    v.num = MemNumber::from_int(14);
    ScanOptions o;
    o.type = ValueType::Dword;
    o.scan = ScanType::Exact;
    o.range_begin = A(0x100);
    o.range_end   = A(0x400);
    check_eq_u64(sc.first_scan(mem.data(), mem.size(), v, o), 12, "shot 1: twelve slots read 14");
    const size_t shot1 = sc.save_pass(ScanType::Exact, ValueType::Dword, "14", "coins?");
    check_eq_u64(shot1, 1, "shot 1 was saved as pass #1");

    // The researcher gains 136 coins, so the real one now reads 150 and three
    // decoys are left at 14.
    const uint64_t real = A(0x120);
    put(real, 150);

    v.num = MemNumber::from_int(150);
    check_eq_u64(sc.next_scan(mem.data(), mem.size(), v, o), 1,
                 "shot 2: only the slot that actually changed still matches");
    const size_t shot2 = sc.save_pass(ScanType::Exact, ValueType::Dword, "150", "after buying");
    check_eq_u64(shot2, 2, "shot 2 was saved as pass #2");
    check(sc.passes().size() == 2, "both shots are remembered");
    check_eq_u64(sc.passes()[1].from, 12, "shot 2 records that it came from shot 1's 12 offsets");

    // Testing a theory often means going back: restore shot 1 and branch with a
    // different filter instead of starting the whole search over.
    check(sc.restore_pass(shot1), "restore shot 1");
    check_eq_u64(sc.candidate_count(), 12, "shot 1's offset list is back in full");

    // From the restored set, filter by "changed" — the three decoys went
    // 14 -> 14 and the real one went 14 -> 150, so only it changed.
    ScanValue none;
    ScanOptions changed = o;
    changed.scan = ScanType::Changed;
    check_eq_u64(sc.next_scan(mem.data(), mem.size(), none, changed), 1,
                 "re-filtering the restored shot finds the same single offset");
    const ScanCandidate& only = sc.candidates()[0];
    check(only.address == real, "and it is the offset that actually changed");

    // A restored shot is a real baseline: its `previous` values are shot 1's.
    check(sc.restore_pass(shot1), "restore shot 1 a second time");
    bool has_decoy_baseline = false;
    for (const ScanCandidate& c : sc.candidates())
        if (c.address != real && c.previous.i == 14) has_decoy_baseline = true;
    check(has_decoy_baseline, "restored candidates carry the values from that shot");

    check(!sc.restore_pass(99), "restoring a shot that does not exist fails cleanly");
    check(sc.drop_pass(shot2), "dropping a shot works");
    check(sc.passes().size() == 1, "dropped shot is gone");

    // A new first scan keeps the saved shots — that is the point of saving them.
    v.num = MemNumber::from_int(150);
    sc.first_scan(mem.data(), mem.size(), v, o);
    check(sc.passes().size() == 1, "a fresh system scan keeps earlier saved shots");

    sc.clear();
    check(sc.passes().empty(), "reset clears the saved shots");
    check_eq_u64(sc.save_pass(ScanType::Exact, ValueType::Dword, "x"), 1,
                 "shot numbering restarts after a reset");

    // Trimming must never evict the first shot: it is the widest set and the one
    // a researcher branches from most often.
    sc.clear();
    for (int i = 0; i < 5; ++i) {
        v.num = MemNumber::from_int(14);
        sc.first_scan(mem.data(), mem.size(), v, o);
        sc.save_pass(ScanType::Exact, ValueType::Dword, "14");
    }
    check_eq_u64(sc.passes().size(), 5, "five shots saved");
    sc.trim_passes(3);
    check_eq_u64(sc.passes().size(), 3, "trim bounds the number of retained shots");
    check_eq_u64(sc.passes()[0].shot, 1, "trimming keeps the first shot");
    check(sc.restore_pass(1), "the retained first shot is still restorable");
}

int main() {
    std::printf("=== mem_scanner / mem_address_list ===\n");
    test_typed_read_write();
    test_write_validation();
    test_exact_and_narrowing();
    test_changed_and_increased();
    test_unknown_initial();
    test_between_and_bounds();
    test_string_and_aob();
    test_float_tolerance();
    test_max_results_and_alignment();
    test_format_and_parse();
    test_edit_round_trip();
    test_scan_combination_validity();
    test_address_list_edit_lands();
    test_address_list_identity();
    test_address_list_write_safety();
    test_freeze_and_apply();
    test_filtering_and_groups();
    test_saved_shots();

    std::printf("\n%s (%d failure%s)\n",
                g_failures ? "FAILED" : "PASSED", g_failures, g_failures == 1 ? "" : "s");
    return g_failures ? 1 : 0;
}
