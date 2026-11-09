#if defined(_WIN32)
#include "local_bedrock_server.hpp"
namespace bedrockai::interop {
LocalBedrock2193Server::LocalBedrock2193Server() = default;
LocalBedrock2193Server::~LocalBedrock2193Server() = default;
bool LocalBedrock2193Server::start(std::string* error) { if (error) *error = "local interoperability harness requires POSIX UDP sockets"; return false; }
bool LocalBedrock2193Server::poll(std::string* error) { if (error) *error = "local interoperability harness requires POSIX UDP sockets"; return false; }
void LocalBedrock2193Server::stop() {}
} // namespace bedrockai::interop
#else
#include "local_bedrock_server.hpp"

#include "core/login_payload.hpp"
#include "core/raknet_transport.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <fcntl.h>
#include <netinet/in.h>
#include <random>
#include <sys/socket.h>
#include <unistd.h>

namespace bedrockai::interop {
namespace {

void set_error(std::string* e, const std::string& s) { if (e) *e = s; }

void put_be16(std::vector<std::uint8_t>& o, std::uint16_t v) { o.push_back(static_cast<std::uint8_t>(v >> 8)); o.push_back(static_cast<std::uint8_t>(v)); }
void put_be64(std::vector<std::uint8_t>& o, std::uint64_t v) { for (int i=7;i>=0;--i)o.push_back(static_cast<std::uint8_t>(v>>(8*i))); }

std::uint16_t get_be16(const std::vector<std::uint8_t>& b, std::size_t p) { return static_cast<std::uint16_t>((static_cast<std::uint16_t>(b[p])<<8)|b[p+1]); }
std::uint64_t get_be64(const std::vector<std::uint8_t>& b, std::size_t p) { std::uint64_t v=0; for(std::size_t i=0;i<8;++i)v=(v<<8)|b[p+i]; return v; }

std::string b64url(const std::vector<std::uint8_t>& in) {
    static constexpr char table[]="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    std::string out;std::uint32_t acc=0;unsigned bits=0;
    for(auto c:in){acc=(acc<<8)|c;bits+=8;while(bits>=6){bits-=6;out.push_back(table[(acc>>bits)&63u]);}}
    if (bits) out.push_back(table[(acc<<(6-bits))&63u]);
    return out;
}
std::string b64std(const std::vector<std::uint8_t>& in) {
    std::string s=b64url(in);for(char&c:s){if(c=='-')c='+';else if(c=='_')c='/';}while(s.size()%4)s.push_back('=');return s;
}
std::vector<std::uint8_t> b64std_decode(const std::string& input,bool& ok) {
    ok=true;std::string s=input;while(!s.empty()&&s.back()=='=')s.pop_back();std::vector<std::uint8_t> out;std::uint32_t acc=0;unsigned bits=0;
    for(char c:s){std::uint8_t v=0;if(c>='A'&&c<='Z')v=static_cast<std::uint8_t>(c-'A');else if(c>='a'&&c<='z')v=static_cast<std::uint8_t>(26+c-'a');else if(c>='0'&&c<='9')v=static_cast<std::uint8_t>(52+c-'0');else if(c=='+')v=62;else if(c=='/')v=63;else{ok=false;return{};}acc=(acc<<6)|v;bits+=6;if(bits>=8){bits-=8;out.push_back(static_cast<std::uint8_t>((acc>>bits)&0xffu));}}
    if(bits>=6||(bits&&(acc&((1u<<bits)-1u))!=0)){ok=false;out.clear();}return out;
}

std::string sign_jwt(OpenSSLBedrockCryptoProvider& crypto, const EcKeyPairDer& key,
                     const std::string& header, const std::string& payload) {
    const std::string h=b64url(std::vector<std::uint8_t>(header.begin(),header.end()));
    const std::string p=b64url(std::vector<std::uint8_t>(payload.begin(),payload.end()));
    const std::string in=h+"."+p;std::vector<std::uint8_t> sig;std::string e;
    if(!crypto.sign_es384(key.private_key_pkcs8_der,std::vector<std::uint8_t>(in.begin(),in.end()),sig,&e)) return{};
    return in+"."+b64url(sig);
}

bool protocol_addr_from_sockaddr(const sockaddr_in& a,std::vector<std::uint8_t>& out) {
    const std::uint32_t ip=ntohl(a.sin_addr.s_addr);out.clear();out.push_back(0x04);
    out.push_back(static_cast<std::uint8_t>(~((ip>>24)&0xffu)));out.push_back(static_cast<std::uint8_t>(~((ip>>16)&0xffu)));
    out.push_back(static_cast<std::uint8_t>(~((ip>>8)&0xffu)));out.push_back(static_cast<std::uint8_t>(~(ip&0xffu)));put_be16(out,ntohs(a.sin_port));return true;
}

std::vector<std::uint8_t> reply1(std::uint64_t guid,std::uint16_t mtu) {
    std::vector<std::uint8_t> o;o.push_back(raknet::offline::OpenConnectionReply1);const auto&m=raknet::offline_magic();o.insert(o.end(),m.begin(),m.end());put_be64(o,guid);o.push_back(0);put_be16(o,mtu);return o;
}
std::vector<std::uint8_t> reply2(std::uint64_t guid,std::uint16_t mtu,const std::vector<std::uint8_t>& address) {
    std::vector<std::uint8_t> o;o.push_back(raknet::offline::OpenConnectionReply2);const auto&m=raknet::offline_magic();o.insert(o.end(),m.begin(),m.end());put_be64(o,guid);o.insert(o.end(),address.begin(),address.end());put_be16(o,mtu);o.push_back(0);return o;
}

std::vector<std::uint8_t> b64url_decode(const std::string& input,bool& ok) {
    ok=true;
    std::uint32_t acc=0;
    unsigned bits=0;
    std::vector<std::uint8_t> out;
    for(char c:input){
        std::uint8_t v=0;
        if(c>='A'&&c<='Z') v=static_cast<std::uint8_t>(c-'A');
        else if(c>='a'&&c<='z') v=static_cast<std::uint8_t>(26+c-'a');
        else if(c>='0'&&c<='9') v=static_cast<std::uint8_t>(52+c-'0');
        else if(c=='-') v=62;
        else if(c=='_') v=63;
        else { ok=false; return{}; }
        acc=(acc<<6)|v;
        bits+=6;
        if(bits>=8){ bits-=8; out.push_back(static_cast<std::uint8_t>((acc>>bits)&0xffu)); }
    }
    if(bits>=6 || (bits && (acc&((1u<<bits)-1u))!=0)){ok=false;out.clear();}
    return out;
}

bool extract_client_public_der(const std::vector<std::uint8_t>& login_payload,std::vector<std::uint8_t>& der) {
    if(login_payload.size()<5)return false;
    std::size_t p=4;std::uint32_t n=0;
    for(unsigned i=0;i<5;++i){
        if(p>=login_payload.size())return false;
        const auto c=login_payload[p++];
        n|=static_cast<std::uint32_t>(c&0x7f)<<(7*i);
        if(!(c&0x80))break;
        if(i==4)return false;
    }
    if(n>login_payload.size()-p)return false;
    const std::string cr(reinterpret_cast<const char*>(login_payload.data()+p),n);
    const auto chain_pos = cr.find("chain");
    if (chain_pos == std::string::npos) return false;
    const auto array_pos = cr.find('[', chain_pos);
    if (array_pos == std::string::npos) return false;
    auto token_start = array_pos + 1;
    while (token_start < cr.size() && (cr[token_start] == '\\' || cr[token_start] == '\"')) ++token_start;
    if (token_start >= cr.size()) return false;
    const auto token_end=cr.find('\"',token_start);
    if(token_end==std::string::npos)return false;
    const std::string token=cr.substr(token_start,token_end-token_start);
    const auto dot=token.find('.');
    if(dot==std::string::npos)return false;
    bool ok=false;
    const auto header_bytes=b64url_decode(token.substr(0,dot),ok);
    if(!ok)return false;
    const std::string header(header_bytes.begin(),header_bytes.end());
    const auto key_pos=header.find("\"x5u\":\"");
    if(key_pos==std::string::npos)return false;
    const auto value_start=key_pos+std::string("\"x5u\":\"").size();
    const auto value_end=header.find('"',value_start);
    if(value_end==std::string::npos)return false;
    const auto x=header.substr(value_start,value_end-value_start);
    der=b64std_decode(x,ok);
    return ok && !der.empty();
}

std::uint64_t steady_ms(){return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());}

} // namespace

