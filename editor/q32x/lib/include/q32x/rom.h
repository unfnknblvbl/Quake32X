// The cartridge: the program in the first 512 KB page, then each level blob
// from a page boundary (the port of tools/mkrom.py). Sega's standard mapper
// (the Super Street Fighter II one) maps a level's pages into the window.
#pragma once

#include <string>
#include <vector>

#include "q32x/bytes.h"

namespace q32x {

constexpr size_t ROM_PAGE = 512 * 1024;
constexpr int ROM_WINDOW_PAGES = 7;
constexpr int ROM_MAX_LEVELS = 8;

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
};

// version: put in the Mega Drive header's memo field (0x1C8); empty = leave it.
// bank: the shared sound bank (needs game 0.7.0), mapped after each level
Bytes build_rom(Bytes program, const std::vector<RomLevel> &levels, const std::string &version,
                RomInfo *info = nullptr, const Bytes &bank = {});

}  // namespace q32x
