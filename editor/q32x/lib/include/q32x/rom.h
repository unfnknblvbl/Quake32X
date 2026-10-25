// The cartridge: the program in the first 512 KB page, then each level blob
// from a page boundary (the port of tools/mkrom.py). Sega's standard mapper
// (the Super Street Fighter II one) maps a level's pages into the window.
// A cartridge that fits in 4 MB needs no mapper: the SH-2s see all of it,
// so the levels are packed at 64 KB steps and read where they lie.
#pragma once

#include <string>
#include <vector>

#include "q32x/bytes.h"

namespace q32x {

constexpr size_t ROM_PAGE = 512 * 1024;
constexpr int ROM_WINDOW_PAGES = 7;
constexpr int ROM_MAX_LEVELS = 8;
constexpr size_t ROM_LINEAR_MAX = 4u << 20;     // a cartridge with no mapper
constexpr size_t ROM_LINEAR_STEP = 64 * 1024;   // where its levels may start

struct RomLevel {
    std::string name;   // up to 11 characters (the file name without extension)
    Bytes blob;
};

struct RomInfo {
    struct Entry {
        std::string name;
        int first, pages;
        size_t bytes;
        bool demo;
    };
    std::vector<Entry> levels;
    size_t size = 0, program = 0;
    int bank_page = 0, bank_pages = 0;
    unsigned checksum = 0;
    bool linear = false;   // 4 MB with no mapper (first, pages and bank in 64 KB steps)
};

// version: put in the Mega Drive header's memo field (0x1C8); empty = leave it.
// bank: the shared sound bank (needs game 0.7.0), mapped after each level.
// A cartridge that fits in 4 MB is built without the mapper (needs game 0.7.0)
// unless allow_linear is false.
Bytes build_rom(Bytes program, const std::vector<RomLevel> &levels, const std::string &version,
                RomInfo *info = nullptr, const Bytes &bank = {}, bool allow_linear = true);
// the size of the 4 MB layout's contents (more than ROM_LINEAR_MAX: it needs the mapper)
size_t linear_size(const std::vector<RomLevel> &levels, const Bytes &bank);

}  // namespace q32x
