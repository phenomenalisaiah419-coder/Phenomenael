#include "core/bedrock2193_observation_decoder.hpp"

#include <cmath>
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
    for (unsigned int i = 0; i < 4U; ++i)
        out.push_back(static_cast<std::uint8_t>((value >> (8U * i)) & 0xffU));
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
    put_u32le(out, raw);
}
void put_string(std::vector<std::uint8_t>& out, const std::string& value) {
    put_varu32(out, static_cast<std::uint32_t>(value.size()));
    out.insert(out.end(), value.begin(), value.end());
}
void put_empty_item(std::vector<std::uint8_t>& out) { put_u16le(out, 0U); }
void put_item(std::vector<std::uint8_t>& out, std::int16_t id, std::uint16_t count,
              std::uint32_t aux, bool has_stack_id, std::int32_t stack_id,
              std::uint32_t block_runtime_id, const std::string& user_data) {
    put_u16le(out, static_cast<std::uint16_t>(id));
    if (id == 0) return;
    put_u16le(out, count);
    put_varu32(out, aux);
    put_u8(out, has_stack_id ? 1U : 0U);
    if (has_stack_id) put_vari32(out, stack_id);
    put_varu32(out, block_runtime_id);
    put_string(out, user_data);
}
std::vector<std::uint8_t> start_game() {
    std::vector<std::uint8_t> payload;
    put_vari64(payload, 1234);
    put_varu64(payload, 42U);
    put_vari32(payload, 1);
    put_f32(payload, 10.0F); put_f32(payload, 65.0F); put_f32(payload, -3.0F);
    put_f32(payload, 5.0F); put_f32(payload, 90.0F);
    return payload;
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
std::vector<std::uint8_t> inventory_content(std::uint8_t container_name = 12U) {
    std::vector<std::uint8_t> p;
    put_varu32(p, 0U); // Container ID
    put_varu32(p, 4U); // four slots
    put_empty_item(p);
    put_item(p, 276, 1U, 7U, true, 37, 900U, std::string("A\0B", 3U));
    put_item(p, 332, 3U, 0U, false, 0, 0U, "");
    put_empty_item(p);
    put_u8(p, container_name); // FullContainerName.ContainerName
    put_u32le(p, 0U);           // FullContainerName.DynamicID
    put_empty_item(p);          // Storage item is an empty descriptor
    return p;
}
std::vector<std::uint8_t> inventory_slot(std::int16_t item_id, std::uint16_t count,
                                         std::uint8_t container_name = 12U) {
    std::vector<std::uint8_t> p;
    put_u8(p, 0U); // container id
    put_varu32(p, 1U); // slot
    put_u8(p, 1U); // FullContainerName present
    put_u8(p, container_name);
    put_u32le(p, 0U);
    put_u8(p, 0U); // Storage Item absent
    put_item(p, item_id, count, 9U, true, 52, 903U, "variant");
    return p;
}
}

