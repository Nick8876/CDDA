#include "farming.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <tuple>
#include <utility>

#include "calendar.h"
#include "character.h"
#include "coordinates.h"
#include "crop_profile.h"
#include "debug.h"
#include "flag.h"
#include "game.h"
#include "item.h"
#include "itype.h"
#include "map.h"
#include "map_scale_constants.h"
#include "mapdata.h"
#include "messages.h"
#include "options.h"
#include "string_formatter.h"
#include "translations.h"
#include "weather.h"
#include "weather_gen.h"
#include "weather_type.h"

static const itype_id itype_withered( "withered" );

static const skill_id skill_survival( "survival" );

namespace
{
// Item variables holding the plant's state.  All numbers are doubles.
constexpr const char *var_last_day = "farm_last_day";
constexpr const char *var_planted = "farm_planted";
constexpr const char *var_gdd = "farm_gdd";
constexpr const char *var_last_gdd = "farm_last_gdd";
constexpr const char *var_health = "farm_health";
constexpr const char *var_yield = "farm_yield";
constexpr const char *var_hardening = "farm_hardening";
constexpr const char *var_snow = "farm_snow";
constexpr const char *var_frost_day = "farm_frost_day";
constexpr const char *var_frost_loss = "farm_frost_loss";
constexpr const char *var_heat_day = "farm_heat_day";
constexpr const char *var_cover = "farm_cover";

// Days of cold it takes a hardy plant to fully acclimate.
constexpr double hardening_days = 14.0;
// The game's default season length, which the seeds' growth times are written for.
constexpr double default_season_days = 91.0;
// Simulating more than this many missed days is pointless: the plant is long settled.
constexpr int max_catch_up_days = 730;
// Snow at least this deep (mm) keeps the ground around low plants near freezing.
constexpr double insulating_snow_mm = 100.0;
// Same values the weather code uses for snow on the ground.
constexpr double snow_per_water_mm = 12.0;
constexpr double snow_melt_mm_per_degree_hour = 2.0;
// Covers and greenhouses trap the ground's warmth at night.
constexpr double cover_low_bonus_c = 2.5;
constexpr double cover_high_bonus_c = 1.0;
constexpr double greenhouse_low_bonus_c = 5.0;
constexpr double greenhouse_cool_day_high_bonus_c = 8.0;
constexpr double greenhouse_warm_day_high_bonus_c = 3.0;

const crop_profile &profile_of( const itype_id &seed_type )
{
    static const crop_profile_id generic( "crop_generic" );
    if( seed_type->seed && seed_type->seed->crop_profile.is_valid() ) {
        return seed_type->seed->crop_profile.obj();
    }
    return generic.obj();
}

double season_scale()
{
    return default_season_days / std::max( 1.0, to_days<double>( calendar::season_length() ) );
}

tripoint_abs_omt ground_omt( const tripoint_abs_ms &pos )
{
    const tripoint_abs_omt omt = project_to<coords::omt>( pos );
    return tripoint_abs_omt( omt.xy(), 0 );
}

/** Index of the growth stage reached with this much accumulated warmth. */
size_t stage_index( const std::vector<double> &thresholds, double gdd )
{
    size_t idx = 0;
    for( size_t i = 0; i < thresholds.size(); ++i ) {
        if( gdd >= thresholds[i] ) {
            idx = i;
        }
    }
    return idx;
}

std::string stage_flag( const itype_id &seed_type, size_t idx )
{
    const auto &stages = seed_type->seed->get_growth_stages();
    if( idx < stages.size() ) {
        return stages[idx].first.str();
    }
    return std::string();
}

void init_state( item &seed )
{
    if( seed.has_var( var_last_day ) ) {
        return;
    }
    const crop_profile &profile = profile_of( seed.typeId() );
    if( seed.has_var( var_planted ) ) {
        seed.set_var( var_last_day, static_cast<double>( farming::day_index( seed.birthday() ) ) );
        seed.set_var( var_gdd, 0.0 );
    } else {
        // Planted before this system existed, or by code that doesn't call on_planted():
        // keep its progress as if it had grown at the reference temperature.
        seed.set_var( var_last_day, static_cast<double>( farming::day_index( calendar::turn ) ) );
        seed.set_var( var_gdd, to_days<double>( seed.age() ) * profile.reference_degree_days() );
    }
    seed.set_var( var_health, 100.0 );
    seed.set_var( var_yield, 1.0 );
}

void kill_plant( map &here, const tripoint_bub_ms &p, item &seed, const std::string &cause )
{
    const std::string pname = seed.get_plant_name();
    add_msg_if_player_sees( p, _( "The %1$s has been killed by %2$s." ), pname, cause );
    farming::drop_cover( here, p, seed );
    const furn_id &furn = here.furn( p );
    furn_str_id base = furn_str_id::NULL_ID();
    if( furn->plant ) {
        base = furn->plant->base;
    }
    // Clears the seed and any fertilizer; the seed reference is dead after this.
    here.i_clear( p );
    here.furn_set( p, base );
    here.add_item_or_charges( p, item( itype_withered, calendar::turn ) );
}

int survival_level( const Character &who )
{
    return static_cast<int>( who.get_skill_level( skill_survival ) );
}

} // namespace

