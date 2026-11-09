#include "core/bedrock2193_observation_decoder.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <utility>

namespace bedrockai {
namespace {
constexpr std::uint32_t kStartGame = 11U;
constexpr std::uint32_t kAddPlayer = 12U;
constexpr std::uint32_t kAddActor = 13U;
constexpr std::uint32_t kRemoveActor = 14U;
constexpr std::uint32_t kMoveActorAbsolute = 18U;
constexpr std::uint32_t kMovePlayer = 19U;
constexpr std::uint32_t kUpdateAttributes = 29U;
constexpr std::uint32_t kMobEffect = 28U;
constexpr std::uint32_t kMobEquipment = 31U;
constexpr std::uint32_t kMobArmorEquipment = 32U;
constexpr std::uint32_t kSetActorMotion = 40U;
constexpr std::uint32_t kSetHealth = 42U;
constexpr std::uint32_t kInventoryContent = 49U;
constexpr std::uint32_t kInventorySlot = 50U;
constexpr std::uint8_t kPlayerInventoryWindowId = 0U;
constexpr std::uint8_t kCombinedHotbarAndInventoryContainer = 12U;
constexpr std::uint8_t kHotbarContainer = 31U;
constexpr std::uint8_t kArmorContainer = 6U;
constexpr std::uint8_t kUnknownContainer = 255U;
constexpr std::size_t kMaximumContainerSlots = 256U;
constexpr std::size_t kMaximumObservedContainers = 32U;
constexpr std::size_t kMaximumItemUserDataBytes = 65536U;
constexpr std::size_t kMaximumItemRegistryEntries = 8192U;
constexpr std::size_t kMaximumItemRegistryEntryNbtBytes = 262144U;
constexpr std::size_t kMaximumItemRegistryNbtBytes = 4194304U;
constexpr std::size_t kMaximumAttributeCount = 256U;
constexpr std::size_t kMaximumAttributeModifiers = 128U;
constexpr std::uint32_t kMoveActorDelta = 111U;
constexpr std::uint32_t kUpdatePlayerGameType = 151U;
constexpr std::uint32_t kMotionPredictionHints = 157U;
constexpr std::uint32_t kCorrectPlayerMovePrediction = 161U;
constexpr std::uint32_t kItemRegistry = 162U;
constexpr double kByteRotationScale = 360.0 / 256.0;

class Reader {
public:
    explicit Reader(const std::vector<std::uint8_t>& bytes) : bytes_(bytes) {}

