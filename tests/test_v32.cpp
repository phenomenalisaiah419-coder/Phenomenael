#include "core/bedrock2193_observation_decoder.hpp"

#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

using namespace bedrockai;

namespace {
int failures = 0;
#define CHECK(condition, message) do { if (!(condition)) { \
    std::cerr << "FAIL: " << message << " (line " << __LINE__ << ")\n"; ++failures; } } while (false)

void put_u8(std::vector<std::uint8_t>& out, std::uint8_t value) { out.push_back(value); }
void put_u16le(std::vector<std::uint8_t>& out, std::uint16_t value) {
    out.push_back(static_cast<std::uint8_t>(value & 0xffU));
    out.push_back(static_cast<std::uint8_t>((value >> 8U) & 0xffU));
}
void put_u32le(std::vector<std::uint8_t>& out, std::uint32_t value) {
    for (unsigned int i = 0U; i < 4U; ++i) out.push_back(static_cast<std::uint8_t>((value >> (8U * i)) & 0xffU));
}
void put_i32le(std::vector<std::uint8_t>& out, std::int32_t value) {
    std::uint32_t bits = 0U;
    std::memcpy(&bits, &value, sizeof(bits));
    put_u32le(out, bits);
}
void put_varu32(std::vector<std::uint8_t>& out, std::uint32_t value) {
    do {
        std::uint8_t byte = static_cast<std::uint8_t>(value & 0x7fU);
        value >>= 7U;
        if (value != 0U) byte = static_cast<std::uint8_t>(byte | 0x80U);
        out.push_back(byte);
    } while (value != 0U);
}
void put_varu64(std::vector<std::uint8_t>& out, std::uint64_t value) {
    do {
        std::uint8_t byte = static_cast<std::uint8_t>(value & 0x7fU);
        value >>= 7U;
        if (value != 0U) byte = static_cast<std::uint8_t>(byte | 0x80U);
        out.push_back(byte);
    } while (value != 0U);
}
void put_vari32(std::vector<std::uint8_t>& out, std::int32_t value) {
    const std::uint32_t raw = (static_cast<std::uint32_t>(value) << 1U) ^ static_cast<std::uint32_t>(value >> 31);
    put_varu32(out, raw);
}
void put_f32(std::vector<std::uint8_t>& out, float value) {
    std::uint32_t raw = 0U;
    std::memcpy(&raw, &value, sizeof(raw));
    put_u32le(out, raw);
}
void put_string(std::vector<std::uint8_t>& out, const std::string& value) {
    put_varu32(out, static_cast<std::uint32_t>(value.size()));
    out.insert(out.end(), value.begin(), value.end());
}
void put_nbt_name(std::vector<std::uint8_t>& out, const std::string& value) {
    put_u16le(out, static_cast<std::uint16_t>(value.size()));
    out.insert(out.end(), value.begin(), value.end());
}
void put_item_stack(std::vector<std::uint8_t>& out, std::int16_t id, std::uint16_t count) {
    put_u16le(out, static_cast<std::uint16_t>(id));
    if (id == 0) return;
    put_u16le(out, count);
    put_varu32(out, 0U); // aux value
    put_u8(out, 0U); // no stack network ID
    put_varu32(out, 0U); // block runtime ID
    put_string(out, ""); // opaque item user data
}
void put_registry_entry(std::vector<std::uint8_t>& out, const std::string& identifier,
                        std::int16_t item_id, bool component_based, std::int32_t version,
                        const std::vector<std::uint8_t>& nbt = {}) {
    put_string(out, identifier);
    put_u16le(out, static_cast<std::uint16_t>(item_id));
    put_u8(out, component_based ? 1U : 0U);
    put_vari32(out, version);
    if (nbt.empty()) {
        // The ItemComponentData field is serialized for every registry entry.
        // Legacy/non-component items may supply an empty compound.
        const std::vector<std::uint8_t> empty_compound = {10U, 0U, 0U, 0U};
        out.insert(out.end(), empty_compound.begin(), empty_compound.end());
    } else {
        out.insert(out.end(), nbt.begin(), nbt.end());
    }
}
std::vector<std::uint8_t> registry_fixture(bool conflicting_id = true) {
    std::vector<std::uint8_t> p;
    put_varu32(p, conflicting_id ? 4U : 3U);
    put_registry_entry(p, "minecraft:diamond_sword", 276, false, 0);
    // Compound root with one TAG_String child named "ability" and a TAG_End.
    const std::vector<std::uint8_t> component_nbt = {
        10U, 0U, 0U,             // root TAG_Compound, empty root name
        8U, 7U, 0U, 'a', 'b', 'i', 'l', 'i', 't', 'y',
        2U, 0U, 'o', 'k',        // TAG_String payload: length 2, "ok"
        0U
    };
    put_registry_entry(p, "hive:explosive_bow", 1500, true, 1, component_nbt);
    put_registry_entry(p, "hive:custom_item_a", 2000, false, 0);
    if (conflicting_id) put_registry_entry(p, "hive:custom_item_b", 2000, false, 0);
    return p;
}
std::vector<std::uint8_t> start_game() {
    std::vector<std::uint8_t> p;
    put_vari32(p, 1234); // unique ID
    put_varu64(p, 42U); // runtime ID
    put_vari32(p, 1); // game type
    put_f32(p, 10.0F); put_f32(p, 65.0F); put_f32(p, -3.0F);
    put_f32(p, 0.0F); put_f32(p, 90.0F);
    return p;
}
std::vector<std::uint8_t> inventory_content() {
    std::vector<std::uint8_t> p;
    put_varu32(p, 0U); // inventory window
    put_varu32(p, 3U);
    put_item_stack(p, 276, 1U);
    put_item_stack(p, 1500, 1U);
    put_item_stack(p, 2000, 1U);
    put_u8(p, 12U); // combined hotbar+inventory container
    put_u32le(p, 0U); // dynamic container ID
    put_u16le(p, 0U); // empty storage item descriptor
    return p;
}
TransportFrame frame(std::uint32_t packet_id, std::vector<std::uint8_t> payload) {
    TransportFrame f;
    f.identity = {ProtocolIdentity::kGameVersion, ProtocolIdentity::kProtocol, BedrockTransport::RakNet};
    f.direction = Direction::Clientbound;
    f.packet_id = packet_id;
    f.sequence = 1U;
    f.payload = std::move(payload);
    return f;
}
void put_attribute(std::vector<std::uint8_t>& p, const std::string& name,
                   float min_value, float max_value, float current_value,
                   float default_min, float default_max, float default_value,
                   bool include_modifier = false) {
    put_f32(p, min_value); put_f32(p, max_value); put_f32(p, current_value);
    put_f32(p, default_min); put_f32(p, default_max); put_f32(p, default_value);
    put_string(p, name);
    put_varu32(p, include_modifier ? 1U : 0U);
    if (include_modifier) {
        put_string(p, "test:modifier");
        put_string(p, "test modifier");
        put_f32(p, 2.0F);
        put_i32le(p, 1); // operation
        put_i32le(p, 0); // operand
        put_u8(p, 1U); // serializable
    }
}
std::vector<std::uint8_t> attributes_fixture(bool truncate_tick = false) {
    std::vector<std::uint8_t> p;
    put_varu64(p, 42U); // local player runtime ID
    put_varu32(p, 3U);
    put_attribute(p, "minecraft:health", 0.0F, 40.0F, 12.5F, 0.0F, 20.0F, 20.0F, true);
    put_attribute(p, "minecraft:absorption", 0.0F, 16.0F, 4.5F, 0.0F, 16.0F, 0.0F);
    put_attribute(p, "minecraft:hunger", 0.0F, 20.0F, 13.0F, 0.0F, 20.0F, 20.0F);
    if (!truncate_tick) put_varu64(p, 123U);
    return p;
}
}

