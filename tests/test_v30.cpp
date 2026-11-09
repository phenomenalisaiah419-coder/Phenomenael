#include "core/bedrock2193_observation_decoder.hpp"

#include <cmath>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

using namespace bedrockai;

static int failures = 0;
#define CHECK(cond, msg) do { if (!(cond)) { std::cerr << "FAIL: " << msg << "\n"; ++failures; } } while (0)

namespace {
void put_u8(std::vector<std::uint8_t>& out, std::uint8_t value) { out.push_back(value); }
void put_u16le(std::vector<std::uint8_t>& out, std::uint16_t value) {
    out.push_back(static_cast<std::uint8_t>(value & 0xffU));
    out.push_back(static_cast<std::uint8_t>((value >> 8U) & 0xffU));
}
void put_varu32(std::vector<std::uint8_t>& out, std::uint32_t value) {
    while (value >= 0x80U) {
        out.push_back(static_cast<std::uint8_t>((value & 0x7fU) | 0x80U));
        value >>= 7U;
    }
    out.push_back(static_cast<std::uint8_t>(value));
}
void put_varu64(std::vector<std::uint8_t>& out, std::uint64_t value) {
    while (value >= 0x80ULL) {
        out.push_back(static_cast<std::uint8_t>((value & 0x7fULL) | 0x80ULL));
        value >>= 7U;
    }
    out.push_back(static_cast<std::uint8_t>(value));
}
void put_vari32(std::vector<std::uint8_t>& out, std::int32_t value) {
    const std::uint32_t raw = (static_cast<std::uint32_t>(value) << 1U) ^
                              static_cast<std::uint32_t>(value >> 31);
    put_varu32(out, raw);
}
void put_vari64(std::vector<std::uint8_t>& out, std::int64_t value) {
    const std::uint64_t raw = (static_cast<std::uint64_t>(value) << 1U) ^
                              static_cast<std::uint64_t>(value >> 63);
    put_varu64(out, raw);
}
void put_f32(std::vector<std::uint8_t>& out, float value) {
    std::uint32_t raw = 0U;
    std::memcpy(&raw, &value, sizeof(raw));
    out.push_back(static_cast<std::uint8_t>(raw & 0xffU));
    out.push_back(static_cast<std::uint8_t>((raw >> 8U) & 0xffU));
    out.push_back(static_cast<std::uint8_t>((raw >> 16U) & 0xffU));
    out.push_back(static_cast<std::uint8_t>((raw >> 24U) & 0xffU));
}
void put_vec3(std::vector<std::uint8_t>& out, float x, float y, float z) {
    put_f32(out, x); put_f32(out, y); put_f32(out, z);
}
void put_rot2(std::vector<std::uint8_t>& out, float pitch, float yaw) {
    put_f32(out, pitch); put_f32(out, yaw);
}
void put_string(std::vector<std::uint8_t>& out, const std::string& value) {
    put_varu32(out, static_cast<std::uint32_t>(value.size()));
    out.insert(out.end(), value.begin(), value.end());
}
TransportFrame frame(std::uint32_t packet_id, std::vector<std::uint8_t> payload,
                     Direction direction = Direction::Clientbound) {
    TransportFrame f;
    f.identity = {ProtocolIdentity::kGameVersion, ProtocolIdentity::kProtocol, BedrockTransport::RakNet};
    f.direction = direction;
    f.packet_id = packet_id;
    f.sequence = 1U;
    f.payload = std::move(payload);
    return f;
}
std::vector<std::uint8_t> start_game() {
    std::vector<std::uint8_t> p;
    put_vari64(p, 1234); // ActorUniqueID
    put_varu64(p, 42U);  // ActorRuntimeID
    put_vari32(p, 1);    // Survival
    put_vec3(p, 10.0F, 65.0F, -3.0F);
    put_rot2(p, 5.0F, 90.0F);
    // Remainder of StartGame is intentionally not synthesized in unit fixtures.
    return p;
}
std::vector<std::uint8_t> move_player(std::uint64_t id, float x, float y, float z, bool ground) {
    std::vector<std::uint8_t> p;
    put_varu64(p, id);
    put_vec3(p, x, y, z);
    put_rot2(p, 2.0F, 180.0F);
    put_f32(p, 179.0F);
    put_u8(p, 0U); // Normal position mode
    put_u8(p, ground ? 1U : 0U);
    put_varu64(p, 0U); // No vehicle
    put_varu64(p, 122U); // Tick (tail currently retained but not otherwise used)
    return p;
}
std::vector<std::uint8_t> add_player(std::uint64_t id, const std::string& name, float x, float z) {
    std::vector<std::uint8_t> p;
    for (std::uint8_t i = 0; i < 16U; ++i) p.push_back(i);
    put_string(p, name);
    put_varu64(p, id);
    put_string(p, ""); // platform chat ID
    put_vec3(p, x, 64.0F, z);
    put_vec3(p, 0.0F, 0.0F, 0.0F);
    put_rot2(p, 0.0F, 45.0F);
    put_f32(p, 45.0F);
    // Remaining carried-item/game type/metadata intentionally omitted from prefix fixture.
    return p;
}
std::vector<std::uint8_t> add_actor(std::int64_t unique_id, std::uint64_t id,
                                   const std::string& type, float x, float y, float z) {
    std::vector<std::uint8_t> p;
    put_vari64(p, unique_id);
    put_varu64(p, id);
    put_string(p, type);
    put_vec3(p, x, y, z);
    put_vec3(p, 0.0F, 0.0F, 0.0F);
    put_rot2(p, 0.0F, 0.0F);
    put_f32(p, 0.0F); put_f32(p, 0.0F);
    // Attributes/metadata follow in the actual packet and aren't consumed here.
    return p;
}
}

