// Quake demos (protocol 15): the reader (tools/dem.py) and the compact
// stream the cartridge plays back (tools/mkdemo.py, src/playdemo.c).
#pragma once

#include <map>
#include <optional>
#include <string>
#include <vector>

#include "q32x/movers.h"

namespace q32x {

// one server message, decoded; fields by name (numbers as doubles)
struct DemMsg {
    std::string name;
    std::vector<double> num;                 // numeric arguments, in the order of tools/dem.py
    std::vector<std::string> str;            // string arguments
    std::map<std::string, double> fields;    // entity update fields, client data
    std::vector<std::string> models, sounds; // serverinfo
    bool has_num2 = false;                   // spawnstatic has no entity number
};

struct DemFrame {
    float angles[3];
    std::vector<DemMsg> msgs;
};

struct Dem {
    std::string track;
    std::vector<DemFrame> frames;
    static Dem parse(const Bytes &data);
};

struct DemoStream {
    Bytes data;
    int count = 0;
    std::vector<std::string> models, sounds;   // what it needs in the level
    double seconds = 0;
};

DemoStream convert_demo(const Bytes &dem, const std::vector<Mover> &movers, std::string *log = nullptr);

}  // namespace q32x