void test_registry_maps_item_ids_without_guessing_custom_capabilities() {
    Bedrock2193ObservationDecoder decoder;
    std::string error;
    CHECK(decoder.accept_frame(frame(11U, start_game()), nullptr, &error), "StartGame fixture accepted");
    CHECK(decoder.accept_frame(frame(49U, inventory_content()), nullptr, &error), "inventory fixture accepted before registry");
    CHECK(decoder.observation().inventory[0].identifier.empty(), "numeric item ID remains unresolved before registry");

    ObservationDecodeReport report;
    CHECK(decoder.accept_frame(frame(162U, registry_fixture()), &report, &error), "ItemRegistry fixture is decoded");
    CHECK(report.disposition == ObservationDecodeDisposition::PartiallyApplied,
          "conflicting duplicate numeric item IDs are reported as partial instead of guessed");
    const auto& observation = decoder.observation();
    CHECK(observation.item_registry_entries.size() == 4U, "all registry entries are preserved, including collisions");
    CHECK(observation.item_registry_index_by_id.find(276) != observation.item_registry_index_by_id.end(),
          "unique numeric item ID gets a validated index");
    CHECK(observation.ambiguous_protocol_item_ids.find(2000) != observation.ambiguous_protocol_item_ids.end() &&
          observation.item_registry_index_by_id.find(2000) == observation.item_registry_index_by_id.end(),
          "duplicate conflicting IDs are isolated as ambiguous");
    CHECK(observation.inventory.size() == 3U && observation.inventory[0].identifier == "minecraft:diamond_sword" &&
          observation.inventory[0].kind == ItemKind::Sword,
          "registry resolution is retroactively applied to existing inventory with known vanilla item kind");
    CHECK(observation.inventory[1].identifier == "hive:explosive_bow" &&
          observation.inventory[1].kind == ItemKind::Unknown &&
          !observation.inventory[1].capability_profile_verified,
          "custom Hive namespace is identified but no item capability is invented");
    CHECK(observation.item_registry_entries[1].component_data_buffer.size() > 4U &&
          observation.item_registry_entries[1].component_data_buffer.front() == static_cast<char>(10),
          "component NBT bytes are retained once in the registry as opaque validated data");
    CHECK(observation.inventory[2].identifier.empty() && observation.inventory[2].kind == ItemKind::Unknown,
          "inventory item with ambiguous numeric ID remains unidentified");

    auto malformed = std::vector<std::uint8_t>{1U, 1U}; // count=1 then truncated entry
    CHECK(!decoder.accept_frame(frame(162U, malformed), &report, &error), "truncated registry is rejected");
    CHECK(report.disposition == ObservationDecodeDisposition::Malformed &&
          decoder.observation().item_registry_entries.size() == 4U &&
          decoder.observation().inventory[0].identifier == "minecraft:diamond_sword",
          "malformed registry does not replace previously validated registry state");
}

