#pragma once
#ifndef CATA_SRC_FARMING_H
#define CATA_SRC_FARMING_H

#include <string>
#include <vector>

#include "calendar.h"
#include "coords_fwd.h"
#include "ret_val.h"
#include "type_id.h"

class Character;
class item;
class map;

/**
 * Realistic crop simulation: growth by accumulated warmth, frost and heat damage,
 * protection from covers, greenhouses and snow.  Plant state lives in item variables
 * on the planted seed; see doc/SIMULATION_FEATURES.md for the design.
 */
namespace farming
{

/** Whether the "Realistic farming" world option is on. */
bool enabled();

/** One day of weather at a location, summarized for crops. */
struct daily_weather {
    double low_c = 0.0;
    double high_c = 0.0;
    double mean_c = 0.0;
    /** Rain that fell, in millimeters (liters per square meter). */
    double rain_mm = 0.0;
    /** Snow that fell, as millimeters of melted water. */
    double snow_water_mm = 0.0;
    /** Sunlight energy reaching the ground, in joules per square meter. */
    double radiant_exposure = 0.0;
};

/** Days since the start of the game; day boundaries are midnight. */
int day_index( const time_point &t );
time_point day_start( int day_index );

/** Weather summary for one day over one overmap tile.  Cached, so cheap to call repeatedly. */
const daily_weather &weather_on_day( const tripoint_abs_omt &omt, int day_index );

/** Call when a seed is put in the ground so the simulation starts from planting day. */
void on_planted( item &seed );

/**
 * Bring a planted seed up to date with all days that passed since it was last simulated.
 * @return false if the plant died; it has then been removed and the tile cleared.
 */
bool simulate_plant( map &here, const tripoint_bub_ms &p, item &seed );

/** How far the plant has grown, expressed as the time it would take at its reference temperature. */
time_duration growth_age( const item &seed );

/** Speed growth by the equivalent of this much time at the reference temperature (fertilizer). */
void add_growth( item &seed, const time_duration &equivalent );

/** Multiplier for the harvest: health and lost flowers. */
double harvest_factor( const item &seed );

/** Lines describing the plant's condition, more precise with Survival skill. */
std::vector<std::string> describe_plant( const map &here, const tripoint_bub_ms &p,
        const item &seed, const Character &observer );

/** Whether the tile is inside a greenhouse. */
bool is_greenhouse( const map &here, const tripoint_bub_ms &p );

/** Whether a crop planted here and now is likely to survive and ripen, judged from the local climate. */
ret_val<void> planting_outlook( const map &here, const tripoint_bub_ms &p,
                                const itype_id &seed_type, int survival_skill );

/** Frost cover currently over the plant, or a null id. */
itype_id cover_of( const item &seed );
void set_cover( item &seed, const itype_id &cover );
/** Drop any frost cover on the plant at the given spot, e.g. when it is harvested or dies. */
void drop_cover( map &here, const tripoint_bub_ms &where, item &seed );

namespace detail
{
/** Required growing degree days to reach the start of each growth stage, in order. */
std::vector<double> stage_thresholds( const itype_id &seed_type );
} // namespace detail

} // namespace farming

#endif // CATA_SRC_FARMING_H