    bool u8(std::uint8_t& value) {
        if (remaining() < 1U) return false;
        value = bytes_[position_++];
        return true;
    }
    bool u16le(std::uint16_t& value) {
        std::uint8_t a = 0, b = 0;
        if (!u8(a) || !u8(b)) return false;
        value = static_cast<std::uint16_t>(static_cast<std::uint16_t>(a) |
                                           static_cast<std::uint16_t>(static_cast<std::uint16_t>(b) << 8U));
        return true;
    }
    bool i16le(std::int16_t& value) {
        std::uint16_t bits = 0U;
        if (!u16le(bits)) return false;
        std::memcpy(&value, &bits, sizeof(value));
        return true;
    }
    bool u32le(std::uint32_t& value) {
        if (remaining() < 4U) return false;
        value = static_cast<std::uint32_t>(bytes_[position_]) |
            (static_cast<std::uint32_t>(bytes_[position_ + 1U]) << 8U) |
            (static_cast<std::uint32_t>(bytes_[position_ + 2U]) << 16U) |
            (static_cast<std::uint32_t>(bytes_[position_ + 3U]) << 24U);
        position_ += 4U;
        return true;
    }
    bool i32le(std::int32_t& value) {
        if (remaining() < 4U) return false;
        const std::uint32_t bits = static_cast<std::uint32_t>(bytes_[position_]) |
            (static_cast<std::uint32_t>(bytes_[position_ + 1U]) << 8U) |
            (static_cast<std::uint32_t>(bytes_[position_ + 2U]) << 16U) |
            (static_cast<std::uint32_t>(bytes_[position_ + 3U]) << 24U);
        position_ += 4U;
        std::memcpy(&value, &bits, sizeof(value));
        return true;
    }
    bool var_u32(std::uint32_t& value) {
        value = 0U;
        for (unsigned int i = 0U; i < 5U; ++i) {
            std::uint8_t byte = 0;
            if (!u8(byte)) return false;
            if (i == 4U && (byte & 0xf0U) != 0U) return false;
            value |= static_cast<std::uint32_t>(byte & 0x7fU) << (7U * i);
            if ((byte & 0x80U) == 0U) return true;
        }
        return false;
    }
    bool var_u64(std::uint64_t& value) {
        value = 0U;
        for (unsigned int i = 0U; i < 10U; ++i) {
            std::uint8_t byte = 0;
            if (!u8(byte)) return false;
            if (i == 9U && (byte & 0xfeU) != 0U) return false;
            value |= static_cast<std::uint64_t>(byte & 0x7fU) << (7U * i);
            if ((byte & 0x80U) == 0U) return true;
        }
        return false;
    }
    bool var_i32(std::int32_t& value) {
        std::uint32_t raw = 0U;
        if (!var_u32(raw)) return false;
        const std::uint32_t sign = 0U - (raw & 1U);
        value = static_cast<std::int32_t>((raw >> 1U) ^ sign);
        return true;
    }
    bool var_i64(std::int64_t& value) {
        std::uint64_t raw = 0U;
        if (!var_u64(raw)) return false;
        const std::uint64_t sign = 0ULL - (raw & 1ULL);
        const std::uint64_t decoded = (raw >> 1U) ^ sign;
        if (decoded <= static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
            value = static_cast<std::int64_t>(decoded);
        } else {
            value = -1 - static_cast<std::int64_t>(~decoded);
        }
        return true;
    }
    bool f32(float& value) {
        if (remaining() < 4U) return false;
        const std::uint32_t bits = static_cast<std::uint32_t>(bytes_[position_]) |
            (static_cast<std::uint32_t>(bytes_[position_ + 1U]) << 8U) |
            (static_cast<std::uint32_t>(bytes_[position_ + 2U]) << 16U) |
            (static_cast<std::uint32_t>(bytes_[position_ + 3U]) << 24U);
        position_ += 4U;
        std::memcpy(&value, &bits, sizeof(value));
        return std::isfinite(value);
    }
    bool vec3(Vec3& value) {
        float x = 0.0F, y = 0.0F, z = 0.0F;
        if (!f32(x) || !f32(y) || !f32(z)) return false;
        value = {static_cast<double>(x), static_cast<double>(y), static_cast<double>(z)};
        return true;
    }
    bool rot2(Rot2& value) {
        float pitch = 0.0F, yaw = 0.0F;
        if (!f32(pitch) || !f32(yaw)) return false;
        value = {static_cast<double>(pitch), static_cast<double>(yaw)};
        return true;
    }
    bool string(std::string& value, std::size_t max_bytes) {
        std::uint32_t length = 0U;
        if (!var_u32(length) || length > max_bytes || remaining() < static_cast<std::size_t>(length)) return false;
        value.assign(reinterpret_cast<const char*>(bytes_.data() + position_), static_cast<std::size_t>(length));
        position_ += static_cast<std::size_t>(length);
        return true;
    }
    bool skip(std::size_t length) {
        if (remaining() < length) return false;
        position_ += length;
        return true;
    }
    bool copy_range(std::size_t start, std::size_t end, std::string& output, std::size_t max_bytes) const {
        if (start > end || end > bytes_.size() || end - start > max_bytes) return false;
        if (start == end) output.clear();
        else output.assign(reinterpret_cast<const char*>(bytes_.data() + start), end - start);
        return true;
    }
    bool bytes(std::uint8_t* output, std::size_t length) {
        if (remaining() < length) return false;
        std::copy_n(bytes_.begin() + static_cast<std::ptrdiff_t>(position_), length, output);
        position_ += length;
        return true;
    }
    bool empty() const { return position_ == bytes_.size(); }
    std::size_t remaining() const { return bytes_.size() - position_; }
    std::size_t position() const { return position_; }

private:
    const std::vector<std::uint8_t>& bytes_;
    std::size_t position_ = 0U;
};

struct FullContainerNameWire {
    std::uint8_t name = kUnknownContainer;
    std::uint32_t dynamic_id = 0U;
};

bool read_nbt_string(Reader& reader) {
    std::uint16_t length = 0U;
    return reader.u16le(length) && reader.skip(static_cast<std::size_t>(length));
}

bool skip_nbt_payload(Reader& reader, std::uint8_t type, unsigned int depth,
                      std::size_t& node_budget);

bool skip_nbt_compound_contents(Reader& reader, unsigned int depth,
                                std::size_t& node_budget) {
    if (depth > 32U) return false;
    for (;;) {
        std::uint8_t child_type = 0U;
        if (!reader.u8(child_type)) return false;
        if (child_type == 0U) return true;
        if (child_type > 12U || !read_nbt_string(reader) ||
            !skip_nbt_payload(reader, child_type, depth + 1U, node_budget)) return false;
    }
}

bool skip_nbt_payload(Reader& reader, std::uint8_t type, unsigned int depth,
                      std::size_t& node_budget) {
    if (depth > 32U || node_budget == 0U) return false;
    --node_budget;
    switch (type) {
        case 1U: return reader.skip(1U);                         // TAG_Byte
        case 2U: return reader.skip(2U);                         // TAG_Short
        case 3U: case 5U: return reader.skip(4U);               // TAG_Int / TAG_Float
        case 4U: case 6U: return reader.skip(8U);               // TAG_Long / TAG_Double
        case 7U: {                                               // TAG_Byte_Array
            std::int32_t count = 0;
            return reader.i32le(count) && count >= 0 &&
                   static_cast<std::size_t>(count) <= kMaximumItemRegistryEntryNbtBytes &&
                   reader.skip(static_cast<std::size_t>(count));
        }
        case 8U: return read_nbt_string(reader);                 // TAG_String
        case 9U: {                                               // TAG_List
            std::uint8_t element_type = 0U;
            std::int32_t count = 0;
            if (!reader.u8(element_type) || element_type > 12U || !reader.i32le(count) ||
                count < 0 || count > 65536 || (element_type == 0U && count != 0)) return false;
            for (std::int32_t i = 0; i < count; ++i) {
                if (!skip_nbt_payload(reader, element_type, depth + 1U, node_budget)) return false;
            }
            return true;
        }
        case 10U: return skip_nbt_compound_contents(reader, depth + 1U, node_budget); // TAG_Compound
        case 11U: case 12U: {                                    // TAG_Int_Array / TAG_Long_Array
            std::int32_t count = 0;
            if (!reader.i32le(count) || count < 0 || count > 65536) return false;
            const std::size_t width = type == 11U ? 4U : 8U;
            const auto unsigned_count = static_cast<std::size_t>(count);
            if (unsigned_count > std::numeric_limits<std::size_t>::max() / width) return false;
            return reader.skip(unsigned_count * width);
        }
        default: return false; // TAG_End has no payload; unknown tags are not skippable.
    }
}

// ItemRegistry serializes its ItemComponentData field as Network Little Endian NBT
// for each ItemData entry. Non-component-based items commonly use an empty compound.
// Keep the complete validated byte range opaque; do not infer semantics from it.
bool read_network_nbt_compound(Reader& reader, std::string& raw) {
    const std::size_t start = reader.position();
    std::uint8_t root_type = 0U;
    if (!reader.u8(root_type) || root_type != 10U || !read_nbt_string(reader)) return false;
    std::size_t node_budget = 200000U;
    if (!skip_nbt_compound_contents(reader, 1U, node_budget)) return false;
    return reader.copy_range(start, reader.position(), raw, kMaximumItemRegistryEntryNbtBytes);
}

bool read_full_container_name(Reader& reader, FullContainerNameWire& name) {
    if (!reader.u8(name.name) || name.name > 66U || !reader.u32le(name.dynamic_id)) return false;
    return true;
}

// Bedrock 1.26.50 / protocol 2193 serializes a non-empty NetworkItemStackDescriptor as
// int16 item ID, uint16 amount, varuint aux, optional signed-varint stack ID, varuint block
// runtime ID, then a length-prefixed binary user-data string. Item ID 0 is the compact empty form.
bool read_network_item_stack(Reader& reader, InventoryStackObservation& item) {
    std::int16_t item_id = 0;
    if (!reader.i16le(item_id)) return false;
    item = InventoryStackObservation{};
    item.protocol_item_id = item_id;
    item.protocol_item_id_known = true;
    item.protocol_descriptor_decoded = true;
    item.kind = ItemKind::Unknown;
    item.ready = true;
    if (item_id == 0) {
        item.count = 0;
        return true;
    }

    std::uint16_t stack_size = 0U;
    std::uint32_t aux_value = 0U;
    std::uint8_t has_stack_id = 0U;
    if (!reader.u16le(stack_size) || stack_size == 0U ||
        !reader.var_u32(aux_value) || !reader.u8(has_stack_id) || has_stack_id > 1U) return false;

    std::int32_t stack_id = 0;
    if (has_stack_id == 1U && !reader.var_i32(stack_id)) return false;
    std::uint32_t block_runtime_id = 0U;
    std::string user_data;
    if (!reader.var_u32(block_runtime_id) || !reader.string(user_data, kMaximumItemUserDataBytes)) return false;

    item.count = static_cast<int>(stack_size);
    item.protocol_aux_value = aux_value;
    item.protocol_block_runtime_id = block_runtime_id;
    item.protocol_network_stack_id = stack_id;
    item.protocol_network_stack_id_known = has_stack_id == 1U;
    item.protocol_user_data_buffer = std::move(user_data);
    return true;
}

bool read_optional_network_item_stack(Reader& reader,
                                      std::optional<InventoryStackObservation>& item) {
    std::uint8_t present = 0U;
    if (!reader.u8(present) || present > 1U) return false;
    if (present == 0U) {
        item.reset();
        return true;
    }
    InventoryStackObservation parsed;
    if (!read_network_item_stack(reader, parsed)) return false;
    if (parsed.count > 0) item = std::move(parsed);
    else item.reset();
    return true;
}

bool string_in_set(const std::string& value, std::initializer_list<const char*> values) {
    return std::find_if(values.begin(), values.end(), [&value](const char* candidate) {
        return value == candidate;
    }) != values.end();
}

ItemKind known_vanilla_item_kind(const std::string& identifier) {
    if (identifier.compare(0U, 10U, "minecraft:") != 0) return ItemKind::Unknown;
    if (string_in_set(identifier, {"minecraft:wooden_sword", "minecraft:stone_sword", "minecraft:iron_sword",
            "minecraft:golden_sword", "minecraft:diamond_sword", "minecraft:netherite_sword"})) return ItemKind::Sword;
    if (string_in_set(identifier, {"minecraft:wooden_axe", "minecraft:stone_axe", "minecraft:iron_axe",
            "minecraft:golden_axe", "minecraft:diamond_axe", "minecraft:netherite_axe"})) return ItemKind::Axe;
    if (identifier == "minecraft:mace") return ItemKind::Mace;
    if (identifier == "minecraft:bow") return ItemKind::Bow;
    if (identifier == "minecraft:crossbow") return ItemKind::Crossbow;
    if (identifier == "minecraft:shield") return ItemKind::Shield;
    if (identifier == "minecraft:golden_apple") return ItemKind::GoldenApple;
    if (identifier == "minecraft:enchanted_golden_apple") return ItemKind::EnchantedGoldenApple;
    if (identifier == "minecraft:ender_pearl") return ItemKind::EnderPearl;
    if (identifier == "minecraft:wind_charge") return ItemKind::WindCharge;
    if (identifier == "minecraft:water_bucket") return ItemKind::WaterBucket;
    if (identifier == "minecraft:milk_bucket") return ItemKind::MilkBucket;
    if (identifier == "minecraft:totem_of_undying") return ItemKind::Totem;
    if (identifier == "minecraft:elytra") return ItemKind::Elytra;
    if (identifier == "minecraft:firework_rocket") return ItemKind::FireworkRocket;
    if (identifier == "minecraft:trident") return ItemKind::Trident;
    if (identifier == "minecraft:arrow" || identifier == "minecraft:tipped_arrow" || identifier == "minecraft:spectral_arrow") return ItemKind::Arrow;
    if (string_in_set(identifier, {"minecraft:wooden_pickaxe", "minecraft:stone_pickaxe", "minecraft:iron_pickaxe",
            "minecraft:golden_pickaxe", "minecraft:diamond_pickaxe", "minecraft:netherite_pickaxe"})) return ItemKind::Pickaxe;
    if (string_in_set(identifier, {"minecraft:apple", "minecraft:bread", "minecraft:beetroot", "minecraft:beetroot_soup",
            "minecraft:carrot", "minecraft:golden_carrot", "minecraft:potato", "minecraft:baked_potato",
            "minecraft:poisonous_potato", "minecraft:melon_slice", "minecraft:glistering_melon_slice",
            "minecraft:sweet_berries", "minecraft:glow_berries", "minecraft:cookie", "minecraft:pumpkin_pie",
            "minecraft:rotten_flesh", "minecraft:spider_eye", "minecraft:chorus_fruit", "minecraft:dried_kelp",
            "minecraft:beef", "minecraft:cooked_beef", "minecraft:porkchop", "minecraft:cooked_porkchop",
            "minecraft:chicken", "minecraft:cooked_chicken", "minecraft:mutton", "minecraft:cooked_mutton",
            "minecraft:rabbit", "minecraft:cooked_rabbit", "minecraft:cod", "minecraft:cooked_cod",
            "minecraft:salmon", "minecraft:cooked_salmon", "minecraft:tropical_fish", "minecraft:pufferfish",
            "minecraft:suspicious_stew", "minecraft:rabbit_stew", "minecraft:mushroom_stew"})) return ItemKind::Food;
    if (string_in_set(identifier, {"minecraft:cobblestone", "minecraft:stone", "minecraft:dirt", "minecraft:grass_block",
            "minecraft:netherrack", "minecraft:end_stone", "minecraft:obsidian", "minecraft:glass",
            "minecraft:sandstone", "minecraft:red_sandstone", "minecraft:bricks", "minecraft:stone_bricks",
            "minecraft:planks", "minecraft:wool", "minecraft:concrete", "minecraft:concrete_powder",
            "minecraft:terracotta", "minecraft:packed_ice", "minecraft:ice", "minecraft:slime_block",
            "minecraft:honey_block", "minecraft:hay_block", "minecraft:purpur_block", "minecraft:quartz_block"}) ||
        identifier.find("_planks") != std::string::npos || identifier.find("_wool") != std::string::npos ||
        identifier.find("_concrete") != std::string::npos || identifier.find("_terracotta") != std::string::npos) {
        return ItemKind::BuildingBlock;
    }
    return ItemKind::Unknown;
}

bool finite(const Vec3& v) {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z) &&
           std::abs(v.x) <= 3.0e7 && std::abs(v.y) <= 3.0e7 && std::abs(v.z) <= 3.0e7;
}

bool known_hostile_type(const std::string& type) {
    static const char* const types[] = {
        "minecraft:zombie", "minecraft:skeleton", "minecraft:creeper", "minecraft:spider",
        "minecraft:cave_spider", "minecraft:enderman", "minecraft:witch", "minecraft:blaze",
        "minecraft:pillager", "minecraft:vindicator", "minecraft:ravager", "minecraft:phantom",
        "minecraft:drowned", "minecraft:husk", "minecraft:stray", "minecraft:slime",
        "minecraft:magma_cube", "minecraft:hoglin", "minecraft:zoglin", "minecraft:piglin_brute",
        "minecraft:wither_skeleton", "minecraft:ghast", "minecraft:wither", "minecraft:ender_dragon",
        "minecraft:silverfish", "minecraft:guardian", "minecraft:elder_guardian", "minecraft:warden",
        "minecraft:shulker", "minecraft:evocation_illager", "minecraft:evoker", "minecraft:vex"
    };
    return std::find_if(std::begin(types), std::end(types), [&type](const char* candidate) {
        return type == candidate;
    }) != std::end(types);
}

std::string uuid_hex(const std::uint8_t* bytes) {
    static constexpr char digits[] = "0123456789abcdef";
    std::string result;
    result.reserve(32U);
    for (std::size_t i = 0; i < 16U; ++i) {
        result.push_back(digits[(bytes[i] >> 4U) & 0x0fU]);
        result.push_back(digits[bytes[i] & 0x0fU]);
    }
    return result;
}

std::uint64_t next_tick(std::uint64_t current) {
    return current == std::numeric_limits<std::uint64_t>::max() ? current : current + 1U;
}
}