static void test_start_game_and_state_source() {
    Bedrock2193ObservationDecoder decoder;
    GameplayObservation observation;
    std::string error;
    CHECK(!decoder.read(observation, &error), "read is refused before a decoded StartGame");
    ObservationDecodeReport report;
    CHECK(decoder.accept_frame(frame(11U, start_game()), &report, &error), "StartGame initial state prefix is decoded");
    CHECK(report.disposition == ObservationDecodeDisposition::PartiallyApplied,
          "StartGame reports partial parsing rather than claiming all level settings were decoded");
    CHECK(decoder.read(observation, &error), "joined snapshot is available through GameplayObservationSource");
    CHECK(observation.world.joined && observation.world.player.runtime_id == 42U,
          "StartGame establishes local runtime identity and joined state");
    CHECK(std::abs(observation.world.player.position.x - 10.0) < 1e-6 &&
          std::abs(observation.world.player.rotation.yaw - 90.0) < 1e-6,
          "StartGame position and rotation are reconciled");
    CHECK(observation.world.player.unique_id == 1234 && observation.world.world.game_type == 1,
          "StartGame unique ID and game type are preserved");
}

static void test_authoritative_move_and_set_health() {
    Bedrock2193ObservationDecoder decoder;
    std::string error;
    CHECK(decoder.accept_frame(frame(11U, start_game()), nullptr, &error), "fixture starts");
    ObservationDecodeReport report;
    CHECK(decoder.accept_frame(frame(19U, move_player(42U, 12.0F, 66.0F, 2.0F, true)), &report, &error),
          "MovePlayer authoritative position is decoded");
    const auto& state = decoder.observation().world;
    CHECK(std::abs(state.player.position.x - 12.0) < 1e-6 && state.player.on_ground && !state.player.airborne,
          "local movement correction updates position and on-ground status");
    CHECK(state.last_correction.present && state.last_correction.distance_error > 0.0,
          "position correction records prediction error for latency/movement learning");
    CHECK(state.server_tick_known && state.server_tick == 122U,
          "MovePlayer carries the real protocol input tick separately from the local packet counter");

    std::vector<std::uint8_t> health;
    put_vari32(health, 13);
    CHECK(decoder.accept_frame(frame(42U, health), &report, &error), "SetHealth varint decoded");
    CHECK(std::abs(decoder.observation().world.player.health - 13.0) < 1e-9,
          "authoritative integer health replaces stale local health");
}

