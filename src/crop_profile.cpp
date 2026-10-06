#include "crop_profile.h"

#include <algorithm>
#include <cmath>
#include <map>

#include "debug.h"
#include "flexbuffer_json.h"
#include "generic_factory.h"
#include "item_factory.h"
#include "itype.h"

namespace
{
generic_factory<crop_profile> crop_profile_factory( "crop_profile" );
generic_factory<soil_amendment> soil_amendment_factory( "soil_amendment" );
// Amendments by the item they describe, filled in when loading finishes.
std::map<itype_id, const soil_amendment *> amendment_by_item;
} // namespace

template<>
const crop_profile &crop_profile_id::obj() const
{
    return crop_profile_factory.obj( *this );
}

/** @relates string_id */
template<>
bool crop_profile_id::is_valid() const
{
    return crop_profile_factory.is_valid( *this );
}

void crop_profile::load_crop_profiles( const JsonObject &jo, const std::string &src )
{
    crop_profile_factory.load( jo, src );
}

void crop_profile::finalize_all()
{
    crop_profile_factory.finalize();
}

void crop_profile::reset()
{
    crop_profile_factory.reset();
}

const std::vector<crop_profile> &crop_profile::get_all()
{
    return crop_profile_factory.get_all();
}

void crop_profile::load( const JsonObject &jo, std::string_view )
{
    optional( jo, was_loaded, "base_temp", base_temp, units::from_celsius( 10.0 ) );
    optional( jo, was_loaded, "max_growth_temp", max_growth_temp, units::from_celsius( 30.0 ) );
    optional( jo, was_loaded, "reference_temp", reference_temp, units::from_celsius( 20.0 ) );
    optional( jo, was_loaded, "frost_damage_temp", frost_damage_temp, units::from_celsius( -1.0 ) );
    optional( jo, was_loaded, "frost_kill_temp", frost_kill_temp, units::from_celsius( -3.0 ) );
    // Not hardy unless told otherwise
    if( jo.has_member( "hardened_kill_temp" ) ) {
        optional( jo, was_loaded, "hardened_kill_temp", hardened_kill_temp );
    } else if( !was_loaded ) {
        hardened_kill_temp = frost_kill_temp;
    }
    optional( jo, was_loaded, "heat_stress_temp", heat_stress_temp, units::from_celsius( 35.0 ) );
    optional( jo, was_loaded, "family", family, "none" );
    optional( jo, was_loaded, "water_use", water_use, 1.0 );
    optional( jo, was_loaded, "drought_tolerance", drought_tolerance, 0.5 );
    optional( jo, was_loaded, "flood_tolerant", flood_tolerant, false );
    optional( jo, was_loaded, "nitrogen", nitrogen, 10.0 );
    optional( jo, was_loaded, "phosphorus", phosphorus, 2.0 );
    optional( jo, was_loaded, "potassium", potassium, 10.0 );
    optional( jo, was_loaded, "nitrogen_fixation", nitrogen_fixation, 0.0 );
}

void crop_profile::check() const
{
    if( max_growth_temp <= base_temp ) {
        debugmsg( "crop_profile %s: max_growth_temp must be above base_temp", id.str() );
    }
    if( reference_temp <= base_temp ) {
        debugmsg( "crop_profile %s: reference_temp must be above base_temp", id.str() );
    }
    if( frost_kill_temp > frost_damage_temp ) {
        debugmsg( "crop_profile %s: frost_kill_temp must not be above frost_damage_temp", id.str() );
    }
    if( nitrogen < 0.0 || phosphorus < 0.0 || potassium < 0.0 ) {
        debugmsg( "crop_profile %s: nutrient demands must not be negative", id.str() );
    }
    if( nitrogen_fixation < 0.0 || nitrogen_fixation > 1.0 ) {
        debugmsg( "crop_profile %s: nitrogen_fixation must be between 0 and 1", id.str() );
    }
    if( water_use <= 0.0 ) {
        debugmsg( "crop_profile %s: water_use must be positive", id.str() );
    }
    if( drought_tolerance <= 0.0 || drought_tolerance >= 1.0 ) {
        debugmsg( "crop_profile %s: drought_tolerance must be between 0 and 1", id.str() );
    }
    if( hardened_kill_temp > frost_kill_temp ) {
        debugmsg( "crop_profile %s: hardened_kill_temp must not be above frost_kill_temp", id.str() );
    }
}

void crop_profile::check_consistency()
{
    for( const crop_profile &profile : get_all() ) {
        profile.check();
    }
    for( const itype *type : item_controller->all() ) {
        if( type->seed && !type->seed->crop_profile.is_valid() ) {
            debugmsg( "seed %s has unknown crop_profile %s", type->get_id().str(),
                      type->seed->crop_profile.str() );
        }
    }
}