Bedrock2193ObservationDecoder::Bedrock2193ObservationDecoder(bool target_other_players_as_hostile)
    : target_other_players_as_hostile_(target_other_players_as_hostile) {}

void Bedrock2193ObservationDecoder::reset() {
    const GameMode mode = observation_.game_mode;
    const GameplayGoal goal = observation_.goal;
    const std::string profile_key = observation_.server_profile_key;
    observation_ = GameplayObservation{};
    observation_.game_mode = mode;
    observation_.goal = goal;
    observation_.server_profile_key = profile_key;
    frame_number_ = 0U;
    malformed_frames_ = 0U;
    packet_counts_.clear();
    last_error_.clear();
}

void Bedrock2193ObservationDecoder::set_gameplay_context(GameMode mode, GameplayGoal goal,
                                                         std::string server_profile_key) {
    observation_.game_mode = mode;
    observation_.goal = goal;
    observation_.server_profile_key = std::move(server_profile_key);
}

void Bedrock2193ObservationDecoder::set_target_other_players_as_hostile(bool enabled) {
    target_other_players_as_hostile_ = enabled;
    for (auto& entry : observation_.world.world.entities) update_entity_relation(entry.second);
}

void Bedrock2193ObservationDecoder::replace_inventory(std::vector<InventoryStackObservation> inventory) {
    observation_.inventory = std::move(inventory);
}

bool Bedrock2193ObservationDecoder::read(GameplayObservation& observation, std::string* error) {
    if (!observation_.world.joined) {
        if (error != nullptr) *error = "no StartGame observation has been decoded yet";
        return false;
    }
    observation = observation_;
    if (error != nullptr) error->clear();
    return true;
}

bool Bedrock2193ObservationDecoder::fail_packet(const TransportFrame& frame, const std::string& message,
                                                ObservationDecodeReport& report, std::string* error) {
    ++malformed_frames_;
    last_error_ = message;
    report.disposition = ObservationDecodeDisposition::Malformed;
    report.packet_id = frame.packet_id;
    report.frame_number = frame_number_;
    report.note = message;
    if (error != nullptr) *error = message;
    return false;
}

bool Bedrock2193ObservationDecoder::accept_frame(const TransportFrame& frame,
                                                ObservationDecodeReport* report_out,
                                                std::string* error) {
    ObservationDecodeReport local_report;
    ObservationDecodeReport& report = report_out != nullptr ? *report_out : local_report;
    report = {};
    report.packet_id = frame.packet_id;
    if (frame.magic != TransportFrame::kMagic || frame.format_version != TransportFrame::kFormatVersion ||
        !frame.identity.is_target()) {
        ++malformed_frames_;
        last_error_ = "observation decoder rejected invalid frame header or non-2193 identity";
        report.disposition = ObservationDecodeDisposition::Malformed;
        report.note = last_error_;
        if (error != nullptr) *error = last_error_;
        return false;
    }
    ++frame_number_;
    // Packet IDs are untrusted input; cap diagnostic map growth even if a peer sends
    // an invalid, ever-changing ID sequence.
    if (frame.packet_id <= 4096U) ++packet_counts_[frame.packet_id];
    report.frame_number = frame_number_;
    if (frame.direction != Direction::Clientbound) {
        report.disposition = ObservationDecodeDisposition::IgnoredDirection;
        report.note = "serverbound frame ignored by clientbound observation decoder";
        if (error != nullptr) error->clear();
        return true;
    }

    bool ok = true;
    switch (frame.packet_id) {
        case kStartGame: ok = decode_start_game(frame, report, error); break;
        case kAddPlayer: ok = decode_add_player(frame, report, error); break;
        case kAddActor: ok = decode_add_actor(frame, report, error); break;
        case kRemoveActor: ok = decode_remove_actor(frame, report, error); break;
        case kMoveActorAbsolute: ok = decode_move_actor_absolute(frame, report, error); break;
        case kMoveActorDelta: ok = decode_move_actor_delta(frame, report, error); break;
        case kMovePlayer: ok = decode_move_player(frame, report, error); break;
        case kMobEffect: ok = decode_mob_effect(frame, report, error); break;
        case kUpdateAttributes: ok = decode_update_attributes(frame, report, error); break;
        case kMobEquipment: ok = decode_mob_equipment(frame, report, error); break;
        case kMobArmorEquipment: ok = decode_mob_armor_equipment(frame, report, error); break;
        case kSetActorMotion: ok = decode_set_actor_motion(frame, report, error); break;
        case kSetHealth: ok = decode_set_health(frame, report, error); break;
        case kInventoryContent: ok = decode_inventory_content(frame, report, error); break;
        case kInventorySlot: ok = decode_inventory_slot(frame, report, error); break;
        case kUpdatePlayerGameType: ok = decode_update_player_game_type(frame, report, error); break;
        case kMotionPredictionHints: ok = decode_set_actor_motion(frame, report, error); break;
        case kCorrectPlayerMovePrediction: ok = decode_correct_player_move_prediction(frame, report, error); break;
        case kItemRegistry: ok = decode_item_registry(frame, report, error); break;
        default:
            report.disposition = ObservationDecodeDisposition::UnsupportedPacket;
            report.note = "packet schema is not implemented by this decoder slice";
            if (error != nullptr) error->clear();
            return true;
    }
    if (ok) last_error_.clear();
    return ok;
}

bool Bedrock2193ObservationDecoder::decode_start_game(const TransportFrame& frame,
    ObservationDecodeReport& report, std::string* error) {
    Reader reader(frame.payload);
    std::int64_t unique_id = 0;
    std::uint64_t runtime_id = 0U;
    std::int32_t game_type = -1;
    Vec3 position{};
    Rot2 rotation{};
    if (!reader.var_i64(unique_id) || !reader.var_u64(runtime_id) || runtime_id == 0U ||
        !reader.var_i32(game_type) || !reader.vec3(position) || !reader.rot2(rotation) || !finite(position)) {
        return fail_packet(frame, "StartGame prefix is truncated or invalid for protocol 2193", report, error);
    }
    observation_.world.player.unique_id = unique_id;
    observation_.world.player.runtime_id = runtime_id;
    observation_.world.player.position = position;
    observation_.world.player.rotation = rotation;
    observation_.world.player.on_ground = false;
    observation_.world.player.airborne = false;
    observation_.world.player.ground_state_known = false;
    observation_.world.world.game_type = game_type;
    observation_.world.tick = next_tick(observation_.world.tick);
    observation_.world.joined = true;
    report.disposition = ObservationDecodeDisposition::PartiallyApplied;
    report.note = "StartGame identity, game type, position and rotation decoded; remaining level settings are not parsed";
    if (error != nullptr) error->clear();
    return true;
}