void test_registry_collision_removes_stale_identity() {
    Bedrock2193ObservationDecoder decoder;
    std::string error;
    CHECK(decoder.accept_frame(frame(11U, start_game()), nullptr, &error), "StartGame fixture accepted");
    std::vector<std::uint8_t> inv;
    put_varu32(inv, 0U); put_varu32(inv, 1U); put_item_stack(inv, 2000, 1U);
    put_u8(inv, 12U); put_u32le(inv, 0U); put_u16le(inv, 0U);
    CHECK(decoder.accept_frame(frame(49U, std::move(inv)), nullptr, &error), "single-item inventory accepted");

    std::vector<std::uint8_t> unique;
    put_varu32(unique, 1U);
    put_registry_entry(unique, "hive:one", 2000, false, 0);
    CHECK(decoder.accept_frame(frame(162U, unique), nullptr, &error), "unique item registry accepted");
    CHECK(decoder.observation().inventory.front().identifier == "hive:one", "unique registry item resolves");

    std::vector<std::uint8_t> collision;
    put_varu32(collision, 2U);
    put_registry_entry(collision, "hive:one", 2000, false, 0);
    put_registry_entry(collision, "hive:two", 2000, false, 0);
    ObservationDecodeReport report;
    CHECK(decoder.accept_frame(frame(162U, std::move(collision)), &report, &error), "collision registry is parsed");
    CHECK(report.disposition == ObservationDecodeDisposition::PartiallyApplied &&
          decoder.observation().inventory.front().identifier.empty() &&
          decoder.observation().inventory.front().kind == ItemKind::Unknown,
          "a new collision clears stale registry-derived identity");
}

void test_update_attributes_populates_vitals_and_tick_atomically() {
    Bedrock2193ObservationDecoder decoder;
    std::string error;
    CHECK(decoder.accept_frame(frame(11U, start_game()), nullptr, &error), "StartGame fixture accepted");
    ObservationDecodeReport report;
    CHECK(decoder.accept_frame(frame(29U, attributes_fixture()), &report, &error), "UpdateAttributes fixture decoded");
    const auto& observation = decoder.observation();
    CHECK(report.disposition == ObservationDecodeDisposition::Applied, "local player attributes report Applied");
    CHECK(observation.world.player.health == 12.5 && observation.conditions.maximum_health == 40.0,
          "health current and max health come from authoritative attribute data");
    CHECK(observation.conditions.absorption == 4.5 && observation.conditions.hunger == 13.0 &&
          observation.conditions.hunger_known,
          "absorption and hunger attribute observations are synchronized");
    CHECK(observation.world.player.attributes.size() == 3U &&
          observation.world.player.attributes.at("minecraft:health").modifiers.size() == 1U &&
          observation.world.player.attributes.at("minecraft:health").modifiers.front().amount == 2.0,
          "attribute modifier metadata is retained");
    CHECK(observation.world.server_tick_known && observation.world.server_tick == 123U,
          "server input tick is tracked separately from local observation sequence");

    const double prior_health = observation.world.player.health;
    const std::uint64_t prior_server_tick = observation.world.server_tick;
    CHECK(!decoder.accept_frame(frame(29U, attributes_fixture(true)), &report, &error),
          "truncated UpdateAttributes packet is rejected");
    CHECK(report.disposition == ObservationDecodeDisposition::Malformed &&
          decoder.observation().world.player.health == prior_health &&
          decoder.observation().world.server_tick == prior_server_tick,
          "malformed attribute update cannot partially mutate vitals or server tick");

    std::vector<std::uint8_t> oversized;
    put_varu64(oversized, 42U); put_varu32(oversized, 257U);
    CHECK(!decoder.accept_frame(frame(29U, std::move(oversized)), &report, &error),
          "oversized attribute count is rejected before allocation");
}


