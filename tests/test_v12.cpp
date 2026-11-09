#include "core/bedrock_crypto.hpp"
#include "core/login_payload.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

using namespace bedrockai;

static int failures = 0;
#define CHECK(c, m) do { if (!(c)) { ++failures; std::cerr << "FAIL: " << m << "\n"; } } while (0)

static std::vector<std::uint8_t> b64url_decode(const std::string& input, bool* ok) {
    std::vector<std::uint8_t> out; std::uint32_t acc=0; unsigned bits=0;
    for(char c:input){ std::uint8_t v; if(c>='A'&&c<='Z')v=static_cast<std::uint8_t>(c-'A');else if(c>='a'&&c<='z')v=static_cast<std::uint8_t>(26+c-'a');else if(c>='0'&&c<='9')v=static_cast<std::uint8_t>(52+c-'0');else if(c=='-')v=62;else if(c=='_')v=63;else{*ok=false;return{};} acc=(acc<<6)|v;bits+=6;if(bits>=8){bits-=8;out.push_back(static_cast<std::uint8_t>((acc>>bits)&0xffu));}}
    if(bits>=6 || (bits && (acc & ((1u<<bits)-1u)))){*ok=false;return{};} *ok=true; return out;
}

static std::string field(const std::string& json, const std::string& name) {
    const auto n = json.find("\"" + name + "\""); if(n==std::string::npos) return {};
    auto p=json.find(':',n); if(p==std::string::npos) return {}; while(++p<json.size() && (json[p]==' '||json[p]=='\t')){} if(p>=json.size()||json[p]!='"') return {}; ++p;
    std::string o; while(p<json.size()){char c=json[p++]; if(c=='"')return o; if(c=='\\'&&p<json.size()){char e=json[p++]; o.push_back(e);}else o.push_back(c);} return{};
}

static void test_crypto_roundtrip() {
    OpenSSLBedrockCryptoProvider crypto;
    EcKeyPairDer a,b;
    std::string e;
    CHECK(crypto.generate_p384_keypair(a,&e), "P-384 key A generated");
    CHECK(crypto.generate_p384_keypair(b,&e), "P-384 key B generated");
    CHECK(a.private_key_pkcs8_der.size() > 5 && a.private_key_pkcs8_der[0] == 0x30 &&
          a.private_key_pkcs8_der[3] == 0x02 && a.private_key_pkcs8_der[4] == 0x01 &&
          a.private_key_pkcs8_der[5] == 0x00, "private key uses PKCS#8 PrivateKeyInfo encoding");
    std::vector<std::uint8_t> sa,sb;
    CHECK(crypto.p384_shared_secret_x(a.private_key_pkcs8_der,b.public_key_pkix_der,sa,&e), "ECDH A succeeds");
    CHECK(crypto.p384_shared_secret_x(b.private_key_pkcs8_der,a.public_key_pkix_der,sb,&e), "ECDH B succeeds");
    CHECK(sa==sb && sa.size()==48, "P-384 shared secret matches and is 48 bytes");

    const std::vector<std::uint8_t> input{'t','e','s','t','.','j','w','t'};
    std::vector<std::uint8_t> sig;
    CHECK(crypto.sign_es384(a.private_key_pkcs8_der,input,sig,&e), "ES384 signing succeeds");
    CHECK(sig.size()==96, "JOSE ES384 signature is 96 bytes");
    CHECK(crypto.verify_es384(a.public_key_pkix_der,input,sig,&e), "ES384 verification succeeds");
    std::vector<std::uint8_t> bad=input; bad.push_back(0x00);
    CHECK(!crypto.verify_es384(a.public_key_pkix_der,bad,sig,&e), "tampered ES384 input rejected");
}

static void test_packet_cipher() {
    BedrockPacketCipher a,b;
    std::array<std::uint8_t,32> key{}; for(size_t i=0;i<key.size();++i) key[i]=static_cast<std::uint8_t>(i+1);
    std::string e;
    CHECK(a.enable(key,&e), "client cipher enabled"); CHECK(b.enable(key,&e), "server cipher enabled");
    std::vector<std::uint8_t> p1{0xfe,1,2,3,4,5}; std::vector<std::uint8_t> original=p1;
    CHECK(a.encrypt_packet(p1,&e), "encrypt packet"); CHECK(p1[0]==0xfe, "packet header stays plaintext"); CHECK(a.send_counter()==1, "send counter advances");
    CHECK(b.decrypt_packet(p1,&e), "decrypt packet"); CHECK(p1==original, "decrypt roundtrip restores packet"); CHECK(b.receive_counter()==1, "receive counter advances");
    std::vector<std::uint8_t> p2{0x04}; CHECK(a.encrypt_packet(p2,&e), "encrypt handshake packet with empty body"); CHECK(p2.size()==9, "empty packet gains 8-byte checksum"); CHECK(b.decrypt_packet(p2,&e), "decrypt empty handshake packet"); CHECK(p2==std::vector<std::uint8_t>{0x04}, "empty handshake packet restores exactly");
}

static std::string b64url_text(const std::string& input) {
    static constexpr char t[]="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    std::string o; std::uint32_t a=0; unsigned b=0;
    for(char ch: input){ const auto x=static_cast<unsigned char>(ch); a=(a<<8)|x;b+=8;while(b>=6){b-=6;o.push_back(t[(a>>b)&63]);}}
    if(b) o.push_back(t[(a<<(6-b))&63]);
    return o;
}

