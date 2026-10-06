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

/** How well someone reads plants: Survival skill, plus 2 with the Gardening proficiency. */
int plant_knowledge( const Character &who );

/** Lines describing the plant's condition, more precise with Survival skill. */
std::vector<std::string> describe_plant( map &here, const tripoint_bub_ms &p,
        const item &seed, const Character &observer );

/** Plant-available water the plot holds when fully moist, in millimeters (liters per tile). */
double field_capacity( const map &here, const tripoint_bub_ms &p );
/** Bring a plot's soil up to date to today, for plots with nothing growing in them. */
void update_soil( map &here, const tripoint_bub_ms &p );
/** Whole liters of water it takes to bring the plot back to fully moist. */
int water_needed_liters( map &here, const tripoint_bub_ms &p );
/** Pour this many liters of water on the plot. */
void add_water( map &here, const tripoint_bub_ms &p, double liters );
/** Whether the plot is covered in mulch. */
bool has_mulch( map &here, const tripoint_bub_ms &p );
/** Spread mulch on the plot; it lasts about a season. */
void add_mulch( map &here, const tripoint_bub_ms &p );

/**
 * Pour water from what the character carries onto the plot, dirty water first, up to what
 * the soil can hold.
 * @return Liters poured.
 */
int water_from_inventory( Character &who, map &here, const tripoint_bub_ms &p );
/** Whether the plant growing here is short of water and would benefit from watering now. */
bool needs_water( map &here, const tripoint_bub_ms &p );
/** Exact readings from a soil test kit (and pH meter, if any) for the plot. */
std::vector<std::string> soil_report( map &here, const tripoint_bub_ms &p, bool has_ph_meter );
/**
 * What a farmhand would tell you about the crops near them, as accurate as their knowledge.
 * Empty if there are no crops nearby.
 */
std::vector<std::string> crop_report( map &here, const Character &reporter );

/** Units of this fertilizer one application uses. */
int amendment_dose( const itype_id &amendment );
/**
 * Work units of a fertilizer or other soil amendment into the plot.
 * @param seed The plant growing there, if any; overapplied fertilizer salts can burn it.
 * @return false if the plant was killed by fertilizer burn (and has been removed).
 */
bool apply_amendment( map &here, const tripoint_bub_ms &p, const itype_id &amendment, int units,
                      item *seed );
/** Call when a crop is harvested so the plot remembers what grew there. */
void on_crop_removed( map &here, const tripoint_bub_ms &p, const item &seed );
/** Pull a plant out of the ground, e.g. to stop a disease spreading.  Removes it from the map. */
void pull_up_plant( map &here, const tripoint_bub_ms &p, item &seed );
/**
 * A warning when the same plant family grew in this bed recently (crop rotation), or empty.
 * Only growers with some Survival skill think of it.
 */
std::string rotation_warning( map &here, const tripoint_bub_ms &p, const itype_id &seed_type,
                              int survival_skill );

/** Whether the tile is inside a greenhouse. */
bool is_greenhouse( const map &here, const tripoint_bub_ms &p );

/** Whether a crop would get enough light here: sun, windows or grow lights. */
ret_val<void> light_outlook( map &here, const tripoint_bub_ms &p, const itype_id &seed_type );

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
