// Quake's moving brush entities and triggers, worked out offline from the
// entity lump (the port of tools/movers.py). Shared by the level builder and
// the game data, so both see the same movers in the same order.
#pragma once

#include <optional>
#include <string>
#include <vector>

#include "q32x/quake.h"

namespace q32x {

constexpr int KEY_STEP = 16;
constexpr int NOT_MEDIUM = 512;

extern const std::vector<std::string> MOVER_CLASSES;
extern const std::vector<std::string> TRIGGER_CLASSES;
extern const std::vector<std::string> STATIC_CLASSES;

bool present(const Entity &e);       // single player, normal skill
Vec3 movedir(const Entity &e);
std::vector<Vec3> keys_along(const std::vector<Vec3> &points, int step = KEY_STEP);

using OptStr = std::optional<std::string>;

struct Mover {
    const Entity *ent = nullptr;
    std::string cls;
    int model = 0;
    Vec3 mins{}, maxs{};
    int flags = 0;
    int hull1 = 0, hull2 = 0;
    int sounds = 0;
    OptStr targetname, target, killtarget, message;
    int health = 0;
    double speed = 0, wait = 0, dmg = 0;
    std::vector<Vec3> pos;
    Vec3 start{};
    int items = 0;
    std::vector<Vec3> keys;
    int step = KEY_STEP;
    int group = -1;
    bool has_field = false;
    Vec3 field_min{}, field_max{};
};

struct Trigger {
    std::string cls;
    Vec3 mins{}, maxs{};
    int flags = 0, sounds = 0;
    OptStr targetname, target, killtarget, message;
    int health = 0;
    double wait = 0;
    int count = 0;
    std::optional<Vec3> facing;
    int owner = -1;
    bool has_dest = false;
    Vec3 dest{};
    double dest_yaw = 0;
};

std::vector<Mover> build_movers(const std::vector<Entity> &ents, const Bsp &bsp);
std::vector<Trigger> build_triggers(const std::vector<Entity> &ents, const Bsp &bsp,
                                    const std::vector<Mover> &movers);

}  // namespace q32x
