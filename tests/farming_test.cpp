#include "cata_catch.h"
#include "crop_profile.h"
#include "type_id.h"
#include "units.h"

static const crop_profile_id crop_profile_crop_corn( "crop_corn" );
static const crop_profile_id crop_profile_crop_generic( "crop_generic" );

static const itype_id itype_feces_manure( "feces_manure" );

// Realistic farming: the crop response curves behave as agronomy describes.

static crop_profile warm_season_crop()
{
    crop_profile profile;
    profile.base_temp = units::from_celsius( 10.0 );
    profile.max_growth_temp = units::from_celsius( 30.0 );
    profile.reference_temp = units::from_celsius( 20.0 );
    profile.frost_damage_temp = units::from_celsius( -1.0 );
    profile.frost_kill_temp = units::from_celsius( -3.0 );
    profile.hardened_kill_temp = units::from_celsius( -3.0 );
    profile.heat_stress_temp = units::from_celsius( 35.0 );
    return profile;
}

TEST_CASE( "growing_degree_days_follow_the_min_max_method", "[farming]" )
{
    const crop_profile profile = warm_season_crop();
    // Nothing grows below the base temperature.
    CHECK( profile.degree_days( 0.0, 8.0 ) == Approx( 0.0 ) );
    // The cold end of the day is raised to the base temperature: (10 + 15) / 2 - 10.
    CHECK( profile.degree_days( 5.0, 15.0 ) == Approx( 2.5 ) );
    // Heat above the cap adds nothing: (20 + 30) / 2 - 10.
    CHECK( profile.degree_days( 20.0, 40.0 ) == Approx( 15.0 ) );
    // A day at the reference temperature gives the reference amount.
    CHECK( profile.degree_days( 20.0, 20.0 ) == Approx( profile.reference_degree_days() ) );
}

TEST_CASE( "frost_damage_rises_from_light_frost_to_killing_frost", "[farming]" )
{
    const crop_profile profile = warm_season_crop();
    CHECK( profile.frost_damage( 0.0, 0.0 ) == Approx( 0.0 ) );
    CHECK( profile.frost_damage( -2.0, 0.0 ) == Approx( 0.25 ) );
    CHECK( profile.frost_damage( -3.0, 0.0 ) == Approx( 1.0 ) );
    CHECK_FALSE( profile.overwinters() );
}

TEST_CASE( "hardy_crops_survive_harder_frost_once_acclimated", "[farming]" )
{
    crop_profile profile = warm_season_crop();
    profile.hardened_kill_temp = units::from_celsius( -20.0 );
    CHECK( profile.overwinters() );
    // Unhardened, -10 C kills it outright; fully hardened, it only burns some leaves.
    CHECK( profile.frost_damage( -10.0, 0.0 ) == Approx( 1.0 ) );
    CHECK( profile.frost_damage( -10.0, 1.0 ) < 0.5 );
}

TEST_CASE( "heat_during_flowering_costs_part_of_the_harvest", "[farming]" )
{
    const crop_profile profile = warm_season_crop();
    CHECK( profile.heat_loss( 34.0 ) == Approx( 0.0 ) );
    CHECK( profile.heat_loss( 37.0 ) == Approx( 0.08 ) );
    CHECK( profile.heat_loss( 60.0 ) == Approx( 0.5 ) );
}

TEST_CASE( "seeds_and_fertilizers_have_farming_data", "[farming]" )
{
    CHECK( crop_profile_crop_generic.is_valid() );
    CHECK( crop_profile_crop_corn.is_valid() );
    const soil_amendment &manure = soil_amendment::for_item( itype_feces_manure );
    CHECK( manure.dose > 1 );
    CHECK( manure.organic_nitrogen > 0.0 );
}
