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
#include "rng.h"
#include "soil.h"
#include "string_formatter.h"
#include "translations.h"
#include "vehicle.h"
#include "vpart_position.h"
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
constexpr const char *var_water_factor = "farm_water_factor";
constexpr const char *var_limit_sum = "farm_limit_sum";
constexpr const char *var_limit_weight = "farm_limit_weight";
constexpr const char *var_drought_day = "farm_drought_day";
constexpr const char *var_rot_day = "farm_rot_day";
constexpr const char *var_n_factor = "farm_n_factor";
constexpr const char *var_p_factor = "farm_p_factor";
constexpr const char *var_k_factor = "farm_k_factor";
constexpr const char *var_infected_day = "farm_infected_day";
constexpr const char *var_burn_day = "farm_burn_day";
constexpr const char *var_matured = "farm_matured";
constexpr const char *var_light_factor = "farm_light_factor";
constexpr const char *var_dark_day = "farm_dark_day";
constexpr const char *var_light_week = "farm_light_week";

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

// Soil water.  A tile is about one square meter, so 1 mm of water is 1 liter.
// Plant-available water held by the root zone of a loam bed (about 120 mm per meter of soil,
// roots in the top half meter), and by a smaller, shallower planter box.
constexpr double ground_capacity_mm = 60.0;
constexpr double planter_capacity_mm = 35.0;
// How fast water above field capacity drains away each day.
constexpr double ground_drain_mm_per_day = 25.0;
constexpr double planter_drain_mm_per_day = 60.0;
// Beyond this multiple of field capacity the bed is flooded and the rest runs off.
constexpr double saturation_factor = 1.6;
// Above this multiple of field capacity after drainage, the roots are short of air.
constexpr double waterlogged_factor = 1.1;
// Days of waterlogging before the roots start to rot.
constexpr int root_rot_days = 3;
// Evaporation from bare, moist soil relative to the reference crop.
constexpr double bare_soil_kc = 0.3;
// Mulch shades the soil surface and cuts evaporation.
constexpr double mulch_et_factor = 0.8;
constexpr double mulch_bare_soil_factor = 0.5;
// Glass passes most but not all sunlight.
constexpr double greenhouse_light_transmission = 0.8;
// Evaporative demand indoors without sun, as mm/day of radiation equivalent.
constexpr double indoor_radiation_mm = 1.0;
// Converts MJ/m2 of solar energy into the mm of water it could evaporate (1 / 2.45 MJ/kg).
constexpr double mj_to_evaporation_mm = 0.408;

// Soil fertility of an ordinary, decent garden bed: plant-available nutrients in g/m2 and pH.
constexpr double initial_nitrogen = 10.0;
constexpr double initial_phosphorus = 4.0;
constexpr double initial_potassium = 15.0;
// Nitrogen held in soil organic matter (about 0.15% of the topsoil's weight).
constexpr double initial_organic_nitrogen = 500.0;
constexpr double initial_ph = 6.5;
// Share of the organic nitrogen soil life releases per year at 20 C; doubles every 10 C warmer.
constexpr double mineralization_per_year = 0.025;
// Phosphorus and potassium weathering out of soil minerals, g/m2 per year at 20 C, which
// slowly refills a depleted bed back toward its background level.
constexpr double phosphorus_weathering_per_year = 0.5;
constexpr double potassium_weathering_per_year = 3.0;
// Nitrogen a legume crop leaves behind in its roots and nodules.
constexpr double legume_nitrogen_credit = 3.0;
// Above this much available nitrogen, soluble fertilizer salts scorch the roots.
constexpr double burn_nitrogen = 25.0;
// Soil-borne disease halves each year its host plants are kept out of the bed.
constexpr double disease_half_life_days = 365.0;
// Daily chance a crop catches a disease: spores drift in anywhere, and build up in beds
// where the same family keeps growing.  Wet weather doubles it.
constexpr double base_infection_per_day = 0.0005;
constexpr double pressure_infection_per_day = 0.003;
// Daily health a diseased plant loses, and the share of growth it can still turn into crop.
constexpr double disease_damage_per_day = 1.5;
constexpr double disease_growth_factor = 0.8;
// How many past crops a bed remembers.
constexpr size_t history_length = 4;