namespace farming
{

bool enabled()
{
    return get_option<bool>( "FARMING_SIMULATION" );
}

int day_index( const time_point &t )
{
    return to_days<int>( t - calendar::turn_zero );
}

time_point day_start( int day_index )
{
    return calendar::turn_zero + time_duration::from_days( day_index );
}

const daily_weather &weather_on_day( const tripoint_abs_omt &omt, int day )
{
    using cache_key = std::tuple<unsigned, int, int, int>;
    static std::map<cache_key, daily_weather> cache;
    const unsigned seed = g->get_seed();
    const cache_key key{ seed, omt.x(), omt.y(), day };
    const auto found = cache.find( key );
    if( found != cache.end() ) {
        return found->second;
    }
    if( cache.size() > 50000 ) {
        cache.clear();
    }

    weather_manager &weather = get_weather();
    const weather_generator &wgen = weather.get_cur_weather_gen();
    const tripoint_abs_ms location = project_to<coords::ms>( omt ) + tripoint_rel_ms( SEEX, SEEY, 0 );
    const time_point start = day_start( day );

    daily_weather result;
    double low = std::numeric_limits<double>::max();
    double high = std::numeric_limits<double>::lowest();
    double sum = 0.0;
    weather_type_id conditions = WEATHER_CLEAR;
    for( int hour = 0; hour < 24; ++hour ) {
        const time_point t = start + time_duration::from_hours( hour );
        // Precipitation and cloud only need sampling every few hours; they are slow to work out.
        if( hour % 3 == 0 ) {
            conditions = weather.weather_override != WEATHER_NULL ? weather.weather_override :
                         wgen.get_weather_conditions( location, t, seed );
            if( conditions->precip != precip_class::none ) {
                const double water_mm = precip_mm_per_hour( conditions->precip ) * 3.0;
                if( conditions->rains ) {
                    result.rain_mm += water_mm;
                } else {
                    result.snow_water_mm += water_mm;
                }
            }
        }
        const double temp = units::to_celsius( wgen.get_weather_temperature( location, t, seed ) );
        low = std::min( low, temp );
        high = std::max( high, temp );
        sum += temp;
        result.radiant_exposure += incident_sun_irradiance( conditions, t ) * 3600.0;
    }
    result.low_c = low;
    result.high_c = high;
    result.mean_c = sum / 24.0;
    return cache.emplace( key, result ).first->second;
}

void on_planted( item &seed )
{
    seed.set_var( var_planted, 1.0 );
    seed.erase_var( var_last_day );
}

namespace detail
{
std::vector<double> stage_thresholds( const itype_id &seed_type )
{
    std::vector<double> result;
    if( !seed_type->seed ) {
        return result;
    }
    const double per_day = profile_of( seed_type ).reference_degree_days();
    double days = 0.0;
    for( const auto &stage : seed_type->seed->get_growth_stages() ) {
        result.push_back( days * per_day );
        days += to_days<double>( stage.second );
    }
    return result;
}
} // namespace detail

bool simulate_plant( map &here, const tripoint_bub_ms &p, item &seed )
{
    if( !seed.type->seed ) {
        return true;
    }
    init_state( seed );
    const itype_id seed_type = seed.typeId();
    const crop_profile &profile = profile_of( seed_type );

    const int today = day_index( calendar::turn );
    int day = static_cast<int>( seed.get_var( var_last_day, static_cast<double>( today ) ) );
    if( day >= today ) {
        return true;
    }
    day = std::max( day, today - max_catch_up_days );

    const std::vector<double> thresholds = detail::stage_thresholds( seed_type );
    const tripoint_abs_omt omt = ground_omt( here.get_abs( p ) );
    const bool greenhouse = is_greenhouse( here, p );
    const bool covered = !cover_of( seed ).is_null();
    const double scale = season_scale();

    double gdd = seed.get_var( var_gdd, 0.0 );
    double last_gdd = seed.get_var( var_last_gdd, 0.0 );
    double health = seed.get_var( var_health, 100.0 );
    double yield = seed.get_var( var_yield, 1.0 );
    double hardening = seed.get_var( var_hardening, 0.0 );
    double snow = seed.get_var( var_snow, 0.0 );
    bool died = false;
    std::string cause;

    for( ; day < today; ++day ) {
        const daily_weather &w = weather_on_day( omt, day );
        double low = w.low_c;
        double high = w.high_c;
        double mean = w.mean_c;

        if( greenhouse ) {
            low += greenhouse_low_bonus_c;
            high += high < 20.0 ? greenhouse_cool_day_high_bonus_c : greenhouse_warm_day_high_bonus_c;
            mean = ( low + high ) / 2.0;
        } else {
            // Snowpack on the bed: builds from snowfall, melts on warm days.
            snow += w.snow_water_mm * snow_per_water_mm;
            if( mean > 0.0 ) {
                snow -= snow_melt_mm_per_degree_hour * 24.0 * mean;
            }
            snow = std::max( 0.0, snow );
        }
        if( covered ) {
            low += cover_low_bonus_c;
            high += cover_high_bonus_c;
        }

        const std::string stage = stage_flag( seed_type, stage_index( thresholds, gdd ) );
        const bool low_growing = stage == "GROWTH_SEED" || stage == "GROWTH_SEEDLING";
        if( snow >= insulating_snow_mm && ( low_growing || profile.overwinters() ) ) {
            low = std::max( low, -2.0 );
        }

        // Hardy plants toughen up over cold weeks and lose it again in warm spells.
        if( mean < 5.0 ) {
            hardening = std::min( hardening_days, hardening + 1.0 );
        } else if( mean > 10.0 ) {
            hardening = std::max( 0.0, hardening - 2.0 );
        }

        const double frost = profile.frost_damage( low, hardening / hardening_days );
        if( frost > 0.0 ) {
            health -= frost * 100.0;
            seed.set_var( var_frost_day, static_cast<double>( day ) );
            seed.set_var( var_frost_loss, frost );
            if( health <= 0.0 ) {
                died = true;
                cause = _( "frost" );
                break;
            }
        }

        if( stage == "GROWTH_MATURE" ) {
            const double heat = profile.heat_loss( high );
            if( heat > 0.0 ) {
                yield *= 1.0 - heat;
                seed.set_var( var_heat_day, static_cast<double>( day ) );
            }
            // Extreme heat scorches the whole plant.
            if( high > units::to_celsius( profile.heat_stress_temp ) + 8.0 ) {
                health -= 10.0;
                if( health <= 0.0 ) {
                    died = true;
                    cause = _( "heat" );
                    break;
                }
            }
        }

        last_gdd = profile.degree_days( low, high ) * scale;
        gdd += last_gdd;
        // Growing plants slowly replace damaged leaves.
        if( frost <= 0.0 && last_gdd > 0.0 ) {
            health = std::min( 100.0, health + 2.0 );
        }
    }

    if( died ) {
        kill_plant( here, p, seed, cause );
        return false;
    }
    seed.set_var( var_last_day, static_cast<double>( today ) );
    seed.set_var( var_gdd, gdd );
    seed.set_var( var_last_gdd, last_gdd );
    seed.set_var( var_health, health );
    seed.set_var( var_yield, yield );
    seed.set_var( var_hardening, hardening );
    seed.set_var( var_snow, snow );
    return true;
}

time_duration growth_age( const item &seed )
{
    if( !seed.type->seed || !seed.has_var( var_gdd ) ) {
        return seed.age();
    }
    const double days = seed.get_var( var_gdd, 0.0 ) /
                        profile_of( seed.typeId() ).reference_degree_days();
    return time_duration::from_seconds( static_cast<int64_t>( std::llround( days * 86400.0 ) ) );
}

void add_growth( item &seed, const time_duration &equivalent )
{
    init_state( seed );
    const double per_day = profile_of( seed.typeId() ).reference_degree_days();
    seed.set_var( var_gdd, seed.get_var( var_gdd, 0.0 ) + to_days<double>( equivalent ) * per_day );
}

double harvest_factor( const item &seed )
{
    const double health = std::clamp( seed.get_var( var_health, 100.0 ), 0.0, 100.0 );
    const double yield = std::clamp( seed.get_var( var_yield, 1.0 ), 0.0, 1.0 );
    // A damaged plant still sets some crop; health matters less than lost flowers.
    return yield * ( 0.25 + 0.75 * health / 100.0 );
}

bool is_greenhouse( const map &here, const tripoint_bub_ms &p )
{
    return here.has_flag_ter( "GREENHOUSE", p );
}

std::vector<std::string> describe_plant( const map &here, const tripoint_bub_ms &p,
        const item &seed, const Character &observer )
{
    std::vector<std::string> lines;
    if( !seed.type->seed ) {
        return lines;
    }
    const int skill = survival_level( observer );
    const int today = day_index( calendar::turn );
    const double health = seed.get_var( var_health, 100.0 );
    const std::string pname = seed.get_plant_name();

    if( skill < 2 ) {
        if( health >= 80.0 ) {
            lines.push_back( string_format( _( "The %s looks healthy." ), pname ) );
        } else if( health >= 35.0 ) {
            lines.push_back( string_format( _( "The %s looks unwell." ), pname ) );
        } else {
            lines.push_back( string_format( _( "The %s looks nearly dead." ), pname ) );
        }
    } else {
        if( health >= 90.0 ) {
            lines.push_back( string_format( _( "The %s looks healthy." ), pname ) );
        } else if( health >= 70.0 ) {
            lines.push_back( string_format( _( "The %s is somewhat damaged but recovering." ), pname ) );
        } else if( health >= 40.0 ) {
            lines.push_back( string_format( _( "The %s is badly damaged." ), pname ) );
        } else {
            lines.push_back( string_format( _( "The %s is barely alive." ), pname ) );
        }
    }

    const double frost_day = seed.get_var( var_frost_day, -1000.0 );
    if( today - frost_day <= 3.0 ) {
        if( skill >= 2 ) {
            lines.emplace_back( _( "Leaves are blackened and limp: frost damage from a recent cold night." ) );
        } else {
            lines.emplace_back( _( "Some leaves have gone dark and limp." ) );
        }
    }
    const double heat_day = seed.get_var( var_heat_day, -1000.0 );
    if( today - heat_day <= 3.0 ) {
        if( skill >= 3 ) {
            lines.emplace_back( _( "Flowers have dropped off in the heat; fewer will set fruit." ) );
        } else {
            lines.emplace_back( _( "Some flowers have shriveled and fallen." ) );
        }
    }

    const bool harvestable = here.has_flag_furn( "GROWTH_HARVEST", p );
    if( !harvestable && seed.has_var( var_last_gdd ) && seed.get_var( var_last_gdd, 0.0 ) <= 0.0 ) {
        lines.emplace_back( _( "It hasn't grown at all in this cold." ) );
    }

    if( !harvestable && skill >= 4 && seed.has_var( var_gdd ) ) {
        const std::vector<double> thresholds = detail::stage_thresholds( seed.typeId() );
        const auto &stages = seed.type->seed->get_growth_stages();
        double harvest_at = -1.0;
        for( size_t i = 0; i < stages.size() && i < thresholds.size(); ++i ) {
            if( stages[i].first.str() == "GROWTH_HARVEST" ) {
                harvest_at = thresholds[i];
            }
        }
        if( harvest_at > 0.0 ) {
            const double remaining = harvest_at - seed.get_var( var_gdd, 0.0 );
            const double per_day = profile_of( seed.typeId() ).reference_degree_days();
            const int days = std::max( 1, static_cast<int>( std::ceil( remaining / per_day ) ) );
            lines.push_back( string_format(
                                 n_gettext( "It should be ready after about %d warm day.",
                                            "It should be ready after about %d warm days.", days ), days ) );
        }
    }

    const itype_id cover = cover_of( seed );
    if( !cover.is_null() ) {
        lines.push_back( string_format( _( "It's covered with a %s against frost." ), cover->nname( 1 ) ) );
    }
    if( is_greenhouse( here, p ) ) {
        lines.emplace_back( _( "The greenhouse keeps it a few degrees warmer, but it isn't heated." ) );
    }
    return lines;
}

ret_val<void> planting_outlook( const map &here, const tripoint_bub_ms &p,
                                const itype_id &seed_type, int survival_skill )
{
    if( !seed_type->seed ) {
        return ret_val<void>::make_success();
    }
    const crop_profile &profile = profile_of( seed_type );
    const bool greenhouse = is_greenhouse( here, p );
    const weather_generator &wgen = get_weather().get_cur_weather_gen();
    const unsigned seed = g->get_seed();
    const tripoint_abs_ms location = here.get_abs( p );
    const double scale = season_scale();

    // What the local climate normally does on a given day; no knowledge of the actual future.
    const auto typical = [&]( int day ) {
        typical_weather_day t = wgen.get_typical_day( location,
                                day_start( day ) + time_duration::from_hours( 12 ), seed );
        double low = units::to_celsius( t.low );
        double high = units::to_celsius( t.high );
        if( greenhouse ) {
            low += greenhouse_low_bonus_c;
            high += high < 20.0 ? greenhouse_cool_day_high_bonus_c : greenhouse_warm_day_high_bonus_c;
        }
        // A cold snap half as deep as the worst the climate does is a real risk.
        return std::make_tuple( low, high, low - 0.5 * t.noise_amplitude_c );
    };

    const int today = day_index( calendar::turn );
    const std::string pname = seed_type->seed->plant_name.translated();
    const auto [low0, high0, risky_low0] = typical( today );

    if( ( low0 + high0 ) / 2.0 < units::to_celsius( profile.base_temp ) - 2.0 &&
        !profile.overwinters() ) {
        return ret_val<void>::make_failure( _( "The soil is still too cold for %s to sprout." ), pname );
    }

    if( profile.frost_damage( risky_low0, 0.0 ) > 0.0 ) {
        int days_until_safe = -1;
        for( int day = today + 1; day < today + 366; ++day ) {
            if( profile.frost_damage( std::get<2>( typical( day ) ), 0.0 ) <= 0.0 ) {
                days_until_safe = day - today;
                break;
            }
        }
        if( survival_skill >= 3 && days_until_safe > 0 ) {
            return ret_val<void>::make_failure(
                       n_gettext( "Frost could still kill young %1$s here for about %2$d more day.",
                                  "Frost could still kill young %1$s here for about %2$d more days.",
                                  days_until_safe ), pname, days_until_safe );
        }
        return ret_val<void>::make_failure( _( "It's frosty enough at night to kill young %s." ),
                                            pname );
    }

    // Will it ripen before the cold kills it?
    double harvest_at = -1.0;
    const std::vector<double> thresholds = detail::stage_thresholds( seed_type );
    const auto &stages = seed_type->seed->get_growth_stages();
    for( size_t i = 0; i < stages.size() && i < thresholds.size(); ++i ) {
        if( stages[i].first.str() == "GROWTH_HARVEST" ) {
            harvest_at = thresholds[i];
        }
    }
    if( harvest_at <= 0.0 ) {
        return ret_val<void>::make_success();
    }
    double gdd = 0.0;
    double hardening = 0.0;
    for( int day = today; day < today + 400; ++day ) {
        const auto [low, high, risky_low] = typical( day );
        const double mean = ( low + high ) / 2.0;
        if( mean < 5.0 ) {
            hardening = std::min( hardening_days, hardening + 1.0 );
        } else if( mean > 10.0 ) {
            hardening = std::max( 0.0, hardening - 2.0 );
        }
        if( profile.frost_damage( risky_low, hardening / hardening_days ) >= 1.0 ) {
            return ret_val<void>::make_failure(
                       _( "It's too late in the year: the cold will likely kill the %s before it ripens." ),
                       pname );
        }
        gdd += profile.degree_days( low, high ) * scale;
        if( gdd >= harvest_at ) {
            return ret_val<void>::make_success();
        }
    }
    return ret_val<void>::make_failure( _( "It's too cold here for the %s to ever ripen." ), pname );
}

itype_id cover_of( const item &seed )
{
    const std::string cover = seed.get_var( var_cover, std::string() );
    return cover.empty() ? itype_id::NULL_ID() : itype_id( cover );
}

void set_cover( item &seed, const itype_id &cover )
{
    if( cover.is_null() ) {
        seed.erase_var( var_cover );
    } else {
        seed.set_var( var_cover, cover.str() );
    }
}

void drop_cover( map &here, const tripoint_bub_ms &where, item &seed )
{
    const itype_id cover = cover_of( seed );
    if( cover.is_null() || !cover.is_valid() ) {
        return;
    }
    set_cover( seed, itype_id::NULL_ID() );
    here.add_item_or_charges( where, item( cover, calendar::turn ) );
}

} // namespace farming
