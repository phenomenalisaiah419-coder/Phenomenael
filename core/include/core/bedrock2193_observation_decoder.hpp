#pragma once

#include "core/gameplay_control_loop.hpp"
#include "core/transport.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>

namespace bedrockai {

enum class ObservationDecodeDisposition : std::uint8_t {
    Applied,
    PartiallyApplied,
    UnsupportedPacket,
    IgnoredDirection,
    Malformed
};

struct ObservationDecodeReport {
    ObservationDecodeDisposition disposition = ObservationDecodeDisposition::UnsupportedPacket;
    std::uint32_t packet_id = 0;
    std::uint64_t frame_number = 0;
    // Present only when this successfully decoded packet contains a non-zero
    // PlayerInputTick tied to the local player, not an unrelated actor.
    std::optional<std::uint64_t> local_player_input_tick;
    std::string note;
};

// Version-pinned, defensive clientbound decoder for the implemented Bedrock 1.26.50 / 2193 packets.
// Unknown item IDs and opaque user-data blobs are preserved raw; semantic item capabilities are not guessed.
class Bedrock2193ObservationDecoder final : public GameplayObservationSource {
public:
    explicit Bedrock2193ObservationDecoder(bool target_other_players_as_hostile = false);

    bool accept_frame(const TransportFrame& frame,
                      ObservationDecodeReport* report = nullptr,
                      std::string* error = nullptr);
    bool read(GameplayObservation& observation, std::string* error) override;

    void reset();
    void set_gameplay_context(GameMode mode, GameplayGoal goal, std::string server_profile_key);
    void set_target_other_players_as_hostile(bool enabled);
    // A future inventory codec/host mapping can insert accurately decoded slots here.
    void replace_inventory(std::vector<InventoryStackObservation> inventory);
    const GameplayObservation& observation() const { return observation_; }
    std::uint64_t frames_seen() const { return frame_number_; }
    std::uint64_t malformed_frames() const { return malformed_frames_; }
    const std::string& last_error() const { return last_error_; }

private:
    GameplayObservation observation_{};
    bool target_other_players_as_hostile_ = false;
    std::uint64_t frame_number_ = 0;
    std::uint64_t malformed_frames_ = 0;
    std::string last_error_;
    std::unordered_map<std::uint32_t, std::uint64_t> packet_counts_;

    bool decode_start_game(const TransportFrame& frame, ObservationDecodeReport& report, std::string* error);
    bool decode_move_player(const TransportFrame& frame, ObservationDecodeReport& report, std::string* error);
    bool decode_set_health(const TransportFrame& frame, ObservationDecodeReport& report, std::string* error);
    bool decode_set_actor_motion(const TransportFrame& frame, ObservationDecodeReport& report, std::string* error);
    bool decode_update_player_game_type(const TransportFrame& frame, ObservationDecodeReport& report, std::string* error);
    bool decode_correct_player_move_prediction(const TransportFrame& frame, ObservationDecodeReport& report, std::string* error);
    bool decode_mob_effect(const TransportFrame& frame, ObservationDecodeReport& report, std::string* error);
    bool decode_add_player(const TransportFrame& frame, ObservationDecodeReport& report, std::string* error);
    bool decode_add_actor(const TransportFrame& frame, ObservationDecodeReport& report, std::string* error);
    bool decode_remove_actor(const TransportFrame& frame, ObservationDecodeReport& report, std::string* error);
    bool decode_move_actor_absolute(const TransportFrame& frame, ObservationDecodeReport& report, std::string* error);
    bool decode_move_actor_delta(const TransportFrame& frame, ObservationDecodeReport& report, std::string* error);
    bool decode_inventory_content(const TransportFrame& frame, ObservationDecodeReport& report, std::string* error);
    bool decode_inventory_slot(const TransportFrame& frame, ObservationDecodeReport& report, std::string* error);
    bool decode_mob_equipment(const TransportFrame& frame, ObservationDecodeReport& report, std::string* error);
    bool decode_mob_armor_equipment(const TransportFrame& frame, ObservationDecodeReport& report, std::string* error);
    bool decode_update_attributes(const TransportFrame& frame, ObservationDecodeReport& report, std::string* error);
    bool decode_item_registry(const TransportFrame& frame, ObservationDecodeReport& report, std::string* error);
    void resolve_item_identity(InventoryStackObservation& stack);
    void refresh_registry_item_identities();
    void sync_strategy_inventory_from_containers();
    void update_entity_relation(EntityState& entity);
    bool fail_packet(const TransportFrame& frame, const std::string& message,
                     ObservationDecodeReport& report, std::string* error);
};

} // namespace bedrockai