// Light.  About 45% of sunlight's energy is photosynthetically active, at 4.57 umol per joule.
constexpr double sun_photons_mol_per_joule = 0.45 * 4.57e-6;
// A window lets a little daylight onto a bed next to it.
constexpr double window_light_share = 0.08;
constexpr double max_window_light_share = 0.25;
// A 900 W LED grow light running 16 hours a day lights its own tile and the eight around it
// to this daily light integral (at 2.7 umol/J).
constexpr double grow_light_dli = 15.0;
// Below this share of its light need a plant slowly starves.
constexpr double starving_light_factor = 0.15;
constexpr double darkness_damage_per_day = 2.0;
// An unheated building evens out the day: nights stay warmer, afternoons cooler.
constexpr double indoor_low_bonus_c = 4.0;
constexpr double indoor_high_cut_c = 2.0;

/** How much of the day's sun reaches a bed: glass, shade from walls and trees, windows indoors. */
double sun_share( const map &here, const tripoint_bub_ms &p, const plot_info &plot )
{
    if( !plot.exposed && !plot.greenhouse && !has_sunlight_access( p ) ) {
        // Indoors: only light through nearby windows.
        int windows = 0;
        for( const tripoint_bub_ms &q : here.points_in_radius( p, 1 ) ) {
            if( q != p && here.has_flag( ter_furn_flag::TFLAG_WINDOW, q ) ) {
                windows++;
            }
        }
        return std::min( max_window_light_share, windows * window_light_share );
    }
    // Neighbors that block the sky.  The sun stands in the south, so obstacles there matter most.
    double shade = 0.0;
    for( const tripoint_bub_ms &q : here.points_in_radius( p, 1 ) ) {
        if( q == p ) {
            continue;
        }
        const bool blocks = here.has_flag( ter_furn_flag::TFLAG_TREE, q ) ||
                            !here.has_flag_ter( ter_furn_flag::TFLAG_TRANSPARENT, q );
        if( !blocks ) {
            continue;
        }
        const int dy = q.y() - p.y();
        shade += dy > 0 ? 0.15 : dy == 0 ? 0.08 : 0.03;
    }
    double share = std::max( 0.3, 1.0 - shade );
    if( !plot.exposed ) {
        // Under glass: a greenhouse, or a glass roof.
        share *= greenhouse_light_transmission;
    }
    return share;
}

/** Daily light integral from switched-on, powered grow lights over the bed. */
double grow_light_at( const map &here, const tripoint_bub_ms &p )
{
    double dli = 0.0;
    for( const tripoint_bub_ms &q : here.points_in_radius( p, 1 ) ) {
        const optional_vpart_position vp = here.veh_at( q );
        if( !vp ) {
            continue;
        }
        // A light whose grid runs dry switches itself off.
        const std::optional<vpart_reference> light = vp->part_with_feature( "GROW_LIGHT", true );
        if( light && light->part().enabled ) {
            dli += grow_light_dli;
        }
    }
    return dli;
}

double light_factor( double dli, const crop_profile &profile )
{
    if( profile.light_need <= 0.0 ) {
        return 1.0;
    }
    return std::clamp( dli / profile.light_need, 0.0, 1.0 );
}

struct plot_info {
    double capacity = ground_capacity_mm;
    double drain = ground_drain_mm_per_day;
    // Open to the sky: rain and snow fall on it.
    bool exposed = true;
    bool greenhouse = false;
};

bool is_planter( const map &here, const tripoint_bub_ms &p )
{
    static const furn_str_id furn_f_planter( "f_planter" );
    const furn_t &furn = here.furn( p ).obj();
    if( furn.id == furn_f_planter ) {
        return true;
    }
    return furn.plant && furn.plant->base == furn_f_planter;
}

plot_info plot_at( const map &here, const tripoint_bub_ms &p )
{
    plot_info plot;
    if( is_planter( here, p ) ) {
        plot.capacity = planter_capacity_mm;
        plot.drain = planter_drain_mm_per_day;
    }
    plot.greenhouse = here.has_flag_ter( "GREENHOUSE", p );
    plot.exposed = here.is_outside( p );
    return plot;
}

/** Reference evapotranspiration (mm/day): Hargreaves' radiation method, ET0 = 0.0135 (T + 17.8) Rs. */
double reference_et_mm( const farming::daily_weather &w, double mean_c, const plot_info &plot )
{
    double radiation_mm = w.radiant_exposure / 1.0e6 * mj_to_evaporation_mm;
    if( plot.greenhouse ) {
        radiation_mm *= greenhouse_light_transmission;
    } else if( !plot.exposed ) {
        radiation_mm = indoor_radiation_mm;
    }
    return std::max( 0.0, 0.0135 * ( mean_c + 17.8 ) * radiation_mm );
}

