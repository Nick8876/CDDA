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

### Part 3: soil nutrients, rotation and disease (Written)

- **Nutrients per plot**, kept between crops: plant-available nitrogen, phosphorus and potassium (g/m2), nitrogen locked in organic matter, and pH. A fresh bed starts as ordinary garden soil (N 10, P 4, K 15, organic N 500, pH 6.5).
- **Crops take up nutrients as they grow**, spread over the growth up to harvest. Each crop profile lists its uptake from published figures (kg/ha divided by ten): corn 18 g N, potatoes 15 g N and 22 g K, lettuce 8 g N, and so on.
- **Soil life** releases about 2.5% of the organic nitrogen a year at 20 C, doubling every 10 C warmer and stopping when frozen. Phosphorus and potassium slowly weather back toward their starting level.
- **Leaching**: nitrate leaves with water draining through the bed, so heavy rain or overwatering wastes nitrogen.
- **pH**: nutrients are fully available between pH 6 and 7, less outside it. Wood ash and bone meal raise pH; ammonium fertilizers lower it.
- **Legumes** (beans, peas) get most of their nitrogen from the air and leave 3 g/m2 for the next crop.
- **Fertilizers are soil amendments** (`data/json/farming/soil_amendments.json`) with realistic content and a dose per application sized for one square meter: 10-10-10 granules (two handfuls, about 50 g), compost tea, manure (2 kg), cow pies, poultry litter, insect frass, wood ash (200 g), bone meal, saltpeter, potash, ammonium nitrate and seaweed. Manure, ash, bone meal, seaweed and the fertilizer chemicals can now be used on plots. Dog droppings cannot (parasites).
- **Fertilizer burn**: soluble fertilizers that push available nitrogen above 25 g/m2 scorch the plant.
- **Harvest** follows the scarcest of water and the three nutrients on each growing day (law of the minimum). Shortages also slow growth.
- **Crop rotation and disease.** Each bed remembers its last four crops' plant families. Every crop leaves disease pressure for its family in the soil, halving each year. Plants catch soil-borne diseases more often where their family grew recently and in wet weather. A diseased plant loses health daily and yields less. Pulling it up (new menu option) stops it. Survival 2+ warns before planting the same family again.
- **Symptoms.** Survival 2+ sees nutrient-specific signs (yellowing older leaves, purple tinge, scorched edges); 4+ names the shortage. Survival 3+ recognizes each family's disease: late blight, clubroot, powdery mildew, white rot, rust, root rot, leaf blight, downy mildew.

### Part 4: light (Written)

- **Daily light from the real sun.** The day's solar energy (already simulated with sun angle and cloud cover) converts to a daily light integral: about 45% of sunlight is photosynthetic, at 4.57 umol per joule. A clear midsummer day gives about 66 mol/m2, close to the real ~60.
- **Each crop has a `light_need`** (mol/m2/day for full growth): about 12-15 for leafy greens and radishes, 16-20 for roots, peas and beans, 22-25 for tomatoes, squash, grain and corn. Mushrooms need none.
- **Shade.** Walls and trees around a bed take light, most from the south side (up to 70%). Glass passes 80%. A frost cover passes 85%.
- **Indoor beds are allowed.** Planters can now go inside: they get some light from adjacent windows (8% of daylight each, up to 25%) or from grow lights. The game warns when a spot is too dark and asks before planting. NPCs won't plant in the dark.
- **LED grow light** (new appliance, `grow_light_led`): placed like a standing lamp and plugged into your power grid. It draws a realistic 900 W while on and lights its own tile and the eight around it to 15 mol/m2/day; stack lights for hungry crops. It switches off when the grid runs dry. Found in garden supply loot or crafted from LED strips (electronics 3).
- **Indoor and underground temperatures.** Rooms even out the day (nights 4 C warmer, afternoons 2 C cooler). Whatever heats the room now (stove, space heater) keeps beds warm. Underground beds sit at the region's year-round ground temperature.
- **Growth and harvest** now follow the scarcest of water, nutrients and light each day. Weeks of too little light starve the plant (a few stormy days don't).
- **Symptoms.** Pale, spindly, stretching plants; Survival 2+ knows it's light.

### Part 5: diagnosis and NPC help (Written)

- **Reading plants.** How much the plant examine screen tells you depends on Survival skill plus 2 for the new **Gardening** proficiency. Real symptoms stay ambiguous at low skill: wilting looks the same from drought and from root rot, pale leaves could be hunger or darkness.
- **Three farming proficiencies** (`data/json/proficiencies/farming.json`): Gardening (12 h), Soil Fertility (20 h) and Plant Pathology (20 h), the last two needing Gardening. They're practiced by tending crops: watering, mulching, covering and harvesting train Gardening; fertilizing and soil tests train Soil Fertility; pulling up plants trains Plant Pathology. Soil Fertility names nutrient shortages; Plant Pathology names diseases.
- **Soil test kit** (`soil_test_kit`, one test each, found with garden supplies): gives exact moisture, nitrogen, phosphorus, potassium and approximate pH, with notes on acidity. Carrying a pH meter gives the exact pH.
- **Books**: the *Old Farmer's Almanac* (Survival 0-3; carrying it improves frost judgment by 2 when planting) and an *Extension Bulletin: Soil Fertility for Home Gardens* (Survival 2-5), in bookstores, libraries and garden supplies.
- **Farmers know their trade.** The Farmer profession and farmer NPCs start with all three farming proficiencies, so farmer NPCs can teach them through the usual training dialogue.
- **NPC farmhands water.** During farm-zone work, NPCs water thirsty plants with water they carry (give them full containers); they don't fetch water themselves.
- **"How are the crops doing?"** A new companion dialogue option. The companion looks over crops within 30 tiles and reports what's ready, thirsty, waterlogged, hungry, diseased or in the dark, as accurately as their own knowledge allows (a novice only says which plants "don't look right").
- **Base camps.** Camp harvests follow the same health and shortage factors, and camp beds remember their crops for rotation.
- **Background map pieces.** Crop simulation also runs when the game loads map pieces outside your area (base camp fields), reading roofs from the terrain itself.
- Fertilizing in realistic mode still leaves the usual "fertilized" marker, so each growth stage gets one application, by you or by NPCs.

## Known gaps and ideas for later

- Raw manure food safety (pathogens on root and leafy crops harvested soon after manuring).
- NPCs fetching water from tanks and barrels.
- Weeds and insect pests.
- Drip irrigation from water tanks.
