#pragma once
/* zip_archive.h — shared minimal ZIP reader/writer (store + deflate).
 *
 * Extracted from the old inline `namespace zip` in ruby_cli.cpp so the CLI,
 * the Ruby GG GUI and the APK session backend all link the SAME zip code and
 * can never drift apart (master TODO 4.1). Central-directory driven reader,
 * CRC-32 + deflate writer via zlib. Byte-compatible with system `unzip` for
 * the APK extract path (verified against a 1433-file APK).
 *
 * Layout notes (APK-specific):
 *  - `external_attr` (Unix mode bits, DOS marker) is parsed on read and
 *    written on write so a repack can preserve executable bits; the extractor
 *    drops them by design (matching Python zipfile), so extracted trees are
 *    plain files unless a caller re-applies attrs.
 *  - entries with method 0 (store) pass through verbatim; method 8 is
 *    inflate/deflate via zlib raw streams.
 */

#include <cstdint>
#include <string>
#include <vector>

namespace zip {

// One central-directory entry. `data` is only filled by read_entry().
struct Entry {
    std::string name;
    uint16_t    method = 0;      // 0 = stored, 8 = deflated
    uint32_t    crc32 = 0;
    uint32_t    comp_size = 0;
    uint32_t    uncomp_size = 0;
    uint32_t    external_attr = 0; // Unix mode bits / DOS attr (preserve on repack)
    uint64_t    data_offset = 0;   // start of the compressed payload
    std::string data;              // decompressed payload (read_entry only)
};

// Entry to write: uncompressed payload + storage method.
struct OutEntry {
    std::string name;
    std::string data;      // uncompressed
    uint16_t    method = 8; // 0 store, 8 deflate
    uint32_t    external_attr = 0; // written into the central directory
};

// Read all central-directory entries of `zip_path`. Returns false when the
// file is missing, too small, or has no end-of-central-directory record.
bool read_entries(const std::string& zip_path, std::vector<Entry>& entries);

// Extract entry `e` (raw stored/deflate bytes -> decompressed) from
// `zip_path` into `out`. Returns false on malformed data.
bool read_entry(const std::string& zip_path, const Entry& e, std::string& out);

// Write a ZIP archive containing `entries`. Directory entries are implied by
// the '/' in entry names; callers add explicit directory entries if needed.
bool write_archive(const std::string& out_path,
                   const std::vector<OutEntry>& entries);

// Result of extract_all().
struct ExtractResult {
    bool ok = false;               // false = fatal (not a zip / cannot create dest)
    size_t extracted = 0;          // files written
    std::vector<std::string> failed; // entry names that failed to extract
    std::string error;             // fatal error text (empty when ok)
};

// Extract every entry of `zip_path` into `dest_dir` (created as needed).
// Path-traversal-safe: backslashes normalize to '/', leading "../" segments
// are stripped, absolute and empty names are skipped; directory entries
// create directories. Non-fatal per-entry failures land in `failed`.
ExtractResult extract_all(const std::string& zip_path, const std::string& dest_dir);

} // namespace zip