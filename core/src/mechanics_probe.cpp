#include "core/mechanics_probe.hpp"
#include <algorithm>
#include <iomanip>
#include <limits>
#include <sstream>
namespace bedrockai {
void MechanicsProbe::record(const ProbeObservation& o){ observations_.push_back(o); }
ProbeStatistics MechanicsProbe::summarize(ProbeKind k) const {
    ProbeStatistics s; double sum=0; bool any=false;
    for(const auto& o:observations_) if(o.kind==k){ s.count++; if(o.success)s.successes++; if(!o.success)continue; any=true; s.min=any&&s.successes==1?o.value:std::min(s.min,o.value); s.max=any&&s.successes==1?o.value:std::max(s.max,o.value); sum+=o.value; }
    if(s.successes) {
        s.mean=sum/static_cast<double>(s.successes);
    } else {
        s.min=s.max=0;
    }
    return s;
}
std::string MechanicsProbe::export_json() const {
    std::ostringstream out; out<<"{\"version\":1,\"game_version\":\"1.26.50\",\"protocol\":2193,\"observations\":[";
    for(size_t i=0;i<observations_.size();++i){ const auto&o=observations_[i]; if(i)out<<","; out<<"{\"kind\":"<<static_cast<int>(o.kind)<<",\"value\":"<<std::setprecision(10)<<o.value<<",\"tick\":"<<o.tick<<",\"success\":"<<(o.success?"true":"false")<<",\"note\":\""; for(char c:o.note){ if(c=='\\'||c=='\"')out<<'\\'; out<<c; } out<<"\"}"; }
    out<<"]}"; return out.str();
}
void MechanicsProbe::clear(){ observations_.clear(); }
}
