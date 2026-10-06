#include "soil.h"

#include "flexbuffer_json.h"
#include "json.h"

void soil_state::serialize( JsonOut &jsout ) const
{
    jsout.start_object();
    jsout.member( "last_day", last_day );
    jsout.member( "water_mm", water_mm );
    jsout.member( "wet_days", wet_days );
    jsout.member( "mulch_until", mulch_until );
    jsout.end_object();
}

void soil_state::deserialize( const JsonObject &jo )
{
    // Fields are added over time; older saves simply lack some of them.
    jo.allow_omitted_members();
    jo.read( "last_day", last_day );
    jo.read( "water_mm", water_mm );
    jo.read( "wet_days", wet_days );
    jo.read( "mulch_until", mulch_until );
}
