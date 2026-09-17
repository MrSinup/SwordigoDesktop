#pragma once

#include <string>

namespace build_studio {

enum class BinarySymbolState {
    NotFound,     // Binary does not exist yet (clean install)
    Unstripped,   // .symtab present — Developer build with debug symbols
    Stripped      // .symtab absent — Stripped release build
};

/**
 * Rapidly inspects an ELF binary (<1ms) by parsing its section header table
 * to determine if it contains full symbol information (.symtab / SHT_SYMTAB).
 */
BinarySymbolState check_binary_symbols(const std::string& binary_path);

} // namespace build_studio
