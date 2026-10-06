#pragma once
#ifndef CATA_SRC_SOIL_H
#define CATA_SRC_SOIL_H

class JsonObject;
class JsonOut;

/**
 * What a farm plot remembers between crops (realistic farming).  Stored per tile on the
 * submap and saved with it.  Values start negative, meaning "not set yet"; the farming code
 * fills in sensible defaults the first time a plot is used.
 */
struct soil_state {
    /** Last day (see farming::day_index) the soil was brought up to date; -1 if never. */
    int last_day = -1;
    /** Plant-available water in the root zone, in millimeters (liters per square meter). */
    double water_mm = -1.0;
    /** Consecutive days the root zone has been waterlogged. */
    int wet_days = 0;
    /** Mulch covers the soil until this day; -1 for none. */
    int mulch_until = -1;

    void serialize( JsonOut &jsout ) const;
    void deserialize( const JsonObject &jo );
};

#endif // CATA_SRC_SOIL_H
