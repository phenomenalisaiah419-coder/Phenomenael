#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

namespace bedrockai {

enum class Direction { Clientbound, Serverbound };

enum class BedrockTransport { Unknown, RakNet, NetherNet };

struct ProtocolIdentity {
    static constexpr const char* kGameVersion = "1.26.50";
    static constexpr std::uint32_t kProtocol = 2193;

    std::string game_version;
    std::uint32_t protocol = 0;
    BedrockTransport transport = BedrockTransport::Unknown;

    bool is_target() const {
        return game_version == kGameVersion && protocol == kProtocol;
    }
};

struct PacketValue {
    using Object = std::unordered_map<std::string, PacketValue>;
    using Array = std::vector<PacketValue>;
    using Scalar = std::variant<std::monostate, bool, std::int64_t, double, std::string>;

    Scalar scalar;
    Object object;
    Array array;

    static PacketValue boolean(bool v);
    static PacketValue integer(std::int64_t v);
    static PacketValue number(double v);
    static PacketValue string(std::string v);
    static PacketValue object_value(Object v);
    static PacketValue array_value(Array v);

    bool is_object() const;
    bool is_number() const;
    std::optional<double> as_number() const;
    std::optional<std::int64_t> as_integer() const;
    std::optional<bool> as_bool() const;
    const PacketValue* get(const std::string& key) const;
};

struct PacketView {
    Direction direction = Direction::Clientbound;
    std::uint32_t id = 0;
    std::string name;
    PacketValue::Object fields;
    std::uint64_t tick = 0;
};

struct AdapterStatus {
    bool connected = false;
    bool target_protocol_verified = false;
    bool schema_ready = false;
    std::string detail;
};

class ProtocolAdapter {
public:
    ProtocolAdapter();

    bool accept_server_identity(const ProtocolIdentity& identity, std::string* error = nullptr);
    bool observe(const PacketView& packet, std::string* error = nullptr);

    const ProtocolIdentity& identity() const { return identity_; }
    const AdapterStatus& status() const { return status_; }

private:
    ProtocolIdentity identity_;
    AdapterStatus status_;
};

namespace packet_id {
constexpr std::uint32_t Login = 1;
constexpr std::uint32_t PlayStatus = 2;
constexpr std::uint32_t ServerToClientHandshake = 3;
constexpr std::uint32_t ClientToServerHandshake = 4;
constexpr std::uint32_t Disconnect = 5;
constexpr std::uint32_t StartGame = 11;
constexpr std::uint32_t MovePlayer = 19;
constexpr std::uint32_t InventoryTransaction = 30;
constexpr std::uint32_t LevelChunk = 58;
constexpr std::uint32_t NetworkSettings = 143;
constexpr std::uint32_t PlayerAuthInput = 144;
constexpr std::uint32_t RequestNetworkSettings = 193;
constexpr std::uint32_t VoxelShapes = 337;
}

} // namespace bedrockai