bool Bedrock2193ObservationDecoder::decode_move_player(const TransportFrame& frame,
    ObservationDecodeReport& report, std::string* error) {
    Reader reader(frame.payload);
    std::uint64_t runtime_id = 0U, riding_id = 0U, protocol_tick = 0U;
    Vec3 position{};
    Rot2 rotation{};
    float head_yaw = 0.0F;
    std::uint8_t mode = 0U, on_ground = 0U;
    if (!reader.var_u64(runtime_id) || !reader.vec3(position) || !reader.rot2(rotation) ||
        !reader.f32(head_yaw) || !reader.u8(mode) || !reader.u8(on_ground) ||
        on_ground > 1U || !reader.var_u64(riding_id) || !finite(position) ||
        mode > 3U || !std::isfinite(head_yaw)) {
        return fail_packet(frame, "MovePlayer core fields are truncated or outside protocol-2193 bounds", report, error);
    }
    std::int32_t teleport_cause = 0, source_actor_type = 0;
    if (mode == 2U && (!reader.i32le(teleport_cause) || !reader.i32le(source_actor_type))) {
        return fail_packet(frame, "MovePlayer teleport metadata is truncated", report, error);
    }
    if (!reader.var_u64(protocol_tick) || !reader.empty()) {
        return fail_packet(frame, "MovePlayer tick/trailing payload is invalid for protocol 2193", report, error);
    }
    (void)riding_id;
    (void)teleport_cause;
    (void)source_actor_type;
    const std::uint64_t observation_tick = next_tick(observation_.world.tick);
    const auto apply_server_tick = [this, protocol_tick]() {
        if (protocol_tick != 0U) {
            observation_.world.server_tick = std::max(observation_.world.server_tick, protocol_tick);
            observation_.world.server_tick_known = true;
        }
    };
    if (runtime_id == observation_.world.player.runtime_id) {
        if (protocol_tick != 0U) report.local_player_input_tick = protocol_tick;
        const Vec3 old = observation_.world.player.position;
        const bool only_head_rotation = mode == 3U;
        if (!only_head_rotation) {
            const double dx = old.x - position.x, dy = old.y - position.y, dz = old.z - position.z;
            observation_.world.last_correction.authoritative_position = position;
            observation_.world.last_correction.authoritative_rotation = rotation;
            observation_.world.last_correction.distance_error = std::sqrt(dx * dx + dy * dy + dz * dz);
            observation_.world.last_correction.server_tick = protocol_tick;
            observation_.world.last_correction.present = observation_.world.last_correction.distance_error > 0.001;
            observation_.world.player.position = position;
            observation_.world.player.on_ground = on_ground == 1U;
            observation_.world.player.airborne = !observation_.world.player.on_ground;
            observation_.world.player.ground_state_known = true;
        }
        observation_.world.player.rotation = rotation;
        observation_.world.player.head_yaw = static_cast<double>(head_yaw);
        if (protocol_tick != 0U) observation_.world.player.last_server_tick = protocol_tick;
        report.note = only_head_rotation ? "MovePlayer head-only rotation/tick decoded without overwriting position" :
                                           "MovePlayer position/rotation/ground/tick decoded";
    } else {
        EntityState& entity = observation_.world.world.entities[runtime_id];
        entity.runtime_id = runtime_id;
        entity.is_player = true;
        if (mode != 3U) {
            entity.position = position;
            entity.on_ground = on_ground == 1U;
            entity.ground_state_known = true;
        }
        entity.rotation = rotation;
        entity.head_yaw = static_cast<double>(head_yaw);
        entity.type_identifier = entity.type_identifier.empty() ? "minecraft:player" : entity.type_identifier;
        entity.last_update_tick = protocol_tick != 0U ? protocol_tick : observation_tick;
        update_entity_relation(entity);
        report.note = mode == 3U ? "other-player head-only rotation/tick decoded" :
                                   "other-player position/rotation/ground/tick decoded; team relation remains policy-driven";
    }
    apply_server_tick();
    observation_.world.tick = observation_tick;
    report.disposition = ObservationDecodeDisposition::Applied;
    if (error != nullptr) error->clear();
    return true;
}

bool Bedrock2193ObservationDecoder::decode_set_health(const TransportFrame& frame,
    ObservationDecodeReport& report, std::string* error) {
    Reader reader(frame.payload);
    std::int32_t health = 0;
    if (!reader.var_i32(health) || !reader.empty() || health < 0 || health > 2048) {
        return fail_packet(frame, "SetHealth payload is malformed or health is out of bounds", report, error);
    }
    if (!observation_.world.joined) {
        report.disposition = ObservationDecodeDisposition::PartiallyApplied;
        report.note = "SetHealth decoded before StartGame; value held is intentionally not applied";
        if (error != nullptr) error->clear();
        return true;
    }
    observation_.world.player.health = static_cast<double>(health);
    observation_.world.tick = next_tick(observation_.world.tick);
    report.disposition = ObservationDecodeDisposition::Applied;
    report.note = "authoritative SetHealth update applied";
    if (error != nullptr) error->clear();
    return true;
}

bool Bedrock2193ObservationDecoder::decode_set_actor_motion(const TransportFrame& frame,
    ObservationDecodeReport& report, std::string* error) {
    Reader reader(frame.payload);
    std::uint64_t runtime_id = 0U;
    Vec3 motion{};
    std::uint8_t on_ground = 0U;
    if (!reader.var_u64(runtime_id) || runtime_id == 0U || !reader.vec3(motion) ||
        std::abs(motion.x) > 1000.0 || std::abs(motion.y) > 1000.0 || std::abs(motion.z) > 1000.0 ||
        (frame.packet_id == kMotionPredictionHints && (!reader.u8(on_ground) || on_ground > 1U)) || !reader.empty()) {
        return fail_packet(frame, "SetActorMotion/MotionPredictionHints payload is malformed", report, error);
    }
    const std::uint64_t tick = next_tick(observation_.world.tick);
    if (runtime_id == observation_.world.player.runtime_id && observation_.world.joined) {
        observation_.world.player.velocity = motion;
        if (frame.packet_id == kMotionPredictionHints) {
            observation_.world.player.on_ground = on_ground == 1U;
            observation_.world.player.airborne = !observation_.world.player.on_ground;
            observation_.world.player.ground_state_known = true;
        }
    } else {
        EntityState& entity = observation_.world.world.entities[runtime_id];
        entity.runtime_id = runtime_id;
        entity.velocity = motion;
        if (frame.packet_id == kMotionPredictionHints) {
            entity.on_ground = on_ground == 1U;
            entity.ground_state_known = true;
        }
        entity.last_update_tick = tick;
        update_entity_relation(entity);
    }
    observation_.world.tick = tick;
    report.disposition = ObservationDecodeDisposition::Applied;
    report.note = frame.packet_id == kMotionPredictionHints
        ? "MotionPredictionHints motion vector and on-ground state decoded"
        : "SetActorMotion authoritative velocity decoded";
    if (error != nullptr) error->clear();
    return true;
}

bool Bedrock2193ObservationDecoder::decode_update_player_game_type(const TransportFrame& frame,
    ObservationDecodeReport& report, std::string* error) {
    Reader reader(frame.payload);
    std::int32_t game_type = -1;
    std::int64_t unique_id = 0;
    std::uint64_t protocol_tick = 0U;
    if (!reader.var_i32(game_type) || !reader.var_i64(unique_id) || !reader.var_u64(protocol_tick) ||
        !reader.empty() || game_type < -1 || game_type > 32) {
        return fail_packet(frame, "UpdatePlayerGameType payload is malformed", report, error);
    }
    const bool is_local_player = observation_.world.joined &&
        unique_id == observation_.world.player.unique_id;
    if (is_local_player) {
        observation_.world.world.game_type = game_type;
        if (protocol_tick != 0U) report.local_player_input_tick = protocol_tick;
    }
    if (protocol_tick != 0U) {
        observation_.world.server_tick = std::max(observation_.world.server_tick, protocol_tick);
        observation_.world.server_tick_known = true;
    }
    observation_.world.tick = next_tick(observation_.world.tick);
    report.disposition = ObservationDecodeDisposition::Applied;
    report.note = is_local_player
        ? "local game mode update applied" : "game mode update for another player observed; local mode unchanged";
    if (error != nullptr) error->clear();
    return true;
}

bool Bedrock2193ObservationDecoder::decode_correct_player_move_prediction(const TransportFrame& frame,
    ObservationDecodeReport& report, std::string* error) {
    Reader reader(frame.payload);
    std::uint8_t prediction_type = 0U, on_ground = 0U;
    Vec3 position{}, velocity{};
    Rot2 rotation{};
    float angular_velocity = 0.0F;
    std::uint64_t protocol_tick = 0U;
    if (!reader.u8(prediction_type) || prediction_type > 1U || !reader.vec3(position) ||
        !reader.vec3(velocity)) {
        return fail_packet(frame, "CorrectPlayerMovePrediction position prefix is malformed", report, error);
    }
    if (prediction_type == 1U && (!reader.rot2(rotation) || !reader.f32(angular_velocity))) {
        return fail_packet(frame, "vehicle movement correction rotation/angular velocity is truncated", report, error);
    }
    if (!reader.u8(on_ground) || on_ground > 1U || !reader.var_u64(protocol_tick) || !reader.empty() ||
        !finite(position) || !finite(velocity) || std::abs(velocity.x) > 1000.0 ||
        std::abs(velocity.y) > 1000.0 || std::abs(velocity.z) > 1000.0 ||
        !std::isfinite(angular_velocity) || std::abs(angular_velocity) > 1000.0) {
        return fail_packet(frame, "CorrectPlayerMovePrediction tail is malformed or out of range", report, error);
    }
    const Vec3 old_position = observation_.world.player.position;
    const double dx = old_position.x - position.x, dy = old_position.y - position.y, dz = old_position.z - position.z;
    observation_.world.last_correction.authoritative_position = position;
    observation_.world.last_correction.authoritative_velocity = velocity;
    observation_.world.last_correction.authoritative_rotation = prediction_type == 1U
        ? rotation : observation_.world.player.rotation;
    observation_.world.last_correction.distance_error = std::sqrt(dx * dx + dy * dy + dz * dz);
    observation_.world.last_correction.server_tick = protocol_tick;
    observation_.world.last_correction.present = true;
    if (prediction_type == 0U) {
        if (protocol_tick != 0U) report.local_player_input_tick = protocol_tick;
        observation_.world.player.position = position;
        observation_.world.player.velocity = velocity;
        observation_.world.player.on_ground = on_ground == 1U;
        observation_.world.player.airborne = !observation_.world.player.on_ground;
        observation_.world.player.ground_state_known = true;
        observation_.world.player.last_server_tick = protocol_tick;
    }
    if (protocol_tick != 0U) {
        observation_.world.server_tick = std::max(observation_.world.server_tick, protocol_tick);
        observation_.world.server_tick_known = true;
    }
    observation_.world.tick = next_tick(observation_.world.tick);
    report.disposition = ObservationDecodeDisposition::Applied;
    report.note = prediction_type == 0U
        ? "server-authoritative local player correction and input tick applied"
        : "vehicle correction recorded separately; player coordinates were not overwritten";
    if (error != nullptr) error->clear();
    return true;
}