LocalBedrock2193Server::LocalBedrock2193Server() : reliability_([&]{raknet::ReliabilityConfig c;c.mtu=1492;return c;}()) {}

LocalBedrock2193Server::~LocalBedrock2193Server(){stop();}

bool LocalBedrock2193Server::start(std::string* error) {
    if(socket_fd_>=0){set_error(error,"interop server already started");return false;}
    socket_fd_=::socket(AF_INET,SOCK_DGRAM,0);if(socket_fd_<0){set_error(error,std::strerror(errno));return false;}
    int one=1;::setsockopt(socket_fd_,SOL_SOCKET,SO_REUSEADDR,&one,sizeof(one));
    sockaddr_in a{};a.sin_family=AF_INET;a.sin_addr.s_addr=htonl(INADDR_LOOPBACK);a.sin_port=htons(0);
    if(::bind(socket_fd_,reinterpret_cast<const sockaddr*>(&a),sizeof(a))!=0){set_error(error,std::strerror(errno));stop();return false;}
    socklen_t alen=sizeof(a);if(::getsockname(socket_fd_,reinterpret_cast<sockaddr*>(&a),&alen)!=0){set_error(error,std::strerror(errno));stop();return false;}
    port_=ntohs(a.sin_port);protocol_addr_from_sockaddr(a,server_address_);
    const int flags=fcntl(socket_fd_,F_GETFL,0);if(flags<0||fcntl(socket_fd_,F_SETFL,flags|O_NONBLOCK)<0){set_error(error,std::strerror(errno));stop();return false;}
    return true;
}

