#pragma once

#include "protocol_adapter.hpp"
#include "state.hpp"

#include <string>

namespace bedrockai {

class StateReconciler {
public:
    bool apply(const PacketView& packet, WorldSnapshot& state, std::string* error = nullptr);

private:
    bool apply_start_game(const PacketView& packet, WorldSnapshot& state, std::string* error);
    bool apply_move_player(const PacketView& packet, WorldSnapshot& state, std::string* error);
    bool apply_level_chunk(const PacketView& packet, WorldSnapshot& state, std::string* error);
    bool apply_auth_input(const PacketView& packet, WorldSnapshot& state, std::string* error);

    static bool read_vec3(const PacketValue* value, Vec3& out);
    static bool read_rot2(const PacketValue* value, Rot2& out);
    static double distance(const Vec3& a, const Vec3& b);
};

} // namespace bedrockai