bool Bedrock2193ObservationDecoder::decode_mob_effect(const TransportFrame& frame,
    ObservationDecodeReport& report, std::string* error) {
    Reader reader(frame.payload);
    std::uint64_t runtime_id = 0U, packet_tick = 0U;
    std::uint8_t event = 0U, particles = 0U, ambient = 0U;
    std::int32_t effect_id = 0, amplifier = 0, duration = 0;
    if (!reader.var_u64(runtime_id) || !reader.u8(event) || !reader.var_i32(effect_id) ||
        !reader.var_i32(amplifier) || !reader.u8(particles) || !reader.var_i32(duration) ||
        !reader.var_u64(packet_tick) || !reader.u8(ambient) || !reader.empty() || event < 1U || event > 3U ||
        particles > 1U || ambient > 1U || effect_id < 0 || effect_id > 4096 || amplifier < 0 || amplifier > 255 ||
        duration < 0 || duration > 20000000) {
        return fail_packet(frame, "MobEffect payload is malformed or outside protocol-2193 bounds", report, error);
    }
    MobEffectState state{amplifier, static_cast<std::uint32_t>(duration), packet_tick,
                         ambient == 1U, particles == 1U};
    if (runtime_id == observation_.world.player.runtime_id && observation_.world.joined) {
        if (packet_tick != 0U) report.local_player_input_tick = packet_tick;
        if (event == 3U) observation_.world.player.effects.erase(effect_id);
        else observation_.world.player.effects[effect_id] = state;
    } else {
        auto it = observation_.world.world.entities.find(runtime_id);
        if (it != observation_.world.world.entities.end()) {
            if (event == 3U) it->second.effects.erase(effect_id);
            else it->second.effects[effect_id] = state;
        }
    }
    observation_.world.tick = next_tick(observation_.world.tick);
    if (packet_tick != 0U) {
        observation_.world.server_tick = std::max(observation_.world.server_tick, packet_tick);
        observation_.world.server_tick_known = true;
    }
    report.disposition = ObservationDecodeDisposition::Applied;
    report.note = event == 3U ? "effect removal applied" : "effect add/update applied";
    if (error != nullptr) error->clear();
    return true;
}

bool Bedrock2193ObservationDecoder::decode_add_player(const TransportFrame& frame,
    ObservationDecodeReport& report, std::string* error) {
    Reader reader(frame.payload);
    std::uint8_t uuid[16]{};
    std::string player_name, platform_id;
    std::uint64_t runtime_id = 0U;
    Vec3 position{}, velocity{};
    Rot2 rotation{};
    float head_yaw = 0.0F;
    if (!reader.bytes(uuid, sizeof(uuid)) || !reader.string(player_name, 256U) ||
        !reader.var_u64(runtime_id) || runtime_id == 0U || !reader.string(platform_id, 256U) ||
        !reader.vec3(position) || !reader.vec3(velocity) || !reader.rot2(rotation) ||
        !reader.f32(head_yaw) || !finite(position) || !finite(velocity) || player_name.empty()) {
        return fail_packet(frame, "AddPlayer state prefix is truncated or invalid", report, error);
    }
    EntityState& entity = observation_.world.world.entities[runtime_id];
    entity.runtime_id = runtime_id;
    entity.type_identifier = "minecraft:player";
    entity.player_name = player_name;
    entity.player_uuid = uuid_hex(uuid);
    entity.is_player = true;
    entity.position = position;
    entity.velocity = velocity;
    entity.rotation = rotation;
    entity.head_yaw = static_cast<double>(head_yaw);
    entity.ground_state_known = false;
    entity.health = 0.0;
    entity.alive = true;
    entity.last_update_tick = next_tick(observation_.world.tick);
    update_entity_relation(entity);
    observation_.world.tick = entity.last_update_tick;
    report.disposition = ObservationDecodeDisposition::PartiallyApplied;
    report.note = "AddPlayer identity/name/motion prefix decoded; carried item and metadata are not parsed";
    if (error != nullptr) error->clear();
    return true;
}

bool Bedrock2193ObservationDecoder::decode_add_actor(const TransportFrame& frame,
    ObservationDecodeReport& report, std::string* error) {
    Reader reader(frame.payload);
    std::int64_t unique_id = 0;
    std::uint64_t runtime_id = 0U;
    std::string type;
    Vec3 position{}, velocity{};
    Rot2 rotation{};
    float head_yaw = 0.0F, body_yaw = 0.0F;
    if (!reader.var_i64(unique_id) || !reader.var_u64(runtime_id) || runtime_id == 0U ||
        !reader.string(type, 256U) || type.empty() || !reader.vec3(position) || !reader.vec3(velocity) ||
        !reader.rot2(rotation) || !reader.f32(head_yaw) || !reader.f32(body_yaw) ||
        !finite(position) || !finite(velocity)) {
        return fail_packet(frame, "AddActor state prefix is truncated or invalid", report, error);
    }
    EntityState& entity = observation_.world.world.entities[runtime_id];
    entity.runtime_id = runtime_id;
    entity.unique_id = unique_id;
    entity.type_identifier = type;
    entity.type_id = 0; // Actor type arrives as a namespaced string, not a numeric type ID.
    entity.position = position;
    entity.velocity = velocity;
    entity.rotation = rotation;
    entity.head_yaw = static_cast<double>(head_yaw);
    entity.ground_state_known = false;
    entity.health = 0.0;
    entity.alive = true;
    entity.last_update_tick = next_tick(observation_.world.tick);
    update_entity_relation(entity);
    observation_.world.tick = entity.last_update_tick;
    report.disposition = ObservationDecodeDisposition::PartiallyApplied;
    report.note = "AddActor identity/type/motion prefix decoded; attributes and actor metadata are not parsed";
    if (error != nullptr) error->clear();
    return true;
}

bool Bedrock2193ObservationDecoder::decode_remove_actor(const TransportFrame& frame,
    ObservationDecodeReport& report, std::string* error) {
    Reader reader(frame.payload);
    std::int64_t unique_id = 0;
    if (!reader.var_i64(unique_id) || !reader.empty()) {
        return fail_packet(frame, "RemoveActor payload is malformed", report, error);
    }
    bool removed = false;
    for (auto it = observation_.world.world.entities.begin(); it != observation_.world.world.entities.end();) {
        if (!it->second.is_player && it->second.unique_id == unique_id) {
            it = observation_.world.world.entities.erase(it);
            removed = true;
        } else ++it;
    }
    observation_.world.tick = next_tick(observation_.world.tick);
    report.disposition = ObservationDecodeDisposition::Applied;
    report.note = removed ? "removed tracked non-player actor by unique ID" : "actor removal observed; no uniquely matching tracked non-player actor";
    if (error != nullptr) error->clear();
    return true;
}

bool Bedrock2193ObservationDecoder::decode_move_actor_absolute(const TransportFrame& frame,
    ObservationDecodeReport& report, std::string* error) {
    Reader reader(frame.payload);
    std::uint64_t runtime_id = 0U;
    std::uint8_t header = 0U, rx = 0U, ry = 0U, rz = 0U;
    Vec3 position{};
    if (!reader.var_u64(runtime_id) || !reader.u8(header) || !reader.vec3(position) ||
        !reader.u8(rx) || !reader.u8(ry) || !reader.u8(rz) || !finite(position)) {
        return fail_packet(frame, "MoveActorAbsolute core fields are malformed", report, error);
    }
    constexpr std::uint8_t kGroundFlag = 0x01U;
    constexpr std::uint8_t kTeleportFlag = 0x02U;
    constexpr std::uint8_t kForceMoveLocalFlag = 0x04U;
    constexpr std::uint8_t kForceCompletionFlag = 0x08U;
    constexpr std::uint8_t kKnownHeaderFlags = kGroundFlag | kTeleportFlag | kForceMoveLocalFlag | kForceCompletionFlag;
    if ((header & static_cast<std::uint8_t>(~kKnownHeaderFlags)) != 0U) {
        return fail_packet(frame, "MoveActorAbsolute header contains unknown protocol-2193 flags", report, error);
    }
    (void)kTeleportFlag;
    (void)kForceMoveLocalFlag;
    (void)kForceCompletionFlag;
    const Rot2 rotation{static_cast<double>(rx) * kByteRotationScale, static_cast<double>(ry) * kByteRotationScale};
    const double head_yaw = static_cast<double>(rz) * kByteRotationScale;
    const std::uint64_t tick = next_tick(observation_.world.tick);
    if (runtime_id == observation_.world.player.runtime_id && observation_.world.joined) {
        observation_.world.player.position = position;
        observation_.world.player.rotation = rotation;
        observation_.world.player.head_yaw = head_yaw;
        observation_.world.player.on_ground = (header & kGroundFlag) != 0U;
        observation_.world.player.airborne = !observation_.world.player.on_ground;
        observation_.world.player.ground_state_known = true;
    } else {
        EntityState& entity = observation_.world.world.entities[runtime_id];
        entity.runtime_id = runtime_id;
        entity.position = position;
        entity.rotation = rotation;
        entity.head_yaw = head_yaw;
        entity.on_ground = (header & kGroundFlag) != 0U;
        entity.ground_state_known = true;
        entity.last_update_tick = tick;
        update_entity_relation(entity);
    }
    observation_.world.tick = tick;
    report.disposition = ObservationDecodeDisposition::Applied;
    report.note = "MoveActorAbsolute position, packed rotations and verified header flags decoded";
    if (error != nullptr) error->clear();
    return true;
}