void LocalBedrock2193Server::stop(){if(socket_fd_>=0)::close(socket_fd_);socket_fd_=-1;port_=0;}

bool LocalBedrock2193Server::send_datagram(const std::vector<std::uint8_t>& bytes,std::string* error){
    if(socket_fd_<0||client_address_.empty()||client_address_len_==0){set_error(error,"interop server has no client endpoint");return false;}
    const auto sent=::sendto(socket_fd_,bytes.data(),bytes.size(),0,reinterpret_cast<const sockaddr*>(client_address_.data()),static_cast<socklen_t>(client_address_len_));
    if(sent!=static_cast<ssize_t>(bytes.size())){set_error(error,std::strerror(errno));return false;}return true;
}

bool LocalBedrock2193Server::send_offline_reply1(std::string* error){return send_datagram(reply1(server_guid_,mtu_),error);}
bool LocalBedrock2193Server::send_offline_reply2(std::string* error){return send_datagram(reply2(server_guid_,client_mtu_,server_address_),error);}

bool LocalBedrock2193Server::send_connection_accepted(std::uint64_t request_timestamp,std::string* error){
    if(client_address_.size()<sizeof(sockaddr_in))return false;
    std::vector<std::uint8_t> o;o.push_back(raknet::offline::ConnectionRequestAccepted);o.insert(o.end(),client_address_.begin(),client_address_.begin()+4+1+2); // replaced below for deterministic protocol address
    o.erase(o.begin()+1,o.end());
    sockaddr_in peer{};std::memcpy(&peer,client_address_.data(),sizeof(peer));
    std::vector<std::uint8_t> peer_pa;protocol_addr_from_sockaddr(peer,peer_pa);o.insert(o.end(),peer_pa.begin(),peer_pa.end());put_be16(o,0);
    for (int i=0;i<10;++i) o.insert(o.end(),server_address_.begin(),server_address_.end());
    put_be64(o,request_timestamp);
    put_be64(o,steady_ms());
    if(!reliability_.queue(std::move(o),raknet::RakNetReliability::ReliableOrdered,0,error))return false;
    return true;
}

