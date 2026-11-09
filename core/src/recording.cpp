#include "core/recording.hpp"

#include <fstream>
#include <limits>

namespace bedrockai {
namespace {
constexpr char kMagic[] = "BAIRPL01";
constexpr std::uint16_t kVersion = 2;
constexpr std::uint16_t kLegacyVersion = 1;
constexpr std::size_t kMaxRecordedPayload = 4u * 1024u * 1024u;

void write_u16(std::ofstream& f, std::uint16_t v) { f.put(static_cast<char>(v)); f.put(static_cast<char>(v >> 8)); }
void write_u32(std::ofstream& f, std::uint32_t v) { for (int i=0;i<4;++i) { f.put(static_cast<char>(v)); v >>= 8; } }
void write_u64(std::ofstream& f, std::uint64_t v) { for (int i=0;i<8;++i) { f.put(static_cast<char>(v)); v >>= 8; } }
bool read_u16(std::ifstream& f, std::uint16_t& v) { int a=f.get(),b=f.get(); if(a==EOF||b==EOF)return false; v=static_cast<std::uint16_t>(a|(b<<8)); return true; }
bool read_u32(std::ifstream& f, std::uint32_t& v) { v=0; for(int i=0;i<4;++i){int b=f.get();if(b==EOF)return false;v|=static_cast<std::uint32_t>(b)<<(8*i);}return true; }
bool read_u64(std::ifstream& f, std::uint64_t& v) { v=0; for(int i=0;i<8;++i){int b=f.get();if(b==EOF)return false;v|=static_cast<std::uint64_t>(b)<<(8*i);}return true; }
bool write_bytes(std::ofstream& f, const std::vector<std::uint8_t>& b) { if(b.size()>std::numeric_limits<std::uint32_t>::max())return false; write_u32(f,static_cast<std::uint32_t>(b.size())); if(!b.empty())f.write(reinterpret_cast<const char*>(b.data()),static_cast<std::streamsize>(b.size())); return static_cast<bool>(f); }
bool read_bytes(std::ifstream& f, std::vector<std::uint8_t>& b, std::size_t max) { std::uint32_t n=0;if(!read_u32(f,n)||n>max)return false;b.resize(n);if(n)f.read(reinterpret_cast<char*>(b.data()),static_cast<std::streamsize>(n));return static_cast<bool>(f); }
}

bool TransportRecorder::record(const TransportFrame& frame, std::uint64_t timestamp_ms, std::string* error) {
    if (frames_.size() >= max_frames_) { if(error)*error="transport recorder frame limit reached"; return false; }
    if (frame.identity.game_version != ProtocolIdentity::kGameVersion || frame.identity.protocol != ProtocolIdentity::kProtocol) { if(error)*error="recorder received unsupported protocol"; return false; }
    if (frame.payload.size() > kMaxRecordedPayload) { if(error)*error="recorder payload exceeds safety limit"; return false; }
    frames_.push_back(RecordedFrame{timestamp_ms, frame});
    return true;
}
void TransportRecorder::clear(){frames_.clear();}

bool TransportRecorder::write_file(const std::string& path, std::string* error) const {
    std::ofstream f(path, std::ios::binary|std::ios::trunc);
    if(!f){if(error)*error="unable to open replay file for writing";return false;}
    f.write(kMagic,sizeof(kMagic)-1); write_u16(f,kVersion); write_u32(f,static_cast<std::uint32_t>(frames_.size()));
    for(const auto&r:frames_){
        write_u64(f,r.timestamp_ms); write_u16(f,static_cast<std::uint16_t>(r.frame.format_version));
        f.put(static_cast<char>(r.frame.direction));
        f.put(static_cast<char>(r.frame.identity.transport));
        write_u32(f,r.frame.packet_id); write_u64(f,r.frame.sequence);
        if(!write_bytes(f,r.frame.payload)){if(error)*error="failed writing replay frame";return false;}
    }
    return static_cast<bool>(f);
}

bool TransportRecorder::read_file(const std::string& path,std::vector<RecordedFrame>& out,std::size_t max_frames,std::string* error){
    std::ifstream f(path,std::ios::binary); if(!f){if(error)*error="unable to open replay file for reading";return false;}
    char magic[sizeof(kMagic)-1]{};f.read(magic,sizeof(magic));if(!f||std::string(magic,sizeof(magic))!=std::string(kMagic,sizeof(kMagic)-1)){if(error)*error="invalid replay file magic";return false;}
    std::uint16_t version=0;std::uint32_t count=0;if(!read_u16(f,version)||!read_u32(f,count)||(version!=kVersion && version!=kLegacyVersion)||count>max_frames){if(error)*error="invalid replay file header";return false;}
    out.clear();out.reserve(count);
    for(std::uint32_t i=0;i<count;++i){
        RecordedFrame r;std::uint16_t fmt=0;int dir=0;
        if(!read_u64(f,r.timestamp_ms)||!read_u16(f,fmt)){if(error)*error="truncated replay frame";return false;}
        dir=f.get();if(dir==EOF||fmt!=TransportFrame::kFormatVersion){if(error)*error="invalid replay frame header";return false;}
        if(dir < static_cast<int>(Direction::Clientbound) || dir > static_cast<int>(Direction::Serverbound)){if(error)*error="invalid replay direction";return false;}
        r.frame.format_version=fmt;r.frame.magic=TransportFrame::kMagic;r.frame.direction=static_cast<Direction>(dir);
        int transport=static_cast<int>(BedrockTransport::Unknown);
        if(version >= kVersion) { transport=f.get(); if(transport==EOF) { if(error)*error="truncated replay transport"; return false; } }
        if(transport < static_cast<int>(BedrockTransport::Unknown) || transport > static_cast<int>(BedrockTransport::NetherNet)) { if(error)*error="invalid replay transport"; return false; }
        r.frame.identity.transport=static_cast<BedrockTransport>(transport);
        if(!read_u32(f,r.frame.packet_id)||!read_u64(f,r.frame.sequence)||!read_bytes(f,r.frame.payload,4*1024*1024)){if(error)*error="truncated replay frame payload";return false;}
        r.frame.identity={ProtocolIdentity::kGameVersion,ProtocolIdentity::kProtocol,r.frame.identity.transport};
        out.push_back(std::move(r));
    }
    return true;
}

} // namespace bedrockai