/** Crop coefficient by growth stage (FAO-56): small seedlings use little, a full canopy the most. */
double stage_kc( const std::string &stage )
{
    if( stage == "GROWTH_SEED" ) {
        return 0.4;
    }
    if( stage == "GROWTH_SEEDLING" ) {
        return 0.75;
    }
    if( stage == "GROWTH_MATURE" ) {
        return 1.05;
    }
    if( stage == "GROWTH_HARVEST" ) {
        return 0.85;
    }
    return 0.75;
}

/** Water stress factor (FAO-56 Ks): 1 while the plant can easily draw water, falling to 0 when dry. */
double water_stress( double water_mm, double capacity, double tolerance )
{
    const double threshold = ( 1.0 - tolerance ) * capacity;
    if( water_mm >= threshold ) {
        return 1.0;
    }
    return std::clamp( water_mm / std::max( 0.01, threshold ), 0.0, 1.0 );
}

/** Below this much water the plant wilts and starts to die. */
double wilting_point( double capacity, double tolerance )
{
    return 0.3 * ( 1.0 - tolerance ) * capacity;
}

bool mulched( const soil_state &soil, int day )
{
    return soil.mulch_until >= day;
}

void init_soil( soil_state &soil, const plot_info &plot )
{
    if( soil.water_mm < 0.0 ) {
        soil.water_mm = 0.7 * plot.capacity;
    }
    if( soil.nitrogen < 0.0 ) {
        soil.nitrogen = initial_nitrogen;
    }
    if( soil.phosphorus < 0.0 ) {
        soil.phosphorus = initial_phosphorus;
    }
    if( soil.potassium < 0.0 ) {
        soil.potassium = initial_potassium;
    }
    if( soil.organic_nitrogen < 0.0 ) {
        soil.organic_nitrogen = initial_organic_nitrogen;
    }
    if( soil.ph < 0.0 ) {
        soil.ph = initial_ph;
    }
}

/** How active soil life is at this daily mean temperature (1 at 20 C, half as active per 10 C cooler). */
double soil_activity( double mean_c )
{
    if( mean_c <= 0.0 ) {
        return 0.0;
    }
    return std::min( 2.0, std::pow( 2.0, ( mean_c - 20.0 ) / 10.0 ) );
}

/** Share of the soil's nutrients plants can actually take up at this pH; best between 6 and 7. */
double ph_availability( double ph )
{
    const double off = ph < 6.0 ? 6.0 - ph : ph > 7.0 ? ph - 7.0 : 0.0;
    return std::clamp( 1.0 - 0.3 * off, 0.3, 1.0 );
}

/** One day of soil life: organic matter releases nutrients, rain washes nitrate out, disease fades. */
void soil_biology_day( soil_state &soil, const plot_info &plot, double mean_c, double drained_mm )
{
    const double activity = soil_activity( mean_c );
    const double released = soil.organic_nitrogen * mineralization_per_year / 365.0 * activity;
    soil.organic_nitrogen -= released;
    soil.nitrogen += released;
    if( soil.phosphorus < initial_phosphorus ) {
        soil.phosphorus += phosphorus_weathering_per_year / 365.0 * activity;
    }
    if( soil.potassium < initial_potassium ) {
        soil.potassium += potassium_weathering_per_year / 365.0 * activity;
    }
    if( drained_mm > 0.0 ) {
        // Nitrate dissolves in the soil water and leaves with whatever drains out.
        soil.nitrogen -= soil.nitrogen * std::min( 0.5, drained_mm / ( plot.capacity + drained_mm ) );
    }
    static const double daily_fade = std::pow( 0.5, 1.0 / disease_half_life_days );
    for( auto &pressure : soil.disease ) {
        pressure.second *= daily_fade;
    }
}

/**
 * Take up the nutrients for a day's share of the crop's growth.
 * @return Each nutrient's supply as a share of what the plant wanted (0 to 1).
 */
std::tuple<double, double, double> take_nutrients( soil_state &soil, const crop_profile &profile,
        double share_of_crop )
{
    const double availability = ph_availability( soil.ph );
    const auto take = [&]( double & pool, double need ) {
        if( need <= 0.0 ) {
            return 1.0;
        }
        const double got = std::min( need, std::max( 0.0, pool ) * availability );
        pool -= got;
        return got / need;
    };
    const double n = take( soil.nitrogen,
                           profile.nitrogen * ( 1.0 - profile.nitrogen_fixation ) * share_of_crop );
    const double p = take( soil.phosphorus, profile.phosphorus * share_of_crop );
    const double k = take( soil.potassium, profile.potassium * share_of_crop );
    return { n, p, k };
}

