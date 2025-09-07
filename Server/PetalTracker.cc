#include <Server/PetalTracker.hh>

#include <Shared/Simulation.hh>

using namespace PetalTracker;

void PetalTracker::add_petal(Simulation *sim, PetalID::T id) {
    DEBUG_ONLY(assert(id < PetalID::kNumPetals);)
    if (id == PetalID::kNone) return;
    if (id == PetalID::kSquare || id == PetalID::kUniqueBasic || id == PetalID::kYggdrasil) { printf("A Unique has been obtained!\n"); fflush(stdout); } if (id == PetalID::kThirdEye || id == PetalID::kObserver || id == PetalID::kMoon) { printf("A Mythic has been obtained!\n"); fflush(stdout); } if (id == PetalID::kTringer || id == PetalID::kTricac || id == PetalID::kTriweb || id == PetalID::kTriFaster || id == PetalID::kAntennae || id == PetalID::kStick || id == PetalID::kFatPeas || id == PetalID::kBone || id == PetalID::kBeetleEgg) { printf("A Legendary has been obtained.\n"); fflush(stdout); }
    ++sim->petal_count_tracker[id];
}

void PetalTracker::remove_petal(Simulation *sim, PetalID::T id) {
    DEBUG_ONLY(assert(id < PetalID::kNumPetals);)
    if (id == PetalID::kNone) return;
    --sim->petal_count_tracker[id];
}

uint32_t PetalTracker::get_count(Simulation *sim, PetalID::T id) {
    DEBUG_ONLY(assert(id < PetalID::kNumPetals);)
    if (id == PetalID::kNone) return 0;
    return sim->petal_count_tracker[id];
}