bool Bedrock2193ObservationDecoder::decode_move_actor_delta(const TransportFrame& frame,
    ObservationDecodeReport& report, std::string* error) {
    Reader reader(frame.payload);
    std::uint64_t runtime_id = 0U, expected_ticks = 0U;
    std::uint16_t flags = 0U;
    if (!reader.var_u64(runtime_id) || !reader.u16le(flags)) {
        return fail_packet(frame, "MoveActorDelta actor ID or flags are malformed", report, error);
    }
    const bool is_local_player = runtime_id == observation_.world.player.runtime_id && observation_.world.joined;
    const EntityState* previous_entity = nullptr;
    EntityState previous_entity_snapshot{};
    if (!is_local_player) {
        const auto found = observation_.world.world.entities.find(runtime_id);
        if (found != observation_.world.world.entities.end()) {
            previous_entity_snapshot = found->second;
            previous_entity = &previous_entity_snapshot;
        }
    }
    Vec3 position = is_local_player ? observation_.world.player.position :
                    (previous_entity != nullptr ? previous_entity->position : Vec3{});
    Rot2 rotation = is_local_player ? observation_.world.player.rotation :
                    (previous_entity != nullptr ? previous_entity->rotation : Rot2{});
    double head_yaw = is_local_player ? observation_.world.player.head_yaw :
                      (previous_entity != nullptr ? previous_entity->head_yaw : 0.0);
    float component = 0.0F;
    if ((flags & 0x01U) != 0U) { if (!reader.f32(component)) return fail_packet(frame, "MoveActorDelta X is truncated", report, error); position.x = component; }
    if ((flags & 0x02U) != 0U) { if (!reader.f32(component)) return fail_packet(frame, "MoveActorDelta Y is truncated", report, error); position.y = component; }
    if ((flags & 0x04U) != 0U) { if (!reader.f32(component)) return fail_packet(frame, "MoveActorDelta Z is truncated", report, error); position.z = component; }
    std::uint8_t rotation_byte = 0U;
    if ((flags & 0x08U) != 0U) { if (!reader.u8(rotation_byte)) return fail_packet(frame, "MoveActorDelta pitch is truncated", report, error); rotation.pitch = static_cast<double>(rotation_byte) * kByteRotationScale; }
    if ((flags & 0x10U) != 0U) { if (!reader.u8(rotation_byte)) return fail_packet(frame, "MoveActorDelta yaw is truncated", report, error); rotation.yaw = static_cast<double>(rotation_byte) * kByteRotationScale; }
    if ((flags & 0x20U) != 0U) {
        if (!reader.u8(rotation_byte)) return fail_packet(frame, "MoveActorDelta head rotation is truncated", report, error);
        head_yaw = static_cast<double>(rotation_byte) * kByteRotationScale;
    }
    std::uint8_t on_ground_byte = 0U, force_move = 0U, force_move_local = 0U, force_completion = 0U;
    if (!reader.u8(on_ground_byte) || !reader.u8(force_move) || !reader.u8(force_move_local) ||
        !reader.u8(force_completion) || !reader.var_u64(expected_ticks) || !reader.empty() || !finite(position) ||
        (flags & 0xffc0U) != 0U || on_ground_byte > 1U || force_move > 1U ||
        force_move_local > 1U || force_completion > 1U) {
        return fail_packet(frame, "MoveActorDelta tail/flags are malformed", report, error);
    }
    (void)expected_ticks;
    (void)force_move;
    (void)force_move_local;
    (void)force_completion;
    const std::uint64_t tick = next_tick(observation_.world.tick);
    const bool on_ground = on_ground_byte == 1U;
    if (is_local_player) {
        observation_.world.player.position = position;
        observation_.world.player.rotation = rotation;
        observation_.world.player.head_yaw = head_yaw;
        observation_.world.player.on_ground = on_ground;
        observation_.world.player.airborne = !on_ground;
        observation_.world.player.ground_state_known = true;
    } else {
        EntityState& entity = observation_.world.world.entities[runtime_id];
        entity.runtime_id = runtime_id;
        entity.position = position;
        entity.rotation = rotation;
        entity.head_yaw = head_yaw;
        entity.on_ground = on_ground;
        entity.ground_state_known = true;
        entity.last_update_tick = tick;
        update_entity_relation(entity);
    }
    observation_.world.tick = tick;
    report.disposition = ObservationDecodeDisposition::Applied;
    report.note = "MoveActorDelta partial position/rotation flags and protocol-2193 ticks decoded";
    if (error != nullptr) error->clear();
    return true;
}

void Bedrock2193ObservationDecoder::update_entity_relation(EntityState& entity) {
    if (entity.is_player || entity.type_identifier == "minecraft:player") {
        entity.is_player = true;
        entity.hostility_known = target_other_players_as_hostile_;
        entity.hostile = target_other_players_as_hostile_;
    } else if (!entity.type_identifier.empty() && known_hostile_type(entity.type_identifier)) {
        entity.hostility_known = true;
        entity.hostile = true;
    } else {
        entity.hostility_known = false;
        entity.hostile = false;
    }
}


void Bedrock2193ObservationDecoder::resolve_item_identity(InventoryStackObservation& stack) {
    if (stack.protocol_registry_kind_applied && stack.kind == stack.protocol_registry_kind) {
        stack.kind = ItemKind::Unknown;
    }
    if (stack.protocol_registry_resolution_known && !stack.protocol_registry_identifier.empty() &&
        stack.identifier == stack.protocol_registry_identifier) {
        stack.identifier.clear();
    }
    stack.protocol_registry_identifier.clear();
    stack.protocol_registry_kind_applied = false;
    stack.protocol_registry_kind = ItemKind::Unknown;
    stack.protocol_registry_resolution_known = false;
    if (!stack.protocol_item_id_known) return;

    const std::int16_t item_id = stack.protocol_item_id;
    if (observation_.ambiguous_protocol_item_ids.find(item_id) != observation_.ambiguous_protocol_item_ids.end()) {
        stack.protocol_registry_resolution_known = true;
        return;
    }
    const auto found = observation_.item_registry_index_by_id.find(item_id);
    if (found == observation_.item_registry_index_by_id.end() ||
        found->second >= observation_.item_registry_entries.size()) return;

    const ItemRegistryEntryObservation& entry = observation_.item_registry_entries[found->second];
    stack.protocol_registry_resolution_known = true;
    stack.protocol_registry_identifier = entry.identifier;
    stack.identifier = entry.identifier;
    if (stack.kind == ItemKind::Unknown) {
        const ItemKind inferred = known_vanilla_item_kind(entry.identifier);
        if (inferred != ItemKind::Unknown) {
            stack.kind = inferred;
            stack.protocol_registry_kind_applied = true;
            stack.protocol_registry_kind = inferred;
        }
    }
}

void Bedrock2193ObservationDecoder::refresh_registry_item_identities() {
    for (auto& container : observation_.inventory_containers) {
        for (auto& stack : container.slots) resolve_item_identity(stack);
        if (container.storage_item.has_value()) resolve_item_identity(*container.storage_item);
    }
    for (auto& stack : observation_.inventory) resolve_item_identity(stack);
    for (auto& stack : observation_.armor_inventory) resolve_item_identity(stack);
    if (observation_.held_item_observation.has_value()) resolve_item_identity(*observation_.held_item_observation);
    sync_strategy_inventory_from_containers();
}

void Bedrock2193ObservationDecoder::sync_strategy_inventory_from_containers() {
    const InventoryContainerObservation* selected = nullptr;
    // Prefer the canonical combined player inventory; fall back to an explicit hotbar-only view.
    for (const auto& container : observation_.inventory_containers) {
        if (container.container_name == kCombinedHotbarAndInventoryContainer) {
            selected = &container;
            break;
        }
        if (container.container_name == kHotbarContainer && selected == nullptr) selected = &container;
    }
    if (selected == nullptr) return;

    std::vector<InventoryStackObservation> active;
    active.reserve(selected->slots.size());
    for (const auto& slot : selected->slots) {
        if (slot.count <= 0 || !slot.protocol_descriptor_decoded) continue;
        InventoryStackObservation copy = slot;
        resolve_item_identity(copy);
        copy.equipped = copy.slot >= 0 && copy.slot == observation_.world.player.selected_slot;
        active.push_back(std::move(copy));
    }
    observation_.inventory = std::move(active);
}

bool Bedrock2193ObservationDecoder::decode_inventory_content(const TransportFrame& frame,
    ObservationDecodeReport& report, std::string* error) {
    Reader reader(frame.payload);
    std::uint32_t raw_container_id = 0U;
    std::uint32_t slot_count = 0U;
    if (!reader.var_u32(raw_container_id) || raw_container_id > 255U ||
        !reader.var_u32(slot_count) || slot_count > kMaximumContainerSlots) {
        return fail_packet(frame, "InventoryContent has an invalid container ID or slot count", report, error);
    }

    std::vector<InventoryStackObservation> slots;
    slots.reserve(static_cast<std::size_t>(slot_count));
    for (std::uint32_t i = 0U; i < slot_count; ++i) {
        InventoryStackObservation item;
        if (!read_network_item_stack(reader, item)) {
            return fail_packet(frame, "InventoryContent contains a truncated or invalid item descriptor", report, error);
        }
        item.slot = static_cast<int>(i);
        item.protocol_container_id = static_cast<std::uint8_t>(raw_container_id);
        slots.push_back(std::move(item));
    }

    FullContainerNameWire full_name;
    if (!read_full_container_name(reader, full_name)) {
        return fail_packet(frame, "InventoryContent has an invalid FullContainerName", report, error);
    }
    InventoryStackObservation storage_item;
    if (!read_network_item_stack(reader, storage_item) || !reader.empty()) {
        return fail_packet(frame, "InventoryContent has an invalid storage item or trailing bytes", report, error);
    }

    const auto container_id = static_cast<std::uint8_t>(raw_container_id);
    for (auto& slot : slots) {
        slot.protocol_container_name = full_name.name;
        slot.protocol_container_dynamic_id = full_name.dynamic_id;
    }

    auto it = std::find_if(observation_.inventory_containers.begin(), observation_.inventory_containers.end(),
        [container_id, &full_name](const InventoryContainerObservation& candidate) {
            return candidate.container_id == container_id && candidate.container_name == full_name.name &&
                   candidate.dynamic_id == full_name.dynamic_id;
        });
    if (it == observation_.inventory_containers.end()) {
        if (observation_.inventory_containers.size() >= kMaximumObservedContainers) {
            return fail_packet(frame, "InventoryContent would exceed the bounded observed-container set", report, error);
        }
        InventoryContainerObservation container;
        container.container_id = container_id;
        container.container_name = full_name.name;
        container.dynamic_id = full_name.dynamic_id;
        container.slots = std::move(slots);
        if (storage_item.count > 0) {
            storage_item.protocol_container_id = container_id;
            storage_item.protocol_container_name = full_name.name;
            storage_item.protocol_container_dynamic_id = full_name.dynamic_id;
            container.storage_item = std::move(storage_item);
        }
        observation_.inventory_containers.push_back(std::move(container));
    } else {
        it->slots = std::move(slots);
        it->storage_item.reset();
        if (storage_item.count > 0) {
            storage_item.protocol_container_id = container_id;
            storage_item.protocol_container_name = full_name.name;
            storage_item.protocol_container_dynamic_id = full_name.dynamic_id;
            it->storage_item = std::move(storage_item);
        }
    }
    sync_strategy_inventory_from_containers();
    report.disposition = ObservationDecodeDisposition::Applied;
    report.note = "InventoryContent container slots and raw item descriptors decoded; semantic item identity remains registry-dependent";
    if (error != nullptr) error->clear();
    return true;
}