/** The bed remembers the crop that just left it: history, disease left in the soil, legume nitrogen. */
void record_crop_end( soil_state &soil, const crop_profile &profile, bool matured )
{
    soil.history.push_back( profile.family );
    while( soil.history.size() > history_length ) {
        soil.history.erase( soil.history.begin() );
    }
    if( profile.family != "none" ) {
        soil.disease[profile.family] += 1.0;
    }
    if( matured && profile.nitrogen_fixation > 0.0 ) {
        soil.nitrogen += legume_nitrogen_credit;
    }
}

double disease_pressure( const soil_state &soil, const std::string &family )
{
    const auto found = soil.disease.find( family );
    return found == soil.disease.end() ? 0.0 : found->second;
}

/** What a diseased plant of this family looks like, and what an expert would call it. */
std::pair<std::string, std::string> disease_symptoms( const std::string &family )
{
    if( family == "nightshade" ) {
        return { _( "Dark, spreading lesions with pale edges cover the leaves and stems." ), _( "late blight" ) };
    } else if( family == "brassica" ) {
        return { _( "It wilts on sunny days, and the base of the roots is swollen and knobby." ), _( "clubroot" ) };
    } else if( family == "cucurbit" ) {
        return { _( "White, powdery patches are spreading over the leaves." ), _( "powdery mildew" ) };
    } else if( family == "allium" ) {
        return { _( "The leaves are yellowing and there's a fluffy white rot at the base." ), _( "white rot" ) };
    } else if( family == "grass" ) {
        return { _( "Orange-brown pustules have broken out along the leaves." ), _( "rust" ) };
    } else if( family == "legume" ) {
        return { _( "The lower stem and roots are rotting reddish-brown." ), _( "root rot" ) };
    } else if( family == "carrot" ) {
        return { _( "Dark spots with yellow halos are spreading across the leaves." ), _( "leaf blight" ) };
    } else if( family == "aster" || family == "amaranth" ) {
        return { _( "Pale patches on top of the leaves have gray-purple fuzz underneath." ), _( "downy mildew" ) };
    }
    return { _( "Brown spots are spreading across the leaves." ), _( "a fungal leaf spot" ) };
}


/**
 * Finish a day of soil water once rain has come in: take out what the plants and soil lose,
 * drain the excess and track waterlogging.
 * @return Millimeters of water that drained through the root zone.
 */
double end_water_day( soil_state &soil, const plot_info &plot, double loss_mm )
{
    soil.water_mm = std::max( 0.0, soil.water_mm - loss_mm );
    const double excess = soil.water_mm - plot.capacity;
    double drained = 0.0;
    if( excess > 0.0 ) {
        drained = std::min( excess, plot.drain );
        soil.water_mm -= drained;
    }
    soil.water_mm = std::min( soil.water_mm, plot.capacity * saturation_factor );
    if( soil.water_mm > plot.capacity * waterlogged_factor ) {
        soil.wet_days++;
    } else {
        soil.wet_days = 0;
    }
    return drained;
}