static void test_inventory_content_decodes_bounded_raw_descriptors() {
    Bedrock2193ObservationDecoder decoder;
    std::string error;
    CHECK(decoder.accept_frame(frame(11U, start_game()), nullptr, &error), "StartGame fixture is accepted");
    ObservationDecodeReport report;
    CHECK(decoder.accept_frame(frame(49U, inventory_content()), &report, &error), "InventoryContent fixture is decoded");
    CHECK(report.disposition == ObservationDecodeDisposition::Applied, "InventoryContent reports applied after full parse");
    CHECK(decoder.observation().inventory_containers.size() == 1U, "player inventory container retained separately");
    const auto& container = decoder.observation().inventory_containers.front();
    CHECK(container.container_id == 0U && container.container_name == 12U && container.slots.size() == 4U,
          "container IDs, semantic container kind, and all slots are retained");
    CHECK(container.slots[1].protocol_item_id == 276 && container.slots[1].count == 1 &&
          container.slots[1].protocol_aux_value == 7U && container.slots[1].protocol_block_runtime_id == 900U,
          "non-empty stack preserves numeric item ID, count, aux, and block runtime ID");
    CHECK(container.slots[1].protocol_network_stack_id_known &&
          container.slots[1].protocol_network_stack_id == 37,
          "server item stack ID is decoded as signed VarInt");
    CHECK(container.slots[1].protocol_user_data_buffer == std::string("A\0B", 3U),
          "opaque binary user data including NUL bytes is preserved without semantic guessing");
    CHECK(container.slots[2].protocol_item_id == 332 && container.slots[2].count == 3 &&
          !container.slots[2].protocol_network_stack_id_known,
          "descriptor with absent network stack ID is preserved");
    CHECK(decoder.observation().inventory.size() == 2U &&
          decoder.observation().inventory[0].slot == 1 && decoder.observation().inventory[1].slot == 2,
          "only nonempty stacks from the canonical player container feed strategy inventory");

    const std::size_t before_containers = decoder.observation().inventory_containers.size();
    CHECK(decoder.accept_frame(frame(49U, inventory_content(7U)), &report, &error),
          "non-player container snapshot also decodes");
    CHECK(decoder.observation().inventory_containers.size() == before_containers + 1U,
          "other container snapshots remain isolated");
    CHECK(decoder.observation().inventory.size() == 2U && decoder.observation().inventory[0].slot == 1,
          "chest/entity container slots are not confused with hotbar slots");
}

static void test_inventory_slot_revises_known_container_atomically() {
    Bedrock2193ObservationDecoder decoder;
    std::string error;
    CHECK(decoder.accept_frame(frame(11U, start_game()), nullptr, &error), "StartGame fixture is accepted");
    CHECK(decoder.accept_frame(frame(49U, inventory_content()), nullptr, &error), "initial inventory is accepted");
    ObservationDecodeReport report;
    CHECK(decoder.accept_frame(frame(50U, inventory_slot(276, 4U)), &report, &error),
          "InventorySlot revision is decoded");
    CHECK(report.disposition == ObservationDecodeDisposition::Applied, "explicit container identity yields Applied report");
    const auto& container = decoder.observation().inventory_containers.front();
    CHECK(container.slots[1].count == 4 && container.slots[1].protocol_aux_value == 9U &&
          container.slots[1].protocol_user_data_buffer == "variant",
          "slot update replaces the stack and preserves the new descriptor metadata");
    CHECK(decoder.observation().inventory.size() == 2U && decoder.observation().inventory[0].count == 4,
          "strategy inventory is synchronized after a main-container slot update");

    const auto before = decoder.observation().inventory_containers.front().slots[1].count;
    auto bad = inventory_slot(276, 2U);
    bad.pop_back();
    CHECK(!decoder.accept_frame(frame(50U, std::move(bad)), &report, &error),
          "truncated slot descriptor is rejected");
    CHECK(report.disposition == ObservationDecodeDisposition::Malformed &&
          decoder.observation().inventory_containers.front().slots[1].count == before,
          "malformed slot update does not partially overwrite existing state");
}