static void test_effect_updates_add_update_remove() {
    Bedrock2193ObservationDecoder decoder;
    std::string error;
    CHECK(decoder.accept_frame(frame(11U, start_game()), nullptr, &error), "fixture starts");
    std::vector<std::uint8_t> effect;
    put_varu64(effect, 42U); put_u8(effect, 1U); put_vari32(effect, 1); put_vari32(effect, 0);
    put_u8(effect, 1U); put_vari32(effect, 200); put_varu64(effect, 150U); put_u8(effect, 0U);
    CHECK(decoder.accept_frame(frame(28U, effect), nullptr, &error), "MobEffect add decoded");
    const auto it = decoder.observation().world.player.effects.find(1);
    CHECK(it != decoder.observation().world.player.effects.end() && it->second.duration_ticks == 200U,
          "effect state is stored against the local player");
    effect.clear(); put_varu64(effect, 42U); put_u8(effect, 2U); put_vari32(effect, 1); put_vari32(effect, 1);
    put_u8(effect, 1U); put_vari32(effect, 100); put_varu64(effect, 160U); put_u8(effect, 0U);
    CHECK(decoder.accept_frame(frame(28U, effect), nullptr, &error), "MobEffect update decoded");
    CHECK(decoder.observation().world.player.effects.at(1).amplifier == 1 &&
          decoder.observation().world.player.effects.at(1).duration_ticks == 100U,
          "effect update replaces amplifier and remaining duration");
    effect.clear(); put_varu64(effect, 42U); put_u8(effect, 3U); put_vari32(effect, 1); put_vari32(effect, 0);
    put_u8(effect, 0U); put_vari32(effect, 0); put_varu64(effect, 161U); put_u8(effect, 0U);
    CHECK(decoder.accept_frame(frame(28U, effect), nullptr, &error), "MobEffect removal decoded");
    CHECK(decoder.observation().world.player.effects.empty(), "effect removal clears the status effect");
}

static void test_players_and_hostile_entities_are_classified_conservatively() {
    Bedrock2193ObservationDecoder decoder;
    std::string error;
    CHECK(decoder.accept_frame(frame(11U, start_game()), nullptr, &error), "fixture starts");
    CHECK(decoder.accept_frame(frame(12U, add_player(71U, "Friend", 4.0F, 5.0F)), nullptr, &error),
          "AddPlayer identity and movement prefix decoded");
    const auto friend_it = decoder.observation().world.world.entities.find(71U);
    CHECK(friend_it != decoder.observation().world.world.entities.end() && friend_it->second.is_player &&
          friend_it->second.player_name == "Friend" && !friend_it->second.hostile,
          "other players are not automatically attacked by default");
    decoder.set_target_other_players_as_hostile(true);
    CHECK(decoder.observation().world.world.entities.at(71U).hostile,
          "explicit PvP mode policy can mark other players as target candidates");
    CHECK(decoder.accept_frame(frame(13U, add_actor(7001, 81U, "minecraft:zombie", 8.0F, 64.0F, 8.0F)), nullptr, &error),
          "AddActor state prefix decoded");
    const auto hostile_it = decoder.observation().world.world.entities.find(81U);
    CHECK(hostile_it != decoder.observation().world.world.entities.end() && hostile_it->second.hostile &&
          hostile_it->second.hostility_known,
          "known hostile mob types are classified with explicit evidence");
    std::vector<std::uint8_t> remove;
    put_vari64(remove, 7001);
    CHECK(decoder.accept_frame(frame(14U, remove), nullptr, &error), "RemoveActor decoded");
    CHECK(decoder.observation().world.world.entities.find(81U) == decoder.observation().world.world.entities.end(),
          "tracked non-player actor is removed by its unique ID");
}

static void test_movement_packets_update_entities_and_ground_state() {
    Bedrock2193ObservationDecoder decoder;
    std::string error;
    CHECK(decoder.accept_frame(frame(11U, start_game()), nullptr, &error), "fixture starts");
    CHECK(decoder.accept_frame(frame(12U, add_player(71U, "Opponent", 2.0F, 3.0F)), nullptr, &error), "opponent added");
    CHECK(decoder.accept_frame(frame(19U, move_player(71U, 5.0F, 65.0F, 7.0F, false)), nullptr, &error),
          "MovePlayer for other player decoded");
    const auto& entities = decoder.observation().world.world.entities;
    CHECK(std::abs(entities.at(71U).position.x - 5.0) < 1e-6,
          "other-player MovePlayer updates tracked entity position");

    std::vector<std::uint8_t> absolute;
    put_varu64(absolute, 71U); put_u8(absolute, 1U); put_vec3(absolute, 6.0F, 65.0F, 8.0F);
    put_u8(absolute, 0U); put_u8(absolute, 128U); put_u8(absolute, 0U);
    CHECK(decoder.accept_frame(frame(18U, absolute), nullptr, &error), "MoveActorAbsolute decoded");
    CHECK(std::abs(decoder.observation().world.world.entities.at(71U).position.x - 6.0) < 1e-6 &&
          decoder.observation().world.world.entities.at(71U).on_ground,
          "absolute actor update moves entity and applies ground flag");

    std::vector<std::uint8_t> delta;
    put_varu64(delta, 71U); put_u16le(delta, 0x0007U); // X/Y/Z are present; ground is a separate boolean
    put_f32(delta, 7.0F); put_f32(delta, 66.0F); put_f32(delta, 9.0F);
    put_u8(delta, 1U); put_u8(delta, 0U); put_u8(delta, 0U); put_u8(delta, 0U); put_varu64(delta, 2U);
    CHECK(decoder.accept_frame(frame(111U, delta), nullptr, &error), "protocol-2193 MoveActorDelta decoded");
    CHECK(std::abs(decoder.observation().world.world.entities.at(71U).position.x - 7.0) < 1e-6 &&
          decoder.observation().world.world.entities.at(71U).on_ground,
          "delta packet's flagged fields are applied without overwriting absent components");
}