/** Bring a plot's soil up to date for days when nothing grew in it. */
void soil_catch_up( soil_state &soil, const plot_info &plot, const tripoint_abs_omt &omt,
                    int until_day )
{
    init_soil( soil, plot );
    if( soil.last_day < 0 ) {
        soil.last_day = until_day;
        return;
    }
    for( int day = std::max( soil.last_day, until_day - max_catch_up_days ); day < until_day; ++day ) {
        const farming::daily_weather &w = farming::weather_on_day( omt, day );
        if( plot.exposed ) {
            // Snow on a bare plot melts into it sooner or later.
            soil.water_mm += w.rain_mm + w.snow_water_mm;
        }
        // Bare soil only loses much water while its surface is moist.
        const double surface_wetness = std::min( 1.0, soil.water_mm / ( 0.5 * plot.capacity ) );
        double evaporation = reference_et_mm( w, w.mean_c, plot ) * bare_soil_kc * surface_wetness;
        if( mulched( soil, day ) ) {
            evaporation *= mulch_bare_soil_factor;
        }
        const double drained = end_water_day( soil, plot, evaporation );
        soil_biology_day( soil, plot, w.mean_c, drained );
    }
    soil.last_day = std::max( soil.last_day, until_day );
}

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
    record_crop_end( here.get_soil( p ), profile_of( seed.typeId() ), false );
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
    // Nutrient uptake is spread over the growth up to harvest.
    double harvest_gdd = thresholds.empty() ? 0.0 : thresholds.back();
    const auto &stages = seed_type->seed->get_growth_stages();
    for( size_t i = 0; i < stages.size() && i < thresholds.size(); ++i ) {
        if( stages[i].first.str() == "GROWTH_HARVEST" ) {
            harvest_gdd = thresholds[i];
        }
    }
    harvest_gdd = std::max( harvest_gdd, 1.0 );
    const tripoint_abs_omt omt = ground_omt( here.get_abs( p ) );
    const plot_info plot = plot_at( here, p );
    const bool greenhouse = plot.greenhouse;
    const bool covered = !cover_of( seed ).is_null();
    const double scale = season_scale();
    const bool underground = p.z() < 0;
    // Light reaching the bed: a share of the day's sun, plus any grow lights switched on now.
    // A frost cover lets most light through.
    const double sun = sun_share( here, p, plot ) * ( covered ? 0.85 : 1.0 );
    const double lamp_dli = grow_light_at( here, p );
    // Underground it's the same all year; a heated room is as warm as it is now.
    const double cave_c = get_weather().get_cur_weather_gen().base_temperature;
    const double room_c = units::to_celsius( get_weather().get_temperature( p ) );

    // The bed itself: bring it up to the day this plant's record starts.
    soil_state &soil = here.get_soil( p );
    soil_catch_up( soil, plot, omt, day );

    double gdd = seed.get_var( var_gdd, 0.0 );
    double last_gdd = seed.get_var( var_last_gdd, 0.0 );
    double health = seed.get_var( var_health, 100.0 );
    double yield = seed.get_var( var_yield, 1.0 );
    double hardening = seed.get_var( var_hardening, 0.0 );
    double snow = seed.get_var( var_snow, 0.0 );
    double water_factor = seed.get_var( var_water_factor, 1.0 );
    double n_factor = seed.get_var( var_n_factor, 1.0 );
    double p_factor = seed.get_var( var_p_factor, 1.0 );
    double k_factor = seed.get_var( var_k_factor, 1.0 );
    bool infected = seed.has_var( var_infected_day );
    bool matured = seed.get_var( var_matured, 0.0 ) > 0.0;
    double limit_sum = seed.get_var( var_limit_sum, 0.0 );
    double limit_weight = seed.get_var( var_limit_weight, 0.0 );
    double light_seen = seed.get_var( var_light_factor, 1.0 );
    double light_week = seed.get_var( var_light_week, 1.0 );
    bool died = false;
    std::string cause;

    for( ; day < today; ++day ) {
        const daily_weather &w = weather_on_day( omt, day );
        double low = w.low_c;
        double high = w.high_c;
        double mean = w.mean_c;
        double melt_water = 0.0;

        if( underground ) {
            low = cave_c;
            high = cave_c;
            mean = cave_c;
        } else if( greenhouse ) {
            low += greenhouse_low_bonus_c;
            high += high < 20.0 ? greenhouse_cool_day_high_bonus_c : greenhouse_warm_day_high_bonus_c;
            mean = ( low + high ) / 2.0;
        } else if( !plot.exposed ) {
            // Inside a building.
            low += indoor_low_bonus_c;
            high = std::max( low, high - indoor_high_cut_c );
            mean = ( low + high ) / 2.0;
        }
        if( !plot.exposed && day == today - 1 && room_c > low ) {
            // Whatever is heating the room now (a stove, a space heater) kept it warm.
            low = std::max( low, room_c - 3.0 );
            high = std::max( high, room_c );
            mean = ( low + high ) / 2.0;
        }
        if( plot.exposed ) {
            // Snowpack on the bed: builds from snowfall, melts on warm days.
            snow += w.snow_water_mm * snow_per_water_mm;
            if( mean > 0.0 ) {
                const double melted = std::min( snow, snow_melt_mm_per_degree_hour * 24.0 * mean );
                snow -= melted;
                melt_water = melted / snow_per_water_mm;
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

        // Soil water, unless the bed was already brought past this day.
        if( day >= soil.last_day ) {
            if( plot.exposed ) {
                soil.water_mm += w.rain_mm + melt_water;
            }
            water_factor = water_stress( soil.water_mm, plot.capacity, profile.drought_tolerance );
            double kc = stage_kc( stage ) * profile.water_use;
            if( mulched( soil, day ) ) {
                kc *= mulch_et_factor;
            }
            // A thirsty plant closes its pores and transpires less (FAO-56 Ks).
            const double drained = end_water_day( soil, plot,
                                                  reference_et_mm( w, mean, plot ) * kc * water_factor );
            soil_biology_day( soil, plot, mean, drained );
            soil.last_day = day + 1;
        }

        // Soil-borne disease, worse where the family grew recently and in wet weather.
        if( !infected && profile.family != "none" && stage != "GROWTH_HARVEST" ) {
            double chance = base_infection_per_day + pressure_infection_per_day *
                            disease_pressure( soil, profile.family );
            if( w.rain_mm > 1.0 || soil.wet_days > 0 ) {
                chance *= 2.0;
            }
            if( rng_float( 0.0, 1.0 ) < chance ) {
                infected = true;
                seed.set_var( var_infected_day, static_cast<double>( day ) );
            }
        }

        const double dli = w.radiant_exposure * sun_photons_mol_per_joule * sun + lamp_dli;
        const double light = light_factor( dli, profile );

        // A few dark, stormy days don't hurt; weeks without enough light do.
        light_week = light_week * 0.85 + light * 0.15;

        double damage = 0.0;
        if( light_week < starving_light_factor && stage != "GROWTH_SEED" ) {
            damage += darkness_damage_per_day;
            seed.set_var( var_dark_day, static_cast<double>( day ) );
            cause = _( "lack of light" );
        }
        if( infected ) {
            damage += disease_damage_per_day;
            cause = _( "disease" );
        }
        if( soil.water_mm < wilting_point( plot.capacity, profile.drought_tolerance ) ) {
            damage += 5.0;
            seed.set_var( var_drought_day, static_cast<double>( day ) );
            cause = _( "drought" );
        }
        if( soil.wet_days >= root_rot_days && !profile.flood_tolerant ) {
            damage += 4.0;
            seed.set_var( var_rot_day, static_cast<double>( day ) );
            cause = _( "root rot" );
        }
        if( damage > 0.0 ) {
            health -= damage;
            if( health <= 0.0 ) {
                died = true;
                break;
            }
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

        const double potential_gdd = profile.degree_days( low, high ) * scale;
        // Feed the day's growth from the soil, unless the crop is already ripe.
        if( stage != "GROWTH_HARVEST" && potential_gdd > 0.0 ) {
            std::tie( n_factor, p_factor, k_factor ) =
                take_nutrients( soil, profile, potential_gdd / harvest_gdd );
        }
        const double nutrient_factor = std::min( { n_factor, p_factor, k_factor } );
        // Liebig's law of the minimum: the scarcest of water and nutrients sets the day's growth.
        double limit = std::min( { water_factor, nutrient_factor, light } );
        light_seen = light;
        if( infected ) {
            limit *= disease_growth_factor;
        }
        // Shortages slow development somewhat, and growth (the harvest) a lot.
        last_gdd = potential_gdd * ( 0.4 + 0.6 * limit );
        gdd += last_gdd;
        if( stage != "GROWTH_SEED" && stage != "GROWTH_HARVEST" ) {
            // The harvest follows the worst shortage on each growing day, weighted by growth.
            limit_sum += potential_gdd * limit;
            limit_weight += potential_gdd;
        }
        if( stage == "GROWTH_MATURE" || stage == "GROWTH_HARVEST" ) {
            matured = true;
        }
        // Growing, unstressed plants slowly replace damaged leaves.
        if( frost <= 0.0 && damage <= 0.0 && last_gdd > 0.0 && limit > 0.5 ) {
            health = std::min( 100.0, health + 2.0 );
        }
    }

    if( died ) {
        soil.last_day = std::max( soil.last_day, day + 1 );
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
    seed.set_var( var_water_factor, water_factor );
    seed.set_var( var_light_factor, light_seen );
    seed.set_var( var_light_week, light_week );
    seed.set_var( var_n_factor, n_factor );
    seed.set_var( var_p_factor, p_factor );
    seed.set_var( var_k_factor, k_factor );
    seed.set_var( var_matured, matured ? 1.0 : 0.0 );
    seed.set_var( var_limit_sum, limit_sum );
    seed.set_var( var_limit_weight, limit_weight );
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
    const double weight = seed.get_var( var_limit_weight, 0.0 );
    const double shortages = weight > 0.0 ?
                             std::clamp( seed.get_var( var_limit_sum, 0.0 ) / weight, 0.0, 1.0 ) : 1.0;
    // A damaged plant still sets some crop; health matters less than lost flowers.
    return yield * shortages * ( 0.25 + 0.75 * health / 100.0 );
}

bool is_greenhouse( const map &here, const tripoint_bub_ms &p )
{
    return here.has_flag_ter( "GREENHOUSE", p );
}

std::vector<std::string> describe_plant( map &here, const tripoint_bub_ms &p,
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

    // Light.  Seedlings stretching for light are easy to spot; knowing why takes experience.
    const double light = seed.get_var( var_light_week, 1.0 );
    if( light < 0.5 ) {
        lines.emplace_back( skill >= 2 ?
                            _( "It's pale and spindly, stretching toward the light: it needs more light than it gets here." ) :
                            _( "It's pale and spindly." ) );
    } else if( light < 0.85 && skill >= 3 ) {
        lines.emplace_back( _( "It's a little leggy; more light would help it." ) );
    }

    // Nutrient shortages.  Pale, yellowing leaves could be many things to a beginner.
    const double n_factor = seed.get_var( var_n_factor, 1.0 );
    const double p_factor = seed.get_var( var_p_factor, 1.0 );
    const double k_factor = seed.get_var( var_k_factor, 1.0 );
    constexpr double shortage = 0.85;
    if( std::min( { n_factor, p_factor, k_factor } ) < shortage ) {
        if( skill < 2 ) {
            lines.emplace_back( _( "The leaves are pale and it isn't growing well." ) );
        } else {
            if( n_factor < shortage ) {
                lines.emplace_back( skill >= 4 ?
                                    _( "Older, lower leaves are turning yellow: it's short of nitrogen." ) :
                                    _( "Older, lower leaves are turning yellow." ) );
            }
            if( p_factor < shortage ) {
                lines.emplace_back( skill >= 4 ?
                                    _( "The leaves have a dull purple tinge and it's stunted: it's short of phosphorus." ) :
                                    _( "The leaves have a dull purple tinge and it's stunted." ) );
            }
            if( k_factor < shortage ) {
                lines.emplace_back( skill >= 4 ?
                                    _( "The leaf edges are brown and scorched: it's short of potassium." ) :
                                    _( "The leaf edges are brown and scorched." ) );
            }
        }
    }
    if( today - seed.get_var( var_burn_day, -1000.0 ) <= 5.0 ) {
        lines.emplace_back( skill >= 2 ?
                            _( "The leaf tips are burned brown: too much fertilizer." ) :
                            _( "The leaf tips have gone brown." ) );
    }
    if( seed.has_var( var_infected_day ) ) {
        const std::pair<std::string, std::string> symptoms = disease_symptoms(
                    profile_of( seed.typeId() ).family );
        if( skill >= 3 ) {
            lines.push_back( symptoms.first + "  " + string_format(
                                 _( "It's %s.  Pulling it up would keep the rest of the bed from catching it." ),
                                 symptoms.second ) );
        } else {
            lines.emplace_back( _( "Spots and rotting patches are spreading over the leaves." ) );
        }
    }

    // Soil water.  Wilting looks the same whether the roots are dry or drowning;
    // only an experienced grower reads the soil to tell which.
    const plot_info plot = plot_at( here, p );
    const soil_state &soil = here.get_soil( p );
    const crop_profile &profile = profile_of( seed.typeId() );
    if( soil.water_mm >= 0.0 ) {
        const bool rotting = soil.wet_days >= root_rot_days && !profile.flood_tolerant;
        const bool parched = soil.water_mm < wilting_point( plot.capacity, profile.drought_tolerance );
        if( parched || rotting ) {
            if( skill < 3 ) {
                lines.emplace_back( _( "The leaves are drooping and wilted." ) );
            } else if( parched ) {
                lines.emplace_back( _( "The leaves are wilting and the soil is bone dry: it badly needs water." ) );
            } else {
                lines.emplace_back(
                    _( "The leaves are wilting although the soil is soaked: the roots are rotting in the wet." ) );
            }
        }
        if( parched ) {
            lines.emplace_back( _( "The soil is dry and crumbly." ) );
        } else if( water_stress( soil.water_mm, plot.capacity, profile.drought_tolerance ) < 1.0 ) {
            if( skill >= 1 ) {
                lines.emplace_back( _( "The soil is dry a finger's depth down; it could use watering." ) );
            } else {
                lines.emplace_back( _( "The soil looks dry." ) );
            }
        } else if( soil.wet_days > 0 ) {
            lines.emplace_back( _( "The soil is soggy and puddled." ) );
        } else {
            lines.emplace_back( _( "The soil is moist." ) );
        }
        if( mulched( soil, today ) ) {
            lines.emplace_back( _( "A layer of mulch covers the soil." ) );
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

ret_val<void> light_outlook( map &here, const tripoint_bub_ms &p, const itype_id &seed_type )
{
    if( !seed_type->seed ) {
        return ret_val<void>::make_success();
    }
    const crop_profile &profile = profile_of( seed_type );
    const plot_info plot = plot_at( here, p );
    const double sun = sun_share( here, p, plot );
    if( profile.light_need <= 0.0 || sun >= 0.5 ) {
        return ret_val<void>::make_success();
    }
    // A clear summer day gives about 50 mol/m2 of sunlight outdoors.
    const double dli = 50.0 * sun + grow_light_at( here, p );
    if( light_factor( dli, profile ) < 0.5 ) {
        return ret_val<void>::make_failure(
                   _( "It's too dark here for %s to grow well: it needs sunlight or a grow light." ),
                   seed_type->seed->plant_name.translated() );
    }
    return ret_val<void>::make_success();
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

double field_capacity( const map &here, const tripoint_bub_ms &p )
{
    return plot_at( here, p ).capacity;
}

void update_soil( map &here, const tripoint_bub_ms &p )
{
    const plot_info plot = plot_at( here, p );
    soil_catch_up( here.get_soil( p ), plot, ground_omt( here.get_abs( p ) ),
                   day_index( calendar::turn ) );
}

int water_needed_liters( map &here, const tripoint_bub_ms &p )
{
    const plot_info plot = plot_at( here, p );
    const soil_state &soil = here.get_soil( p );
    if( soil.water_mm < 0.0 ) {
        return 0;
    }
    return std::max( 0, static_cast<int>( std::ceil( plot.capacity - soil.water_mm ) ) );
}

void add_water( map &here, const tripoint_bub_ms &p, double liters )
{
    const plot_info plot = plot_at( here, p );
    soil_state &soil = here.get_soil( p );
    init_soil( soil, plot );
    soil.water_mm = std::min( soil.water_mm + liters, plot.capacity * saturation_factor );
}

bool has_mulch( map &here, const tripoint_bub_ms &p )
{
    return mulched( here.get_soil( p ), day_index( calendar::turn ) );
}

void add_mulch( map &here, const tripoint_bub_ms &p )
{
    soil_state &soil = here.get_soil( p );
    init_soil( soil, plot_at( here, p ) );
    soil.mulch_until = day_index( calendar::turn ) + to_days<int>( calendar::season_length() );
}

int amendment_dose( const itype_id &amendment )
{
    return std::max( 1, soil_amendment::for_item( amendment ).dose );
}

bool apply_amendment( map &here, const tripoint_bub_ms &p, const itype_id &amendment, int units,
                      item *seed )
{
    const soil_amendment &what = soil_amendment::for_item( amendment );
    const plot_info plot = plot_at( here, p );
    soil_state &soil = here.get_soil( p );
    init_soil( soil, plot );
    soil.nitrogen += what.nitrogen * units;
    soil.phosphorus += what.phosphorus * units;
    soil.potassium += what.potassium * units;
    soil.organic_nitrogen += what.organic_nitrogen * units;
    soil.ph = std::clamp( soil.ph + what.ph_change * units, 4.0, 9.0 );

    if( seed == nullptr || !what.burns || soil.nitrogen <= burn_nitrogen ) {
        return true;
    }
    // Too much soluble fertilizer draws water out of the roots and scorches them.
    init_state( *seed );
    const double health = seed->get_var( var_health, 100.0 ) -
                          std::min( 60.0, ( soil.nitrogen - burn_nitrogen ) * 4.0 );
    seed->set_var( var_health, health );
    seed->set_var( var_burn_day, static_cast<double>( day_index( calendar::turn ) ) );
    if( health <= 0.0 ) {
        kill_plant( here, p, *seed, _( "fertilizer burn" ) );
        return false;
    }
    return true;
}

void on_crop_removed( map &here, const tripoint_bub_ms &p, const item &seed )
{
    if( !seed.type->seed ) {
        return;
    }
    record_crop_end( here.get_soil( p ), profile_of( seed.typeId() ),
                     seed.get_var( var_matured, 1.0 ) > 0.0 );
}

void pull_up_plant( map &here, const tripoint_bub_ms &p, item &seed )
{
    record_crop_end( here.get_soil( p ), profile_of( seed.typeId() ), false );
    drop_cover( here, p, seed );
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

std::string rotation_warning( map &here, const tripoint_bub_ms &p, const itype_id &seed_type,
                              int survival_skill )
{
    if( survival_skill < 2 || !seed_type->seed ) {
        return std::string();
    }
    const crop_profile &profile = profile_of( seed_type );
    if( profile.family == "none" ) {
        return std::string();
    }
    const soil_state &soil = here.get_soil( p );
    const bool recent = !soil.history.empty() && soil.history.back() == profile.family;
    if( recent || disease_pressure( soil, profile.family ) >= 1.0 ) {
        return string_format(
                   _( "Plants of the same family as %s grew here recently.  Diseases they left in the soil "
                      "could attack this crop; it would be safer to plant something else here this year." ),
                   seed_type->seed->plant_name.translated() );
    }
    return std::string();
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
