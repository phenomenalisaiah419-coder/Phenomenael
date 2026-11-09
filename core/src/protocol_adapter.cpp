#include "core/protocol_adapter.hpp"

namespace bedrockai {
PacketValue PacketValue::boolean(bool v){ PacketValue p; p.scalar=v; return p; }
PacketValue PacketValue::integer(std::int64_t v){ PacketValue p; p.scalar=v; return p; }
PacketValue PacketValue::number(double v){ PacketValue p; p.scalar=v; return p; }
PacketValue PacketValue::string(std::string v){ PacketValue p; p.scalar=std::move(v); return p; }
PacketValue PacketValue::object_value(Object v){ PacketValue p; p.object=std::move(v); return p; }
PacketValue PacketValue::array_value(Array v){ PacketValue p; p.array=std::move(v); return p; }
bool PacketValue::is_object() const { return !object.empty(); }
bool PacketValue::is_number() const { return std::holds_alternative<std::int64_t>(scalar)||std::holds_alternative<double>(scalar); }
std::optional<double> PacketValue::as_number() const { if(auto p=std::get_if<double>(&scalar)) return *p; if(auto p=std::get_if<std::int64_t>(&scalar)) return static_cast<double>(*p); return std::nullopt; }
std::optional<std::int64_t> PacketValue::as_integer() const { if(auto p=std::get_if<std::int64_t>(&scalar)) return *p; if(auto p=std::get_if<double>(&scalar)) return static_cast<std::int64_t>(*p); return std::nullopt; }
std::optional<bool> PacketValue::as_bool() const { if(auto p=std::get_if<bool>(&scalar)) return *p; return std::nullopt; }
const PacketValue* PacketValue::get(const std::string& key) const { auto it=object.find(key); return it==object.end()?nullptr:&it->second; }

ProtocolAdapter::ProtocolAdapter(){ identity_.game_version.clear(); }

bool ProtocolAdapter::accept_server_identity(const ProtocolIdentity& identity, std::string* error){
    if(!identity.is_target()){
        status_.target_protocol_verified=false;
        status_.connected=false;
        status_.detail="Protocol mismatch: expected Bedrock 1.26.50 / 2193";
        if(error) *error=status_.detail;
        return false;
    }
    identity_=identity;
    status_.connected=true;
    status_.target_protocol_verified=true;
    status_.schema_ready=true;
    status_.detail="Target protocol verified";
    return true;
}

bool ProtocolAdapter::observe(const PacketView& packet, std::string* error){
    if(!status_.target_protocol_verified){ if(error) *error="Packet rejected before target protocol verification"; return false; }
    if(packet.id==packet_id::StartGame && (packet.direction!=Direction::Clientbound || packet.name!="start_game")) { if(error) *error="StartGame direction/name mismatch"; return false; }
    if(packet.id==packet_id::MovePlayer && (packet.direction!=Direction::Clientbound || packet.name!="move_player")) { if(error) *error="MovePlayer direction/name mismatch"; return false; }
    if(packet.id==packet_id::LevelChunk && (packet.direction!=Direction::Clientbound || packet.name!="level_chunk")) { if(error) *error="LevelChunk direction/name mismatch"; return false; }
    if(packet.id==packet_id::PlayerAuthInput && (packet.direction!=Direction::Serverbound || packet.name!="player_auth_input")) { if(error) *error="PlayerAuthInput direction/name mismatch"; return false; }
    if(packet.id==packet_id::InventoryTransaction && packet.name!="inventory_transaction") { if(error) *error="InventoryTransaction id/name mismatch"; return false; }
    return true;
}
} // namespace bedrockai