static void test_login_builder() {
    OpenSSLBedrockCryptoProvider crypto;
    std::string e;
    EcKeyPairDer chain_key;
    CHECK(crypto.generate_p384_keypair(chain_key,&e), "chain fixture key generated");

    auto std_b64 = [](const std::vector<std::uint8_t>& input) {
        static constexpr char t[]="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        std::string o; std::uint32_t a=0; unsigned b=0;
        for(auto x:input){a=(a<<8)|x;b+=8;while(b>=6){b-=6;o.push_back(t[(a>>b)&63]);}}
        if(b) o.push_back(t[(a<<(6-b))&63]);
        while(o.size()%4) o.push_back('=');
        return o;
    };
    const auto x5u=std_b64(chain_key.public_key_pkix_der);
    LoginCredentialMaterial m;
    m.authentication_type = 0;
    m.login_chain_json = std::string("{\"chain\":[\"") +
                         b64url_text(std::string("{\"x5u\":\"") + x5u + "\"}") +
                         ".payload.signature\"]}";
    m.client_data_json = "{\"GameVersion\":\"1.26.50\",\"ServerAddress\":\"127.0.0.1:19132\"}";

    class ContextProvider : public BedrockLoginAuthProvider {
    public:
        explicit ContextProvider(LoginCredentialMaterial material) : material_(std::move(material)) {}
        LoginCredentialMaterial material_;
        bool get_credentials(const LoginAuthRequestContext& ctx, LoginCredentialMaterial& out, std::string*) override {
            out = material_;
            const std::string payload = std::string("{\"cpk\":\"") + ctx.client_public_key_base64 + "\"}";
            out.multiplayer_token = b64url_text("{\"alg\":\"none\"}") + "." + b64url_text(payload) + ".test";
            return true;
        }
    } provider(m);

    Bedrock2193LoginPayloadBuilder builder(crypto);
    LoginAuthRequestContext ctx{ProtocolIdentity{ProtocolIdentity::kGameVersion,ProtocolIdentity::kProtocol,BedrockTransport::RakNet},"127.0.0.1:19132",""};
    LoginArtifact artifact;
    const bool built = builder.build(ctx,provider,artifact,&e);
    CHECK(built, std::string("exact Login connection request is built: ") + e);
    if (!built) return;
    CHECK(!artifact.connection_request.empty(), "connection request non-empty");
    CHECK(!artifact.login_packet_payload.empty(), "Login packet payload non-empty");
    CHECK(artifact.login_packet_payload.size() > 4 && artifact.login_packet_payload[0]==0x00 && artifact.login_packet_payload[1]==0x00 && artifact.login_packet_payload[2]==0x08 && artifact.login_packet_payload[3]==0x91,
          "Login packet starts with big-endian protocol 2193");

    // Verify the raw client-data JWT signature and cpk header with the generated identity key.
    const auto& bytes=artifact.connection_request;
    CHECK(bytes.size()>8, "connection request has length fields");
    if (bytes.size() <= 8) return;
    auto read_le32=[&](size_t at){ return static_cast<std::uint32_t>(bytes[at]) | (static_cast<std::uint32_t>(bytes[at+1])<<8) | (static_cast<std::uint32_t>(bytes[at+2])<<16) | (static_cast<std::uint32_t>(bytes[at+3])<<24); };
    const auto chain_len=read_le32(0); CHECK(4+chain_len+4<=bytes.size(), "certificate length fits");
    const auto raw_len=read_le32(4+chain_len); CHECK(8+chain_len+raw_len==bytes.size(), "raw token length fits exactly");
    const std::string raw(reinterpret_cast<const char*>(bytes.data()+8+chain_len),raw_len);
    const auto p1=raw.find('.'), p2=raw.find('.',p1+1); CHECK(p1!=std::string::npos && p2!=std::string::npos, "raw client-data token is compact JWT");
    bool ok=false; const auto sig=b64url_decode(raw.substr(p2+1),&ok); CHECK(ok && sig.size()==96, "raw JWT signature is JOSE ES384");
    const auto signed_input=std::vector<std::uint8_t>(raw.begin(),raw.begin()+static_cast<std::ptrdiff_t>(p2));
    CHECK(crypto.verify_es384(artifact.identity_key.public_key_pkix_der,signed_input,sig,&e), "raw ClientData JWT verifies with generated identity key");
    const auto hdr_bytes=b64url_decode(raw.substr(0,p1),&ok); CHECK(ok, "raw JWT header decodes");
    const std::string hdr(hdr_bytes.begin(),hdr_bytes.end()); CHECK(field(hdr,"x5u")==artifact.client_public_key_base64, "raw JWT x5u matches generated public key");

    class BadProvider : public BedrockLoginAuthProvider {
    public:
        explicit BadProvider(LoginCredentialMaterial material) : material_(std::move(material)) {}
        LoginCredentialMaterial material_;
        bool get_credentials(const LoginAuthRequestContext&, LoginCredentialMaterial& out, std::string*) override {
            out = material_;
            out.multiplayer_token = b64url_text("{\"alg\":\"none\"}") + "." + b64url_text("{\"cpk\":\"mismatch\"}") + ".test";
            return true;
        }
    } bad_provider(m);
    LoginArtifact bad;
    CHECK(!builder.build(ctx, bad_provider, bad, &e), "mismatched multiplayer cpk is rejected");
}

int main(){ test_crypto_roundtrip(); test_packet_cipher(); test_login_builder(); if(failures){std::cerr<<"v12 tests: "<<failures<<" failure(s)\n";return 1;} std::cout<<"v12 tests: PASS\n";return 0; }
