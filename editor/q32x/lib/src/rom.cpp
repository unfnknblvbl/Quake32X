#include "q32x/rom.h"

#include <algorithm>
#include <cctype>

namespace q32x {

static size_t up(size_t v, size_t step) { return (v + step - 1) / step * step; }

size_t linear_size(const std::vector<RomLevel> &levels, const Bytes &bank)
{
    size_t at = ROM_PAGE;
    if (!bank.empty()) at = up(at + bank.size(), ROM_LINEAR_STEP);
    for (auto &lv : levels) at = up(at + lv.blob.size(), ROM_LINEAR_STEP);
    return at;
}

static bool level_has_demo(const Bytes &blob)
{
    // the LVL_DEMO lump's count (src/level.h)
    const int LVL_DEMO = 28;
    Buf b(true);
    b.data() = blob;
    return int32_t(uint32_t(b.get_at(8 + LVL_DEMO * 8 + 4, 4))) > 0;
}

Bytes build_rom(Bytes prog, const std::vector<RomLevel> &levels, const std::string &version, RomInfo *info,
                const Bytes &bank, bool allow_linear)
{
    if (!version.empty()) {
        std::string memo = ("Version " + version).substr(0, 40);
        memo.resize(40, ' ');
        check(prog.size() >= 0x1C8 + 40, "program too small for a Mega Drive header");
        std::copy(memo.begin(), memo.end(), prog.begin() + 0x1C8);
    }
    {   // the header's game names (domestic and overseas, 48 characters): the levels on it
        std::string title = "QUAKE 32X -";
        for (auto &lv : levels) {
            std::string nm = lv.name.substr(0, 11);
            for (auto &c : nm) c = char(std::toupper(static_cast<unsigned char>(c)));
            if (title.size() + 1 + nm.size() > 48) break;
            title += " " + nm;
        }
        if (levels.empty()) title = "QUAKE 32X";
        title.resize(48, ' ');
        check(prog.size() >= 0x180, "program too small for a Mega Drive header");
        std::copy(title.begin(), title.end(), prog.begin() + 0x120);
        std::copy(title.begin(), title.end(), prog.begin() + 0x150);
    }
    check(prog.size() <= ROM_PAGE, "program too big for the fixed page: " + std::to_string(prog.size()) + " bytes");
    check(levels.size() <= size_t(ROM_MAX_LEVELS), "more than 8 levels");
    Bytes d = prog;
    d.resize(ROM_PAGE, 0xFF);
    struct Dir {
        int first, npages, flags;
        std::string name;
    };
    std::vector<Dir> dirent;
    if (info) info->levels.clear();
    bool uses_lib = false;
    for (auto &lv : levels) uses_lib |= !lv.lib_relocs.empty();
    const bool linear = allow_linear && linear_size(levels, bank) <= ROM_LINEAR_MAX;
    if (uses_lib)
        check(linear, "the cartridge is " + std::to_string(linear_size(levels, bank) / 1024) +
                          " KB, more than 4096 KB: leave out a level, textures or sounds (cartridges are 4 MB at most for now)");
    int bank_page = 0, bank_pages = 0;
    size_t size = 0;
    if (linear) {
        // everything where the SH-2s see it: positions and sizes in 64 KB steps
        // (the levels, then the shared library: the levels' offsets into it
        // stay positive)
        const size_t U = ROM_LINEAR_STEP;
        size_t lib_at = d.size();
        for (auto &lv : levels) lib_at = up(lib_at + lv.blob.size(), U);
        for (auto &lv : levels) {
            int first = int(d.size() / U);
            size_t at = d.size();
            d.insert(d.end(), lv.blob.begin(), lv.blob.end());
            for (uint32_t r : lv.lib_relocs) {
                check(r + 4 <= lv.blob.size(), lv.name + ": a library reference outside the level");
                uint32_t v = uint32_t(d[at + r]) << 24 | uint32_t(d[at + r + 1]) << 16 | uint32_t(d[at + r + 2]) << 8 | d[at + r + 3];
                v += uint32_t(lib_at - at);
                for (int k = 0; k < 4; k++) d[at + r + size_t(k)] = uint8_t(v >> (24 - 8 * k));
            }
            d.resize(up(d.size(), U), 0xFF);
            int npages = int(d.size() / U) - first;
            std::string name = lv.name.substr(0, 11);
            bool demo = level_has_demo(lv.blob);
            dirent.push_back({first, npages, demo ? 1 : 0, name});
            if (info) info->levels.push_back({name, first, npages, lv.blob.size(), demo});
        }
        if (!bank.empty()) {
            bank_page = int(d.size() / U);
            bank_pages = int(up(bank.size(), U) / U);
            d.insert(d.end(), bank.begin(), bank.end());
            d.resize(up(d.size(), U), 0xFF);
        }
        size = ROM_LINEAR_MAX;
        // (a smaller power of two when it fits: 1, 2 or 4 MB)
        while (size / 2 >= d.size() && size > (1u << 20)) size /= 2;
    }
    // the shared sound bank first: every level maps it in after its own pages
    if (!linear && !bank.empty()) {
        bank_pages = int((bank.size() + ROM_PAGE - 1) / ROM_PAGE);
        check(bank_pages < ROM_WINDOW_PAGES, "the sound bank is too big for the window");
        bank_page = int(d.size() / ROM_PAGE);
        d.insert(d.end(), bank.begin(), bank.end());
        d.resize(size_t(bank_page + bank_pages) * ROM_PAGE, 0xFF);
    }
    const int window = ROM_WINDOW_PAGES - bank_pages;
    for (auto &lv : levels) {
        if (linear) break;
        const Bytes &blob = lv.blob;
        int npages = int((blob.size() + ROM_PAGE - 1) / ROM_PAGE);
        check(npages <= window, lv.name + ": " + std::to_string(blob.size()) + " bytes, more than the " +
                                    std::to_string(window * ROM_PAGE / 1024) + " KB window" +
                                    (bank_pages ? " left beside the sound bank" : ""));
        int first = int(d.size() / ROM_PAGE);
        d.insert(d.end(), blob.begin(), blob.end());
        d.resize(size_t(first + npages) * ROM_PAGE, 0xFF);
        std::string name = lv.name.substr(0, 11);
        // flags: 1 = the level has a demo (its LVL_DEMO lump, src/level.h)
        bool has_demo = level_has_demo(blob);
        dirent.push_back({first, npages, has_demo ? 1 : 0, name});
        if (info) info->levels.push_back({name, first, npages, blob.size(), has_demo});
    }
    // whole megabytes, at least 8 MB (some loaders expect a power of two)
    if (!linear) {
        size = 8u << 20;
        while (size < d.size()) size += 1u << 20;
    }
    d.resize(size, 0xFF);
    const char *tag = "LEVELDIR";
    auto end = d.begin() + std::ptrdiff_t(ROM_PAGE);
    auto at = std::search(d.begin(), end, tag, tag + 8);
    check(at != end && std::search(at + 1, end, tag, tag + 8) == end, "no single LEVELDIR in the program");
    size_t o = size_t(at - d.begin());
    Buf w(true);
    w.data() = std::move(d);
    w.put_at(o + 8, dirent.size(), 4);
    for (size_t i = 0; i < dirent.size(); i++) {
        size_t e = o + 12 + 16 * i;
        w.put_at(e, uint8_t(dirent[i].first), 1);
        w.put_at(e + 1, uint8_t(dirent[i].npages), 1);
        w.put_at(e + 2, uint16_t(dirent[i].flags), 2);
        for (size_t k = 0; k < 12; k++) w.put_at(e + 4 + k, k < dirent[i].name.size() ? uint8_t(dirent[i].name[k]) : 0, 1);
    }
    w.put_at(o + 12 + 16 * 8, uint8_t(bank_page), 1);
    w.put_at(o + 12 + 16 * 8 + 1, uint8_t(bank_pages), 1);
    w.put_at(o + 12 + 16 * 8 + 2, uint16_t(linear ? 1 : 0), 2);   // flags: 1 = no mapper, 64 KB steps
    w.put_at(0x1A4, size - 1, 4);
    unsigned s = 0;
    Bytes &dd = w.data();
    for (size_t i = 0x200; i < size; i += 2) s += unsigned(dd[i] << 8 | dd[i + 1]);
    w.put_at(0x18E, s & 0xFFFF, 2);
    if (info) {
        info->size = size;
        info->program = prog.size();
        info->checksum = s & 0xFFFF;
        info->bank_page = bank_page;
        info->bank_pages = bank_pages;
        info->linear = linear;
    }
    return std::move(w.data());
}

}  // namespace q32x