static void test_equipment_and_armor_observations() {
    Bedrock2193ObservationDecoder decoder;
    std::string error;
    CHECK(decoder.accept_frame(frame(11U, start_game()), nullptr, &error), "StartGame fixture is accepted");
    CHECK(decoder.accept_frame(frame(49U, inventory_content()), nullptr, &error), "inventory fixture is accepted");

    std::vector<std::uint8_t> equipment;
    put_varu64(equipment, 42U);
    put_item(equipment, 276, 2U, 1U, true, 77, 0U, "held");
    put_u8(equipment, 1U); // slot
    put_u8(equipment, 1U); // selected slot
    put_u8(equipment, 0U); // container ID matching observed player inventory
    ObservationDecodeReport report;
    CHECK(decoder.accept_frame(frame(31U, equipment), &report, &error), "MobEquipment packet is decoded");
    CHECK(decoder.observation().world.player.selected_slot == 1 &&
          decoder.observation().held_item_observation.has_value(),
          "selected hotbar slot and held item descriptor are retained");
    CHECK(decoder.observation().held_item_observation->protocol_item_id == 276 &&
          decoder.observation().held_item_observation->count == 2 &&
          decoder.observation().held_item_observation->equipped,
          "held item raw identity and equipment state are preserved");
    CHECK(decoder.observation().inventory_containers.front().slots[1].count == 2,
          "equipment updates an already-observed matching player container slot");

    std::vector<std::uint8_t> offhand_equipment;
    put_varu64(offhand_equipment, 42U);
    put_item(offhand_equipment, 999, 1U, 0U, true, 80, 0U, "offhand");
    put_u8(offhand_equipment, 1U); // same numeric slot as the selected hotbar slot
    put_u8(offhand_equipment, 1U);
    put_u8(offhand_equipment, 119U); // offhand container, not the main inventory window
    CHECK(decoder.accept_frame(frame(31U, offhand_equipment), &report, &error),
          "offhand MobEquipment is structurally decoded");
    CHECK(decoder.observation().held_item_observation->protocol_item_id == 276 &&
          decoder.observation().inventory_containers.front().slots[1].count == 2,
          "offhand equipment does not overwrite the selected hotbar item or player inventory");

    std::vector<std::uint8_t> armor;
    put_varu64(armor, 42U);
    put_item(armor, 310, 1U, 0U, true, 101, 0U, "head");
    put_item(armor, 311, 1U, 0U, true, 102, 0U, "torso");
    put_item(armor, 312, 1U, 0U, true, 103, 0U, "legs");
    put_empty_item(armor);
    put_item(armor, 313, 1U, 0U, true, 104, 0U, "body");
    CHECK(decoder.accept_frame(frame(32U, armor), &report, &error), "MobArmorEquipment packet is decoded");
    CHECK(decoder.observation().armor_inventory.size() == 5U,
          "all five armor/body slots are represented in protocol order");
    CHECK(decoder.observation().armor_inventory[0].protocol_item_id == 310 &&
          decoder.observation().armor_inventory[1].protocol_item_id == 311 &&
          decoder.observation().armor_inventory[2].protocol_item_id == 312 &&
          decoder.observation().armor_inventory[3].count == 0 &&
          decoder.observation().armor_inventory[4].protocol_item_id == 313,
          "armor descriptor IDs and empty slot are retained correctly");
}

static void test_bounds_and_unknown_item_semantics() {
    Bedrock2193ObservationDecoder decoder;
    std::string error;
    CHECK(decoder.accept_frame(frame(11U, start_game()), nullptr, &error), "StartGame fixture is accepted");
    ObservationDecodeReport report;
    auto too_many_slots = std::vector<std::uint8_t>{0U};
    put_varu32(too_many_slots, 257U);
    CHECK(!decoder.accept_frame(frame(49U, too_many_slots), &report, &error),
          "InventoryContent rejects oversized slot count before allocation");
    CHECK(report.disposition == ObservationDecodeDisposition::Malformed,
          "oversized container payload gets a malformed diagnostic");

    CHECK(decoder.accept_frame(frame(49U, inventory_content()), &report, &error), "valid inventory follows rejected payload");
    CHECK(decoder.observation().inventory[0].kind == ItemKind::Unknown &&
          decoder.observation().inventory[0].identifier.empty() &&
          !decoder.observation().inventory[0].capability_profile_verified,
          "unknown numeric IDs are never turned into invented names or action capabilities");
}

int main() {
    test_inventory_content_decodes_bounded_raw_descriptors();
    test_inventory_slot_revises_known_container_atomically();
    test_equipment_and_armor_observations();
    test_bounds_and_unknown_item_semantics();
    if (failures != 0) {
        std::cerr << failures << " v31 inventory/equipment test(s) failed\n";
        return 1;
    }
    std::cout << "All v31 inventory/equipment decoder tests passed\n";
    return 0;
}
