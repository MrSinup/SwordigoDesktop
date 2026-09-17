#include "elf_check.h"
#include <fstream>
#include <vector>
#include <cstring>
#include <cstdint>

namespace build_studio {

// Minimal standard ELF64 definitions for cross-platform parsing
#pragma pack(push, 1)
struct Elf64Header {
    uint8_t  e_ident[16];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint64_t e_entry;
    uint64_t e_phoff;
    uint64_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
};

struct Elf64SectionHeader {
    uint32_t sh_name;
    uint32_t sh_type;
    uint64_t sh_flags;
    uint64_t sh_addr;
    uint64_t sh_offset;
    uint64_t sh_size;
    uint32_t sh_link;
    uint32_t sh_info;
    uint64_t sh_addralign;
    uint64_t sh_entsize;
};
#pragma pack(pop)

constexpr uint32_t SHT_SYMTAB_TYPE = 2; // SHT_SYMTAB

BinarySymbolState check_binary_symbols(const std::string& binary_path) {
    std::ifstream file(binary_path, std::ios::binary);
    if (!file.is_open()) {
        return BinarySymbolState::NotFound;
    }

    Elf64Header ehdr;
    file.read(reinterpret_cast<char*>(&ehdr), sizeof(Elf64Header));
    if (file.gcount() < static_cast<std::streamsize>(sizeof(Elf64Header))) {
        return BinarySymbolState::Stripped;
    }

    // Check ELF magic (\x7fELF)
    if (ehdr.e_ident[0] != 0x7F || ehdr.e_ident[1] != 'E' ||
        ehdr.e_ident[2] != 'L'  || ehdr.e_ident[3] != 'F') {
        return BinarySymbolState::NotFound;
    }

    if (ehdr.e_shoff == 0 || ehdr.e_shnum == 0) {
        // No section headers present — definitely stripped
        return BinarySymbolState::Stripped;
    }

    // Seek to section headers
    file.seekg(ehdr.e_shoff, std::ios::beg);
    std::vector<Elf64SectionHeader> shdrs(ehdr.e_shnum);
    file.read(reinterpret_cast<char*>(shdrs.data()), ehdr.e_shnum * sizeof(Elf64SectionHeader));
    if (file.gcount() < static_cast<std::streamsize>(ehdr.e_shnum * sizeof(Elf64SectionHeader))) {
        return BinarySymbolState::Stripped;
    }

    // Read section header string table
    if (ehdr.e_shstrndx >= ehdr.e_shnum) {
        return BinarySymbolState::Stripped;
    }

    const auto& strtab_hdr = shdrs[ehdr.e_shstrndx];
    std::vector<char> strtab(strtab_hdr.sh_size);
    file.seekg(strtab_hdr.sh_offset, std::ios::beg);
    file.read(strtab.data(), strtab_hdr.sh_size);

    // Look for SHT_SYMTAB (2) or section named ".symtab"
    for (const auto& sh : shdrs) {
        if (sh.sh_type == SHT_SYMTAB_TYPE) {
            return BinarySymbolState::Unstripped;
        }
        if (sh.sh_name < strtab.size()) {
            const char* name = strtab.data() + sh.sh_name;
            if (std::strcmp(name, ".symtab") == 0) {
                return BinarySymbolState::Unstripped;
            }
        }
    }

    return BinarySymbolState::Stripped;
}

} // namespace build_studio