static void test_invalid_frames_and_payloads_fail_closed() {
    Bedrock2193ObservationDecoder decoder;
    std::string error;
    ObservationDecodeReport report;
    TransportFrame bad_protocol = frame(11U, start_game());
    bad_protocol.identity.protocol = 2192U;
    CHECK(!decoder.accept_frame(bad_protocol, &report, &error), "preview protocol 2192 cannot be parsed as stable 2193");
    CHECK(decoder.malformed_frames() == 1U, "invalid identity increments diagnostic counter");

    CHECK(!decoder.accept_frame(frame(11U, {0x80U}), &report, &error), "truncated StartGame varint fails");
    CHECK(!decoder.accept_frame(frame(42U, {}), &report, &error), "empty SetHealth payload fails");
    std::vector<std::uint8_t> invalid_health;
    put_vari32(invalid_health, -1);
    CHECK(!decoder.accept_frame(frame(42U, invalid_health), &report, &error), "negative health is rejected");

    CHECK(!decoder.accept_frame(frame(49U, {0U, 0U, 0U}), &report, &error),
          "truncated InventoryContent is rejected now that packet 49 has a strict decoder");
    CHECK(report.disposition == ObservationDecodeDisposition::Malformed,
          "malformed inventory update is explicit to the host");
    CHECK(decoder.accept_frame(frame(42U, {26U}, Direction::Serverbound), &report, &error),
          "serverbound frame is ignored rather than applied as authoritative clientbound state");
    CHECK(report.disposition == ObservationDecodeDisposition::IgnoredDirection,
          "packet-direction mismatch is visible in the decode report");
}

static void test_server_motion_and_game_type_updates() {
    Bedrock2193ObservationDecoder decoder;
    std::string error;
    CHECK(decoder.accept_frame(frame(11U, start_game()), nullptr, &error), "fixture starts");

    std::vector<std::uint8_t> velocity;
    put_varu64(velocity, 42U); put_vec3(velocity, 0.5F, -1.25F, 2.0F);
    CHECK(decoder.accept_frame(frame(40U, velocity), nullptr, &error), "SetActorMotion decoded");
    CHECK(std::abs(decoder.observation().world.player.velocity.y + 1.25) < 1e-6,
          "SetActorMotion updates local velocity for prediction and fall-risk reasoning");

    std::vector<std::uint8_t> prediction;
    put_varu64(prediction, 42U); put_vec3(prediction, -0.25F, -0.5F, 0.75F); put_u8(prediction, 0U);
    CHECK(decoder.accept_frame(frame(157U, prediction), nullptr, &error), "MotionPredictionHints decoded");
    CHECK(decoder.observation().world.player.ground_state_known &&
          !decoder.observation().world.player.on_ground &&
          std::abs(decoder.observation().world.player.velocity.x + 0.25) < 1e-6,
          "MotionPredictionHints applies its motion vector and explicit ground state");

    std::vector<std::uint8_t> mode;
    put_vari32(mode, 2); put_vari64(mode, 1234); put_varu64(mode, 123U);
    CHECK(decoder.accept_frame(frame(151U, mode), nullptr, &error), "UpdatePlayerGameType decoded");
    CHECK(decoder.observation().world.world.game_type == 2,
          "game-mode change for the local unique ID updates the world model");
    CHECK(decoder.observation().world.server_tick_known && decoder.observation().world.server_tick == 123U,
          "server input tick is tracked from versioned gameplay packets");
}

