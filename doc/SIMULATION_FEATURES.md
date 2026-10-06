# Simulation features (Nick8876 fork)

This file tracks new simulation content added to this fork, what each piece changes, and whether it has been built and tested yet.

## Working rules for this fork

- Commit straight to `master`.
- Nobody compiles the game until the owner asks. GitHub's automatic checks are fine.
- Playable builds come from the **Experimental Release** workflow, started by hand from the Actions tab ("Run workflow"). It produces a Windows zip with graphics, sound, all tilesets (UltiCa is the default) and CC-Sounds, English only.
- Plans for major changes get a visualization before content is written.

## Status key

- **Written**: code and data are in `master` but have never been compiled or played.
- **Built**: compiles cleanly.
- **Tested**: played in game and works as intended.

## Realistic farming

World option: **Realistic farming** (`FARMING_SIMULATION`, on by default). Off restores the old fixed-timer crops.

Everything is simulated one day at a time. Crops in areas you are away from are caught up from the recalculated weather when you return (up to two years). Weather is a fixed function of place and time, so the catch-up sees the same days you would have seen.

### Part 1: temperature (Written)

- **Growth by accumulated warmth.** Each crop has a `crop_profile` (`data/json/farming/crop_profiles.json`, loaded by `src/crop_profile.cpp`). Growth is counted in growing degree days above the crop's base temperature, capped at its maximum growth temperature. A seed's listed stage durations are what it takes at the profile's reference temperature, so existing seed data keeps working. Seeds without a profile use `crop_generic`.
- **Frost damage** reduces plant health. Light frost burns leaves, deeper frost kills. Hardy crops (`hardened_kill_temp`) acclimate over about two weeks of cold and can overwinter.
- **Snow cover** of 100 mm or more keeps the ground around low plants and overwintering crops near freezing.
- **Heat** above the crop's heat stress temperature during flowering drops flowers and shrinks the harvest; extreme heat damages the plant.
- **Protection.** Row covers (new item), sheets, blankets, tarps and groundsheets (`FROST_COVER` flag) add about 2.5 C at night. Greenhouses (`GREENHOUSE` terrain flag) add about 5 C at night and more on cool days, but are not heated.
- **Planting without a perfect forecast.** The old check looked at the real future weather. Now the player sees an outlook from the local climate normals (more detail with Survival 3+) and can plant anyway. NPCs, farm zones and camps still refuse risky plantings.
- **Harvest size** scales with plant health and flowers lost to heat.
- **Plant death** leaves a withered plant and returns the plot to bare soil.
- **Examining a plant** shows its condition, recent frost or heat damage, and (Survival 4+) days left until harvest, with options to harvest, fertilize, cover or uncover.
- Fertilizer still advances growth (by the equivalent warm time) until part 3 replaces it.

Main code: `src/farming.cpp`, `src/crop_profile.cpp`, `map::grow_plant` and `map::process_crops` (`src/map.cpp`), `iexamine::aggie_plant` and `iexamine::dirtmound` (`src/iexamine.cpp`), `weather_generator::get_typical_day` (`src/weather_gen.cpp`).

### Part 2: soil water (Written)

- **Each plot remembers its soil** (`soil_state` in `src/soil.h`), stored per tile on the submap and saved with it, so it carries over between crops.
- **Water balance, one day at a time.** Rain and snowmelt fill the root zone. Evapotranspiration takes water out: reference evaporation from the day's sunlight and temperature (Hargreaves radiation method, ET0 = 0.0135 (T + 17.8) Rs), times a crop coefficient for the growth stage (0.4 seed, 0.75 seedling, 1.05 full canopy) and the crop's `water_use`.
- **Capacity and drainage.** A ground bed holds about 60 mm (liters) of usable water and drains excess at 25 mm a day; a planter holds 35 mm and drains 60 mm a day.
- **Drought.** Once a crop has used up more than its `drought_tolerance` share of the water, it transpires less, grows more slowly and its harvest shrinks. Near the wilting point it loses health daily and can die.
- **Waterlogging.** Three days soaked above field capacity starts root rot, except in `flood_tolerant` crops (wild rice, aquatic plants).
- **No rain under a roof.** Greenhouses and indoor plots must be watered by hand. Greenhouse glass passes 80% of the sun.
- **Watering** from the plant menu uses water you carry (dirty water first), 250 ml per charge, about 3 seconds per liter, up to what the soil can hold.
- **Mulch** (straw, leaves, withered plants; `MULCH` flag) cuts evaporation and lasts a season.
- **Harvest** now follows the average shortage over the growing days (the law of the minimum; nutrients and light join in parts 3 and 4).
- **Symptoms.** Wilting looks the same from drought and from root rot; Survival 3+ tells them apart by the soil.

### Part 3: soil nutrients, rotation and disease (Planned)

- Nitrogen, phosphorus, potassium and pH per plot, kept between crops.
- Slow natural release from soil organic matter; nitrogen leaches with heavy rain.
- Beans and peas fix their own nitrogen and leave some for the next crop.
- Fertilizers become soil amendments with realistic nutrient content: compost, manure, urine, wood ash, bone meal, commercial fertilizer. Too much fast fertilizer burns plants.
- Harvest set by the scarcest of water, nutrients and light (law of the minimum).
- Plant families build up soil disease when repeated; rotation and fallow let it fade.

### Part 4: light (Planned)

- Daily light from the real sun and cloud cover, reduced by shade from nearby trees and walls.
- Indoor planters allowed; light comes from windows (weak) or grow lights.
- Grow lights as appliances on the power grid, with realistic power draw.

### Part 5: diagnosis and NPC help (Planned)

- Symptoms for each problem, more specific with skill; ambiguous where real symptoms are ambiguous.
- Farming proficiencies, a soil test kit, and farming books.
- NPCs water plots, report problems, fix them with supplies on hand, and teach.
- Base camp farms use the same simulation.
