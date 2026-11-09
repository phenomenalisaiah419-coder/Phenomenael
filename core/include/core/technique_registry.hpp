#pragma once

// The skill collection (v0.35): a catalogue of Minecraft techniques with HONEST maturity.
//
//   Executable  decision logic + executor exist in this repository and are verified in the
//               closed-loop simulator (not yet in a real game).
//   Scripted    recipe, preconditions and success test are defined; needs measured timings from
//               the mechanics probe and a live executor.
//   Planned     catalogued only; no executor yet.
//   Blocked     forbidden by policy and never implemented (bed destruction, economy automation,
//               exploits that need modified packets, anti-cheat evasion, identity spoofing).
//
// "Glitches and tricks" that a human can perform with normal inputs (clutches, bridging styles,
// movement tech) are catalogued as techniques.  Exploits that require packets or client changes
// beyond what a normal player can do are Blocked, not "future work".

#include <cstdint>
#include <string>
#include <vector>

namespace bedrockai {

enum class TechFamily : std::uint8_t {
    MeleeCombat, RangedCombat, Movement, Bridging, Clutch, Survival, Mining, Building, Items, Mobs,
    Boss, Speedrun, Navigation, Perception, Planning, Blocked
};
enum class Maturity : std::uint8_t { Executable, Scripted, Planned, Blocked };
enum class Risk : std::uint8_t { Low, Medium, High };

constexpr std::uint8_t kBedrock = 1;
constexpr std::uint8_t kJava = 2;
constexpr std::uint8_t kBothEditions = 3;

constexpr std::uint32_t kModePvP = 1U;
constexpr std::uint32_t kModeBedWars = 2U;
constexpr std::uint32_t kModeSurvival = 4U;
constexpr std::uint32_t kModeSpeedrun = 8U;
constexpr std::uint32_t kModeParkour = 16U;
constexpr std::uint32_t kModeBuild = 32U;
constexpr std::uint32_t kModeBoss = 64U;
constexpr std::uint32_t kModeAny = 0xFFU;

const char* family_name(TechFamily f);
const char* maturity_name(Maturity m);

struct TechniqueSpec {
    const char* id;
    const char* name;
    TechFamily family;
    std::uint8_t editions;
    Maturity maturity;
    Risk risk;
    bool survival_critical;
    bool needs_calibration;
    std::uint32_t modes;
    const char* requires_text;
    const char* summary;
};

class TechniqueRegistry {
public:
    struct Counts {
        std::size_t total = 0, executable = 0, scripted = 0, planned = 0, blocked = 0;
    };
    static const TechniqueRegistry& instance();
    const std::vector<TechniqueSpec>& all() const { return specs_; }
    const TechniqueSpec* find(const std::string& id) const;
    std::vector<const TechniqueSpec*> filter(std::uint8_t edition_mask, std::uint32_t mode_mask,
                                             Maturity at_most) const;
    Counts counts() const;
    bool blocked_by_policy(const std::string& id) const;
    // Catalogue integrity: unique ids, non-empty fields, Blocked entries live in the Blocked family.
    bool validate(std::string* error = nullptr) const;

private:
    TechniqueRegistry();
    std::vector<TechniqueSpec> specs_;
};

} // namespace bedrockai
