#pragma once
#ifndef CATA_SRC_CROP_PROFILE_H
#define CATA_SRC_CROP_PROFILE_H

#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "type_id.h"
#include "units.h"

class JsonObject;
template<typename T>
class generic_factory;

/**
 * How a crop responds to its environment.  Seeds point at one of these through
 * "crop_profile" in their seed data; seeds without one use "crop_generic".
 *
 * Temperatures follow standard agronomy:
 *  - growth is measured in growing degree days above base_temp, capped at max_growth_temp;
 *  - the seed's listed growth stage durations are what it takes at reference_temp;
 *  - frost below frost_damage_temp hurts the plant, at frost_kill_temp it dies;
 *  - hardy crops acclimate in cold weather and can survive down to hardened_kill_temp.
 */
class crop_profile
{
    public:
        static void load_crop_profiles( const JsonObject &jo, const std::string &src );
        static void finalize_all();
        static void check_consistency();
        static void reset();
        static const std::vector<crop_profile> &get_all();

        void load( const JsonObject &jo, std::string_view src );
        void check() const;

        crop_profile_id id;
        std::vector<std::pair<crop_profile_id, mod_id>> src;
        bool was_loaded = false;

        /** Below this temperature the plant does not grow. */
        units::temperature base_temp = units::from_celsius( 10.0 );
        /** Above this temperature extra warmth no longer speeds growth. */
        units::temperature max_growth_temp = units::from_celsius( 30.0 );
        /** Daily mean temperature the seed's listed growth times assume. */
        units::temperature reference_temp = units::from_celsius( 20.0 );
        /** Frost at or below this temperature starts damaging the plant. */
        units::temperature frost_damage_temp = units::from_celsius( -1.0 );
        /** Frost at or below this temperature kills an unacclimated plant. */
        units::temperature frost_kill_temp = units::from_celsius( -3.0 );
        /** Kill temperature after two weeks of cold acclimation.  Same as frost_kill_temp if not hardy. */
        units::temperature hardened_kill_temp = units::from_celsius( -3.0 );
        /** Daily highs above this during flowering and fruiting reduce the harvest. */
        units::temperature heat_stress_temp = units::from_celsius( 35.0 );
        /** Botanical family, used for crop rotation and disease. */
        std::string family = "none";
        /**
         * How thirsty the crop is compared with a typical vegetable at full canopy (crop coefficient
         * multiplier): 1.0 is average, 0.4 a cactus, 1.2 rice.
         */
        double water_use = 1.0;
        /**
         * Fraction of the soil's available water the crop can use up before it starts to suffer
         * (FAO-56 "p"): 0.3 for shallow-rooted lettuce, 0.5 typical, 0.8 for a cactus.
         */
        double drought_tolerance = 0.5;
        /** Grows in standing water without its roots rotting (rice, cattails). */
        bool flood_tolerant = false;
        /**
         * Nutrients a full crop takes up over its life, in grams per square meter (one tile).
         * Agronomy figures in kg/ha divide by ten: corn's 180 kg N/ha is 18 g here.
         */
        double nitrogen = 10.0;
        double phosphorus = 2.0;
        double potassium = 10.0;
        /** Share of its nitrogen a legume gets from the air through its root nodules (0 to 1). */
        double nitrogen_fixation = 0.0;
        /**
         * Daily light integral (mol of photosynthetic photons per square meter per day) the crop
         * needs to grow at full speed: about 12-17 for leafy greens, 20-30 for fruiting crops.
         * Fungi need none.
         */
        double light_need = 17.0;

        /** Growing degree days (C) gained in a day with this low and high. */
        double degree_days( double low_c, double high_c ) const;
        /** Growing degree days a day at reference_temp gives. */
        double reference_degree_days() const;
        /** Whether the crop can harden off and overwinter. */
        bool overwinters() const;
        /**
         * Fraction of the plant's health lost to a night at low_c (0 to 1).
         * @param hardening How far the plant has acclimated to cold, 0 to 1.
         */
        double frost_damage( double low_c, double hardening ) const;
        /** Fraction of the harvest lost to a day with this high during flowering (0 to 1). */
        double heat_loss( double high_c ) const;
};

/**
 * What applying a fertilizer or other soil amendment does to a farm plot (realistic farming).
 * One per item type; values are per unit (one charge, or one item for items without charges).
 */
class soil_amendment
{
    public:
        static void load_soil_amendments( const JsonObject &jo, const std::string &src );
        static void finalize_all();
        static void check_consistency();
        static void reset();
        static const std::vector<soil_amendment> &get_all();
        /** The amendment for this item, or a modest generic one for unlisted fertilizers. */
        static const soil_amendment &for_item( const itype_id &item );

        void load( const JsonObject &jo, std::string_view src );

        soil_amendment_id id;
        std::vector<std::pair<soil_amendment_id, mod_id>> src;
        bool was_loaded = false;

        /** The item this describes. */
        itype_id item;
        /** Units one application uses, so a dose covers a square meter at a sensible rate. */
        int dose = 1;
        /** Plant-available nutrients per unit, grams. */
        double nitrogen = 0.0;
        double phosphorus = 0.0;
        double potassium = 0.0;
        /** Nitrogen bound in organic matter per unit, grams; released slowly as it decays. */
        double organic_nitrogen = 0.0;
        /** Change in soil pH per unit. */
        double ph_change = 0.0;
        /** Soluble salts that scorch roots when overapplied. */
        bool burns = false;
};

#endif // CATA_SRC_CROP_PROFILE_H