bool Bedrock2193ObservationDecoder::decode_inventory_slot(const TransportFrame& frame,
    ObservationDecodeReport& report, std::string* error) {
    Reader reader(frame.payload);
    std::uint8_t raw_container_id = 0U;
    std::uint32_t raw_slot = 0U;
    std::uint8_t has_full_name = 0U;
    if (!reader.u8(raw_container_id) || !reader.var_u32(raw_slot) || raw_slot >= kMaximumContainerSlots ||
        !reader.u8(has_full_name) || has_full_name > 1U) {
        return fail_packet(frame, "InventorySlot has an invalid container, slot, or FullContainerName presence flag", report, error);
    }

    FullContainerNameWire full_name;
    if (has_full_name == 1U && !read_full_container_name(reader, full_name)) {
        return fail_packet(frame, "InventorySlot has an invalid FullContainerName", report, error);
    }
    std::optional<InventoryStackObservation> storage_item;
    if (!read_optional_network_item_stack(reader, storage_item)) {
        return fail_packet(frame, "InventorySlot has an invalid optional storage-item descriptor", report, error);
    }
    InventoryStackObservation item;
    if (!read_network_item_stack(reader, item) || !reader.empty()) {
        return fail_packet(frame, "InventorySlot has an invalid item descriptor or trailing bytes", report, error);
    }

    auto it = observation_.inventory_containers.end();
    if (has_full_name == 1U) {
        it = std::find_if(observation_.inventory_containers.begin(), observation_.inventory_containers.end(),
            [raw_container_id, &full_name](const InventoryContainerObservation& candidate) {
                return candidate.container_id == raw_container_id && candidate.container_name == full_name.name &&
                       candidate.dynamic_id == full_name.dynamic_id;
            });
    } else {
        auto candidate = observation_.inventory_containers.end();
        bool ambiguous = false;
        for (auto scan = observation_.inventory_containers.begin(); scan != observation_.inventory_containers.end(); ++scan) {
            if (scan->container_id != raw_container_id) continue;
            if (candidate != observation_.inventory_containers.end()) {
                ambiguous = true;
                break;
            }
            candidate = scan;
        }
        if (!ambiguous) it = candidate;
    }

    if (it == observation_.inventory_containers.end()) {
        if (observation_.inventory_containers.size() >= kMaximumObservedContainers) {
            return fail_packet(frame, "InventorySlot would exceed the bounded observed-container set", report, error);
        }
        InventoryContainerObservation container;
        container.container_id = raw_container_id;
        container.container_name = has_full_name == 1U ? full_name.name : kUnknownContainer;
        container.dynamic_id = has_full_name == 1U ? full_name.dynamic_id : 0U;
        container.slots.resize(static_cast<std::size_t>(raw_slot) + 1U);
        observation_.inventory_containers.push_back(std::move(container));
        it = std::prev(observation_.inventory_containers.end());
    }

    if (it->slots.size() <= static_cast<std::size_t>(raw_slot))
        it->slots.resize(static_cast<std::size_t>(raw_slot) + 1U);
    item.slot = static_cast<int>(raw_slot);
    item.protocol_container_id = raw_container_id;
    item.protocol_container_name = it->container_name;
    item.protocol_container_dynamic_id = it->dynamic_id;
    it->slots[static_cast<std::size_t>(raw_slot)] = std::move(item);
    if (has_full_name == 1U) {
        it->container_name = full_name.name;
        it->dynamic_id = full_name.dynamic_id;
    }
    it->storage_item = std::move(storage_item);
    if (it->storage_item.has_value()) {
        it->storage_item->protocol_container_id = raw_container_id;
        it->storage_item->protocol_container_name = it->container_name;
        it->storage_item->protocol_container_dynamic_id = it->dynamic_id;
    }
    sync_strategy_inventory_from_containers();
    report.disposition = has_full_name == 1U ? ObservationDecodeDisposition::Applied
                                              : ObservationDecodeDisposition::PartiallyApplied;
    report.note = has_full_name == 1U
        ? "InventorySlot update decoded with explicit container identity"
        : "InventorySlot update decoded; container identity was inherited only from an unambiguous observed container";
    if (error != nullptr) error->clear();
    return true;
}

bool Bedrock2193ObservationDecoder::decode_mob_equipment(const TransportFrame& frame,
    ObservationDecodeReport& report, std::string* error) {
    Reader reader(frame.payload);
    std::uint64_t target_runtime_id = 0U;
    InventoryStackObservation item;
    std::uint8_t slot = 0U, selected_slot = 0U, container_id = 0U;
    if (!reader.var_u64(target_runtime_id) || target_runtime_id == 0U ||
        !read_network_item_stack(reader, item) || !reader.u8(slot) ||
        !reader.u8(selected_slot) || !reader.u8(container_id) || !reader.empty()) {
        return fail_packet(frame, "MobEquipment has an invalid target, item descriptor, fields, or trailing bytes", report, error);
    }
    if (target_runtime_id != observation_.world.player.runtime_id) {
        report.disposition = ObservationDecodeDisposition::PartiallyApplied;
        report.note = "MobEquipment was structurally decoded for a non-local actor; actor equipment storage is not yet modeled";
        if (error != nullptr) error->clear();
        return true;
    }
    if (selected_slot > 8U) {
        return fail_packet(frame, "MobEquipment selected hotbar slot is outside 0..8", report, error);
    }

    observation_.world.player.selected_slot = static_cast<int>(selected_slot);
    for (auto& stack : observation_.inventory) stack.equipped = stack.slot == static_cast<int>(selected_slot);
    item.slot = static_cast<int>(slot);
    item.equipped = slot == selected_slot;
    resolve_item_identity(item);
    item.protocol_container_id = container_id;
    item.protocol_container_name = kUnknownContainer;

    // Apply to a full inventory snapshot only when the packet container ID matches a known
    // canonical player-hotbar/inventory container. The selected held item is preserved separately.
    auto container_it = std::find_if(observation_.inventory_containers.begin(), observation_.inventory_containers.end(),
        [container_id](const InventoryContainerObservation& candidate) {
            return candidate.container_id == container_id &&
                   (candidate.container_name == kCombinedHotbarAndInventoryContainer ||
                    candidate.container_name == kHotbarContainer);
        });
    if (container_it != observation_.inventory_containers.end() &&
        static_cast<std::size_t>(slot) < kMaximumContainerSlots) {
        if (container_it->slots.size() <= static_cast<std::size_t>(slot))
            container_it->slots.resize(static_cast<std::size_t>(slot) + 1U);
        item.protocol_container_name = container_it->container_name;
        item.protocol_container_dynamic_id = container_it->dynamic_id;
        container_it->slots[slot] = item;
        sync_strategy_inventory_from_containers();
    }
    // Only the main inventory window can define the player's held hotbar item.
    // Offhand/other container equipment can reuse the same slot number and must not
    // overwrite the main-hand observation.
    if (container_id == kPlayerInventoryWindowId && slot == selected_slot) {
        if (item.count > 0) {
            item.slot = static_cast<int>(selected_slot);
            item.equipped = true;
            observation_.held_item_observation = item;
            auto existing = std::find_if(observation_.inventory.begin(), observation_.inventory.end(),
                [selected_slot](const InventoryStackObservation& stack) { return stack.slot == static_cast<int>(selected_slot); });
            if (existing == observation_.inventory.end()) observation_.inventory.push_back(item);
            else *existing = item;
        } else {
            observation_.held_item_observation.reset();
            observation_.inventory.erase(std::remove_if(observation_.inventory.begin(), observation_.inventory.end(),
                [selected_slot](const InventoryStackObservation& stack) { return stack.slot == static_cast<int>(selected_slot); }),
                observation_.inventory.end());
        }
    }
    for (auto& stack : observation_.inventory) stack.equipped = stack.slot == static_cast<int>(selected_slot);
    report.disposition = ObservationDecodeDisposition::Applied;
    report.note = "local MobEquipment and selected hotbar slot decoded; item semantics are not inferred from the raw numeric ID";
    if (error != nullptr) error->clear();
    return true;
}

