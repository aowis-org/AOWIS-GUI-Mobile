#include <aowis/model/units/quantity.h>
#include <aowis/model/units/unit_profile.h>

#ifdef NDEBUG
#undef NDEBUG // Keep regression assertions enabled in RelWithDebInfo builds.
#endif
#include <cassert>
#include <string>
#include <vector>

int main()
{
    const std::vector<aowis::units::UnitProfile> profiles = aowis::units::predefinedProfiles();
    assert(profiles.size() == 12);
    assert(profiles.front().id == "canonical");
    assert(profiles.at(1).id == "CMH");
    assert(profiles.at(7).id == "CFS");
    assert(aowis::units::resolvedUnit(profiles.at(7), "length") == "ft");
    assert(aowis::units::resolvedUnit(profiles.at(7), "volumetric_flow_rate") == "ft3/s");
    assert(aowis::units::resolvedUnit(profiles.at(7), "base_demand_flow_rate") == "ft3/s");
    assert(!aowis::units::isAllowedUnit("altitude", "psi"));
    assert(aowis::units::isAllowedUnit("altitude", "ft"));

    aowis::units::UnitProfileCollection collection;
    aowis::units::UnitProfile custom = profiles.at(7);
    custom.id = "custom-id";
    custom.name = "Field";
    custom.builtin = false;
    assert(collection.add(custom));
    assert(!collection.add(custom));
    assert(collection.select("custom-id"));
    assert(!collection.setUnit("custom-id", "altitude", "psi"));
    assert(collection.setUnit("custom-id", "altitude", "m"));
    assert(aowis::units::resolvedUnit(*collection.find("custom-id"), "altitude") == "m");
    assert(collection.remove("custom-id"));
    assert(collection.activeId() == "canonical");
    return 0;
}