void test_truncation_mutations_never_commit_partial_state() {
    Bedrock2193ObservationDecoder registry_decoder;
    std::string error;
    CHECK(registry_decoder.accept_frame(frame(11U, start_game()), nullptr, &error), "StartGame fixture accepted");
    CHECK(registry_decoder.accept_frame(frame(49U, inventory_content()), nullptr, &error), "inventory fixture accepted");
    const auto valid_registry = registry_fixture(false);
    CHECK(registry_decoder.accept_frame(frame(162U, valid_registry), nullptr, &error), "baseline registry accepted");
    const std::size_t baseline_entries = registry_decoder.observation().item_registry_entries.size();
    const std::string baseline_identifier = registry_decoder.observation().inventory[0].identifier;
    for (std::size_t length = 0U; length < valid_registry.size(); ++length) {
        std::vector<std::uint8_t> prefix(valid_registry.begin(),
            valid_registry.begin() + static_cast<std::ptrdiff_t>(length));
        ObservationDecodeReport report;
        CHECK(!registry_decoder.accept_frame(frame(162U, std::move(prefix)), &report, &error),
              "every strict ItemRegistry payload prefix is rejected");
        CHECK(report.disposition == ObservationDecodeDisposition::Malformed &&
              registry_decoder.observation().item_registry_entries.size() == baseline_entries &&
              registry_decoder.observation().inventory[0].identifier == baseline_identifier,
              "truncated ItemRegistry prefix cannot replace committed registry/inventory identity");
    }

    Bedrock2193ObservationDecoder attribute_decoder;
    CHECK(attribute_decoder.accept_frame(frame(11U, start_game()), nullptr, &error), "attribute StartGame fixture accepted");
    const auto valid_attributes = attributes_fixture();
    CHECK(attribute_decoder.accept_frame(frame(29U, valid_attributes), nullptr, &error), "baseline attributes accepted");
    const double baseline_health = attribute_decoder.observation().world.player.health;
    const std::uint64_t baseline_tick = attribute_decoder.observation().world.server_tick;
    for (std::size_t length = 0U; length < valid_attributes.size(); ++length) {
        std::vector<std::uint8_t> prefix(valid_attributes.begin(),
            valid_attributes.begin() + static_cast<std::ptrdiff_t>(length));
        ObservationDecodeReport report;
        CHECK(!attribute_decoder.accept_frame(frame(29U, std::move(prefix)), &report, &error),
              "every strict UpdateAttributes payload prefix is rejected");
        CHECK(report.disposition == ObservationDecodeDisposition::Malformed &&
              attribute_decoder.observation().world.player.health == baseline_health &&
              attribute_decoder.observation().world.server_tick == baseline_tick,
              "truncated attribute prefix cannot update vitals or server tick");
    }
}

void test_malformed_component_nbt_is_rejected_atomically() {
    Bedrock2193ObservationDecoder decoder;
    std::string error;
    CHECK(decoder.accept_frame(frame(11U, start_game()), nullptr, &error), "StartGame fixture accepted");
    ObservationDecodeReport report;
    CHECK(decoder.accept_frame(frame(162U, registry_fixture(false)), &report, &error), "baseline registry accepted");
    const std::size_t baseline_count = decoder.observation().item_registry_entries.size();

    std::vector<std::uint8_t> broken;
    put_varu32(broken, 1U);
    put_string(broken, "hive:broken_component");
    put_u16le(broken, 3000U);
    put_u8(broken, 1U);
    put_vari32(broken, 1);
    // NBT compound with a byte-array child whose signed length is -1.
    put_u8(broken, 10U); put_u16le(broken, 0U);
    put_u8(broken, 7U); put_nbt_name(broken, "bad");
    put_i32le(broken, -1); put_u8(broken, 0U);
    CHECK(!decoder.accept_frame(frame(162U, std::move(broken)), &report, &error),
          "invalid ItemRegistry NBT collection length is rejected");
    CHECK(report.disposition == ObservationDecodeDisposition::Malformed &&
          decoder.observation().item_registry_entries.size() == baseline_count,
          "invalid registry NBT leaves prior registry intact");
}

int main() {
    test_registry_maps_item_ids_without_guessing_custom_capabilities();
    test_registry_collision_removes_stale_identity();
    test_update_attributes_populates_vitals_and_tick_atomically();
    test_truncation_mutations_never_commit_partial_state();
    test_malformed_component_nbt_is_rejected_atomically();
    if (failures != 0) {
        std::cerr << failures << " v32 test(s) failed\n";
        return 1;
    }
    std::cout << "All v32 ItemRegistry and UpdateAttributes tests passed\n";
    return 0;
}
