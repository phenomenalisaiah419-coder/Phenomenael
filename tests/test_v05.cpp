#include "core/transport.hpp"
#include <cassert>
#include <iostream>
#include <vector>
using namespace bedrockai;
#define CHECK(x) do { if(!(x)){ std::cerr << "FAIL: " << #x << "\n"; return 1; } } while(false)

class FakeBackend final : public BedrockTransportBackend {
public:
    bool connected=false, closed=false;
    std::vector<TransportFrame> sent;
    ReceiveHandler handler;
    bool connect(const ProtocolIdentity& id, std::string*) override { connected=id.is_target(); return connected; }
    bool send(const TransportFrame& f, std::string*) override { if(!connected) return false; sent.push_back(f); return true; }
    void close() override { closed=true; connected=false; }
    TransportState state() const override { return connected ? TransportState::InGame : TransportState::Disconnected; }
    void set_receive_handler(ReceiveHandler h) override { handler=std::move(h); }
    void push(const TransportFrame& f) { if(handler) handler(f); }
};

int main(){
    TransportFrame f;
    f.identity={"1.26.50",2193,BedrockTransport::RakNet};
    f.direction=Direction::Serverbound;
    f.packet_id=packet_id::PlayerAuthInput;
    f.sequence=7;
    f.payload={0,1,2,3,4,5};
    auto bytes=TransportFrameCodec::encode(f);
    std::string err;
    auto decoded=TransportFrameCodec::decode(bytes,&err);
    CHECK(decoded.has_value());
    CHECK(decoded->identity.is_target());
    CHECK(decoded->packet_id==packet_id::PlayerAuthInput);
    CHECK(decoded->payload==f.payload);
    bytes.back() ^= 1;
    CHECK(!TransportFrameCodec::decode(bytes,&err).has_value());

    FakeBackend backend;
    Protocol2193Session session;
    CHECK(session.attach(backend,&err));
    CHECK(session.state()==TransportState::Authenticating);
    CHECK(session.queue_serverbound(packet_id::PlayerAuthInput,{9,8,7},&err));
    CHECK(session.pending_outbound()==1);
    CHECK(session.flush(&err));
    CHECK(backend.sent.size()==1 && backend.sent[0].packet_id==packet_id::PlayerAuthInput);

    TransportFrame start;
    start.identity={"1.26.50",2193,BedrockTransport::RakNet};
    start.direction=Direction::Clientbound;
    start.packet_id=packet_id::StartGame;
    start.sequence=1;
    backend.push(start);
    CHECK(session.state()==TransportState::InGame);

    TransportFrame bad=start;
    bad.identity.protocol=2192;
    CHECK(!session.ingest(bad,&err));
    CHECK(session.state()==TransportState::Failed);

    session.detach();
    CHECK(backend.closed);
    std::cout << "v0.5 transport tests: PASS\n";
}
