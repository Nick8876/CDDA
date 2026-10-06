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

### Part 2: soil water (Planned)

- Per-plot soil moisture stored on the submap and saved with it.
- Rain and snowmelt fill it; evapotranspiration (Hargreaves radiation method, from the day's sunlight and temperature, times a crop coefficient by growth stage) empties it.
- Water above field capacity drains; plots that stay waterlogged for days get root rot. Planters drain faster but hold less.
- Drought slows growth and shrinks the harvest; long drought kills.
- Greenhouses and indoor planters get no rain.
- Watering from carried water; mulch cuts evaporation.

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
