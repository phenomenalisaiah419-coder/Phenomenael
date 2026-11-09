// Prints docs/SKILL_CATALOG.md from the compiled registry, so the document can never drift from the code.
//   make catalog > docs/SKILL_CATALOG.md
#include "core/technique_registry.hpp"

#include <iostream>
#include <map>
#include <vector>

using namespace bedrockai;

static std::string editions(std::uint8_t m) { return m == kBothEditions ? "Bedrock + Java" : (m == kBedrock ? "Bedrock" : "Java"); }

int main() {
    const TechniqueRegistry& reg = TechniqueRegistry::instance();
    const TechniqueRegistry::Counts c = reg.counts();
    std::cout << "# Skill catalogue (generated from `core/src/technique_registry.cpp`)\n\n"
              << "Total **" << c.total << "** entries: **" << c.executable << "** executable (simulator-verified), **" << c.scripted
              << "** scripted (need live calibration), **" << c.planned << "** planned, **" << c.blocked << "** blocked by policy.\n\n"
              << "Maturity is deliberately strict. *Executable* means the decision logic and an executor exist in this repository and pass the\n"
                 "closed-loop simulator tests; it does **not** mean it has been proven in a real Minecraft client.\n"
                 "*Blocked* entries are things the controller will never do (bed destruction, economy automation, exploits, anti-cheat evasion).\n\n";
    std::map<TechFamily, std::vector<const TechniqueSpec*>> by;
    for (const TechniqueSpec& s : reg.all()) by[s.family].push_back(&s);
    for (const auto& kv : by) {
        std::cout << "## " << family_name(kv.first) << "\n\n| id | technique | editions | maturity | risk | needs calibration | requires |\n|---|---|---|---|---|---|---|\n";
        for (const TechniqueSpec* s : kv.second) {
            const char* risk = s->risk == Risk::Low ? "low" : (s->risk == Risk::Medium ? "medium" : "high");
            std::cout << "| `" << s->id << "` | " << s->name << " | " << editions(s->editions) << " | " << maturity_name(s->maturity) << " | " << risk
                      << " | " << (s->needs_calibration ? "yes" : "no") << " | " << s->requires_text << " |\n";
        }
        std::cout << "\n";
    }
    return 0;
}