bool Bedrock2193ObservationDecoder::decode_mob_armor_equipment(const TransportFrame& frame,
    ObservationDecodeReport& report, std::string* error) {
    Reader reader(frame.payload);
    std::uint64_t target_runtime_id = 0U;
    if (!reader.var_u64(target_runtime_id) || target_runtime_id == 0U) {
        return fail_packet(frame, "MobArmorEquipment has an invalid target runtime ID", report, error);
    }
    std::vector<InventoryStackObservation> armor;
    armor.reserve(5U);
    for (std::uint32_t i = 0U; i < 5U; ++i) {
        InventoryStackObservation item;
        if (!read_network_item_stack(reader, item)) {
            return fail_packet(frame, "MobArmorEquipment contains a truncated or invalid armor descriptor", report, error);
        }
        item.slot = static_cast<int>(i);
        resolve_item_identity(item);
        item.protocol_container_id = kUnknownContainer;
        item.protocol_container_name = kArmorContainer;
        item.equipped = item.count > 0;
        armor.push_back(std::move(item));
    }
    if (!reader.empty()) return fail_packet(frame, "MobArmorEquipment contains trailing bytes", report, error);
    if (target_runtime_id == observation_.world.player.runtime_id) {
        observation_.armor_inventory = std::move(armor);
        report.disposition = ObservationDecodeDisposition::Applied;
        report.note = "local armor slots decoded in head/torso/legs/feet/body order; durability/armor points remain unknown";
    } else {
        report.disposition = ObservationDecodeDisposition::PartiallyApplied;
        report.note = "MobArmorEquipment was structurally decoded for a non-local actor; actor armor storage is not yet modeled";
    }
    if (error != nullptr) error->clear();
    return true;
}

bool Bedrock2193ObservationDecoder::decode_item_registry(const TransportFrame& frame,
    ObservationDecodeReport& report, std::string* error) {
    Reader reader(frame.payload);
    std::uint32_t count = 0U;
    if (!reader.var_u32(count) || count > kMaximumItemRegistryEntries) {
        return fail_packet(frame, "ItemRegistry has an invalid or oversized entry count", report, error);
    }

    std::vector<ItemRegistryEntryObservation> entries;
    entries.reserve(static_cast<std::size_t>(count));
    std::size_t aggregate_nbt_bytes = 0U;
    for (std::uint32_t i = 0U; i < count; ++i) {
        ItemRegistryEntryObservation entry;
        std::uint8_t component_based = 0U;
        if (!reader.string(entry.identifier, 512U) || entry.identifier.empty() ||
            entry.identifier.find('\0') != std::string::npos || entry.identifier.find(':') == std::string::npos ||
            !reader.i16le(entry.item_id) || !reader.u8(component_based) || component_based > 1U ||
            !reader.var_i32(entry.version) || entry.version < 0 || entry.version > 2) {
            return fail_packet(frame, "ItemRegistry contains an invalid identifier, ID, component flag, or version", report, error);
        }
        entry.component_based = component_based == 1U;
        // ItemComponentData is serialized as a Network Little Endian NBT compound
        // for every ItemData entry; non-component items commonly carry an empty compound.
        // Do not conditionally skip these bytes based on IsComponentBased.
        if (!read_network_nbt_compound(reader, entry.component_data_buffer)) {
            return fail_packet(frame, "ItemRegistry item data is not a bounded network-little-endian NBT compound", report, error);
        }
        if (entry.component_data_buffer.size() > kMaximumItemRegistryNbtBytes - aggregate_nbt_bytes) {
            return fail_packet(frame, "ItemRegistry NBT exceeds the aggregate memory bound", report, error);
        }
        aggregate_nbt_bytes += entry.component_data_buffer.size();
        entries.push_back(std::move(entry));
    }
    if (!reader.empty()) return fail_packet(frame, "ItemRegistry contains trailing bytes", report, error);

    std::unordered_map<std::int16_t, std::size_t> indices;
    std::unordered_set<std::int16_t> ambiguous;
    for (std::size_t i = 0U; i < entries.size(); ++i) {
        const auto id = entries[i].item_id;
        if (ambiguous.find(id) != ambiguous.end()) continue;
        const auto found = indices.find(id);
        if (found == indices.end()) {
            indices.emplace(id, i);
            continue;
        }
        const ItemRegistryEntryObservation& previous = entries[found->second];
        const ItemRegistryEntryObservation& current = entries[i];
        const bool exact_duplicate = previous.identifier == current.identifier &&
            previous.component_based == current.component_based && previous.version == current.version &&
            previous.component_data_buffer == current.component_data_buffer;
        if (!exact_duplicate) {
            indices.erase(found);
            ambiguous.insert(id);
        }
    }

    observation_.item_registry_entries = std::move(entries);
    observation_.item_registry_index_by_id = std::move(indices);
    observation_.ambiguous_protocol_item_ids = std::move(ambiguous);
    refresh_registry_item_identities();
    report.disposition = observation_.ambiguous_protocol_item_ids.empty()
        ? ObservationDecodeDisposition::Applied : ObservationDecodeDisposition::PartiallyApplied;
    report.note = observation_.ambiguous_protocol_item_ids.empty()
        ? "ItemRegistry names and opaque component NBT decoded; vanilla known kinds were classified and custom capabilities remain unverified"
        : "ItemRegistry decoded, but conflicting duplicate numeric IDs were retained as ambiguous and were not assigned an identity";
    if (error != nullptr) error->clear();
    return true;
}

bool Bedrock2193ObservationDecoder::decode_update_attributes(const TransportFrame& frame,
    ObservationDecodeReport& report, std::string* error) {
    Reader reader(frame.payload);
    std::uint64_t target_runtime_id = 0U;
    std::uint32_t count = 0U;
    if (!reader.var_u64(target_runtime_id) || target_runtime_id == 0U ||
        !reader.var_u32(count) || count > kMaximumAttributeCount) {
        return fail_packet(frame, "UpdateAttributes has an invalid target runtime ID or attribute count", report, error);
    }

    std::vector<ActorAttributeState> attributes;
    attributes.reserve(static_cast<std::size_t>(count));
    for (std::uint32_t i = 0U; i < count; ++i) {
        ActorAttributeState attribute;
        float min_value = 0.0F, max_value = 0.0F, current_value = 0.0F;
        float default_min = 0.0F, default_max = 0.0F, default_value = 0.0F;
        std::uint32_t modifier_count = 0U;
        if (!reader.f32(min_value) || !reader.f32(max_value) || !reader.f32(current_value) ||
            !reader.f32(default_min) || !reader.f32(default_max) || !reader.f32(default_value) ||
            min_value > max_value || default_min > default_max ||
            !reader.string(attribute.name, 256U) || attribute.name.empty() ||
            attribute.name.find('\0') != std::string::npos ||
            !reader.var_u32(modifier_count) || modifier_count > kMaximumAttributeModifiers) {
            return fail_packet(frame, "UpdateAttributes contains invalid bounds, name, or modifier count", report, error);
        }
        attribute.min_value = min_value;
        attribute.max_value = max_value;
        attribute.current_value = current_value;
        attribute.default_min_value = default_min;
        attribute.default_max_value = default_max;
        attribute.default_value = default_value;
        attribute.modifiers.reserve(static_cast<std::size_t>(modifier_count));
        for (std::uint32_t j = 0U; j < modifier_count; ++j) {
            AttributeModifierState modifier;
            float amount = 0.0F;
            std::uint8_t serializable = 0U;
            if (!reader.string(modifier.id, 512U) || !reader.string(modifier.name, 512U) ||
                !reader.f32(amount) || !reader.i32le(modifier.operation) ||
                !reader.i32le(modifier.operand) || !reader.u8(serializable) || serializable > 1U) {
                return fail_packet(frame, "UpdateAttributes contains a malformed modifier", report, error);
            }
            modifier.amount = amount;
            modifier.serializable = serializable == 1U;
            attribute.modifiers.push_back(std::move(modifier));
        }
        attributes.push_back(std::move(attribute));
    }
    std::uint64_t server_tick = 0U;
    if (!reader.var_u64(server_tick) || !reader.empty()) {
        return fail_packet(frame, "UpdateAttributes has a truncated PlayerInputTick or trailing bytes", report, error);
    }

    bool actor_found = false;
    const bool is_local_player = target_runtime_id == observation_.world.player.runtime_id;
    if (is_local_player) {
        actor_found = true;
        if (server_tick != 0U) report.local_player_input_tick = server_tick;
        for (auto& attribute : attributes) {
            attribute.observed_tick = server_tick;
            observation_.world.player.attributes[attribute.name] = attribute;
            if (attribute.name == "minecraft:health") {
                observation_.world.player.health = attribute.current_value;
                observation_.conditions.maximum_health = attribute.max_value;
            } else if (attribute.name == "minecraft:absorption") {
                observation_.conditions.absorption = std::max(0.0, attribute.current_value);
            } else if (attribute.name == "minecraft:hunger" || attribute.name == "minecraft:player.hunger") {
                observation_.conditions.hunger = attribute.current_value;
                observation_.conditions.hunger_known = true;
            } else if (attribute.name == "minecraft:armor") {
                observation_.conditions.armour_points = std::max(0.0, attribute.current_value);
            }
        }
    } else {
        const auto found = observation_.world.world.entities.find(target_runtime_id);
        if (found != observation_.world.world.entities.end()) {
            actor_found = true;
            for (auto& attribute : attributes) {
                attribute.observed_tick = server_tick;
                found->second.attributes[attribute.name] = attribute;
                if (attribute.name == "minecraft:health") found->second.health = attribute.current_value;
            }
        }
    }
    observation_.world.server_tick = server_tick;
    observation_.world.server_tick_known = true;
    observation_.world.tick = next_tick(observation_.world.tick);
    report.disposition = actor_found ? ObservationDecodeDisposition::Applied
                                     : ObservationDecodeDisposition::PartiallyApplied;
    report.note = actor_found
        ? "UpdateAttributes values and bounded modifiers applied to the observed actor; health/maximum-health signals synchronized"
        : "UpdateAttributes was structurally decoded, but the target actor is not currently tracked";
    if (error != nullptr) error->clear();
    return true;
}

} // namespace bedrockai