double crop_profile::degree_days( double low_c, double high_c ) const
{
    const double base = units::to_celsius( base_temp );
    const double cap = units::to_celsius( max_growth_temp );
    // Modified min/max method: clamp both ends of the day into the growing range.
    const double lo = std::clamp( std::min( low_c, high_c ), base, cap );
    const double hi = std::clamp( std::max( low_c, high_c ), base, cap );
    return std::max( 0.0, ( lo + hi ) / 2.0 - base );
}

double crop_profile::reference_degree_days() const
{
    return std::max( 0.5, units::to_celsius( reference_temp ) - units::to_celsius( base_temp ) );
}

bool crop_profile::overwinters() const
{
    return hardened_kill_temp < frost_kill_temp;
}

double crop_profile::frost_damage( double low_c, double hardening ) const
{
    hardening = std::clamp( hardening, 0.0, 1.0 );
    const double kill_shift = ( units::to_celsius( frost_kill_temp ) -
                                units::to_celsius( hardened_kill_temp ) ) * hardening;
    const double damage_at = units::to_celsius( frost_damage_temp ) - kill_shift;
    const double kill_at = units::to_celsius( frost_kill_temp ) - kill_shift;
    if( low_c > damage_at ) {
        return 0.0;
    }
    if( low_c <= kill_at ) {
        return 1.0;
    }
    // A light frost only burns some leaves; damage climbs steeply toward the kill point.
    const double depth = ( damage_at - low_c ) / std::max( 0.1, damage_at - kill_at );
    return std::clamp( 0.1 + 0.6 * depth * depth, 0.0, 0.95 );
}

double crop_profile::heat_loss( double high_c ) const
{
    const double over = high_c - units::to_celsius( heat_stress_temp );
    if( over <= 0.0 ) {
        return 0.0;
    }
    // Each degree over the threshold costs about 4% of the flowers that day.
    return std::clamp( 0.04 * over, 0.0, 0.5 );
}

template<>
const soil_amendment &soil_amendment_id::obj() const
{
    return soil_amendment_factory.obj( *this );
}

/** @relates string_id */
template<>
bool soil_amendment_id::is_valid() const
{
    return soil_amendment_factory.is_valid( *this );
}

void soil_amendment::load_soil_amendments( const JsonObject &jo, const std::string &src )
{
    soil_amendment_factory.load( jo, src );
}

void soil_amendment::finalize_all()
{
    soil_amendment_factory.finalize();
    amendment_by_item.clear();
    for( const soil_amendment &amendment : soil_amendment_factory.get_all() ) {
        amendment_by_item[amendment.item] = &amendment;
    }
}

void soil_amendment::reset()
{
    soil_amendment_factory.reset();
    amendment_by_item.clear();
}

const std::vector<soil_amendment> &soil_amendment::get_all()
{
    return soil_amendment_factory.get_all();
}

const soil_amendment &soil_amendment::for_item( const itype_id &item )
{
    const auto found = amendment_by_item.find( item );
    if( found != amendment_by_item.end() ) {
        return *found->second;
    }
    // A fertilizer nobody described: assume a small dose of a balanced garden feed.
    static soil_amendment generic = []() {
        soil_amendment result;
        result.nitrogen = 2.0;
        result.phosphorus = 0.5;
        result.potassium = 1.0;
        return result;
    }();
    return generic;
}

void soil_amendment::load( const JsonObject &jo, std::string_view )
{
    mandatory( jo, was_loaded, "item", item );
    optional( jo, was_loaded, "dose", dose, 1 );
    optional( jo, was_loaded, "nitrogen", nitrogen, 0.0 );
    optional( jo, was_loaded, "phosphorus", phosphorus, 0.0 );
    optional( jo, was_loaded, "potassium", potassium, 0.0 );
    optional( jo, was_loaded, "organic_nitrogen", organic_nitrogen, 0.0 );
    optional( jo, was_loaded, "ph_change", ph_change, 0.0 );
    optional( jo, was_loaded, "burns", burns, false );
}

void soil_amendment::check_consistency()
{
    for( const soil_amendment &amendment : get_all() ) {
        if( !amendment.item.is_valid() ) {
            debugmsg( "soil_amendment %s refers to unknown item %s", amendment.id.str(),
                      amendment.item.str() );
        }
        if( amendment.dose < 1 ) {
            debugmsg( "soil_amendment %s: dose must be at least 1", amendment.id.str() );
        }
    }
}