bool LocalBedrock2193Server::handle_login_payload(const std::vector<std::uint8_t>& payload,std::string* error){
    saw_login_=true;
    std::vector<std::uint8_t> client_pub;if(!extract_client_public_der(payload,client_pub)){
        set_error(error, "unable to extract client public key from Login connection request");
        return false;
    }
    EcKeyPairDer server_key;if(!crypto_.generate_p384_keypair(server_key,error))return false;
    const std::vector<std::uint8_t> salt{0x10,0x11,0x12,0x13,0x14,0x15,0x16,0x17,0x18,0x19,0x1a,0x1b,0x1c,0x1d,0x1e,0x1f};
    std::vector<std::uint8_t> shared;if(!crypto_.p384_shared_secret_x(server_key.private_key_pkcs8_der,client_pub,shared,error))return false;
    std::vector<std::uint8_t> mat=salt;mat.insert(mat.end(),shared.begin(),shared.end());if(!crypto_.sha256(mat,encryption_key_,error))return false;
    const std::string header=std::string("{\"alg\":\"ES384\",\"x5u\":\"")+b64std(server_key.public_key_pkix_der)+"\"}";
    const std::string body=std::string("{\"salt\":\"")+b64std(salt)+"\"}";
    const auto jwt=sign_jwt(crypto_,server_key,header,body);if(jwt.empty()){set_error(error,"server handshake JWT signing failed");return false;}
    if(!send_game_packet(packet_id::ServerToClientHandshake,std::vector<std::uint8_t>(jwt.begin(),jwt.end()),false,error))return false;
    if (!cipher_.enable(encryption_key_, error)) return false;
    sent_server_handshake_ = true;
    return true;
}

bool LocalBedrock2193Server::send_game_packet(std::uint32_t id,const std::vector<std::uint8_t>& payload,bool encrypted,std::string* error){
    std::vector<std::uint8_t> game;while(id>=0x80){game.push_back(static_cast<std::uint8_t>(id)|0x80u);id>>=7;}game.push_back(static_cast<std::uint8_t>(id));game.insert(game.end(),payload.begin(),payload.end());
    wire::BatchCodecConfig cfg;auto batch=wire::BedrockBatchCodec::encode({game},cfg,error?nullptr:nullptr);if(!batch){set_error(error,"game batch encode failed");return false;}
    std::vector<std::uint8_t> wire=*batch;if(encrypted&&!cipher_.encrypt_packet(wire,error))return false;
    if (!reliability_.queue(wire, raknet::RakNetReliability::ReliableOrdered, 0, error)) return false;
    return true;
}

bool LocalBedrock2193Server::handle_client_game_batch(const std::vector<std::uint8_t>& wire,std::string* error){
    std::vector<std::uint8_t> data=wire;if(cipher_.enabled()&&!cipher_.decrypt_packet(data,error))return false;
    wire::BatchError be;auto decoded=wire::BedrockBatchCodec::decode(data,wire::BatchCodecConfig{},&be);if(!decoded){set_error(error,be.message);return false;}
    for(const auto&p:*decoded){
        TransportFrame f;f.identity={ProtocolIdentity::kGameVersion,ProtocolIdentity::kProtocol,BedrockTransport::RakNet};f.direction=Direction::Serverbound;f.packet_id=p.packet_id;f.sequence=0;f.payload=p.payload;
        recorder_.record(f,steady_ms(),nullptr);
        if(p.packet_id==packet_id::RequestNetworkSettings){saw_request_network_settings_=true;if(!send_game_packet(packet_id::NetworkSettings,std::vector<std::uint8_t>{0,0,0xff,0xff,0,0,0,0,0x80,0x3f},false,error))return false;}
        else if(p.packet_id==packet_id::Login){if(!handle_login_payload(p.payload,error))return false;}
        else if(p.packet_id==packet_id::ClientToServerHandshake){saw_client_handshake_=true;if(!send_game_packet(packet_id::StartGame,{},true,error))return false;sent_start_game_=true;}
    }
    return true;
}