static void test_server_authoritative_correction_and_head_only_move() {
    Bedrock2193ObservationDecoder decoder;
    std::string error;
    CHECK(decoder.accept_frame(frame(11U, start_game()), nullptr, &error), "fixture starts");
    std::vector<std::uint8_t> correction;
    put_u8(correction, 0U); // Player correction, not vehicle.
    put_vec3(correction, 11.0F, 64.0F, -2.0F);
    put_vec3(correction, 0.0F, -0.1F, 0.0F);
    put_u8(correction, 1U); put_varu64(correction, 130U); // On-ground and input tick.
    CHECK(decoder.accept_frame(frame(161U, correction), nullptr, &error), "CorrectPlayerMovePrediction decoded");
    const auto& player = decoder.observation().world.player;
    CHECK(std::abs(player.position.x - 11.0) < 1e-6 &&
          std::abs(player.velocity.y + 0.1) < 1e-6 && player.ground_state_known && player.on_ground,
          "server movement correction reconciles position, velocity and ground knowledge");
    CHECK(decoder.observation().world.server_tick_known && decoder.observation().world.server_tick == 130U,
          "corrected server input tick is recorded separately from packet arrival count");

    const Vec3 before = player.position;
    std::vector<std::uint8_t> head_only;
    put_varu64(head_only, 42U); put_vec3(head_only, 500.0F, 500.0F, 500.0F);
    put_rot2(head_only, 6.0F, 20.0F); put_f32(head_only, 30.0F);
    put_u8(head_only, 3U); put_u8(head_only, 0U); put_varu64(head_only, 0U); put_varu64(head_only, 131U);
    CHECK(decoder.accept_frame(frame(19U, head_only), nullptr, &error), "MovePlayer OnlyHeadRot decoded");
    CHECK(std::abs(decoder.observation().world.player.position.x - before.x) < 1e-6 &&
          std::abs(decoder.observation().world.player.position.y - before.y) < 1e-6 &&
          std::abs(decoder.observation().world.player.head_yaw - 30.0) < 1e-6,
          "OnlyHeadRot updates look direction without teleporting the player to placeholder coordinates");
}

static void test_malformed_move_tail_and_unknown_delta_flags_fail_closed() {
    Bedrock2193ObservationDecoder decoder;
    std::string error;
    CHECK(decoder.accept_frame(frame(11U, start_game()), nullptr, &error), "fixture starts");
    std::vector<std::uint8_t> bad_move = move_player(42U, 1.0F, 2.0F, 3.0F, true);
    bad_move.pop_back();
    ObservationDecodeReport report;
    CHECK(!decoder.accept_frame(frame(19U, bad_move), &report, &error),
          "truncated final MovePlayer tick must reject the packet instead of applying a partial state");
    std::vector<std::uint8_t> bad_delta;
    put_varu64(bad_delta, 42U); put_u16le(bad_delta, 0x0040U);
    put_u8(bad_delta, 0U); put_u8(bad_delta, 0U); put_u8(bad_delta, 0U); put_u8(bad_delta, 0U);
    put_u8(bad_delta, 0U); put_u8(bad_delta, 0U); put_varu64(bad_delta, 1U);
    CHECK(!decoder.accept_frame(frame(111U, bad_delta), &report, &error),
          "unknown MoveActorDelta flags are not silently ignored");
}

static void test_context_and_inventory_extension_contract() {
    Bedrock2193ObservationDecoder decoder;
    decoder.set_gameplay_context(GameMode::BedWars, GameplayGoal::BedWarsRush, "hive:bedwars");
    CHECK(decoder.accept_frame(frame(11U, start_game())), "fixture starts");
    InventoryStackObservation stack;
    stack.kind = ItemKind::BuildingBlock;
    stack.slot = 2;
    stack.count = 32;
    decoder.replace_inventory({stack});
    GameplayObservation observation;
    std::string error;
    CHECK(decoder.read(observation, &error), "observation source copies latest snapshot");
    CHECK(observation.game_mode == GameMode::BedWars && observation.goal == GameplayGoal::BedWarsRush &&
          observation.server_profile_key == "hive:bedwars", "host mode/profile context is retained across packet updates");
    CHECK(observation.inventory.size() == 1U && observation.inventory[0].count == 32,
          "decoded inventory can be attached through the explicit future codec/host adapter seam");
}

int main() {
    test_start_game_and_state_source();
    test_authoritative_move_and_set_health();
    test_effect_updates_add_update_remove();
    test_players_and_hostile_entities_are_classified_conservatively();
    test_movement_packets_update_entities_and_ground_state();
    test_server_motion_and_game_type_updates();
    test_server_authoritative_correction_and_head_only_move();
    test_malformed_move_tail_and_unknown_delta_flags_fail_closed();
    test_invalid_frames_and_payloads_fail_closed();
    test_context_and_inventory_extension_contract();
    if (failures == 0) std::cout << "All v30 protocol observation decoder tests passed\n";
    return failures == 0 ? 0 : 1;
}
