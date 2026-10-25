#include "q32x/rom.h"

#include <algorithm>

namespace q32x {

Bytes build_rom(Bytes prog, const std::vector<RomLevel> &levels, const std::string &version, RomInfo *info,
                const Bytes &bank)
{
    if (!version.empty()) {
        std::string memo = ("Version " + version).substr(0, 40);
        memo.resize(40, ' ');
        check(prog.size() >= 0x1C8 + 40, "program too small for a Mega Drive header");
        std::copy(memo.begin(), memo.end(), prog.begin() + 0x1C8);
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
    // the shared sound bank first: every level maps it in after its own pages
    int bank_page = 0, bank_pages = 0;
    if (!bank.empty()) {
        bank_pages = int((bank.size() + ROM_PAGE - 1) / ROM_PAGE);
        check(bank_pages < ROM_WINDOW_PAGES, "the sound bank is too big for the window");
        bank_page = int(d.size() / ROM_PAGE);
        d.insert(d.end(), bank.begin(), bank.end());
        d.resize(size_t(bank_page + bank_pages) * ROM_PAGE, 0xFF);
    }
    const int window = ROM_WINDOW_PAGES - bank_pages;
    for (auto &lv : levels) {
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
        const int LVL_DEMO = 28;
        Buf b(true);
        b.data() = blob;
        bool has_demo = int32_t(uint32_t(b.get_at(8 + LVL_DEMO * 8 + 4, 4))) > 0;
        dirent.push_back({first, npages, has_demo ? 1 : 0, name});
        if (info) info->levels.push_back({name, first, npages, blob.size(), has_demo});
    }
    // whole megabytes, at least 8 MB (some loaders expect a power of two)
    size_t size = 8u << 20;
    while (size < d.size()) size += 1u << 20;
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
    }
    return std::move(w.data());
}

}  // namespace q32x