bool LocalBedrock2193Server::handle_connected_payload(const raknet::ReceivedPayload& payload,std::string* error){
    if (payload.payload.empty()) return true;
    const auto id = payload.payload[0];
    if(id==raknet::offline::ConnectionRequest){if(payload.payload.size()!=18){set_error(error,"ConnectionRequest size invalid");return false;}client_guid_=get_be64(payload.payload,1);const auto ts=get_be64(payload.payload,9);if(!connection_accepted_){if(!send_connection_accepted(ts,error))return false;connection_accepted_=true;}return true;}
    if(id==0xfe)return handle_client_game_batch(payload.payload,error);
    return true;
}

bool LocalBedrock2193Server::process_datagram(const std::vector<std::uint8_t>& bytes,const void* peer_addr,std::size_t peer_addr_len,std::string* error){
    if(client_address_.empty()){client_address_.resize(peer_addr_len);std::memcpy(client_address_.data(),peer_addr,peer_addr_len);client_address_len_=peer_addr_len;}
    if (bytes.empty()) return true;
    const auto id = bytes[0];
    if(id==raknet::offline::OpenConnectionRequest1){if(bytes.size()<18||!raknet::OfflineMessageCodec::is_offline_magic(bytes,1)){set_error(error,"bad OCR1");return false;}got_open1_=true;return send_offline_reply1(error);}
    if(id==raknet::offline::OpenConnectionRequest2){if(bytes.size()<31||!raknet::OfflineMessageCodec::is_offline_magic(bytes,1)){set_error(error,"bad OCR2");return false;}client_mtu_=std::min<std::uint16_t>(mtu_,get_be16(bytes,bytes.size()-10));got_open2_=true;return send_offline_reply2(error);}
    if(id==raknet::kAck||id==raknet::kNack|| (id>=raknet::kFrameSetMin&&id<=raknet::kFrameSetMax)){
        bool callback_failed=false;std::string cb;
        if(!reliability_.ingest(bytes,steady_ms(),[this,&callback_failed,&cb](const raknet::ReceivedPayload&p){std::string e;if(!handle_connected_payload(p,&e)){callback_failed=true;cb=e;}},error))return false;
        if(callback_failed){set_error(error,cb.empty()?"connected payload rejected":cb);return false;}
        return true;
    }
    return true;
}

bool LocalBedrock2193Server::poll(std::string* error){
    if(socket_fd_<0){set_error(error,"interop server is not started");return false;}
    for(;;){std::array<std::uint8_t,2048> buf{};sockaddr_storage peer{};socklen_t plen=sizeof(peer);const auto n=::recvfrom(socket_fd_,buf.data(),buf.size(),0,reinterpret_cast<sockaddr*>(&peer),&plen);if(n<0){if(errno==EAGAIN||errno==EWOULDBLOCK||errno==EINTR)break;set_error(error,std::strerror(errno));return false;}if(n==0)continue;if(!process_datagram(std::vector<std::uint8_t>(buf.begin(),buf.begin()+n),&peer,plen,error))return false;}
    reliability_.tick(steady_ms());if(reliability_.terminal()){set_error(error,reliability_.last_error());return false;}
    while(auto p=reliability_.next_outbound()){if(!send_datagram(p->bytes,error)){reliability_.requeue_front(std::move(*p));return false;}reliability_.mark_sent(*p,steady_ms());}
    return true;
}

} // namespace bedrockai::interop

#endif
