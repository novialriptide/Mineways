/*
Copyright (c) 2010, Sean Kasun
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

* Redistributions of source code must retain the above copyright notice, this
list of conditions and the following disclaimer.

* Redistributions in binary form must reproduce the above copyright notice,
this list of conditions and the following disclaimer in the documentation
and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
THE POSSIBILITY OF SUCH DAMAGE.
*/


#include "stdafx.h"
#include <string.h>
#include <assert.h>

// We know we won't run into names longer than 100 characters. The old code was
// safe, but was also allocating strings all the time - seems slow.
#define MAX_NAME_LENGTH 100


static int skipType(bfFile* pbf, int type);
static int skipList(bfFile* pbf);
static int skipCompound(bfFile* pbf);

typedef struct BlockTranslator {
    int hashSum;
    unsigned char blockId;
    unsigned char dataVal;
    char* name;
    unsigned long translateFlags;
} BlockTranslator;

// our bit shift code reader can read only up to 2^9 entries right now. TODO
#define MAX_PALETTE	512

static bool hashMade = false;
static int worldVersion = 0;

// if defined, only those data values that have an effect on graphics display (vs. sound or
// simulation) are actually filled in. This is a good thing for instancing, but allows the
// code to be turned on in the future (though there are still a bunch of non-graphical values
// that we nonetheless don't track).
#define GRAPHICAL_ONLY

// Shows all data names and values and what uses them: https://minecraft.fandom.com/wiki/Java_Edition_data_values

// properties and which blocks use them:
// age: AGE_PROP
// attached: TRIPWIRE_PROP, TRIPWIRE_HOOK_PROP
// axis: AXIS_PROP, QUARTZ_PILLAR_PROP
// bites: CANDLE_CAKE_PROP
// bottom: true|false - scaffolding, for which we ignore the "distance" field
// conditional: COMMAND_BLOCK_PROP
// delay: REPEATER_PROP
// disarmed: TRIPWIRE_PROP
// distance: LEAF_PROP
// down|up: MUSHROOM_PROP, MUSHROOM_STEM_PROP
// east|north|west|south: MUSHROOM_PROP, MUSHROOM_STEM_PROP, WIRE_PROP
// eggs: EGG_PROP
// enabled: HOPPER_PROP
// extended: PISTON_PROP
// face: LEVER_PROP, BUTTON_PROP
// facing: DOOR_PROP, TORCH_PROP, STAIRS_PROP, LEVER_PROP, CHEST_PROP, FURNACE_PROP, FACING_PROP, BUTTON_PROP, SWNE_FACING_PROP, 
//     BED_PROP, DROPPER_PROP, TRAPDOOR_PROP, PISTON_PROP, PISTON_HEAD_PROP, COMMAND_BLOCK_PROP, HOPPER_PROP, OBSERVER_PROP,
//     REPEATER_PROP, COMPARATOR_PROP, HEAD_WALL_PROP
// falling: FLUID_PROP
// half: DOOR_PROP, TALL_FLOWER_PROP, STAIRS_PROP, TRAPDOOR_PROP
// hanging: LANTERN_PROP (really just sets dataVal directly to 0x1 if true)
// has_book: LECTERN_PROP
// hinge: DOOR_PROP
// in_wall: fence gate
// inverted: DAYLIGHT_PROP
// layers: SNOW_PROP
// leaves: LEAF_SIZE_PROP
// level: FLUID_PROP
// lit: TORCH_PROP (redstone only), FURNACE_PROP, REDSTONE_ORE_PROP
// locked: REPEATER_PROP
// mode: STRUCTURE_PROP, COMPARATOR_PROP
// moisture: FARMLAND_PROP
// north|east|south|west[|up]: VINE_PROP, TRIPWIRE_PROP
// occupied: BED_PROP
// open: DOOR_PROP, TRAPDOOR_PROP
// part: BED_PROP
// persistent: LEAF_PROP
// pickles: PICKLE_PROP
// power: WIRE_PROP, WT_PRESSURE_PROP, DAYLIGHT_PROP
// powered: DOOR_PROP, LEVER_PROP, RAIL_PROP, PRESSURE_PROP, BUTTON_PROP, TRAPDOOR_PROP, TRIPWIRE_PROP, TRIPWIRE_HOOK_PROP, REPEATER_PROP, COMPARATOR_PROP
// rotation: STANDING_SIGN_PROP, HEAD_PROP
// shape: STAIRS_PROP, RAIL_PROP
// short: PISTON_HEAD_PROP
// signal_fire: CAMPFIRE_PROP
// snowy: SNOWY_PROP
// stage: SAPLING_PROP
// triggered: DROPPER_PROP
// type: SLAB_PROP, CHEST_PROP, PISTON_HEAD_PROP
// waterlogged: so common that we always add it in and use the dedicated bit 0x10

// various properties of palette entries, can be used to differentiate how to use properties
#define NO_PROP				  0
// axis: x|y|z
#define AXIS_PROP			  1
// snowy: false|true
// note that the snowy prop actually does nothing; put here mostly to keep track; could be deleted.
#define SNOWY_PROP			  2
// facing: north|south|east|west
// half: upper|lower
// hinge: left|right
// open: true|false
// powered: true|false
#define DOOR_PROP			  3
// for water and lava
// See https://minecraft.fandom.com/wiki/Water#Block_states
// level: 1-7|8 when falling true - note that level 0 is the "source block" and higher means further away
#define FLUID_PROP			  4
// saplings
// stage: 0|1 - non-graphical, so ignored
#define SAPLING_PROP		  5
// persistent: true|false - non-graphical, so ignored
// distance: 1-7 - non-graphical, so ignored
#define LEAF_PROP			  6
// for sunflowers, etc.
// half: upper|lower
#define TALL_FLOWER_PROP	  7
// down|up|east|north|west|south: true|false
#define MUSHROOM_PROP		  8
#define MUSHROOM_STEM_PROP	  9
// type: top|bottom|double
// waterlogged: true|false
#define SLAB_PROP			 10
// facing: north|east|south|west
// lit: true|false - for redstone 
#define TORCH_PROP			 11
// facing: north|east|south|west
// half: top|bottom - note this is different from upper|lower for doors and sunflowers
// shape: straight|inner_left|inner_right|outer_left|outer_right - we ignore, deriving from neighbors
#define STAIRS_PROP			 12
// Redstone wire
// north|south|east|west: none|side|up https://minecraft.gamepedia.com/Redstone#Block_state - we ignore, it's from geometry
// power: 0-15
#define WIRE_PROP		     13
// Lever
// face: floor|ceiling|wall
// facing: north|west|south|east
// powered: true|false
#define LEVER_PROP			 14
// facing: north|west|south|east
// type: single|left|right
#define CHEST_PROP			 15
// wheat, frosted ice, cactus, sugar cane, etc.
// leaves: leaf size prop (for bamboo) - none/small/large
#define LEAF_SIZE_PROP		 16
// moisture: 0-7
#define FARMLAND_PROP		 17
// facing: north|west|south|east - like torches
// lit: true|false 
#define FURNACE_PROP		 18
// used for ladders and wall signs
// facing: north|west|south|east - like torches
#define FACING_PROP			 19
// rotation: 0-15
#define STANDING_SIGN_PROP	 20
// shape: north_south|east_west|north_east|north_west|south_east|south_west|ascending_north|ascending_south|ascending_east|ascending_west
// powered: true|false - all but rail itself
#define RAIL_PROP			 21
// powered: true|false
// may use higher bits for types of pressure plates? - TODO
#define PRESSURE_PROP		 22
// power: 0-15 - really, same as WIRE_PROP, but just in case
#define WT_PRESSURE_PROP	 23
// lit: true|false 
#define REDSTONE_ORE_PROP	 24
// face: floor|ceiling|wall
// facing: north|west|south|east
// powered: true|false
#define BUTTON_PROP			 25
// layers: 1-8
#define SNOW_PROP			 26
// facing: north|south|east|west
// open: true|false
// in_wall: true|false - If true, the gate is lowered by three pixels, to accommodate attaching more cleanly with normal and mossy Cobblestone Walls
// powered: true|false - ignored
#define FENCE_GATE_PROP		 27
// facing: south|west|north|east
#define SWNE_FACING_PROP	 28
// bites: 0-6; 7 means it's actually a normal candle
// if BIT_16 is flagged, it means there's a candle on top and that 0xf is the color
// lit: true or false, goes to BIT_32
#define CANDLE_CAKE_PROP	 29
// facing: south|west|north|east
// occupied: true|false - seems to be occupied only if someone else is occupying it? non-graphical, so ignored
// part: head|foot
#define BED_PROP			 30

#define EXTENDED_FACING_PROP 31
// facing: down|up|north|south|west|east
// triggered: true|false - ignore, non-graphical
#define DROPPER_PROP		 EXTENDED_FACING_PROP
// facing: down|up|north|south|west|east
// half: top|bottom
// open: true|false
// powered: true|false
// waterlogged: true|false
#define TRAPDOOR_PROP		 32
// facing: north|east|south|west
#define ANVIL_PROP			 33
// facing: down|up|north|south|west|east
// extended: true|false
#define PISTON_PROP			 EXTENDED_FACING_PROP
// facing: down|up|north|south|west|east
// extended: true|false - ignored, don't know what that is (block wouldn't exist otherwise, right?
// type: sticky|normal
// short: true|false - TODOTODO, piston arm is shorter by 4 pixels, https://minecraft.gamepedia.com/Piston#Block_state_2 - not sure how to get this
#define PISTON_HEAD_PROP	 EXTENDED_FACING_PROP
// south|west|north|east|down|up: true|false
#define FENCE_AND_VINE_PROP	 34
// facing: south|west|north|east
#define END_PORTAL_PROP		 35
// age: 0-3
// facing: north|east|south|west
#define COCOA_PROP			 36
// south|west|north|east|up: true|false - ignored, from context
// attached: true|false
// powered: true|false
// disarmed: true|false
#define TRIPWIRE_PROP		 37
// facing: south|west|north|east
// attached: true|false
// powered: true|false
#define TRIPWIRE_HOOK_PROP	 38
// facing: down|up|north|south|west|east
// conditional: true|false
#define COMMAND_BLOCK_PROP	 EXTENDED_FACING_PROP
// inverted: true|false
// power: 0-15
#define DAYLIGHT_PROP		 39
// enabled: true|false
// facing: down|north|south|west|east
#define HOPPER_PROP			 EXTENDED_FACING_PROP
// axis: x|y|z
#define QUARTZ_PILLAR_PROP	 40
// facing: down|up|south|north|east|west
// powered: true|false
#define OBSERVER_PROP		 EXTENDED_FACING_PROP
// mode: data|save|load|corner
#define STRUCTURE_PROP		 41
// delay: 1-4
// facing: north|east|south|west
// locked: true|false - ignored
// powered: true|false - ignored
#define REPEATER_PROP		 42
// facing: north|east|south|west
// powered: true|false
// delayed: 0-3
// locked: true|false
#define COMPARATOR_PROP		 43
// rotation: 0-15
#define HEAD_PROP			 44
// facing: north|south|east|west
#define HEAD_WALL_PROP		 45
// clear out dataVal, which might get "age" etc. before saving
#define TRULY_NO_PROP		 46
// waterlogged: true|false, 1 HIGH bit
// facing: south|north|west|east - 2 HIGH bits
#define FAN_PROP			 47
// pickles: 1-4
// waterlogged: true|false
#define PICKLE_PROP			 48
// eggs: 1-4
#define EGG_PROP			 49
// for wall signs - basically a dropper, without the other stuff such as up, etc.
#define WALL_SIGN_PROP		 EXTENDED_FACING_PROP
// facing: down|up|north|south|west|east
// open: true|false
#define BARREL_PROP			 EXTENDED_FACING_PROP
// facing: north|south|west|east - done as 0,1,2,3 SWNE
#define EXTENDED_SWNE_FACING_PROP 50
// facing: north|south|west|east - done as 0,1,2,3 SWNE
// face: floor|ceiling|wall
#define GRINDSTONE_PROP		EXTENDED_SWNE_FACING_PROP
// facing: north|south|west|east - done as 0,1,2,3 SWNE
// has_book: true|false
// powered: true|false
#define LECTERN_PROP		EXTENDED_SWNE_FACING_PROP
// facing: north|south|west|east - done as 0,1,2,3 SWNE
// powered: true|false
// attachment: floor|ceiling|single_wall|double_wall
#define BELL_PROP		EXTENDED_SWNE_FACING_PROP
// hanging: true|false
#define LANTERN_PROP		51
// facing: north|south|west|east - done as 0,1,2,3 SWNE
// lit: true|false
// signal_fire: true|false
#define CAMPFIRE_PROP		EXTENDED_SWNE_FACING_PROP
// UGH, not implemented - too many bits! Need data values to be 16 bits to consider it. TODO
// east, north, south, west: low|none|tall
// up: false|true
// waterlogged
// which adds up to 9 bits, which is too many
#define WALL_PROP           NO_PROP
// axis: 1 EW, 2 NS
#define NETHER_PORTAL_AXIS_PROP	52
// pumpkin and melon stems
// age:0-7
// facing: north|south|west|east - done as BIT_32|BIT_16 0,1,2,3 ESWN
#define HIGH_FACING_PROP    53
// candles: 1-4 maps to 0x00-0x30
// lit: adds one to the type
// waterlogged: the usual bit
#define CANDLE_PROP         54
// bottom two bits is sub-type.
// facing: 0-6 << 2 dropper_facing
#define AMETHYST_PROP       55
// thickness: 5 states
// vertical_direction: up/down
#define DRIPSTONE_PROP      56
// bottommost is stem
// facing: 0-3 door_facing << 1
// tilt: none/partial/unstable/full 0xc0 fields (0x0,0x4,0x8,0xC) << 1
#define BIG_DRIPLEAF_PROP   57
// facing: 0-3 door_facing
// half: lower/upper 0x0/0x4
#define SMALL_DRIPLEAF_PROP 58
// berries: 0x2 if berries
#define BERRIES_PROP        59
// age: 0-7 or 0-15, (0-3 for frosted ice)
// property is treated as NO_PROP; if something has just an age, it simply gets the value in dataVal - search on "age" (with quotes) to see code
#define AGE_PROP			60

#define NUM_TRANS 864

BlockTranslator BlockTranslations[NUM_TRANS] = {
    //hash ID data name flags
    // hash is computed once when 1.13 data is first read in.
    // second column is "traditional" type value, as found in blockInfo.cpp; third column is high-order bit and data value, fourth is Minecraft name
    // Note: the HIGH_BIT gets "transferred" to the type in MinewaysMap's IDBlock() method, about 100 lines in.
    // The list of names and data values: https://minecraft.gamepedia.com/Java_Edition_data_values
    // and older https://minecraft.gamepedia.com/Java_Edition_data_values/Pre-flattening#Block_IDs
    //hash,ID,BIT|dataval,  name, common properties flags
    { 0,   0,           0, "air", NO_PROP },
    { 0, 166,           0, "barrier", NO_PROP },
    { 0,   1,           0, "stone", NO_PROP },
    { 0,   1,           1, "granite", NO_PROP },
    { 0,   1,           2, "polished_granite", NO_PROP },
    { 0,   1,           3, "diorite", NO_PROP },
    { 0,   1,           4, "polished_diorite", NO_PROP },
    { 0,   1,           5, "andesite", NO_PROP },
    { 0,   1,           6, "polished_andesite", NO_PROP },
    { 0, 170,           0, "hay_block", AXIS_PROP },
    { 0,   2,           0, "grass_block", SNOWY_PROP },
    { 0,   3,           0, "dirt", NO_PROP }, // no SNOWY_PROP
    { 0,   3,           1, "coarse_dirt", NO_PROP }, // note no SNOWY_PROP
    { 0,   3,           2, "podzol", SNOWY_PROP },
    { 0,   4,           0, "cobblestone", NO_PROP },
    { 0,   5,           0, "oak_planks", NO_PROP },
    { 0,   5,           1, "spruce_planks", NO_PROP },
    { 0,   5,           2, "birch_planks", NO_PROP },
    { 0,   5,           3, "jungle_planks", NO_PROP },
    { 0,   5,           4, "acacia_planks", NO_PROP },
    { 0,   5,           5, "dark_oak_planks", NO_PROP },
    { 0,   6,           0, "oak_sapling", SAPLING_PROP },
    { 0,   6,           1, "spruce_sapling", SAPLING_PROP },
    { 0,   6,           2, "birch_sapling", SAPLING_PROP },
    { 0,   6,           3, "jungle_sapling", SAPLING_PROP },
    { 0,   6,           4, "acacia_sapling", SAPLING_PROP },
    { 0,   6,           5, "dark_oak_sapling", SAPLING_PROP },
    { 0,  64,           0, "oak_door", DOOR_PROP },
    { 0, 193,           0, "spruce_door", DOOR_PROP },
    { 0, 194,           0, "birch_door", DOOR_PROP },
    { 0, 195,           0, "jungle_door", DOOR_PROP },
    { 0, 196,           0, "acacia_door", DOOR_PROP },
    { 0, 197,           0, "dark_oak_door", DOOR_PROP },
    { 0,   7,           0, "bedrock", NO_PROP },
    { 0,   9,           0, "water", FLUID_PROP },   // FLUID_PROP
    { 0,  11,           0, "lava", FLUID_PROP },   // FLUID_PROP
    { 0,  12,           0, "sand", NO_PROP },
    { 0,  12,           1, "red_sand", NO_PROP },
    { 0,  24,           0, "sandstone", NO_PROP }, // TODO 1.13 check: For normal sandstone the bottom has a cracked pattern. The other types of sandstone have bottom faces same as the tops.
    { 0,  24,           1, "chiseled_sandstone", NO_PROP },
    { 0,  24,           2, "cut_sandstone", NO_PROP }, // aka smooth sandstone
    { 0, 179,           0, "red_sandstone", NO_PROP },
    { 0, 179,           1, "chiseled_red_sandstone", NO_PROP },
    { 0, 179,           2, "cut_red_sandstone", NO_PROP }, // aka smooth red sandstone
    { 0,  13,           0, "gravel", NO_PROP },
    { 0,  14,           0, "gold_ore", NO_PROP },
    { 0,  15,           0, "iron_ore", NO_PROP },
    { 0,  16,           0, "coal_ore", NO_PROP },
    { 0,  17,  BIT_16 | 0, "oak_wood", AXIS_PROP },	// same as logs below, but with a high bit set to mean that it's "wood" texture on the endcaps. 
    { 0,  17,  BIT_16 | 1, "spruce_wood", AXIS_PROP },
    { 0,  17,  BIT_16 | 2, "birch_wood", AXIS_PROP },
    { 0,  17,  BIT_16 | 3, "jungle_wood", AXIS_PROP },
    { 0, 162,  BIT_16 | 0, "acacia_wood", AXIS_PROP },
    { 0, 162,  BIT_16 | 1, "dark_oak_wood", AXIS_PROP },
    { 0,  17,           0, "oak_log", AXIS_PROP },
    { 0,  17,           1, "spruce_log", AXIS_PROP },
    { 0,  17,           2, "birch_log", AXIS_PROP },
    { 0,  17,           3, "jungle_log", AXIS_PROP },
    { 0, 162,           0, "acacia_log", AXIS_PROP },
    { 0, 162,           1, "dark_oak_log", AXIS_PROP },
    { 0,  18,           0, "oak_leaves", LEAF_PROP },
    { 0,  18,           1, "spruce_leaves", LEAF_PROP },
    { 0,  18,           2, "birch_leaves", LEAF_PROP },
    { 0,  18,           3, "jungle_leaves", LEAF_PROP },
    { 0, 161,           0, "acacia_leaves", LEAF_PROP },
    { 0, 161,           1, "dark_oak_leaves", LEAF_PROP },
    { 0,  32,           0, "dead_bush", NO_PROP },
    { 0,  31,           1, "grass", NO_PROP }, // tall grass
    { 0,  31,           2, "fern", NO_PROP },
    { 0,  19,           0, "sponge", NO_PROP },
    { 0,  19,           1, "wet_sponge", NO_PROP },
    { 0,  20,           0, "glass", NO_PROP },
    { 0,  95,           0, "white_stained_glass", NO_PROP },
    { 0,  95,           1, "orange_stained_glass", NO_PROP },
    { 0,  95,           2, "magenta_stained_glass", NO_PROP },
    { 0,  95,           3, "light_blue_stained_glass", NO_PROP },
    { 0,  95,           4, "yellow_stained_glass", NO_PROP },
    { 0,  95,           5, "lime_stained_glass", NO_PROP },
    { 0,  95,           6, "pink_stained_glass", NO_PROP },
    { 0,  95,           7, "gray_stained_glass", NO_PROP },
    { 0,  95,           8, "light_gray_stained_glass", NO_PROP },
    { 0,  95,           9, "cyan_stained_glass", NO_PROP },
    { 0,  95,          10, "purple_stained_glass", NO_PROP },
    { 0,  95,          11, "blue_stained_glass", NO_PROP },
    { 0,  95,          12, "brown_stained_glass", NO_PROP },
    { 0,  95,          13, "green_stained_glass", NO_PROP },
    { 0,  95,          14, "red_stained_glass", NO_PROP },
    { 0,  95,          15, "black_stained_glass", NO_PROP },
    { 0, 160,           0, "white_stained_glass_pane", NO_PROP },   // sadly, these all share a type so there are not 4 bits for directions, especially since it may waterlog
    { 0, 160,           1, "orange_stained_glass_pane", NO_PROP },
    { 0, 160,           2, "magenta_stained_glass_pane", NO_PROP },
    { 0, 160,           3, "light_blue_stained_glass_pane", NO_PROP },
    { 0, 160,           4, "yellow_stained_glass_pane", NO_PROP },
    { 0, 160,           5, "lime_stained_glass_pane", NO_PROP },
    { 0, 160,           6, "pink_stained_glass_pane", NO_PROP },
    { 0, 160,           7, "gray_stained_glass_pane", NO_PROP },
    { 0, 160,           8, "light_gray_stained_glass_pane", NO_PROP },
    { 0, 160,           9, "cyan_stained_glass_pane", NO_PROP },
    { 0, 160,          10, "purple_stained_glass_pane", NO_PROP },
    { 0, 160,          11, "blue_stained_glass_pane", NO_PROP },
    { 0, 160,          12, "brown_stained_glass_pane", NO_PROP },
    { 0, 160,          13, "green_stained_glass_pane", NO_PROP },
    { 0, 160,          14, "red_stained_glass_pane", NO_PROP },
    { 0, 160,          15, "black_stained_glass_pane", NO_PROP },
    { 0, 102,           0, "glass_pane", FENCE_AND_VINE_PROP },
    { 0,  37,           0, "dandelion", NO_PROP },
    { 0,  38,           0, "poppy", NO_PROP },
    { 0,  38,           1, "blue_orchid", NO_PROP },
    { 0,  38,           2, "allium", NO_PROP },
    { 0,  38,           3, "azure_bluet", NO_PROP },
    { 0,  38,           4, "red_tulip", NO_PROP },
    { 0,  38,           5, "orange_tulip", NO_PROP },
    { 0,  38,           6, "white_tulip", NO_PROP },
    { 0,  38,           7, "pink_tulip", NO_PROP },
    { 0,  38,           8, "oxeye_daisy", NO_PROP },
    { 0, 175,           0, "sunflower", TALL_FLOWER_PROP },
    { 0, 175,           1, "lilac", TALL_FLOWER_PROP },
    { 0, 175,           2, "tall_grass", TALL_FLOWER_PROP },
    { 0, 175,           3, "large_fern", TALL_FLOWER_PROP },
    { 0, 175,           4, "rose_bush", TALL_FLOWER_PROP },
    { 0, 175,           5, "peony", TALL_FLOWER_PROP },
    { 0,  39,           0, "brown_mushroom", NO_PROP },
    { 0, 100,           0, "red_mushroom_block", MUSHROOM_PROP },
    { 0,  99,           0, "brown_mushroom_block", MUSHROOM_PROP },
    { 0, 100,           0, "mushroom_stem", MUSHROOM_STEM_PROP }, // red mushroom block chosen, arbitrarily; either is fine
    { 0,  41,           0, "gold_block", NO_PROP },
    { 0,  42,           0, "iron_block", NO_PROP },
    { 0,  44,           0, "smooth_stone_slab", SLAB_PROP },	// renamed in 1.14 from stone_slab in 1.13 - it means "the chiseled one" as it's traditionally been; the new 1.14 "stone_slab" means "pure flat stone"
    { 0,  44,           1, "sandstone_slab", SLAB_PROP },
    { 0, 182,           0, "red_sandstone_slab", SLAB_PROP }, // really, just uses 182 exclusively; sometimes rumored to be 205/0, but not so https://minecraft.gamepedia.com/Java_Edition_data_values#Stone_Slabs
    { 0,  44,           2, "petrified_oak_slab", SLAB_PROP },
    { 0,  44,           3, "cobblestone_slab", SLAB_PROP },
    { 0,  44,           4, "brick_slab", SLAB_PROP },
    { 0,  44,           5, "stone_brick_slab", SLAB_PROP },
    { 0,  44,           6, "nether_brick_slab", SLAB_PROP },
    { 0,  44,           7, "quartz_slab", SLAB_PROP },
    { 0, 126,           0, "oak_slab", SLAB_PROP },
    { 0, 126,           1, "spruce_slab", SLAB_PROP },
    { 0, 126,           2, "birch_slab", SLAB_PROP },
    { 0, 126,           3, "jungle_slab", SLAB_PROP },
    { 0, 126,           4, "acacia_slab", SLAB_PROP },
    { 0, 126,           5, "dark_oak_slab", SLAB_PROP },
    { 0,  45,           0, "bricks", NO_PROP },
    { 0, BLOCK_TNT,     0, "tnt", TRULY_NO_PROP },
    { 0,  47,           0, "bookshelf", NO_PROP },
    { 0,  48,           0, "mossy_cobblestone", NO_PROP },
    { 0,  49,           0, "obsidian", NO_PROP },
    { 0,  50,           0, "torch", TORCH_PROP },
    { 0,  50,           0, "wall_torch", TORCH_PROP },
#ifdef GRAPHICAL_ONLY
    { 0, BLOCK_FIRE,    0, "fire", TRULY_NO_PROP }, // ignore age
#else
    { 0, BLOCK_FIRE,    0, "fire", NO_PROP },
#endif
    { 0,  52,           0, "spawner", NO_PROP },
    { 0,  53,           0, "oak_stairs", STAIRS_PROP },
    { 0, 134,           0, "spruce_stairs", STAIRS_PROP },
    { 0, 135,           0, "birch_stairs", STAIRS_PROP },
    { 0, 136,           0, "jungle_stairs", STAIRS_PROP },
    { 0, 163,           0, "acacia_stairs", STAIRS_PROP },
    { 0, 164,           0, "dark_oak_stairs", STAIRS_PROP },
    { 0,  54,           0, "chest", CHEST_PROP },
    { 0, 146,           0, "trapped_chest", CHEST_PROP },
    { 0,  55,           0, "redstone_wire", WIRE_PROP },  // WIRE_PROP
    { 0,  56,           0, "diamond_ore", NO_PROP },
    { 0,  93,           0, "repeater", REPEATER_PROP },
    { 0, 149,           0, "comparator", COMPARATOR_PROP },
    { 0, 173,           0, "coal_block", NO_PROP },
    { 0,  57,           0, "diamond_block", NO_PROP },
    { 0,  58,           0, "crafting_table", NO_PROP },
    { 0,  59,           0, "wheat", AGE_PROP },
    { 0,  60,           0, "farmland", FARMLAND_PROP },
    { 0,  61,           0, "furnace", FURNACE_PROP },
    { 0,  63,           0, "sign", STANDING_SIGN_PROP }, // 1.13 - in 1.14 it's oak_sign, acacia_sign, etc.
    { 0,  68,           0, "wall_sign", WALL_SIGN_PROP }, // 1.13 - in 1.14 it's oak_wall_sign, acacia_wall_sign, etc.
    { 0,  65,           0, "ladder", FACING_PROP },
    { 0,  66,           0, "rail", RAIL_PROP },   /* 200 */
    { 0,  27,           0, "powered_rail", RAIL_PROP },
    { 0, 157,           0, "activator_rail", RAIL_PROP },
    { 0,  28,           0, "detector_rail", RAIL_PROP },
    { 0,  67,           0, "cobblestone_stairs", STAIRS_PROP },
    { 0, 128,           0, "sandstone_stairs", STAIRS_PROP },
    { 0, 180,           0, "red_sandstone_stairs", STAIRS_PROP },
    { 0,  69,           0, "lever", LEVER_PROP },
    { 0,  70,           0, "stone_pressure_plate", PRESSURE_PROP },
    { 0,  72,           0, "oak_pressure_plate", PRESSURE_PROP },
    { 0, 147,           0, "light_weighted_pressure_plate", WT_PRESSURE_PROP },
    { 0, 148,           0, "heavy_weighted_pressure_plate", WT_PRESSURE_PROP },
    { 0,  71,           0, "iron_door", DOOR_PROP },
    { 0,  73,           0, "redstone_ore", REDSTONE_ORE_PROP },	// unlit by default
    { 0,  76,           0, "redstone_torch", TORCH_PROP },
    { 0,  76,           0, "redstone_wall_torch", TORCH_PROP },
    { 0,  77,           0, "stone_button", BUTTON_PROP },
    { 0, 143,           0, "oak_button", BUTTON_PROP },
    { 0,  78,           0, "snow", SNOW_PROP },
    { 0, 171,           0, "white_carpet", NO_PROP },
    { 0, 171,           1, "orange_carpet", NO_PROP },
    { 0, 171,           2, "magenta_carpet", NO_PROP },
    { 0, 171,           3, "light_blue_carpet", NO_PROP },
    { 0, 171,           4, "yellow_carpet", NO_PROP },
    { 0, 171,           5, "lime_carpet", NO_PROP },
    { 0, 171,           6, "pink_carpet", NO_PROP },
    { 0, 171,           7, "gray_carpet", NO_PROP },
    { 0, 171,           8, "light_gray_carpet", NO_PROP },
    { 0, 171,           9, "cyan_carpet", NO_PROP },
    { 0, 171,          10, "purple_carpet", NO_PROP },
    { 0, 171,          11, "blue_carpet", NO_PROP },
    { 0, 171,          12, "brown_carpet", NO_PROP },
    { 0, 171,          13, "green_carpet", NO_PROP },
    { 0, 171,          14, "red_carpet", NO_PROP },
    { 0, 171,          15, "black_carpet", NO_PROP },
    { 0,  79,           0, "ice", NO_PROP },
    { 0, 212,           0, "frosted_ice", AGE_PROP },
    { 0, 174,           0, "packed_ice", NO_PROP },
    { 0,  81,           0, "cactus", AGE_PROP },
    { 0,  82,           0, "clay", NO_PROP },
    { 0, 159,           0, "white_terracotta", NO_PROP },
    { 0, 159,           1, "orange_terracotta", NO_PROP },
    { 0, 159,           2, "magenta_terracotta", NO_PROP },
    { 0, 159,           3, "light_blue_terracotta", NO_PROP },
    { 0, 159,           4, "yellow_terracotta", NO_PROP },
    { 0, 159,           5, "lime_terracotta", NO_PROP },
    { 0, 159,           6, "pink_terracotta", NO_PROP },
    { 0, 159,           7, "gray_terracotta", NO_PROP },
    { 0, 159,           8, "light_gray_terracotta", NO_PROP },
    { 0, 159,           9, "cyan_terracotta", NO_PROP },
    { 0, 159,          10, "purple_terracotta", NO_PROP },
    { 0, 159,          11, "blue_terracotta", NO_PROP },
    { 0, 159,          12, "brown_terracotta", NO_PROP },
    { 0, 159,          13, "green_terracotta", NO_PROP },
    { 0, 159,          14, "red_terracotta", NO_PROP },
    { 0, 159,          15, "black_terracotta", NO_PROP },
    { 0, 172,           0, "terracotta", NO_PROP },
    { 0,  83,           0, "sugar_cane", AGE_PROP },
    { 0,  84,           0, "jukebox", NO_PROP },
    { 0,  85,           0, "oak_fence", FENCE_AND_VINE_PROP },
    { 0, 188,           0, "spruce_fence", FENCE_AND_VINE_PROP },
    { 0, 189,           0, "birch_fence", FENCE_AND_VINE_PROP },
    { 0, 190,           0, "jungle_fence", FENCE_AND_VINE_PROP },
    { 0, 191,           0, "dark_oak_fence", FENCE_AND_VINE_PROP },
    { 0, 192,           0, "acacia_fence", FENCE_AND_VINE_PROP },
    { 0, 107,           0, "oak_fence_gate", FENCE_GATE_PROP },
    { 0, 183,           0, "spruce_fence_gate", FENCE_GATE_PROP },
    { 0, 184,           0, "birch_fence_gate", FENCE_GATE_PROP },
    { 0, 185,           0, "jungle_fence_gate", FENCE_GATE_PROP },
    { 0, 186,           0, "dark_oak_fence_gate", FENCE_GATE_PROP },
    { 0, 187,           0, "acacia_fence_gate", FENCE_GATE_PROP },
    { 0, 104,           0, "pumpkin_stem", AGE_PROP },
    { 0, 104,     0x8 | 7, "attached_pumpkin_stem", HIGH_FACING_PROP }, // 0x8 means attached
    { 0,  86,           4, "pumpkin", NO_PROP }, //  uncarved pumpkin, same on all sides - dataVal 4
    { 0,  86,           0, "carved_pumpkin", SWNE_FACING_PROP },	// black carved pumpkin
    { 0,  91,           0, "jack_o_lantern", SWNE_FACING_PROP },
    { 0,  87,           0, "netherrack", NO_PROP },
    { 0, BLOCK_SOUL_SAND, 0, "soul_sand", NO_PROP },
    { 0,  89,           0, "glowstone", NO_PROP },
    { 0,  90,           0, "nether_portal", NETHER_PORTAL_AXIS_PROP }, // axis: portal's long edge runs east-west or north-south
    { 0,  35,           0, "white_wool", NO_PROP },
    { 0,  35,           1, "orange_wool", NO_PROP },
    { 0,  35,           2, "magenta_wool", NO_PROP },
    { 0,  35,           3, "light_blue_wool", NO_PROP },
    { 0,  35,           4, "yellow_wool", NO_PROP },
    { 0,  35,           5, "lime_wool", NO_PROP },
    { 0,  35,           6, "pink_wool", NO_PROP },
    { 0,  35,           7, "gray_wool", NO_PROP },
    { 0,  35,           8, "light_gray_wool", NO_PROP },
    { 0,  35,           9, "cyan_wool", NO_PROP },
    { 0,  35,          10, "purple_wool", NO_PROP },
    { 0,  35,          11, "blue_wool", NO_PROP },
    { 0,  35,          12, "brown_wool", NO_PROP },
    { 0,  35,          13, "green_wool", NO_PROP },
    { 0,  35,          14, "red_wool", NO_PROP },
    { 0,  35,          15, "black_wool", NO_PROP },
    { 0,  21,           0, "lapis_ore", NO_PROP },
    { 0,  22,           0, "lapis_block", NO_PROP },
    { 0,  23,           0, "dispenser", DROPPER_PROP },
    { 0, 158,           0, "dropper", DROPPER_PROP },
    { 0,  25,           0, "note_block", NO_PROP },	// pitch, powered, instrument - ignored
    { 0,  92,           0, "cake", CANDLE_CAKE_PROP },
    { 0,  26,           0, "bed", BED_PROP },
    { 0,  96,           0, "oak_trapdoor", TRAPDOOR_PROP },
    { 0, 167,           0, "iron_trapdoor", TRAPDOOR_PROP },
    { 0,  30,           0, "cobweb", NO_PROP },
    { 0,  98,           0, "stone_bricks", NO_PROP },
    { 0,  98,           1, "mossy_stone_bricks", NO_PROP },
    { 0,  98,           2, "cracked_stone_bricks", NO_PROP },
    { 0,  98,           3, "chiseled_stone_bricks", NO_PROP },
    { 0,  97,           0, "infested_stone", NO_PROP }, // was called "monster egg"
    { 0,  97,           1, "infested_cobblestone", NO_PROP },
    { 0,  97,           2, "infested_stone_bricks", NO_PROP },
    { 0,  97,           3, "infested_mossy_stone_bricks", NO_PROP },
    { 0,  97,           4, "infested_cracked_stone_bricks", NO_PROP },
    { 0,  97,           5, "infested_chiseled_stone_bricks", NO_PROP },
    { 0,  33,           0, "piston", PISTON_PROP },
    { 0,  29,           0, "sticky_piston", PISTON_PROP },
    { 0, 101,           0, "iron_bars", FENCE_AND_VINE_PROP },
    { 0, 103,           0, "melon", NO_PROP },
    { 0, 108,           0, "brick_stairs", STAIRS_PROP },
    { 0, 109,           0, "stone_brick_stairs", STAIRS_PROP },
    { 0, 106,           0, "vine", FENCE_AND_VINE_PROP },
    { 0, 112,           0, "nether_bricks", NO_PROP },
    { 0, 113,           0, "nether_brick_fence", FENCE_AND_VINE_PROP },
    { 0, 114,           0, "nether_brick_stairs", STAIRS_PROP },
    { 0, 115,           0, "nether_wart", AGE_PROP },
    { 0, 118,           0, "cauldron", NO_PROP }, // level directly translates to dataVal, bottom two bits
    { 0, 116,           0, "enchanting_table", NO_PROP },
    { 0, 145,           0, "anvil", ANVIL_PROP },
    { 0, 145,           4, "chipped_anvil", ANVIL_PROP },
    { 0, 145,           8, "damaged_anvil", ANVIL_PROP },
    { 0, 121,           0, "end_stone", NO_PROP },
    { 0, 120,           0, "end_portal_frame", END_PORTAL_PROP },
    { 0, 110,           0, "mycelium", SNOWY_PROP },
    { 0, 111,           0, "lily_pad", NO_PROP },
    { 0, 122,           0, "dragon_egg", NO_PROP },
    { 0, 123,           0, "redstone_lamp", REDSTONE_ORE_PROP }, // goes to 124 when lit
    { 0, 127,           0, "cocoa", COCOA_PROP },
    { 0, 130,           0, "ender_chest", FACING_PROP }, // note that ender chest does not have "single" property that normal chests have; can be waterlogged
    { 0, 129,           0, "emerald_ore", NO_PROP },
    { 0, 133,           0, "emerald_block", NO_PROP },
    { 0, 152,           0, "redstone_block", NO_PROP },
    { 0, 132,           0, "tripwire", TRIPWIRE_PROP },
    { 0, 131,           0, "tripwire_hook", TRIPWIRE_HOOK_PROP },
    { 0, 137,           0, "command_block", COMMAND_BLOCK_PROP },
    { 0, 210,           0, "repeating_command_block", COMMAND_BLOCK_PROP },
    { 0, 211,           0, "chain_command_block", COMMAND_BLOCK_PROP },
    { 0, 138,           0, "beacon", NO_PROP },
    { 0, 139,           0, "cobblestone_wall", WALL_PROP },
    { 0, 139,           1, "mossy_cobblestone_wall", WALL_PROP },
    { 0, 141,           0, "carrots", NO_PROP },
    { 0, 142,           0, "potatoes", NO_PROP },
    { 0, 151,           0, "daylight_detector", DAYLIGHT_PROP },
    { 0, 153,           0, "nether_quartz_ore", NO_PROP },
    { 0, 154,           0, "hopper", HOPPER_PROP },
    { 0, 155,           0, "quartz_block", NO_PROP },	// has AXIS_PROP in Bedrock edition, but not here, https://minecraft.gamepedia.com/Block_of_Quartz
    { 0, 155,           1, "chiseled_quartz_block", NO_PROP },	// has AXIS_PROP in Bedrock edition, but not here, https://minecraft.gamepedia.com/Block_of_Quartz
    { 0, 155,           0, "quartz_pillar", QUARTZ_PILLAR_PROP },	// note this always has an axis, so will be set to 2,3,4
    { 0, 155,           5, "quartz_bricks", NO_PROP },
    { 0, 156,           0, "quartz_stairs", STAIRS_PROP },
    { 0, 165,           0, "slime_block", NO_PROP },
    { 0, 168,           0, "prismarine", NO_PROP },
    { 0, 168,           1, "prismarine_bricks", NO_PROP },
    { 0, 168,           2, "dark_prismarine", NO_PROP },
    { 0, 169,           0, "sea_lantern", NO_PROP },
    { 0, 198,           0, "end_rod", EXTENDED_FACING_PROP },
    { 0, 199,           0, "chorus_plant", FENCE_AND_VINE_PROP },
    { 0, 200,           0, "chorus_flower", NO_PROP },	// uses age
    { 0, 201,           0, "purpur_block", NO_PROP },
    { 0, 202,           0, "purpur_pillar", AXIS_PROP },
    { 0, 203,           0, "purpur_stairs", STAIRS_PROP },
    { 0, 205,           0, "purpur_slab", SLAB_PROP },	// allegedly data value is 1
    { 0, 206,           0, "end_stone_bricks", NO_PROP },
    { 0, 207,           0, "beetroots", AGE_PROP },
    { 0, 208,           0, "grass_path", NO_PROP },
    { 0, 213,           0, "magma_block", NO_PROP },
    { 0, 214,           0, "nether_wart_block", NO_PROP },
    { 0, 215,           0, "red_nether_bricks", NO_PROP },
    { 0, 216,           0, "bone_block", AXIS_PROP },
    { 0, 218,           0, "observer", OBSERVER_PROP },
    { 0, 229,           0, "shulker_box", EXTENDED_FACING_PROP },	// it's a pale purple one, but we just use the purple one TODO
    { 0, 219,           0, "white_shulker_box", EXTENDED_FACING_PROP },
    { 0, 220,           0, "orange_shulker_box", EXTENDED_FACING_PROP },
    { 0, 221,           0, "magenta_shulker_box", EXTENDED_FACING_PROP },
    { 0, 222,           0, "light_blue_shulker_box", EXTENDED_FACING_PROP },
    { 0, 223,           0, "yellow_shulker_box", EXTENDED_FACING_PROP },
    { 0, 224,           0, "lime_shulker_box", EXTENDED_FACING_PROP },
    { 0, 225,           0, "pink_shulker_box", EXTENDED_FACING_PROP },
    { 0, 226,           0, "gray_shulker_box", EXTENDED_FACING_PROP },
    { 0, 227,           0, "light_gray_shulker_box", EXTENDED_FACING_PROP },
    { 0, 228,           0, "cyan_shulker_box", EXTENDED_FACING_PROP },
    { 0, 229,           0, "purple_shulker_box", EXTENDED_FACING_PROP },
    { 0, 230,           0, "blue_shulker_box", EXTENDED_FACING_PROP },
    { 0, 231,           0, "brown_shulker_box", EXTENDED_FACING_PROP },
    { 0, 232,           0, "green_shulker_box", EXTENDED_FACING_PROP },
    { 0, 233,           0, "red_shulker_box", EXTENDED_FACING_PROP },
    { 0, 234,           0, "black_shulker_box", EXTENDED_FACING_PROP },
    { 0, 235,           0, "white_glazed_terracotta", SWNE_FACING_PROP },
    { 0, 236,           0, "orange_glazed_terracotta", SWNE_FACING_PROP },
    { 0, 237,           0, "magenta_glazed_terracotta", SWNE_FACING_PROP },
    { 0, 238,           0, "light_blue_glazed_terracotta", SWNE_FACING_PROP },
    { 0, 239,           0, "yellow_glazed_terracotta", SWNE_FACING_PROP },
    { 0, 240,           0, "lime_glazed_terracotta", SWNE_FACING_PROP },
    { 0, 241,           0, "pink_glazed_terracotta", SWNE_FACING_PROP },
    { 0, 242,           0, "gray_glazed_terracotta", SWNE_FACING_PROP },
    { 0, 243,           0, "light_gray_glazed_terracotta", SWNE_FACING_PROP },
    { 0, 244,           0, "cyan_glazed_terracotta", SWNE_FACING_PROP },
    { 0, 245,           0, "purple_glazed_terracotta", SWNE_FACING_PROP },
    { 0, 246,           0, "blue_glazed_terracotta", SWNE_FACING_PROP },
    { 0, 247,           0, "brown_glazed_terracotta", SWNE_FACING_PROP },
    { 0, 248,           0, "green_glazed_terracotta", SWNE_FACING_PROP },
    { 0, 249,           0, "red_glazed_terracotta", SWNE_FACING_PROP },
    { 0, 250,           0, "black_glazed_terracotta", SWNE_FACING_PROP },
    { 0, 251,          15, "black_concrete", NO_PROP },
    { 0, 251,          14, "red_concrete", NO_PROP },
    { 0, 251,          13, "green_concrete", NO_PROP },
    { 0, 251,          12, "brown_concrete", NO_PROP },
    { 0, 251,          11, "blue_concrete", NO_PROP },
    { 0, 251,          10, "purple_concrete", NO_PROP },
    { 0, 251,           9, "cyan_concrete", NO_PROP },
    { 0, 251,           8, "light_gray_concrete", NO_PROP },
    { 0, 251,           7, "gray_concrete", NO_PROP },
    { 0, 251,           6, "pink_concrete", NO_PROP },
    { 0, 251,           5, "lime_concrete", NO_PROP },
    { 0, 251,           4, "yellow_concrete", NO_PROP },
    { 0, 251,           3, "light_blue_concrete", NO_PROP },
    { 0, 251,           2, "magenta_concrete", NO_PROP },
    { 0, 251,           1, "orange_concrete", NO_PROP },
    { 0, 251,           0, "white_concrete", NO_PROP },
    { 0, 252,          15, "black_concrete_powder", NO_PROP },
    { 0, 252,          14, "red_concrete_powder", NO_PROP },
    { 0, 252,          13, "green_concrete_powder", NO_PROP },
    { 0, 252,          12, "brown_concrete_powder", NO_PROP },
    { 0, 252,          11, "blue_concrete_powder", NO_PROP },
    { 0, 252,          10, "purple_concrete_powder", NO_PROP },
    { 0, 252,           9, "cyan_concrete_powder", NO_PROP },
    { 0, 252,           8, "light_gray_concrete_powder", NO_PROP },
    { 0, 252,           7, "gray_concrete_powder", NO_PROP },
    { 0, 252,           6, "pink_concrete_powder", NO_PROP },
    { 0, 252,           5, "lime_concrete_powder", NO_PROP },
    { 0, 252,           4, "yellow_concrete_powder", NO_PROP },
    { 0, 252,           3, "light_blue_concrete_powder", NO_PROP },
    { 0, 252,           2, "magenta_concrete_powder", NO_PROP },
    { 0, 252,           1, "orange_concrete_powder", NO_PROP },
    { 0, 252,           0, "white_concrete_powder", NO_PROP },
    { 0,  34,           0, "piston_head", PISTON_HEAD_PROP },
    { 0,  34,           0, "moving_piston", PISTON_HEAD_PROP },	// not 100% sure that's what this is...
    { 0,  40,           0, "red_mushroom", NO_PROP },
    { 0,  80,           0, "snow_block", NO_PROP },
    { 0, 105,     0x8 | 7, "attached_melon_stem", HIGH_FACING_PROP },
    { 0, 105,           0, "melon_stem", AGE_PROP },
    { 0, 117,           0, "brewing_stand", NO_PROP },	// see has_bottle_0
    { 0, 119,           0, "end_portal", NO_PROP },
    { 0, BLOCK_FLOWER_POT,                        0, "flower_pot", NO_PROP },
    { 0, BLOCK_FLOWER_POT,        SAPLING_FIELD | 0, "potted_oak_sapling", NO_PROP },
    { 0, BLOCK_FLOWER_POT,        SAPLING_FIELD | 1, "potted_spruce_sapling", NO_PROP },
    { 0, BLOCK_FLOWER_POT,        SAPLING_FIELD | 2, "potted_birch_sapling", NO_PROP },
    { 0, BLOCK_FLOWER_POT,        SAPLING_FIELD | 3, "potted_jungle_sapling", NO_PROP },
    { 0, BLOCK_FLOWER_POT,        SAPLING_FIELD | 4, "potted_acacia_sapling", NO_PROP },
    { 0, BLOCK_FLOWER_POT,        SAPLING_FIELD | 5, "potted_dark_oak_sapling", NO_PROP },
    { 0, BLOCK_FLOWER_POT,      TALLGRASS_FIELD | 2, "potted_fern", NO_PROP },
    { 0, BLOCK_FLOWER_POT,  YELLOW_FLOWER_FIELD | 0, "potted_dandelion", NO_PROP },
    { 0, BLOCK_FLOWER_POT,     RED_FLOWER_FIELD | 0, "potted_poppy", NO_PROP },
    { 0, BLOCK_FLOWER_POT,     RED_FLOWER_FIELD | 1, "potted_blue_orchid", NO_PROP },
    { 0, BLOCK_FLOWER_POT,     RED_FLOWER_FIELD | 2, "potted_allium", NO_PROP },
    { 0, BLOCK_FLOWER_POT,     RED_FLOWER_FIELD | 3, "potted_azure_bluet", NO_PROP },
    { 0, BLOCK_FLOWER_POT,     RED_FLOWER_FIELD | 4, "potted_red_tulip", NO_PROP },
    { 0, BLOCK_FLOWER_POT,     RED_FLOWER_FIELD | 5, "potted_orange_tulip", NO_PROP },
    { 0, BLOCK_FLOWER_POT,     RED_FLOWER_FIELD | 6, "potted_white_tulip", NO_PROP },
    { 0, BLOCK_FLOWER_POT,     RED_FLOWER_FIELD | 7, "potted_pink_tulip", NO_PROP },
    { 0, BLOCK_FLOWER_POT,     RED_FLOWER_FIELD | 8, "potted_oxeye_daisy", NO_PROP },
    { 0, BLOCK_FLOWER_POT,   RED_MUSHROOM_FIELD | 0, "potted_red_mushroom", NO_PROP },
    { 0, BLOCK_FLOWER_POT, BROWN_MUSHROOM_FIELD | 0, "potted_brown_mushroom", NO_PROP },
    { 0, BLOCK_FLOWER_POT,       DEADBUSH_FIELD | 0, "potted_dead_bush", NO_PROP },
    { 0, BLOCK_FLOWER_POT,         CACTUS_FIELD | 0, "potted_cactus", NO_PROP },
    { 0, 144,           0, "skeleton_wall_skull", HEAD_WALL_PROP },
    { 0, 144,    0x80 | 0, "skeleton_skull", HEAD_PROP },
    { 0, 144,        1 << 4, "wither_skeleton_wall_skull", HEAD_WALL_PROP },
    { 0, 144, 0x80 | 1 << 4, "wither_skeleton_skull", HEAD_PROP },
    { 0, 144,        2 << 4, "zombie_wall_head", HEAD_WALL_PROP },
    { 0, 144, 0x80 | 2 << 4, "zombie_head", HEAD_PROP },
    { 0, 144,        3 << 4, "player_wall_head", HEAD_WALL_PROP },
    { 0, 144, 0x80 | 3 << 4, "player_head", HEAD_PROP },
    { 0, 144,        4 << 4, "creeper_wall_head", HEAD_WALL_PROP },
    { 0, 144, 0x80 | 4 << 4, "creeper_head", HEAD_PROP },
    { 0, 144,        5 << 4, "dragon_wall_head", HEAD_WALL_PROP },
    { 0, 144, 0x80 | 5 << 4, "dragon_head", HEAD_PROP },
    { 0, 209,           0, "end_gateway", NO_PROP },
    { 0, 217,           0, "structure_void", NO_PROP },
    { 0, 255,           0, "structure_block", STRUCTURE_PROP },
    // new 1.13 on down
    { 0,   0,           0, "void_air", NO_PROP },	// consider these air until proven otherwise https://minecraft.gamepedia.com/Air
    { 0,   0,           0, "cave_air", NO_PROP },	// consider these air until proven otherwise https://minecraft.gamepedia.com/Air
    { 0, 205,           2, "prismarine_slab", SLAB_PROP }, // added to purpur slab and double slab, dataVal 2, just to be safe (see purpur_slab)
    { 0, 205,           3, "prismarine_brick_slab", SLAB_PROP }, // added to purpur slab and double slab, dataVal 3
    { 0, 205,           4, "dark_prismarine_slab", SLAB_PROP }, // added to purpur slab and double slab, dataVal 4
    { 0,   1,    HIGH_BIT, "prismarine_stairs", STAIRS_PROP },
    { 0,   2,    HIGH_BIT, "prismarine_brick_stairs", STAIRS_PROP },
    { 0,   3,    HIGH_BIT, "dark_prismarine_stairs", STAIRS_PROP },
    { 0,   4,    HIGH_BIT, "spruce_trapdoor", TRAPDOOR_PROP },
    { 0,   5,    HIGH_BIT, "birch_trapdoor", TRAPDOOR_PROP },
    { 0,   6,    HIGH_BIT, "jungle_trapdoor", TRAPDOOR_PROP },
    { 0,   7,    HIGH_BIT, "acacia_trapdoor", TRAPDOOR_PROP },
    { 0,   8,    HIGH_BIT, "dark_oak_trapdoor", TRAPDOOR_PROP },
    { 0,   9,    HIGH_BIT, "spruce_button", BUTTON_PROP },	// TODO++
    { 0,  10,    HIGH_BIT, "birch_button", BUTTON_PROP },
    { 0,  11,    HIGH_BIT, "jungle_button", BUTTON_PROP },
    { 0,  12,    HIGH_BIT, "acacia_button", BUTTON_PROP },
    { 0,  13,    HIGH_BIT, "dark_oak_button", BUTTON_PROP },
    { 0,  14,    HIGH_BIT, "spruce_pressure_plate", PRESSURE_PROP }, // could stuff these into material bits, but KISS
    { 0,  15,    HIGH_BIT, "birch_pressure_plate", PRESSURE_PROP },
    { 0,  16,    HIGH_BIT, "jungle_pressure_plate", PRESSURE_PROP },
    { 0,  17,    HIGH_BIT, "acacia_pressure_plate", PRESSURE_PROP },
    { 0,  18,    HIGH_BIT, "dark_oak_pressure_plate", PRESSURE_PROP },
    { 0,  19,  HIGH_BIT | 0, "stripped_oak_log", AXIS_PROP },
    { 0,  19,  HIGH_BIT | 1, "stripped_spruce_log", AXIS_PROP },
    { 0,  19,  HIGH_BIT | 2, "stripped_birch_log", AXIS_PROP },
    { 0,  19,  HIGH_BIT | 3, "stripped_jungle_log", AXIS_PROP },
    { 0,  20,  HIGH_BIT | 0, "stripped_acacia_log", AXIS_PROP },
    { 0,  20,  HIGH_BIT | 1, "stripped_dark_oak_log", AXIS_PROP },
    { 0,  21,  HIGH_BIT | 0, "stripped_oak_wood", AXIS_PROP },
    { 0,  21,  HIGH_BIT | 1, "stripped_spruce_wood", AXIS_PROP },
    { 0,  21,  HIGH_BIT | 2, "stripped_birch_wood", AXIS_PROP },
    { 0,  21,  HIGH_BIT | 3, "stripped_jungle_wood", AXIS_PROP },
    { 0,  22,  HIGH_BIT | 0, "stripped_acacia_wood", AXIS_PROP },
    { 0,  22,  HIGH_BIT | 1, "stripped_dark_oak_wood", AXIS_PROP },
    { 0, 176,           0, "white_banner", STANDING_SIGN_PROP },
    { 0,  23,    HIGH_BIT, "orange_banner", STANDING_SIGN_PROP },	// we could crush these a bit into four banners per entry by using bits 32 and 64 for different types.
    { 0,  24,    HIGH_BIT, "magenta_banner", STANDING_SIGN_PROP },
    { 0,  25,    HIGH_BIT, "light_blue_banner", STANDING_SIGN_PROP },
    { 0,  26,    HIGH_BIT, "yellow_banner", STANDING_SIGN_PROP },
    { 0,  27,    HIGH_BIT, "lime_banner", STANDING_SIGN_PROP },
    { 0,  28,    HIGH_BIT, "pink_banner", STANDING_SIGN_PROP },
    { 0,  29,    HIGH_BIT, "gray_banner", STANDING_SIGN_PROP },
    { 0,  30,    HIGH_BIT, "light_gray_banner", STANDING_SIGN_PROP },
    { 0,  31,    HIGH_BIT, "cyan_banner", STANDING_SIGN_PROP },
    { 0,  32,    HIGH_BIT, "purple_banner", STANDING_SIGN_PROP },
    { 0,  33,    HIGH_BIT, "blue_banner", STANDING_SIGN_PROP },
    { 0,  34,    HIGH_BIT, "brown_banner", STANDING_SIGN_PROP },
    { 0,  35,    HIGH_BIT, "green_banner", STANDING_SIGN_PROP },
    { 0,  36,    HIGH_BIT, "red_banner", STANDING_SIGN_PROP },
    { 0,  37,    HIGH_BIT, "black_banner", STANDING_SIGN_PROP }, // TODO++ colors need to be added
    { 0, 177,           0, "white_wall_banner", FACING_PROP },
    { 0,  38,    HIGH_BIT, "orange_wall_banner", FACING_PROP },
    { 0,  39,    HIGH_BIT, "magenta_wall_banner", FACING_PROP },
    { 0,  40,    HIGH_BIT, "light_blue_wall_banner", FACING_PROP },
    { 0,  41,    HIGH_BIT, "yellow_wall_banner", FACING_PROP },
    { 0,  42,    HIGH_BIT, "lime_wall_banner", FACING_PROP },
    { 0,  43,    HIGH_BIT, "pink_wall_banner", FACING_PROP },
    { 0,  44,    HIGH_BIT, "gray_wall_banner", FACING_PROP },
    { 0,  45,    HIGH_BIT, "light_gray_wall_banner", FACING_PROP },
    { 0,  46,    HIGH_BIT, "cyan_wall_banner", FACING_PROP },
    { 0,  47,    HIGH_BIT, "purple_wall_banner", FACING_PROP },
    { 0,  48,    HIGH_BIT, "blue_wall_banner", FACING_PROP },
    { 0,  49,    HIGH_BIT, "brown_wall_banner", FACING_PROP },
    { 0,  50,    HIGH_BIT, "green_wall_banner", FACING_PROP },
    { 0,  51,    HIGH_BIT, "red_wall_banner", FACING_PROP },
    { 0,  52,    HIGH_BIT, "black_wall_banner", FACING_PROP },
    { 0,  53,    HIGH_BIT, "tall_seagrass", TALL_FLOWER_PROP },
    { 0,  54,    HIGH_BIT, "seagrass", NO_PROP },
    { 0,  55,  HIGH_BIT | 0, "smooth_stone", NO_PROP },
    { 0,  55,  HIGH_BIT | 1, "smooth_sandstone", NO_PROP },
    { 0,  55,  HIGH_BIT | 2, "smooth_red_sandstone", NO_PROP },
    { 0,  55,  HIGH_BIT | 3, "smooth_quartz", NO_PROP },
    { 0,  56,    HIGH_BIT, "blue_ice", NO_PROP },
    { 0,  57,    HIGH_BIT, "dried_kelp_block", NO_PROP },
    { 0,  58,  HIGH_BIT | 0, "kelp_plant", TRULY_NO_PROP }, // the lower part
    { 0,  58,  HIGH_BIT | 1, "kelp", TRULY_NO_PROP }, // the top, growing part; don't care about the age
    { 0,   9,      BIT_16, "bubble_column", 0x0 },	// consider as full block of water for now, need to investigate if there's anything to static render (I don't think so...?)
    { 0,  59,  HIGH_BIT | 0, "tube_coral_block", NO_PROP },
    { 0,  59,  HIGH_BIT | 1, "brain_coral_block", NO_PROP },
    { 0,  59,  HIGH_BIT | 2, "bubble_coral_block", NO_PROP },
    { 0,  59,  HIGH_BIT | 3, "fire_coral_block", NO_PROP },
    { 0,  59,  HIGH_BIT | 4, "horn_coral_block", NO_PROP },
    { 0,  60,  HIGH_BIT | 0, "dead_tube_coral_block", NO_PROP },
    { 0,  60,  HIGH_BIT | 1, "dead_brain_coral_block", NO_PROP },
    { 0,  60,  HIGH_BIT | 2, "dead_bubble_coral_block", NO_PROP },
    { 0,  60,  HIGH_BIT | 3, "dead_fire_coral_block", NO_PROP },
    { 0,  60,  HIGH_BIT | 4, "dead_horn_coral_block", NO_PROP },
    { 0,  61,  HIGH_BIT | 0, "tube_coral", NO_PROP },
    { 0,  61,  HIGH_BIT | 1, "brain_coral", NO_PROP },
    { 0,  61,  HIGH_BIT | 2, "bubble_coral", NO_PROP },
    { 0,  61,  HIGH_BIT | 3, "fire_coral", NO_PROP },
    { 0,  61,  HIGH_BIT | 4, "horn_coral", NO_PROP },
    { 0,  62,  HIGH_BIT | 0, "tube_coral_fan", NO_PROP },	// here's where we go nuts: using 7 bits (one waterlogged)
    { 0,  62,  HIGH_BIT | 1, "brain_coral_fan", NO_PROP },
    { 0,  62,  HIGH_BIT | 2, "bubble_coral_fan", NO_PROP },
    { 0,  62,  HIGH_BIT | 3, "fire_coral_fan", NO_PROP },
    { 0,  62,  HIGH_BIT | 4, "horn_coral_fan", NO_PROP },
    { 0,  63,  HIGH_BIT | 0, "dead_tube_coral_fan", NO_PROP },
    { 0,  63,  HIGH_BIT | 1, "dead_brain_coral_fan", NO_PROP },
    { 0,  63,  HIGH_BIT | 2, "dead_bubble_coral_fan", NO_PROP },
    { 0,  63,  HIGH_BIT | 3, "dead_fire_coral_fan", NO_PROP },
    { 0,  63,  HIGH_BIT | 4, "dead_horn_coral_fan", NO_PROP },
    { 0,  64,  HIGH_BIT | 0, "tube_coral_wall_fan", FAN_PROP },
    { 0,  64,  HIGH_BIT | 1, "brain_coral_wall_fan", FAN_PROP },
    { 0,  64,  HIGH_BIT | 2, "bubble_coral_wall_fan", FAN_PROP },
    { 0,  64,  HIGH_BIT | 3, "fire_coral_wall_fan", FAN_PROP },
    { 0,  64,  HIGH_BIT | 4, "horn_coral_wall_fan", FAN_PROP },
    { 0,  65,  HIGH_BIT | 0, "dead_tube_coral_wall_fan", FAN_PROP },
    { 0,  65,  HIGH_BIT | 1, "dead_brain_coral_wall_fan", FAN_PROP },
    { 0,  65,  HIGH_BIT | 2, "dead_bubble_coral_wall_fan", FAN_PROP },
    { 0,  65,  HIGH_BIT | 3, "dead_fire_coral_wall_fan", FAN_PROP },
    { 0,  65,  HIGH_BIT | 4, "dead_horn_coral_wall_fan", FAN_PROP },
    { 0,  66,    HIGH_BIT, "conduit", NO_PROP },
    { 0,  67,    HIGH_BIT, "sea_pickle", PICKLE_PROP },
    { 0,  68,    HIGH_BIT, "turtle_egg", EGG_PROP },
    { 0,  26,           0, "black_bed", BED_PROP }, // TODO+ bed colors should have separate blocks or whatever
    { 0,  26,           0, "red_bed", BED_PROP },
    { 0,  26,           0, "green_bed", BED_PROP },
    { 0,  26,           0, "brown_bed", BED_PROP },
    { 0,  26,           0, "blue_bed", BED_PROP },
    { 0,  26,           0, "purple_bed", BED_PROP },
    { 0,  26,           0, "cyan_bed", BED_PROP },
    { 0,  26,           0, "light_gray_bed", BED_PROP },
    { 0,  26,           0, "gray_bed", BED_PROP },
    { 0,  26,           0, "pink_bed", BED_PROP },
    { 0,  26,           0, "lime_bed", BED_PROP },
    { 0,  26,           0, "yellow_bed", BED_PROP },
    { 0,  26,           0, "light_blue_bed", BED_PROP },
    { 0,  26,           0, "magenta_bed", BED_PROP },
    { 0,  26,           0, "orange_bed", BED_PROP },
    { 0,  26,           0, "white_bed", BED_PROP },

    // 1.14
    { 0,  69,  HIGH_BIT | 0, "dead_tube_coral", NO_PROP },
    { 0,  69,  HIGH_BIT | 1, "dead_brain_coral", NO_PROP },
    { 0,  69,  HIGH_BIT | 2, "dead_bubble_coral", NO_PROP },
    { 0,  69,  HIGH_BIT | 3, "dead_fire_coral", NO_PROP },
    { 0,  69,  HIGH_BIT | 4, "dead_horn_coral", NO_PROP },
    { 0,  63,           0, "oak_sign", STANDING_SIGN_PROP }, // in 1.14 it's no longer just "sign", it's oak_sign, acacia_sign, etc. - use bits 16, 32, 64 for the 6 types
    { 0,  63,      BIT_16, "spruce_sign", STANDING_SIGN_PROP },
    { 0,  63,      BIT_32, "birch_sign", STANDING_SIGN_PROP },
    { 0,  63,BIT_32 | BIT_16, "jungle_sign", STANDING_SIGN_PROP },
    { 0,  70,      HIGH_BIT, "acacia_sign", STANDING_SIGN_PROP },
    { 0,  70,HIGH_BIT | BIT_16, "dark_oak_sign", STANDING_SIGN_PROP },
    { 0,  68,           0, "oak_wall_sign", WALL_SIGN_PROP }, // in 1.14 it's oak_wall_sign, acacia_wall_sign, etc.
    { 0,  68,       BIT_8, "spruce_wall_sign", WALL_SIGN_PROP },
    { 0,  68,      BIT_16, "birch_wall_sign", WALL_SIGN_PROP },
    { 0,  68,BIT_16 | BIT_8, "jungle_wall_sign", WALL_SIGN_PROP },
    { 0,  68,      BIT_32, "acacia_wall_sign", WALL_SIGN_PROP },
    { 0,  68,BIT_32 | BIT_8, "dark_oak_wall_sign", WALL_SIGN_PROP },
    { 0,  38,           9, "cornflower", NO_PROP },
    { 0,  38,          10, "lily_of_the_valley", NO_PROP },
    { 0,  38,          11, "wither_rose", NO_PROP },
    { 0,  71,    HIGH_BIT, "sweet_berry_bush", AGE_PROP },
    { 0, BLOCK_FLOWER_POT,     RED_FLOWER_FIELD | 9, "potted_cornflower", NO_PROP },
    { 0, BLOCK_FLOWER_POT,     RED_FLOWER_FIELD | 10, "potted_lily_of_the_valley", NO_PROP },
    { 0, BLOCK_FLOWER_POT,     RED_FLOWER_FIELD | 11, "potted_wither_rose", NO_PROP },
    { 0, BLOCK_FLOWER_POT,         BAMBOO_FIELD | 0, "potted_bamboo", NO_PROP },
    { 0,   6,	           6, "bamboo_sapling", SAPLING_PROP },	// put with the other saplings
    { 0,  72,	    HIGH_BIT, "bamboo", LEAF_SIZE_PROP },
    { 0, 182,	           1, "cut_red_sandstone_slab", SLAB_PROP }, // added to red_sandstone_slab and double slab
    { 0, 182,	           2, "smooth_red_sandstone_slab", SLAB_PROP },
    { 0, 182,	           3, "cut_sandstone_slab", SLAB_PROP },
    { 0, 182,	           4, "smooth_sandstone_slab", SLAB_PROP },
    { 0, 182,	           5, "granite_slab", SLAB_PROP },
    { 0, 182,	           6, "polished_granite_slab", SLAB_PROP },
    { 0, 182,	           7, "smooth_quartz_slab", SLAB_PROP },
    { 0, 205,	           5, "red_nether_brick_slab", SLAB_PROP }, // added to purpur slab and double slab, dataVal 4
    { 0, 205,	           6, "mossy_stone_brick_slab", SLAB_PROP },
    { 0, 205,	           7, "mossy_cobblestone_slab", SLAB_PROP },
    { 0,  74,	HIGH_BIT | 0, "andesite_slab", SLAB_PROP },
    { 0,  74,	HIGH_BIT | 1, "polished_andesite_slab", SLAB_PROP },
    { 0,  74,	HIGH_BIT | 2, "diorite_slab", SLAB_PROP },
    { 0,  74,	HIGH_BIT | 3, "polished_diorite_slab", SLAB_PROP },
    { 0,  74,	HIGH_BIT | 4, "end_stone_brick_slab", SLAB_PROP },
    { 0,  74,	HIGH_BIT | 5, "stone_slab", SLAB_PROP },	// the 1.14 stone_slab is entirely "normal" stone, no chiseling - it's a new slab type; 1.13 used this to mean what is now "smooth_stone_slab", and so we rename that in the nbt.cpp code.
    { 0, 109,	    HIGH_BIT, "stone_stairs", STAIRS_PROP },
    { 0, 110,	    HIGH_BIT, "granite_stairs", STAIRS_PROP },
    { 0, 111,       HIGH_BIT, "polished_granite_stairs", STAIRS_PROP },
    { 0, 112,	    HIGH_BIT, "smooth_quartz_stairs", STAIRS_PROP },
    { 0, 113,	    HIGH_BIT, "diorite_stairs", STAIRS_PROP },
    { 0, 114,       HIGH_BIT, "polished_diorite_stairs", STAIRS_PROP },
    { 0, 115,	    HIGH_BIT, "end_stone_brick_stairs", STAIRS_PROP },
    { 0, 116,	    HIGH_BIT, "andesite_stairs", STAIRS_PROP },
    { 0, 117,       HIGH_BIT, "polished_andesite_stairs", STAIRS_PROP },
    { 0, 118,	    HIGH_BIT, "red_nether_brick_stairs", STAIRS_PROP },
    { 0, 119,	    HIGH_BIT, "mossy_stone_brick_stairs", STAIRS_PROP },
    { 0, 120,       HIGH_BIT, "mossy_cobblestone_stairs", STAIRS_PROP },
    { 0, 121,	    HIGH_BIT, "smooth_sandstone_stairs", STAIRS_PROP },
    { 0, 122,	    HIGH_BIT, "smooth_red_sandstone_stairs", STAIRS_PROP },
    { 0, 139,              2, "brick_wall", WALL_PROP },
    { 0, 139,              3, "granite_wall", WALL_PROP },
    { 0, 139,              4, "diorite_wall", WALL_PROP },
    { 0, 139,              5, "andesite_wall", WALL_PROP },
    { 0, 139,              6, "prismarine_wall", WALL_PROP },
    { 0, 139,              7, "stone_brick_wall", WALL_PROP },
    { 0, 139,              8, "mossy_stone_brick_wall", WALL_PROP },
    { 0, 139,              9, "end_stone_brick_wall", WALL_PROP },
    { 0, 139,             10, "nether_brick_wall", WALL_PROP },
    { 0, 139,             11, "red_nether_brick_wall", WALL_PROP },
    { 0, 139,             12, "sandstone_wall", WALL_PROP },
    { 0, 139,             13, "red_sandstone_wall", WALL_PROP },
    { 0,  75,		HIGH_BIT, "jigsaw", EXTENDED_FACING_PROP },
    { 0,  76,       HIGH_BIT, "composter", NO_PROP }, // level directly translates to dataVal
    { 0, BLOCK_FURNACE,	      BIT_16, "loom", FACING_PROP },	// add to furnace and burning furnace
    { 0, BLOCK_FURNACE,	      BIT_32, "smoker", FURNACE_PROP },
    { 0, BLOCK_FURNACE,BIT_32 | BIT_16, "blast_furnace", FURNACE_PROP },
    { 0,  77,       HIGH_BIT, "barrel", BARREL_PROP },
    { 0,  78,       HIGH_BIT, "stonecutter", SWNE_FACING_PROP },	// use just the lower two bits instead of three for facing. S=0, etc.
    { 0, BLOCK_CRAFTING_TABLE,	1, "cartography_table", NO_PROP },
    { 0, BLOCK_CRAFTING_TABLE,	2, "fletching_table", NO_PROP },
    { 0, BLOCK_CRAFTING_TABLE,	3, "smithing_table", NO_PROP },
    { 0,  79,       HIGH_BIT, "grindstone", GRINDSTONE_PROP }, // facing SWNE and face: floor|ceiling|wall
    { 0,  80,       HIGH_BIT, "lectern", LECTERN_PROP },
    { 0,  81,       HIGH_BIT, "bell", BELL_PROP },
    { 0,  82,       HIGH_BIT, "lantern", LANTERN_PROP },	// uses just "hanging" for bit 0x1
    { 0,  83,       HIGH_BIT, "campfire", CAMPFIRE_PROP },
    { 0,  84,       HIGH_BIT, "scaffolding", NO_PROP },	// uses just "bottom" for bit 0x1

    // 1.15
    { 0,  85,       HIGH_BIT, "bee_nest", EXTENDED_SWNE_FACING_PROP },	// facing is 0x3, honey_level is 0x01C, nest/hive is 0x20
    { 0,  85,HIGH_BIT | BIT_32, "beehive", EXTENDED_SWNE_FACING_PROP },
    { 0,  86,       HIGH_BIT, "honey_block", NO_PROP },
    { 0,  87,       HIGH_BIT, "honeycomb_block", NO_PROP },

    // 1.16
    { 0, BLOCK_SOUL_SAND,  1, "soul_soil", NO_PROP },	// with soul sand
    { 0, 214,			   1, "warped_wart_block", NO_PROP },
    { 0, 216,			   1, "basalt", AXIS_PROP },
    { 0, 216,			   2, "polished_basalt", AXIS_PROP },
    { 0,   3,              3, "crimson_nylium", NO_PROP }, // note no SNOWY_PROP
    { 0,   3,              4, "warped_nylium", NO_PROP }, // note no SNOWY_PROP
    { 0,  40,              1, "crimson_fungus", NO_PROP },
    { 0,  40,              2, "warped_fungus", NO_PROP },
    { 0,  31,			   3, "nether_sprouts", NO_PROP },
    { 0,  31,              4, "crimson_roots", NO_PROP },	// We *don't* put these two as types of red poppy, but *do* make them this way when put in a pot.
    { 0,  31,              5, "warped_roots", NO_PROP },	// This is done because the "in the pot" tile is different than the "in the wild" version, so this made it easier. Ugh.
    { 0, BLOCK_FLOWER_POT,     RED_FLOWER_FIELD | 12, "potted_crimson_fungus", NO_PROP },
    { 0, BLOCK_FLOWER_POT,     RED_FLOWER_FIELD | 13, "potted_warped_fungus", NO_PROP },
    { 0, BLOCK_FLOWER_POT,     RED_FLOWER_FIELD | 14, "potted_crimson_roots", NO_PROP },
    { 0, BLOCK_FLOWER_POT,     RED_FLOWER_FIELD | 15, "potted_warped_roots", NO_PROP },
    { 0,  89,              1, "shroomlight", NO_PROP },
    { 0, 162,     BIT_16 | 2, "crimson_hyphae", AXIS_PROP },	// same as logs below, but with a high bit set to mean that it's "wood" texture on the endcaps. 
    { 0, 162,     BIT_16 | 3, "warped_hyphae", AXIS_PROP },	// same as logs below, but with a high bit set to mean that it's "wood" texture on the endcaps. 
    { 0, 162,              2, "crimson_stem", AXIS_PROP },	// log equivalent
    { 0, 162,              3, "warped_stem", AXIS_PROP },
    { 0,  20,   HIGH_BIT | 2, "stripped_crimson_stem", AXIS_PROP },	// extension of stripped acacia (log)
    { 0,  20,   HIGH_BIT | 3, "stripped_warped_stem", AXIS_PROP },
    { 0,  22,   HIGH_BIT | 2, "stripped_crimson_hyphae", AXIS_PROP },	// extension of stripped acacia wood
    { 0,  22,   HIGH_BIT | 3, "stripped_warped_hyphae", AXIS_PROP },
    { 0,   5,              6, "crimson_planks", NO_PROP },
    { 0,   5,              7, "warped_planks", NO_PROP },
    { 0,   1,              7, "blackstone", NO_PROP },
    { 0,   1,              8, "chiseled_polished_blackstone", NO_PROP },
    { 0,   1,              9, "polished_blackstone", NO_PROP },
    { 0,   1,             10, "gilded_blackstone", NO_PROP },
    { 0,   1,             11, "polished_blackstone_bricks", NO_PROP },
    { 0,   1,             12, "cracked_polished_blackstone_bricks", NO_PROP },
    { 0,   1,             13, "netherite_block", NO_PROP },
    { 0,   1,             14, "ancient_debris", NO_PROP },
    { 0,   1,             15, "nether_gold_ore", NO_PROP },
    { 0, 112,              1, "chiseled_nether_bricks", NO_PROP },
    { 0, 112,              2, "cracked_nether_bricks", NO_PROP },
    { 0, BLOCK_CRAFTING_TABLE,	4, "lodestone", NO_PROP },
    { 0,  88,       HIGH_BIT, "crying_obsidian", NO_PROP },
    { 0, BLOCK_TNT,		   1, "target", TRULY_NO_PROP },
    { 0,  89,       HIGH_BIT, "respawn_anchor", NO_PROP },
    { 0, 139,             14, "blackstone_wall", WALL_PROP },
    { 0, 139,             15, "polished_blackstone_wall", WALL_PROP },
    { 0, 139,             16, "polished_blackstone_brick_wall", WALL_PROP },	// yeah, that's right, 16 baby - no data values used for walls, it's all implied in Mineways
    { 0, 123,       HIGH_BIT, "crimson_stairs", STAIRS_PROP },
    { 0, 124,       HIGH_BIT, "warped_stairs", STAIRS_PROP },
    { 0, 125,       HIGH_BIT, "blackstone_stairs", STAIRS_PROP },
    { 0, 126,       HIGH_BIT, "polished_blackstone_stairs", STAIRS_PROP },
    { 0, 127,       HIGH_BIT, "polished_blackstone_brick_stairs", STAIRS_PROP },
    { 0,  90,       HIGH_BIT, "crimson_trapdoor", TRAPDOOR_PROP },
    { 0,  91,       HIGH_BIT, "warped_trapdoor", TRAPDOOR_PROP },
    { 0,  92,       HIGH_BIT, "crimson_button", BUTTON_PROP },
    { 0,  93,       HIGH_BIT, "warped_button", BUTTON_PROP },
    { 0,  94,       HIGH_BIT, "polished_blackstone_button", BUTTON_PROP },
    { 0,  95,       HIGH_BIT, "crimson_fence", FENCE_AND_VINE_PROP },
    { 0,  96,       HIGH_BIT, "warped_fence", FENCE_AND_VINE_PROP },
    { 0,  97,       HIGH_BIT, "crimson_fence_gate", FENCE_GATE_PROP },
    { 0,  98,       HIGH_BIT, "warped_fence_gate", FENCE_GATE_PROP },
    { 0,  99,       HIGH_BIT, "crimson_door", DOOR_PROP },
    { 0, 100,       HIGH_BIT, "warped_door", DOOR_PROP },
    { 0, 101,       HIGH_BIT, "crimson_pressure_plate", PRESSURE_PROP },
    { 0, 102,       HIGH_BIT, "warped_pressure_plate", PRESSURE_PROP },
    { 0, 103,       HIGH_BIT, "polished_blackstone_pressure_plate", PRESSURE_PROP },
    { 0, 105,       HIGH_BIT, "crimson_slab", SLAB_PROP },	// new set of slabs - note that 104 is used by the corresponding double slabs
    { 0, 105,   HIGH_BIT | 1, "warped_slab", SLAB_PROP },
    { 0, 105,   HIGH_BIT | 2, "blackstone_slab", SLAB_PROP },
    { 0, 105,   HIGH_BIT | 3, "polished_blackstone_slab", SLAB_PROP },
    { 0, 105,   HIGH_BIT | 4, "polished_blackstone_brick_slab", SLAB_PROP },
    { 0,  70, HIGH_BIT | BIT_32, "crimson_sign", STANDING_SIGN_PROP },
    { 0,  70, HIGH_BIT | BIT_32 | BIT_16, "warped_sign", STANDING_SIGN_PROP },
    { 0,  68, BIT_32 | BIT_16, "crimson_wall_sign", WALL_SIGN_PROP },
    { 0,  68, BIT_32 | BIT_16 | BIT_8, "warped_wall_sign", WALL_SIGN_PROP },
    { 0, BLOCK_FIRE,  BIT_16, "soul_fire", NO_PROP },
    { 0, 106,       HIGH_BIT, "soul_torch", TORCH_PROP },	// was soul_fire_torch in an earlier 1.16 beta, like 16
    { 0, 106,       HIGH_BIT, "soul_wall_torch", TORCH_PROP },	// was soul_fire_torch in an earlier 1.16 beta, like 16
    { 0,  82, HIGH_BIT | 0x2, "soul_lantern", LANTERN_PROP },	// uses just "hanging" for bit 0x1
    { 0,  83, HIGH_BIT | 0x8, "soul_campfire", CAMPFIRE_PROP },
    { 0, 107,       HIGH_BIT, "weeping_vines_plant", TRULY_NO_PROP },
    { 0, 107, HIGH_BIT | BIT_32, "weeping_vines", TRULY_NO_PROP },
    { 0, 107,       HIGH_BIT | 1, "twisting_vines_plant", TRULY_NO_PROP },
    { 0, 107, HIGH_BIT | BIT_32 | 1, "twisting_vines", TRULY_NO_PROP },
    { 0, 108,       HIGH_BIT, "chain", AXIS_PROP },

    // 1.17
    { 0, 128,       HIGH_BIT, "candle", CANDLE_PROP },  // 129 is lit
    { 0, 130,   HIGH_BIT |  0, "white_candle", CANDLE_PROP }, // 131 is lit
    { 0, 130,   HIGH_BIT |  1, "orange_candle", CANDLE_PROP },
    { 0, 130,   HIGH_BIT |  2, "magenta_candle", CANDLE_PROP },
    { 0, 130,   HIGH_BIT |  3, "light_blue_candle", CANDLE_PROP },
    { 0, 130,   HIGH_BIT |  4, "yellow_candle", CANDLE_PROP },
    { 0, 130,   HIGH_BIT |  5, "lime_candle", CANDLE_PROP },
    { 0, 130,   HIGH_BIT |  6, "pink_candle", CANDLE_PROP },
    { 0, 130,   HIGH_BIT |  7, "gray_candle", CANDLE_PROP },
    { 0, 130,   HIGH_BIT |  8, "light_gray_candle", CANDLE_PROP },
    { 0, 130,   HIGH_BIT |  9, "cyan_candle", CANDLE_PROP },
    { 0, 130,   HIGH_BIT | 10, "purple_candle", CANDLE_PROP },
    { 0, 130,   HIGH_BIT | 11, "blue_candle", CANDLE_PROP },
    { 0, 130,   HIGH_BIT | 12, "brown_candle", CANDLE_PROP },
    { 0, 130,   HIGH_BIT | 13, "green_candle", CANDLE_PROP },
    { 0, 130,   HIGH_BIT | 14, "red_candle", CANDLE_PROP },
    { 0, 130,   HIGH_BIT | 15, "black_candle", CANDLE_PROP },
    { 0,  92,            0x7, "candle_cake", CANDLE_CAKE_PROP },
    { 0,  92,     BIT_16 | 0, "white_candle_cake", CANDLE_CAKE_PROP },  // funky: cake can be either with a single candle, lit or not, OR have a bite taken out of it. 
    { 0,  92,     BIT_16 | 1, "orange_candle_cake", CANDLE_CAKE_PROP },
    { 0,  92,     BIT_16 | 2, "magenta_candle_cake", CANDLE_CAKE_PROP },
    { 0,  92,     BIT_16 | 3, "light_blue_candle_cake", CANDLE_CAKE_PROP },
    { 0,  92,     BIT_16 | 4, "yellow_candle_cake", CANDLE_CAKE_PROP },
    { 0,  92,     BIT_16 | 5, "lime_candle_cake", CANDLE_CAKE_PROP },
    { 0,  92,     BIT_16 | 6, "pink_candle_cake", CANDLE_CAKE_PROP },
    { 0,  92,     BIT_16 | 7, "gray_candle_cake", CANDLE_CAKE_PROP },
    { 0,  92,     BIT_16 | 8, "light_gray_candle_cake", CANDLE_CAKE_PROP },
    { 0,  92,     BIT_16 | 9, "cyan_candle_cake", CANDLE_CAKE_PROP },
    { 0,  92,     BIT_16 | 10, "purple_candle_cake", CANDLE_CAKE_PROP },
    { 0,  92,     BIT_16 | 11, "blue_candle_cake", CANDLE_CAKE_PROP },
    { 0,  92,     BIT_16 | 12, "brown_candle_cake", CANDLE_CAKE_PROP },
    { 0,  92,     BIT_16 | 13, "green_candle_cake", CANDLE_CAKE_PROP },
    { 0,  92,     BIT_16 | 14, "red_candle_cake", CANDLE_CAKE_PROP },
    { 0,  92,     BIT_16 | 15, "black_candle_cake", CANDLE_CAKE_PROP },
    { 0, 132,   HIGH_BIT | 0, "amethyst_block", NO_PROP },
    { 0, 133,   HIGH_BIT | 0, "small_amethyst_bud", AMETHYST_PROP }, // 2 bits for type, 3 bits for direction
    { 0, 133,   HIGH_BIT | 1, "medium_amethyst_bud", AMETHYST_PROP }, // 2 bits for type, 3 bits for direction
    { 0, 133,   HIGH_BIT | 2, "large_amethyst_bud", AMETHYST_PROP }, // 2 bits for type, 3 bits for direction
    { 0, 133,   HIGH_BIT | 3, "amethyst_cluster", AMETHYST_PROP }, // 2 bits for type, 3 bits for direction
    { 0, 132,   HIGH_BIT | 1, "budding_amethyst", NO_PROP },
    { 0, 132,   HIGH_BIT | 2, "calcite", NO_PROP },
    { 0, 132,   HIGH_BIT | 3, "tuff", NO_PROP },
    { 0,  20,              1, "tinted_glass", NO_PROP },  // stuffed in with glass
    { 0, 132,   HIGH_BIT | 4, "dripstone_block", NO_PROP },
    { 0, 134,       HIGH_BIT, "pointed_dripstone", DRIPSTONE_PROP },    // 5 thickness, vertical_direction: up/down
    { 0, 132,   HIGH_BIT | 5, "copper_ore", NO_PROP },
    { 0, 132,   HIGH_BIT | 6, "deepslate_copper_ore", NO_PROP },
    { 0, 132,   HIGH_BIT | 7, "copper_block", NO_PROP },
    { 0, 132,   HIGH_BIT | 8, "exposed_copper", NO_PROP },
    { 0, 132,   HIGH_BIT | 9, "weathered_copper", NO_PROP },
    { 0, 132,  HIGH_BIT | 10, "oxidized_copper", NO_PROP },
    { 0, 132,  HIGH_BIT | 11, "cut_copper", NO_PROP },
    { 0, 132,  HIGH_BIT | 12, "exposed_cut_copper", NO_PROP },
    { 0, 132,  HIGH_BIT | 13, "weathered_cut_copper", NO_PROP },
    { 0, 132,  HIGH_BIT | 14, "oxidized_cut_copper", NO_PROP },
    { 0, 135,	    HIGH_BIT, "cut_copper_stairs", STAIRS_PROP },
    { 0, 136,	    HIGH_BIT, "exposed_cut_copper_stairs", STAIRS_PROP },
    { 0, 137,	    HIGH_BIT, "weathered_cut_copper_stairs", STAIRS_PROP },
    { 0, 138,	    HIGH_BIT, "oxidized_cut_copper_stairs", STAIRS_PROP },
    { 0, 142,	HIGH_BIT | 0, "cut_copper_slab", SLAB_PROP },
    { 0, 142,	HIGH_BIT | 1, "exposed_cut_copper_slab", SLAB_PROP },
    { 0, 142,	HIGH_BIT | 2, "weathered_cut_copper_slab", SLAB_PROP },
    { 0, 142,	HIGH_BIT | 3, "oxidized_cut_copper_slab", SLAB_PROP },
    { 0, 132,  HIGH_BIT | 15, "waxed_copper_block", NO_PROP },
    { 0, 132,  HIGH_BIT | 16, "waxed_exposed_copper", NO_PROP },
    { 0, 132,  HIGH_BIT | 17, "waxed_weathered_copper", NO_PROP },
    { 0, 132,  HIGH_BIT | 18, "waxed_oxidized_copper", NO_PROP },
    { 0, 132,  HIGH_BIT | 19, "waxed_cut_copper", NO_PROP },
    { 0, 132,  HIGH_BIT | 20, "waxed_exposed_cut_copper", NO_PROP },
    { 0, 132,  HIGH_BIT | 21, "waxed_weathered_cut_copper", NO_PROP },
    { 0, 132,  HIGH_BIT | 22, "waxed_oxidized_cut_copper", NO_PROP },
    { 0, 143,	    HIGH_BIT, "waxed_cut_copper_stairs", STAIRS_PROP },
    { 0, 145,	    HIGH_BIT, "waxed_exposed_cut_copper_stairs", STAIRS_PROP },
    { 0, 146,	    HIGH_BIT, "waxed_weathered_cut_copper_stairs", STAIRS_PROP },
    { 0, 147,	    HIGH_BIT, "waxed_oxidized_cut_copper_stairs", STAIRS_PROP },
    { 0, 142,	HIGH_BIT | 4, "waxed_cut_copper_slab", SLAB_PROP },
    { 0, 142,	HIGH_BIT | 5, "waxed_exposed_cut_copper_slab", SLAB_PROP },
    { 0, 142,	HIGH_BIT | 6, "waxed_weathered_cut_copper_slab", SLAB_PROP },
    { 0, 142,	HIGH_BIT | 7, "waxed_oxidized_cut_copper_slab", SLAB_PROP },
    { 0, 139,	    HIGH_BIT, "lightning_rod", EXTENDED_FACING_PROP },
    { 0, 148,	    HIGH_BIT, "cave_vines", BERRIES_PROP },
    { 0, 148,	HIGH_BIT | 1, "cave_vines_plant", BERRIES_PROP },    // ignore the age
    { 0, 150,	    HIGH_BIT, "spore_blossom", NO_PROP },
    { 0, 151,	    HIGH_BIT, "azalea", NO_PROP },
    { 0, BLOCK_FLOWER_POT,         AZALEA_FIELD | 0, "potted_azalea_bush", NO_PROP },
    { 0, BLOCK_FLOWER_POT,         AZALEA_FIELD | 1, "potted_flowering_azalea_bush", NO_PROP },
    { 0, 151,	HIGH_BIT | 1, "flowering_azalea", NO_PROP },
    { 0, 161,	           2, "azalea_leaves", NO_PROP },
    { 0, 161,	           3, "flowering_azalea_leaves", NO_PROP },
    { 0, 171,             16, "moss_carpet", NO_PROP },
    { 0, 132,  HIGH_BIT | 23, "moss_block", NO_PROP },
    { 0, 152,	    HIGH_BIT, "big_dripleaf", BIG_DRIPLEAF_PROP },
    { 0, 152,	HIGH_BIT | 1, "big_dripleaf_stem", BIG_DRIPLEAF_PROP },
    { 0, 153,	    HIGH_BIT, "small_dripleaf", SMALL_DRIPLEAF_PROP },
    { 0, 132,  HIGH_BIT | 24, "rooted_dirt", NO_PROP },
    { 0, 107,   HIGH_BIT | 2, "hanging_roots", TRULY_NO_PROP },  // weeping vines
    { 0, 132,  HIGH_BIT | 25, "powder_snow", NO_PROP },
    { 0, 154,       HIGH_BIT, "glow_lichen", FENCE_AND_VINE_PROP },
        /*
    { 0, 155,       HIGH_BIT, "sculk_sensor", SCULK_SENSOR_PROP },
    { 0, 135,   HIGH_BIT | 0, "deepslate", LOG_PROP },
    { 0, 132,  HIGH_BIT | 26, "cobbled_deepslate", NO_PROP },
    { 0, 137,	HIGH_BIT | 0, "cobbled_deepslate_slab", SLAB_PROP },    // double slab is 136, traditional (and a waste)
    { 0, 138,	    HIGH_BIT, "cobbled_deepslate_stairs", STAIRS_PROP },
    { 0, 139,             17, "cobbled_deepslate_wall", WALL_PROP },	// no data values used for walls, it's all implied in Mineways
    { 0, 132,  HIGH_BIT | 27, "chiseled_deepslate", NO_PROP },
    { 0, 132,  HIGH_BIT | 28, "polished_deepslate", NO_PROP },
    { 0, 136,	HIGH_BIT | 1, "polished_deepslate_slab", SLAB_PROP },
    { 0, 138,	    HIGH_BIT, "polished_deepslate_stairs", STAIRS_PROP },
    { 0, 139,             18, "polished_deepslate_wall", WALL_PROP },	// no data values used for walls, it's all implied in Mineways
    { 0, 132,  HIGH_BIT | 29, "deepslate_bricks", NO_PROP },
    { 0, 136,	HIGH_BIT | 2, "deepslate_bricks_slab", SLAB_PROP },
    { 0, 139,	    HIGH_BIT, "deepslate_bricks_stairs", STAIRS_PROP },
    { 0, 139,             19, "deepslate_bricks_wall", WALL_PROP },	// no data values used for walls, it's all implied in Mineways
    { 0, 132,  HIGH_BIT | 30, "deepslate_tiles", NO_PROP },
    { 0, 136,	HIGH_BIT | 3, "deepslate_tiles_slab", SLAB_PROP },
    { 0, 141,	    HIGH_BIT, "deepslate_tiles_stairs", STAIRS_PROP },
    { 0, 139,             20, "deepslate_tiles_wall", WALL_PROP },	// no data values used for walls, it's all implied in Mineways
    { 0, 132,  HIGH_BIT | 31, "cracked_deepslate_bricks", NO_PROP },
    { 0, 132,  HIGH_BIT | 32, "crackeinfeepslate_tiles", NO_PROP },
    { 0, 135,   HIGH_BIT | 1, "infested_deepslate", LOG_PROP },
    { 0, 132,  HIGH_BIT | 33, "smooth_basalt", NO_PROP },   // note this form of basalt is simply a block, no directionality like other basalt
    { 0, 132,  HIGH_BIT | 34, "raw_iron_block", NO_PROP },
    { 0, 132,  HIGH_BIT | 35, "raw_copper_block", NO_PROP },
    { 0, 132,  HIGH_BIT | 36, "raw_gold_block", NO_PROP },
    { 0, 208,              0, "dirt_path", NO_PROP },   // in 1.17 renamed to dirt path and given textures https://minecraft.fandom.com/wiki/Dirt_Path
    { 0, 132,  HIGH_BIT | 37, "deepslate_coal_ore", NO_PROP },
    { 0, 132,  HIGH_BIT | 38, "deepslate_iron_ore", NO_PROP },  // copper done way earlier, so be it...
    { 0, 132,  HIGH_BIT | 39, "deepslate_gold_ore", NO_PROP },
    { 0, 132,  HIGH_BIT | 30, "deepslate_redstone_ore", NO_PROP },
    { 0, 132,  HIGH_BIT | 41, "deepslate_emerald_ore", NO_PROP },
    { 0, 132,  HIGH_BIT | 42, "deepslate_lapis_ore", NO_PROP },
    { 0, 132,  HIGH_BIT | 43, "deepslate_diamond_ore", NO_PROP },
    { 0, 132,  HIGH_BIT | 43, "deepslate_diamond_ore", NO_PROP },
    { 0, 118,            0x0, "water_cauldron", NO_PROP }, // I assume this is the same as a cauldron, basically, with the level > 0, https://minecraft.fandom.com/wiki/Cauldron
    { 0, 118,            0x4, "lava_cauldron", NO_PROP }, // level directly translates to dataVal, bottom two bits
    { 0, 118,            0x8, "powder_snow_cauldron", NO_PROP }, // level directly translates to dataVal, bottom two bits
    { 0, 166,       HIGH_BIT, "light", NO_PROP },   // has just the level property, and waterlogged; normally invisible, https://minecraft.fandom.com/wiki/Light_Block


    DEEPSLATE COAL ORE etc. - not sure how to add these. 
    */
    // last used was 141

 // Note: 140, 144 are reserved for the extra bit needed for BLOCK_FLOWER_POT and BLOCK_HEAD, so don't use these HIGH_BIT values
};

#define HASH_SIZE 1024
#define HASH_MASK 0x3ff

int HashLists[HASH_SIZE + NUM_TRANS];
int* HashArray[HASH_SIZE];


int computeHash(char* name)
{
    int hashVal = 0;
    while (*name) {
        hashVal += (int)*name++;
    }
    return hashVal;
}

void makeHashTable()
{
    // go through entries and convert to hashes, note how many hashes per array spot
    int hashPerIndex[HASH_SIZE];
    memset(hashPerIndex, 0, 4 * HASH_SIZE);
    int hashStart[HASH_SIZE];
    int i;
    // If we really wanted to super-optimize, we could take the "air" entry out of the hash table,
    // since we special-case that in findIndexFromName. Let's not...
    for (i = 0; i < NUM_TRANS; i++) {
        BlockTranslations[i].hashSum = computeHash(BlockTranslations[i].name);
        hashPerIndex[BlockTranslations[i].hashSum & HASH_MASK]++;
    }
    // OK, have how many per index, so offset appropriately
    int offset = 0;
    for (i = 0; i < HASH_SIZE; i++) {
        hashStart[i] = offset;
        HashArray[i] = &HashLists[offset];
        offset += hashPerIndex[i] + 1;
    }
    if (offset != HASH_SIZE + NUM_TRANS) {
        // this is an error
        assert(0);
        return;
    }
    // now populate the hashLists, ending with -1; first just set all to -1
    for (i = 0; i < HASH_SIZE + NUM_TRANS; i++) {
        HashLists[i] = -1;
    }
    for (i = 0; i < NUM_TRANS; i++) {
        int index = BlockTranslations[i].hashSum & HASH_MASK;
        HashLists[hashStart[index]++] = i;
    }
    // done once to initialize gBlockDefinitions[i].subtype_mask values properly.
    // These values determine if a bit determines if an object is a separate type of
    // thing, e.g., granite vs. stone, or needs a separate material, e.g., redstone wire
    // at different levels of illumination.
    // Note that these values are *entirely* determined here. They are also shown in the
    // table in blockInfo.cpp, but those are just for reference purposes.
    static bool determineMasks = true;
    if (determineMasks)
    {
        determineMasks = false;
        unsigned char mask_array[NUM_BLOCKS_DEFINED];
        for (i = 0; i < NUM_BLOCKS_DEFINED; i++) {
            mask_array[i] = 0x0;
        }
        // special case: flower pot uses high bit for which type of pot  - just set them all:
        mask_array[BLOCK_FLOWER_POT] |= 0xFF;
        // special case: mob head uses high bit for whether head is on ground or not:
        mask_array[BLOCK_HEAD] |= 0x80;
        // special case: redstone wire is given 16 levels of output when separated by material
        mask_array[BLOCK_REDSTONE_WIRE] |= 0x0F;
        // special case: sea pickles have 4 levels of illumination based on number of pickles
        mask_array[BLOCK_SEA_PICKLE] |= 0x03;
        // special case: campfire on or off
        mask_array[BLOCK_CAMPFIRE] |= 0x04;
        // special case: respawn anchors have 5 levels based on charges
        mask_array[BLOCK_RESPAWN_ANCHOR] |= 0x07;
        // for lit candles, different emissive value for # of candles
        mask_array[BLOCK_LIT_CANDLE] |= 0x30;
        mask_array[BLOCK_LIT_COLORED_CANDLE] |= 0x30;
        for (i = 0; i < NUM_TRANS; i++)
        {
            // these are special and already set, so should not be set here
            if ((BlockTranslations[i].blockId != BLOCK_FLOWER_POT) && (BlockTranslations[i].blockId != BLOCK_HEAD)) {
                // reality check
                if (((BlockTranslations[i].blockId | (BlockTranslations[i].dataVal & 0x80) << 1)) >= NUM_BLOCKS_DEFINED) {
                    // out of bounds access
                    assert(0);
                    return;
                }
                // note bits used for different objects - high-bit is masked off.
                int type = BlockTranslations[i].blockId | (BlockTranslations[i].dataVal & 0x80) << 1;
                mask_array[type] |= BlockTranslations[i].dataVal & 0x7F;
            }
        }
        // special cases: double-slabs should be given same bits as slabs
        mask_array[BLOCK_STONE_DOUBLE_SLAB] |= mask_array[BLOCK_STONE_SLAB];
        mask_array[BLOCK_WOODEN_DOUBLE_SLAB] |= mask_array[BLOCK_WOODEN_SLAB];
        mask_array[BLOCK_RED_SANDSTONE_DOUBLE_SLAB] |= mask_array[BLOCK_RED_SANDSTONE_SLAB];
        mask_array[BLOCK_PURPUR_DOUBLE_SLAB] |= mask_array[BLOCK_PURPUR_SLAB];
        mask_array[BLOCK_ANDESITE_DOUBLE_SLAB] |= mask_array[BLOCK_ANDESITE_SLAB];
        mask_array[BLOCK_CRIMSON_DOUBLE_SLAB] |= mask_array[BLOCK_CRIMSON_SLAB];
        mask_array[BLOCK_CUT_COPPER_DOUBLE_SLAB] |= mask_array[BLOCK_CUT_COPPER_SLAB];
        // special case: kelp and kelp_plant are really the same thing, material-wise
        mask_array[BLOCK_KELP] = 0x0;
        // special case: cake can have a lit candle (a bit debatable anyway - illuminates the whole cake)
        mask_array[BLOCK_CAKE] |= BIT_32;
        // lit version of cave_vines is same as unlit
        mask_array[BLOCK_CAVE_VINES_LIT] |= mask_array[BLOCK_CAVE_VINES];
        // really, these should all be set properly already, but might as well make sure...
        for (i = 0; i < NUM_BLOCKS_DEFINED; i++) {
            // if you hit this assert, set the proper subtype_mask to be equal to mask_array's value here.
            assert(gBlockDefinitions[i].subtype_mask == mask_array[i]);
            gBlockDefinitions[i].subtype_mask = mask_array[i];
        }

#ifdef WIN32
        DWORD br;
#endif
#ifdef _DEBUG
        static int outputMasks = 0;	// set to 1 to output these masks to a file
        if (outputMasks) {
            static PORTAFILE outFile = PortaCreate(L"c:\\temp\\mineways_subtype_masks.txt");
            if (outFile != INVALID_HANDLE_VALUE)
            {
                // write file
                for (i = 0; i < NUM_BLOCKS_DEFINED; i++)
                {
                    char outputString[MAX_PATH_AND_FILE];
                    sprintf_s(outputString, 256, "%3d: 0x%02x\n", i, mask_array[i]);
                    if (PortaWrite(outFile, outputString, strlen(outputString))) {
                        // write error!
                        assert(0);
                        return;
                    }
                }
                PortaClose(outFile);
            }
        }
#endif
        // NOTE: this "debug" code runs only under Release, as otherwise we hit asserts in the RetrieveBlockSubname method
        static int outputSubblockNames = 0;	// set to 1 to output all block and subblock names to files
        if (outputSubblockNames) {
            char outputString[MAX_PATH_AND_FILE];
            static PORTAFILE outBlockFile = PortaCreate(L"c:\\temp\\mineways_block_names.csv");
            if (outBlockFile != INVALID_HANDLE_VALUE)
            {
                // write file
                for (i = 0; i < NUM_BLOCKS_DEFINED; i++)
                {
                    sprintf_s(outputString, 256, "%3d, %s\n", i, gBlockDefinitions[i].name);
                    if (PortaWrite(outBlockFile, outputString, strlen(outputString))) {
                        // write error!
                        assert(0);
                        return;
                    }
                }
                PortaClose(outBlockFile);
            }
            static PORTAFILE outFile = PortaCreate(L"c:\\temp\\mineways_subblock_names.csv");
            if (outFile != INVALID_HANDLE_VALUE)
            {
                int count = 0;
                // write file
                for (i = 0; i < NUM_BLOCKS_DEFINED; i++)
                {
                    // is there any subblock?
                    if (mask_array[i]) {
                        // walk through bits until new name is found
                        char prevSubName[256];
                        prevSubName[0] = 0x0;
                        for (int j = 0; j <= mask_array[i]; j++) {
                            const char* subName = RetrieveBlockSubname(i, j);
                            // dataVal 0 should just about always be valid, and otherwise try to avoid duplicates or bits outside the mask
                            if (((j == 0) || (j & mask_array[i])) && (strcmp(subName, prevSubName) != 0) && (strcmp(subName, gBlockDefinitions[i].name) != 0)) {
                                // new name, so output it
                                sprintf_s(outputString, 256, "%3d, %s\n", i, subName);
                                if (PortaWrite(outFile, outputString, strlen(outputString))) {
                                    // write error!
                                    assert(0);
                                    return;
                                }
                                strcpy_s(prevSubName, 256, subName);
                                count++;
                            }
                        }
                    }
                }
                sprintf_s(outputString, 256, "Total of %3d subblock names\n", count);
                if (PortaWrite(outFile, outputString, strlen(outputString))) {
                    // write error!
                    assert(0);
                    return;
                }
                PortaClose(outFile);
            }
        }
    }
}

int findIndexFromName(char* name)
{
    // quick out: if it's air, just be done
    if (strcmp("air", name) == 0) {
        return 0;
    }
    // to break on a specific named block
#ifdef _DEBUG
    //if (strcmp("acacia_stairs", name) == 0) {
    //	name[0] = name[0];
    //}
#endif
    int hashNum = computeHash(name);
    int* hl = HashArray[hashNum & HASH_MASK];

    // now compare entries one by one in hash table list until a match is found
    while (*hl > -1) {
        if (hashNum == BlockTranslations[*hl].hashSum) {
            // OK, do full string compare
            if (strcmp(name, BlockTranslations[*hl].name) == 0) {
                return *hl;
            }
        }
        hl++;
    }
    // fail!
    return -1;
}

// return -1 if error (corrupt file)
int bfread(bfFile* pbf, void* target, int len)
{
    if (pbf->type == BF_BUFFER) {
        memcpy(target, pbf->buf + *pbf->offset, len);
        *pbf->offset += len;
    }
    else if (pbf->type == BF_GZIP) {
        return gzread(pbf->gz, target, len);
    }
    return len;
}

// positive number returned is offset, 0 means no movement, -1 means error (corrupt file)
int bfseek(bfFile* pbf, int offset, int whence)
{
    if (pbf->type == BF_BUFFER) {
        if (whence == SEEK_CUR)
            *pbf->offset += offset;
        else if (whence == SEEK_SET)
            *pbf->offset = offset;
    }
    else if (pbf->type == BF_GZIP) {
        return gzseek(pbf->gz, offset, whence);
    }
    return offset;
}

bfFile newNBT(const wchar_t* filename, int* err)
{
    bfFile ret;
    ret.type = BF_GZIP;

    *err = _wfopen_s(&ret.fptr, filename, L"rb");
    if (ret.fptr == NULL || *err != 0)
    {
        ret.gz = 0x0;
        return ret;
    }

    ret.gz = gzdopen(_fileno(ret.fptr), "rb");
    ret._offset = 0;
    ret.offset = &ret._offset;
    return ret;
}

static unsigned short readWord(bfFile* pbf)
{
    unsigned char buf[2];
    bfread(pbf, buf, 2);
    return (buf[0] << 8) | buf[1];
}
static unsigned int readDword(bfFile* pbf)
{
    unsigned char buf[4];
    bfread(pbf, buf, 4);
    return (buf[0] << 24) | (buf[1] << 16) | (buf[2] << 8) | buf[3];
}
//static unsigned long long readLong(bfFile *pbf)
//{
//    int i;
//    union {
//        double f;
//        unsigned long long l;
//    } fl;
//    unsigned char buf[8];
//    bfread(pbf,buf,8);
//    fl.l=0;
//    for (i=0;i<8;i++)
//    {
//        fl.l<<=8;
//        fl.l|=buf[i];
//    }
//    return fl.l;
//}
static double readDouble(bfFile* pbf)
{
    int i;
    union {
        double f;
        unsigned long long l;
    } fl;
    unsigned char buf[8];
    bfread(pbf, buf, 8);
    fl.l = 0;
    for (i = 0; i < 8; i++)
    {
        fl.l <<= 8;
        fl.l |= buf[i];
    }
    return fl.f;
}
static int skipType(bfFile* pbf, int type)
{
    int len;
    switch (type)
    {
    default:
        // unknown type! That's bad.
        assert(0);
        break;
    case 0:
        // perfectly reasonable case: type 0 is end
        return 0;
    case 1: //byte
        return bfseek(pbf, 1, SEEK_CUR);
    case 2: //short
        return bfseek(pbf, 2, SEEK_CUR);
    case 3: //int
        return bfseek(pbf, 4, SEEK_CUR);
    case 4: //long
        return bfseek(pbf, 8, SEEK_CUR);
    case 5: //float
        return bfseek(pbf, 4, SEEK_CUR);
    case 6: //double
        return bfseek(pbf, 8, SEEK_CUR);
    case 7: //byte array
        len = readDword(pbf);
        return bfseek(pbf, len, SEEK_CUR);
    case 8: //string
        len = readWord(pbf);
        return bfseek(pbf, len, SEEK_CUR);
    case 9: //list
        return skipList(pbf);
    case 10: //compound
        return skipCompound(pbf);
    case 11: //int array
        len = readDword(pbf);
        return bfseek(pbf, len * 4, SEEK_CUR);
    case 12: //long int array
        len = readDword(pbf);
        return bfseek(pbf, len * 8, SEEK_CUR);
    }
    // reach here only for unknown type, in which case we can't continue
    return -1;
}
static int skipList(bfFile* pbf)
{
    int len, i;
    unsigned char type;
    if (bfread(pbf, &type, 1) < 0) return -1;
    len = readDword(pbf);
    int retcode = 1;
    switch (type)
    {
    default:
        return 0;
    case 1: //byte
        return bfseek(pbf, len, SEEK_CUR);
    case 2: //short
        return bfseek(pbf, len * 2, SEEK_CUR);
    case 3: //int
        return bfseek(pbf, len * 4, SEEK_CUR);
    case 4: //long
        return bfseek(pbf, len * 8, SEEK_CUR);
    case 5: //float
        return bfseek(pbf, len * 4, SEEK_CUR);
    case 6: //double
        return bfseek(pbf, len * 8, SEEK_CUR);
    case 7: //byte array
        for (i = 0; (i < len) && (retcode >= 0); i++)
        {
            int slen = readDword(pbf);
            retcode = bfseek(pbf, slen, SEEK_CUR);
        }
        return retcode;
    case 8: //string
        for (i = 0; (i < len) && (retcode >= 0); i++)
        {
            int slen = readWord(pbf);
            retcode = bfseek(pbf, slen, SEEK_CUR);
        }
        return retcode;
    case 9: //list
        for (i = 0; (i < len) && (retcode >= 0); i++)
            retcode = skipList(pbf);
        return retcode;
    case 10: //compound
        for (i = 0; (i < len) && (retcode >= 0); i++)
            retcode = skipCompound(pbf);
        return retcode;
    case 11: //int array
        for (i = 0; (i < len) && (retcode >= 0); i++)
        {
            int slen = readDword(pbf);
            retcode = bfseek(pbf, slen * 4, SEEK_CUR);
        }
        return retcode;
    }
}
static int skipCompound(bfFile* pbf)
{
    int len;
    int retcode = 0;
    unsigned char type = 0;
    do {
        if (bfread(pbf, &type, 1) < 0) return -1;
        if (type)
        {
            len = readWord(pbf);
            if (bfseek(pbf, len, SEEK_CUR) < 0) return -1;	//skip name
            retcode = skipType(pbf, type);
        }
    } while (type && (retcode >= 0));
    return retcode;
}

// 1 they match, 0 they don't, -1 there was a read error
static int compare(bfFile* pbf, char* name)
{
    int ret = 0;
    int len = readWord(pbf);
    char thisName[MAX_NAME_LENGTH];
    if (bfread(pbf, thisName, len) < 0)
        return -1;
    thisName[len] = 0;
    if (strcmp(thisName, name) == 0)
        ret = 1;
    return ret;
}

// this finds an element in a composite list.
// it works progressively, so it only finds elements it hasn't come
// across yet.
static int nbtFindElement(bfFile* pbf, char* name)
{
    for (;;)
    {
        unsigned char type = 0;
        if (bfread(pbf, &type, 1) < 0) return 0;
        if (type == 0) return 0;
        int retcode = compare(pbf, name);
        if (retcode < 0)
            // failed to read file, so abort
            return 0;
        if (retcode > 0)
            // found type
            return type;
        retcode = skipType(pbf, type);
        if (retcode < 0) {
            // error! corrupt file found
            return -1;
        }
    }
}

// use only least significant half-byte of location, since we know what block we're in
unsigned char mod16(int val)
{
    return (unsigned char)(val & 0xf);
}

// return negative value on error, 1 on read OK, 2 on read and it's empty, and higher bits than 1 or 2 are warnings
int nbtGetBlocks(bfFile* pbf, unsigned char* buff, unsigned char* data, unsigned char* blockLight, unsigned char* biome, BlockEntity* entities, int* numEntities, int mcversion, int versionID, int maxHeight)
{
    int len, nsections;
    int biome_save;
    int returnCode = 1;	// means "fine"
    //int found;

    //Level/Blocks
    if (bfseek(pbf, 1, SEEK_CUR) < 0)
        return -1; //skip type
    len = readWord(pbf); //name length
    if (bfseek(pbf, len, SEEK_CUR) < 0)
        return -2; //skip name ()
    if (nbtFindElement(pbf, "Level") != 10)
        return -3;

    // For some reason, on most maps the biome info is before the Sections;
    // on others they're after. So, read biome data, then rewind to find Sections.
    // Format info at http://wiki.vg/Map_Format, though don't trust order.
    biome_save = *pbf->offset;
    memset(biome, 0, 16 * 16);
    int inttype = nbtFindElement(pbf, "Biomes");
    bool newFormat = false;
    if (inttype != 7) {
        // Could be new format 1.13
        // Bizarrely, in the new format the Biome data may be missing for some chunks.
        // When this happens, the Sections area will have 0 entries, i.e. it's an empty chunk.
        // Why this chunk even exists in this funny way, I don't know.
        // What we do is let the code proceed - it *should* zero the biome and read in an empty chunk.
        //if (inttype != 11)
        //    return -4;
        //else {
        newFormat = true;
        if (!hashMade) {
            makeHashTable();
            hashMade = true;
        }
        //}
    }

    // note if Y value needs to be adjusted by +4
    signed char y_offset = (versionID >= 2685) ? 4 : 0;
    // 1.17 has a height of 384
    //int max_height = (versionID >= 2685) ? 384 : 256;
    //int maxSlice = (maxHeight / 16) - 1;

    len = readDword(pbf); //array length
    if (!newFormat) {
        // old, direct format - done
        if (bfread(pbf, biome, len) < 0)
            return -5;
    }
    else {
        // new format 1.13+
        if (len == 256) {
            // 1.13 and 1.14
            // convert to bytes
            unsigned char biomeint[4 * 256];    // 16 x 16 grid of biomes
            memset(biomeint, 0, 4 * len);
            if (bfread(pbf, biomeint, 4 * len) < 0)
                return -6;
            for (int i = 0; i < 256; i++) {
                // wild guess as to the biome - looks like the topmost byte right now.
                int grab = 4 * i + 3;
                biome[i] = (biomeint[grab] > 255) ? 255 : (unsigned char)biomeint[grab];
            }
        }
        else if (len == 1024) {
            // 1.15 on, optionally
            // TODO from https://minecraft.gamepedia.com/Java_Edition_1.15/Development_versions
            // Biome information now stores Y-coordinates, allowing biomes to be changed based on height; previously, biome information only stored X and Z coordinates.
            // The Biomes array in the Level tag for each chunk now contains 1024 integers instead of 256.
            // len should be 1024
            // convert to bytes, tough luck if too high (for now)
            unsigned char biomeint[4 * 1024];
            memset(biomeint, 0, 4 * len);
            if (bfread(pbf, biomeint, 4 * len) < 0)
                return -7;
            for (int loc = 0; loc < 16; loc++) {
                // biomes are now 4x4, 64 levels, so take sea level at 16, which is at location 16 per level * 16 levels up * 4 bytes/int = 1024
                unsigned char biomeVal = biomeint[1024 + 3 + (loc >> 2) * 16 + (loc & 0x3) * 4];
                // offset by x and z times 4, in the output area
                unsigned char* biomeSet = &biome[(loc >> 2) * 64 + (loc & 0x3) * 4];
                for (int ix = 0; ix < 4; ix++) {
                    for (int iz = 0; iz < 4; iz++) {
                        biomeSet[ix * 16 + iz] = biomeVal;
                    }
                }
            }
        }
        else {
            // some unknown length - has the format changed yet again?
            // if the length is 0, that means it's an empty block
            // We don't assert, as this can evidently really be the case: no Biome, empty Section. See "Could be new format 1.13" comments earlier
            //assert(len == 0);
            memset(biome, 0, 256);
        }
    }

    if (bfseek(pbf, biome_save, SEEK_SET) < 0)
        return -8; //rewind to start of section

    if (nbtFindElement(pbf, "Sections") != 9)
        return -9;

    // does Sections have anything inside of it?
    bool empty = false;
    {
        // get rid of "\n" after "Sections".
        unsigned char uctype = 0;
        if (bfread(pbf, &uctype, 1) < 0) return -10;
        // did we find the "\n"? If not, it means the section is empty, so we
        /// can simply clear memory below and return - all done.
        if (uctype != 10) {
            empty = true;
            //return -9;
        }
        // returning -9 here for some reason crashes things later on.
        // The path taken is then "nothing read" and there's a quick out, but for some reason
        // this crashes the program now when exporting and redrawing just before then!
        // It turns out to have to do with the Stack Reserve Size: changing this to 1500000 (1.5 Mb; it's 1 Mb to start)
        // fixes the problem. See https://miztakenjoshi.wordpress.com/2010/01/27/unhandled-exception-0xc00000fd-stack-overflow/
        // But, it's better to read in an empty chunk and cache it anyway, so this new code's better.
        // was:    return -9;
    }

    memset(buff, 0, 16 * 16 * maxHeight);
    memset(data, 0, 16 * 16 * maxHeight);
    memset(blockLight, 0, 16 * 16 * maxHeight / 2);

    // TODO: we could maybe someday have a special "this block is empty" format for empty blocks.
    // Right now we waste memory space with blocks (chunks) that are entirely empty.
    // Weird false positive ("empty is always true") here from cppcheck
    if (empty) {      // cppcheck-suppress 571
        return 2;	// no warnings to OR in here - it's empty
    }

    nsections = readDword(pbf);
    if (nsections < 0) return -11;

    char thisName[MAX_NAME_LENGTH];

    // Number of block sections stacked one atop the other, ranging from 1-16; each is a Y slice,
    // each Y slice has 16 vertical layers to it. So each Y slice can have 16*16*16 values in it.
    // The minimum length of a block/data ID is 4 bits. A long integer is 64 bits, so can contain
    // 16 4-bit numbers.
    // With compression down to a minimum of 4 bits per block/data ID, this gives 256 longs,
    // or 8 * 256 bytes. (Note that this is per slice - 1.17+ does not affect this.)
    // However, it's possible that we could have 256 or more blocks per slice, really, 16*16*16
    // different, unique blocks (remember that "data" matters, too, AFAIK). In such a case, the number
    // of entries in the palette could be 4096 entries, which need 12 bits per entry.
    // This means the array here should be 16*16*16*12/8 (the 8 is bits per byte) = 6144 bytes long.
#define MAX_BLOCK_STATES_ARRAY	6144
    unsigned char bigbuff[MAX_BLOCK_STATES_ARRAY];
    //memset(bigbuff, 0, 256 * 8);

    int ret;
    unsigned char type;
    // read all slices that exist for this vertical block and process each
    while (nsections--)
    {
        signed char y;
        int save = *pbf->offset;
        if (nbtFindElement(pbf, "Y") != 1) //which section of the block stack is this?
            return -12;
        if (bfread(pbf, &y, 1) < 0)
            return -13;
        if (bfseek(pbf, save, SEEK_SET) < 0)
            return -14; //rewind to start of section

        // in 1.17, specifically 20w49a, we can now go from -5 (really, -4 is where data starts) to 19 for "y"; was -1 to 15 previously
        y += y_offset;

        // TODOTODO for now, if world y is greater than 15, we're done - we ignore these higher levels. Need to revise when 1.17 comes out
        //if (y > maxSlice) {
        //    assert(0);
        //    return returnCode;
        //}

        if (!newFormat) {
            // read all the arrays in this section
            for (;;)
            {
                ret = 0;
                type = 0;
                if (bfread(pbf, &type, 1) < 0)
                    return -15;
                if (type == 0)
                    break;
                len = readWord(pbf);
                if (bfread(pbf, thisName, len) < 0)
                    return -16;
                thisName[len] = 0;
                if (strcmp(thisName, "BlockLight") == 0)
                {
                    //found++;
                    ret = 1;
                    len = readDword(pbf); //array length
                    if (bfread(pbf, blockLight + 16 * 16 * 8 * y, len) < 0)
                        return -17;
                }
                else if (strcmp(thisName, "Blocks") == 0)
                {
                    //found++;
                    ret = 1;
                    len = readDword(pbf); //array length
                    if (bfread(pbf, buff + 16 * 16 * 16 * y, len) < 0)
                        return -18;
                }
                else if (strcmp(thisName, "Data") == 0)
                {
                    //found++;
                    ret = 1;
                    len = readDword(pbf); //array length
                    // transfer the data from 4-bits to 8-bits; needed for 1.13
                    unsigned char data4buff[16 * 16 * 8];
                    if (bfread(pbf, data4buff, len) < 0)
                        return -19;
                    unsigned char* din = data4buff;
                    unsigned char* dret = &data[16 * 16 * 16 * y];
                    for (int id = 0; id < 16 * 16 * 8; id++) {
                        // low, high 4-bits saved
                        *dret++ = *din & 0xf;
                        *dret++ = *din++ >> 4;
                    }
                }
                if (!ret)
                    if (skipType(pbf, type) < 0)
                        return -20;
            }
        }
        else {
            // read all the arrays in this section
            // walk through all elements of each Palette array element
            int dataVal = 0;
            // for doors
            bool half, north, south, east, west, down, lit, powered, triggered, extended, attached, disarmed,
                conditional, inverted, enabled, doubleSlab, mode, waterlogged, in_wall, signal_fire, has_book, up;
            int axis, door_facing, hinge, open, face, rails, occupied, part, dropper_facing, eye, age,
                delay, locked, sticky, hatch, leaves, single, attachment, honey_level, stairs, bites, tilt,
                thickness, vertical_direction, berries;
            // to avoid Release build warning, but should always be set by code in practice
            int typeIndex = 0;
            half = north = south = east = west = down = lit = powered = triggered = extended = attached = disarmed
                = conditional = inverted = enabled = doubleSlab = mode = waterlogged = in_wall = signal_fire = has_book = up = false;
            axis = door_facing = hinge = open = face = rails = occupied = part = dropper_facing = eye = age =
                delay = locked = sticky = hatch = leaves = single = attachment = honey_level = stairs = bites = tilt =
                thickness = vertical_direction = berries = 0;

            int bigbufflen = 0;
            int entry_index = 0;
            // could theoretically get higher...
            unsigned char paletteBlockEntry[MAX_PALETTE];
            unsigned char paletteDataEntry[MAX_PALETTE];
            for (;;)
            {
                ret = 0;
                type = 0;
                if (bfread(pbf, &type, 1) < 0)
                    return -21;
                if (type == 0)
                    break;
                len = readWord(pbf);
                if (bfread(pbf, thisName, len) < 0)
                    return -22;
                thisName[len] = 0;
                if (strcmp(thisName, "BlockLight") == 0)
                {
                    ret = 1;
                    len = readDword(pbf); //array length
                    // really need just y < 16 at this point, level y = -1 doesn't have much on it, but let's future proof it here
                    if (y >= 0 && y < 16) {
                        if (bfread(pbf, blockLight + 16 * 16 * 8 * y, len) < 0)
                            return -23;
                    }
                    else {
                        // dummy read - the 1.14 format has blocks at Y = -1 and Y = 16 that have no data
                        // except for BlockLight and SkyLight
                        unsigned char dummyBlockLight[16 * 16 * 128];
                        if (bfread(pbf, dummyBlockLight, len) < 0)
                            return -24;
                    }
                }
                else if (strcmp(thisName, "BlockStates") == 0)
                {
                    ret = 1;
                    bigbufflen = readDword(pbf); //array length
                    if (bigbufflen > MAX_BLOCK_STATES_ARRAY)
                        return -25;	// TODO make better unique return codes, with names
                    // read 8 byte records, so note len is adjusted here from longs (which are 8 bytes long) to the number of bytes to read.
                    if (bfread(pbf, bigbuff, bigbufflen * 8) < 0)
                        return -26;
                }
                else if (strcmp(thisName, "Palette") == 0)
                {
                    ret = 1;

                    {
                        // get rid of "\n" after "Palette".
                        unsigned char uctype = 0;
                        if (bfread(pbf, &uctype, 1) < 0)
                            return -27;
                        if (uctype != 10)
                            return -28;
                    }

                    // walk through entries' names and convert to their block ID
                    int nentries = readDword(pbf);
                    if (nentries <= 0)
                        return -29;	// TODO someday need to clean up these error codes and treat them right
                    if (nentries > MAX_PALETTE)
                        return -30;

                    char thisBlockName[MAX_NAME_LENGTH];

                    // go through entries in Palette
                    while (nentries--) {
                        // clear, so that NO_PROP doesn't inherit from other blocks, etc.
                        dataVal = 0;
                        // avoid inheriting these properties, which are always folded in (false if not found in block, so does no harm)
                        waterlogged = false;

                        for (;;)
                        {
                            type = 0;
                            if (bfread(pbf, &type, 1) < 0)
                                return -31;
                            // done walking through subarray?
                            if (type == 0)
                                break;
                            len = readWord(pbf);
                            if (bfread(pbf, thisBlockName, len) < 0)
                                return -32;
                            thisBlockName[len] = 0;

                            if ((type == 8) && (strcmp(thisBlockName, "Name") == 0)) {

                                len = readWord(pbf);
                                if (len < MAX_NAME_LENGTH) {
                                    if (bfread(pbf, thisBlockName, len) < 0)
                                        return -33;
                                }
                                else {
                                    return -34;
                                }
                                // have to add end of string
                                thisBlockName[len] = 0x0;

                                // incredibly stupid special case:
                                // in 1.13 "stone_slab" means "smooth_stone_slab" in 1.14 (in 1.14 "stone_slab" gives a slab with no chiseling, just pure stone)
                                if ((mcversion == 13) && (strcmp("minecraft:stone_slab", thisBlockName) == 0)) {
                                    strcpy_s(thisBlockName, 100, "minecraft:smooth_stone_slab");
                                }

                                // code to look for a specific name when debugging
                                //if ((mcversion >= 13) && (strcmp("minecraft:frame", thisBlockName) == 0)) {
                                //	type = type;
                                //}

                                // convert name to block value. +10 is to avoid (useless) "minecraft:" string.
                                // could be dangerous if len < 10 for whatever reason.
                                typeIndex = findIndexFromName(thisBlockName + 10);
                                if (typeIndex > -1) {
                                    paletteBlockEntry[entry_index] = BlockTranslations[typeIndex].blockId;
                                    paletteDataEntry[entry_index] = BlockTranslations[typeIndex].dataVal;
                                }
                                else {
                                    // unknown type
                                    //  THIS IS WHERE TO PUT A DEBUG BREAK TO SEE WHAT NAME IS UNKNOWN: see thisBlockName
#ifdef _DEBUG
                                    // Make it bedrock, so we see it's not translated
                                    paletteBlockEntry[entry_index] = 7;
#else
                                    // For release, make it air, so it doesn't gunk up the export
                                    paletteBlockEntry[entry_index] = 0;
#endif
                                    // data value always reset to 0
                                    paletteDataEntry[entry_index] = 0;
                                    returnCode |= NBT_WARNING_NAME_NOT_FOUND;
                                }
                            }
                            else if ((type == 10) && (strcmp(thisBlockName, "Properties") == 0)) {
                                do {
                                    if (bfread(pbf, &type, 1) < 0)
                                        return -35;
                                    if (type)
                                    {
                                        // read token value
                                        char token[100];
                                        char value[100];
                                        len = readWord(pbf);
                                        if (bfread(pbf, token, len) < 0)
                                            return -36;
                                        token[len] = 0;
                                        len = readWord(pbf);
                                        if (bfread(pbf, value, len) < 0)
                                            return -37;
                                        value[len] = 0;

                                        // TODO: I'm guessing that these zillion strcmps that follow are costing a lot of time.
                                        // Nicer would be to be able to save the token/values away in an array, then get the
                                        // name (which gets parsed after), then given the name just test the possible tokens needed.

                                        // Very common, for grass blocks, so checked first
                                        if (strcmp(token, "snowy") == 0) {
                                            // blocks with the SNOWY_PROP property have only this flag, plus sub-type data values, so we can set it directly.
                                            // This is why the SNOWY_PROP does not have to be defined
                                            if (strcmp(value, "true") == 0) {
                                                dataVal = SNOWY_BIT;
                                            }
                                        } // for grassy blocks, podzol, mycellium

                                        // interpret token value
                                        // wood axis, quartz block axis AXIS_PROP and NETHER_PORTAL_AXIS_PROP
                                        else if (strcmp(token, "axis") == 0) {
                                            if (strcmp(value, "y") == 0) {
                                                axis = 0;
                                            }
                                            else if (strcmp(value, "x") == 0) {
                                                axis = 4;
                                            }
                                            else if (strcmp(value, "z") == 0) {
                                                axis = 8;
                                            }
                                        }
                                        // FLUID_PROP
                                        else if (strcmp(token, "level") == 0) {
                                            dataVal = atoi(value);
                                        }
                                        // STANDING_SIGN_PROP
                                        else if (strcmp(token, "rotation") == 0) {
                                            dataVal = atoi(value);
                                        }
                                        // SAPLING_PROP
                                        else if (strcmp(token, "stage") == 0) {
                                            // 0 or 1, 1 is about to grow into a tree.
                                            // mask out, since this is non-graphical
#ifndef GRAPHICAL_ONLY
                                            dataVal = 8 * atoi(value);
#endif
                                        }
                                        // leaves LEAF_PROP
                                        // there does not seem to be any "check decay" flag 
                                        else if (strcmp(token, "persistent") == 0) {
                                            // if true, will decay; if false, will be checked for decay (what?)
                                            // https://minecraft.gamepedia.com/Leaves#Block_states
                                            // Instead, I'm guessing persistent means "no decay"
                                            // https://minecraft.gamepedia.com/Java_Edition_data_values#Leaves
                                            // ignore, since it is has no graphical effect
#ifndef GRAPHICAL_ONLY
                                            dataVal = (strcmp(value, "true") == 0) ? 4 : 0;
#endif
                                        }
                                        // SLAB_PROP
                                        else if (strcmp(token, "type") == 0) {
                                            // for mushroom blocks:
                                            if (strcmp(value, "top") == 0) {
                                                dataVal = 8;
                                                doubleSlab = false;
                                            }
                                            else if (strcmp(value, "bottom") == 0) {
                                                dataVal = 0;
                                                doubleSlab = false;
                                            }
                                            else if (strcmp(value, "double") == 0) {
                                                doubleSlab = true;
                                            }
                                            else if (strcmp(value, "sticky") == 0) {
                                                sticky = 8;
                                            }
                                            else if (strcmp(value, "normal") == 0) {
                                                sticky = false;
                                            }
                                            /* chest */
                                            else if (strcmp(value, "single") == 0) {
                                                single = 1;
                                            }
                                            else if (strcmp(value, "left") == 0) {
                                                single = 3;	// this value is reversed from how I think of left and right
                                            }
                                            else if (strcmp(value, "right") == 0) {
                                                single = 2;	// this value is reversed from how I think of left and right
                                            }
                                        }
                                        // common property
                                        else if (strcmp(token, "waterlogged") == 0) {
                                            waterlogged = (strcmp(value, "true") == 0) ? true : false;
                                        }
                                        // SNOW_PROP
                                        else if (strcmp(token, "layers") == 0) {
                                            // 1-8, which turns into 0-7
                                            dataVal = atoi(value) - 1;
                                            // test and bound, just in case. Tate Hickman reported snow turning into stonecutters.
                                            if (dataVal > 7 || dataVal < 0)
                                                dataVal = 1;
                                        }
                                        // frosted ice, crops, cocoa (which needs age separate), fire (useless, and ignored), cave vines plant (also ignored, but there)
                                        else if (strcmp(token, "age") == 0) {
                                            // AGE_PROP
                                            // 0-3 or 0-7 or 0-25 (for cave vines plant)
                                            dataVal |= atoi(value);
                                            age = atoi(value);
                                        }
                                        // RAIL_PROP and STAIRS_PROP - we ignore the stairs effect, instead deriving it from the geometry. Seems to work fine.
                                        else if (strcmp(token, "shape") == 0) {
                                            // stairs
                                            if (strcmp(value, "straight") == 0) {
                                                stairs = 0x0;
                                            }
                                            else if (strcmp(value, "inner_left") == 0) {
                                                stairs = BIT_8 | BIT_32;
                                            }
                                            else if (strcmp(value, "inner_right") == 0) {
                                                stairs = BIT_8;
                                            }
                                            else if (strcmp(value, "outer_left") == 0) {
                                                stairs = BIT_16 | BIT_32;
                                            }
                                            else if (strcmp(value, "outer_right") == 0) {
                                                stairs = BIT_16;
                                            }
                                            // rails
                                            else if (strcmp(value, "north_south") == 0) {
                                                rails = 0;
                                            }
                                            else if (strcmp(value, "east_west") == 0) {
                                                rails = 1;
                                            }
                                            else if (strcmp(value, "ascending_east") == 0) {
                                                rails = 2;
                                            }
                                            else if (strcmp(value, "ascending_west") == 0) {
                                                rails = 3;
                                            }
                                            else if (strcmp(value, "ascending_north") == 0) {
                                                rails = 4;
                                            }
                                            else if (strcmp(value, "ascending_south") == 0) {
                                                rails = 5;
                                            }
                                            else if (strcmp(value, "south_east") == 0) {
                                                rails = 6;
                                            }
                                            else if (strcmp(value, "south_west") == 0) {
                                                rails = 7;
                                            }
                                            else if (strcmp(value, "north_west") == 0) {
                                                rails = 8;
                                            }
                                            // really, it's the only thing left, so test is not needed.
                                            //else if (strcmp(value, "north_east") == 0) {
                                            else {
                                                rails = 9;
                                            }
                                        }
                                        // DOOR_PROP, STAIRS_PROP; for TORCH_PROP we set the data value directly
                                        else if (strcmp(token, "facing") == 0) {
                                            // door_facing starts at 1: south,west,north,east
                                            // stairs_facing starts at 0: east,west,south,north - dataVal minus one
                                            // dropper_facing starts at 0: down,up,north,south,west,east
                                            // dataVal starts at 1: east,west,south,north
                                            if (strcmp(value, "south") == 0) {
                                                door_facing = 1;
                                                //stairs_facing = 2;
                                                //chest_facing = 3; - same as 5-stairs_facing
                                                dropper_facing = 3;
                                                dataVal = 3;
                                            }
                                            else if (strcmp(value, "west") == 0) {
                                                door_facing = 2;
                                                //stairs_facing = 1;
                                                //chest_facing = 4;
                                                dropper_facing = 4;
                                                dataVal = 2;
                                            }
                                            else if (strcmp(value, "north") == 0) {
                                                door_facing = 3;
                                                //stairs_facing = 3;
                                                //chest_facing = 2;
                                                dropper_facing = 2;
                                                dataVal = 4;
                                            }
                                            else if (strcmp(value, "east") == 0) {
                                                door_facing = 0;
                                                //stairs_facing = 0;
                                                //chest_facing = 5;
                                                dropper_facing = 5;
                                                dataVal = 1;
                                            }
                                            // dispenser, dropper, amethyst buds
                                            else if (strcmp(value, "up") == 0) {
                                                door_facing = 0;
                                                //chest_facing = 5;
                                                dropper_facing = 1;
                                                dataVal = 0;
                                            }
                                            else if (strcmp(value, "down") == 0) {
                                                door_facing = 0;
                                                //chest_facing = 5;
                                                dropper_facing = 0;
                                                dataVal = 1;
                                            }
                                        }
                                        else if (strcmp(token, "half") == 0) {
                                            // upper for sunflowers, top for stairs. Good job, guys.
                                            half = ((strcmp(value, "upper") == 0) || strcmp(value, "top") == 0) ? true : false;
                                            // do not set the data value, as the interpreters later on will interpret "half"
                                        }
                                        // DOOR_PROP only
                                        else if (strcmp(token, "hinge") == 0) {
                                            // NOTE: this is flipped from what the docs at https://minecraft.gamepedia.com/Java_Edition_data_values#Door
                                            // say, they say left is 1, but this works properly. Mojang, I suspect, means the hinge on the inside of
                                            // the door, vs. outside, or something.
                                            hinge = (strcmp(value, "right") == 0) ? 1 : 0;
                                        }
                                        else if (strcmp(token, "open") == 0) {
                                            open = (strcmp(value, "true") == 0) ? 4 : 0;
                                        }
                                        else if (strcmp(token, "in_wall") == 0) {
                                            in_wall = (strcmp(value, "true") == 0);
                                        }
                                        // lever, grindstone
                                        else if (strcmp(token, "face") == 0) {
                                            if (strcmp(value, "floor") == 0) {
                                                face = 0;
                                            }
                                            else if (strcmp(value, "wall") == 0) {
                                                face = 1;
                                            }
                                            else // assumed ceiling
                                                face = 2;
                                        }
                                        // also used by lever, powered rails
                                        else if (strcmp(token, "powered") == 0) {
                                            powered = (strcmp(value, "true") == 0);
                                        }
                                        // WIRE_PROP
                                        else if (strcmp(token, "power") == 0) {
                                            dataVal |= atoi(value);
                                        }
                                        // CANDLE_CAKE_PROP
                                        else if (strcmp(token, "bites") == 0) {
                                            bites = atoi(value);
                                        }
                                        // FARMLAND_PROP
                                        else if (strcmp(token, "moisture") == 0) {
                                            dataVal = atoi(value);
                                        }
                                        // PISTON_PROP and PISTON_HEAD_PROP
                                        else if (strcmp(token, "extended") == 0) {
                                            extended = (strcmp(value, "true") == 0);
                                        }
                                        // MUSHROOM_PROP and MUSHROOM_STEM_PROP
                                        // also WIRE_PROP: none or side;
                                        // we test for "side" for redstone dots vs. crosses
                                        else if (strcmp(token, "north") == 0) {
                                            north = (strcmp(value, "true") == 0);
                                            // setting this bit means "make it a cross" - TODO, ran out of bits!
                                            //redstone_side = (strcmp(value, "side") == 0) ? BIT_16 : 0;
                                        }
                                        else if (strcmp(token, "south") == 0) {
                                            south = (strcmp(value, "true") == 0);
                                        }
                                        else if (strcmp(token, "east") == 0) {
                                            east = (strcmp(value, "true") == 0);
                                        }
                                        else if (strcmp(token, "west") == 0) {
                                            west = (strcmp(value, "true") == 0);
                                        }
                                        else if (strcmp(token, "up") == 0) {
                                            up = (strcmp(value, "true") == 0);
                                        }
                                        else if (strcmp(token, "down") == 0) {
                                            down = (strcmp(value, "true") == 0);
                                        }
                                        // redstone
                                        else if (strcmp(token, "lit") == 0) {
                                            lit = (strcmp(value, "true") == 0);
                                        }
                                        // bed
                                        else if (strcmp(token, "occupied") == 0) {
#ifndef GRAPHICAL_ONLY
                                            occupied = (strcmp(value, "true") == 0) ? 4 : 0; // non-graphical
#endif
                                        }
                                        else if (strcmp(token, "part") == 0) {
                                            part = (strcmp(value, "head") == 0) ? 8 : 0;
                                        }
                                        // dropper/dispenser
                                        else if (strcmp(token, "triggered") == 0) {
                                            // ignore, non-graphical
#ifndef GRAPHICAL_ONLY
                                            triggered = (strcmp(value, "true") == 0);
#endif
                                        }
                                        // END_PORTAL_PROP (really, the frame)
                                        else if (strcmp(token, "eye") == 0) {
                                            eye = (strcmp(value, "true") == 0) ? 4 : 0;
                                        }
                                        // DAYLIGHT_PROP
                                        else if (strcmp(token, "inverted") == 0) {
                                            inverted = (strcmp(value, "true") == 0);
                                        }
                                        // REPEATER_PROP
                                        else if (strcmp(token, "delay") == 0) {
                                            // 1-4
                                            delay = atoi(value) - 1;
                                        }
                                        else if (strcmp(token, "locked") == 0) {
                                            locked = (strcmp(value, "true") == 0);
                                        }
                                        // HOPPER_PROP
                                        else if (strcmp(token, "enabled") == 0) {
                                            enabled = (strcmp(value, "true") == 0);
                                        }
                                        // TRIPWIRE_PROP
                                        else if (strcmp(token, "disarmed") == 0) {
                                            attached = (strcmp(value, "true") == 0);
                                        }
                                        // TRIPWIRE_PROP and TRIPWIRE_HOOK_PROP
                                        else if (strcmp(token, "attached") == 0) {
                                            attached = (strcmp(value, "true") == 0);
                                        }
                                        // COMMAND_BLOCK_PROP
                                        else if (strcmp(token, "conditional") == 0) {
                                            conditional = (strcmp(value, "true") == 0);
                                        }
                                        // brewing stand - just happens, no prop
                                        else if (strcmp(token, "has_bottle_0") == 0) {
                                            dataVal |= (strcmp(value, "true") == 0) ? 1 : 0;
                                        }
                                        else if (strcmp(token, "has_bottle_1") == 0) {
                                            dataVal |= (strcmp(value, "true") == 0) ? 2 : 0;
                                        }
                                        else if (strcmp(token, "has_bottle_2") == 0) {
                                            dataVal |= (strcmp(value, "true") == 0) ? 4 : 0;
                                        }
                                        // STRUCTURE_PROP
                                        else if (strcmp(token, "mode") == 0) {
                                            // only matters for rails
                                            if (strcmp(value, "data") == 0) {
                                                dataVal = 1;
                                            }
                                            else if (strcmp(value, "save") == 0) {
                                                dataVal = 2;
                                            }
                                            else if (strcmp(value, "load") == 0) {
                                                dataVal = 3;
                                            }
                                            else if (strcmp(value, "corner") == 0) {
                                                dataVal = 4;
                                            }
                                            // comparator
                                            else if (strcmp(value, "subtract") == 0) {
                                                mode = true;
                                            }
                                            else {
                                                mode = false;
                                            }
                                        }
                                        else if (strcmp(token, "pickles") == 0) {
                                            dataVal = atoi(value) - 1;
                                        }
                                        else if (strcmp(token, "eggs") == 0) {
                                            dataVal = atoi(value) - 1;
                                        }
                                        else if (strcmp(token, "hatch") == 0) {
                                            hatch = atoi(value);
                                        }
                                        else if (strcmp(token, "leaves") == 0) {
                                            // for LEAF_SIZE_PROP
                                            // only for bamboo; age is 0 or 1, so we put this in bits 0x2 and 0x4
                                            if (strcmp(value, "none") == 0) {
                                                leaves = 0;
                                            }
                                            else if (strcmp(value, "small") == 0) {
                                                leaves = 1;
                                            }
                                            else if (strcmp(value, "large") == 0) {
                                                leaves = 2;
                                            }
                                        }
                                        // for bell
                                        else if (strcmp(token, "attachment") == 0) {
                                            if (strcmp(value, "floor") == 0) {
                                                attachment = 0;
                                            }
                                            else if (strcmp(value, "ceiling") == 0) {
                                                attachment = 1;
                                            }
                                            else if (strcmp(value, "single_wall") == 0) {	// attached to just one wall
                                                attachment = 2;
                                            }
                                            else if (strcmp(value, "double_wall") == 0) {	// attached to a pair of walls
                                                attachment = 3;
                                            }
                                        }
                                        else if (strcmp(token, "signal_fire") == 0) {
                                            signal_fire = (strcmp(value, "true") == 0);
                                        }
                                        else if (strcmp(token, "has_book") == 0) {
                                            has_book = (strcmp(value, "true") == 0);
                                        }
                                        // for lantern LANTERN_PROP
                                        else if (strcmp(token, "hanging") == 0) {
                                            dataVal = (strcmp(value, "true") == 0) ? 1 : 0;
                                        }
                                        // for scaffolding
                                        else if (strcmp(token, "bottom") == 0) {
                                            dataVal = (strcmp(value, "true") == 0) ? 1 : 0;
                                        }
                                        // for beehive and bee_nest
                                        else if (strcmp(token, "honey_level") == 0) {
                                            honey_level = atoi(value);
                                        }
                                        // for respawn anchor - directly add to data val
                                        else if (strcmp(token, "charges") == 0) {
                                            dataVal = atoi(value);
                                        }
                                        // for jigsaw
                                        else if (strcmp(token, "orientation") == 0) {
                                            if (strcmp(value, "south_up") == 0) {
                                                dropper_facing = 3;
                                            }
                                            else if (strcmp(value, "west_up") == 0) {
                                                dropper_facing = 2;
                                            }
                                            else if (strcmp(value, "north_up") == 0) {
                                                dropper_facing = 4;
                                            }
                                            else if (strcmp(value, "east_up") == 0) {
                                                dropper_facing = 5;
                                            }
                                            else if (strcmp(value, "up_east") == 0) {
                                                dropper_facing = 1 | BIT_16 | BIT_8;
                                            }
                                            else if (strcmp(value, "up_north") == 0) {
                                                dropper_facing = 1 | BIT_16;
                                            }
                                            else if (strcmp(value, "up_west") == 0) {
                                                dropper_facing = 1;
                                            }
                                            else if (strcmp(value, "up_south") == 0) {
                                                dropper_facing = 1 | BIT_8;
                                            }
                                            else if (strcmp(value, "down_east") == 0) {
                                                dropper_facing = 0 | BIT_16 | BIT_8;
                                            }
                                            else if (strcmp(value, "down_north") == 0) {
                                                dropper_facing = 0 | BIT_16;
                                            }
                                            else if (strcmp(value, "down_west") == 0) {
                                                dropper_facing = 0;
                                            }
                                            else if (strcmp(value, "down_south") == 0) {
                                                dropper_facing = 0 | BIT_8;
                                            }
                                            else {
                                                // unknown state found
                                                assert(0);
                                            }
                                        }
                                        // CANDLE_PROP
                                        else if (strcmp(token, "candles") == 0) {
                                            // doesn't need a separate variable - "lit" will change the type
                                            // 1-4 candles == 0-3 * 16 (i.e., 0x00, 0x10, 0x20, 0x30)
                                            dataVal |= ((atoi(value)-1) << 4);
                                        }
                                        // for big dripleaf https://minecraft.fandom.com/wiki/Big_Dripleaf
                                        else if (strcmp(token, "tilt") == 0) {
                                            if (strcmp(value, "none") == 0) {
                                                tilt = 0;
                                            }
                                            else if (strcmp(value, "unstable") == 0) {
                                                // doesn't really do anything
                                                tilt = 1;
                                            }
                                            else if (strcmp(value, "partial") == 0) {
                                                tilt = 2;
                                            }
                                            else if (strcmp(value, "full") == 0) {
                                                tilt = 3;
                                            }
                                            else {
                                                // unknown state found
                                                assert(0);
                                            }
                                        }
                                        // for cave vines and cave vines plant (which also has "age") https://minecraft.fandom.com/wiki/Glow_Berries#ID
                                        // BERRIES_PROP
                                        else if (strcmp(token, "berries") == 0) {
                                            // "age" is also folded into dataVal for cave_vines_plant, which is why we can't use the dataVal directly
                                            berries = (strcmp(value, "true") == 0) ? 0x2 : 0;
                                        }
                                        // for pointed dripstone https://minecraft.fandom.com/wiki/Pointed_Dripstone#ID
                                        else if (strcmp(token, "thickness") == 0) {
                                            if (strcmp(value, "tip") == 0) {
                                                thickness = 0;
                                            }
                                            else if (strcmp(value, "tip_merge") == 0) {
                                                thickness = 1;
                                            }
                                            else if (strcmp(value, "frustum") == 0) {
                                                thickness = 2;
                                            }
                                            else if (strcmp(value, "middle") == 0) {
                                                thickness = 3;
                                            }
                                            else if (strcmp(value, "base") == 0) {
                                                thickness = 4;
                                            }
                                            else {
                                                // unknown state found
                                                assert(0);
                                            }
                                        }
                                        // also for pointed dripstone https://minecraft.fandom.com/wiki/Pointed_Dripstone#ID
                                        else if (strcmp(token, "vertical_direction") == 0) {
                                            vertical_direction = (strcmp(value, "down") == 0) ? 0x8 : 0;
                                        }
                                        else if (strcmp(token, "sculk_sensor_phase") == 0) {
                                            if (strcmp(value, "cooldown") == 0) {
                                                dataVal |= 0;
                                            }
                                            else if (strcmp(value, "active") == 0) {
                                                dataVal |= BIT_16;
                                            }
                                            else if (strcmp(value, "inactive") == 0) {
                                                dataVal |= BIT_32;
                                            }
                                            else {
                                                // unknown state found
                                                assert(0);
                                            }
                                        }


#ifdef _DEBUG
                                        else {
                                            // ignore, not used by Mineways for now, BlockTranslations[typeIndex]
                                            if (strcmp(token, "distance") == 0) {} // for leaves and scaffold, see https://minecraft.gamepedia.com/Leaves - not needed for graphics
                                            else if (strcmp(token, "short") == 0) {} // for piston, TODO - what makes this property be true?
                                            else if (strcmp(token, "note") == 0) {}
                                            else if (strcmp(token, "instrument") == 0) {}
                                            else if (strcmp(token, "drag") == 0) {}
                                            else if (strcmp(token, "has_record") == 0) {}	// jukebox
                                            else if (strcmp(token, "unstable") == 0) {}	// does TNT blow up when punched? I don't care
                                            else {
                                                // unknown property - look at token and value
                                                assert(0);
                                            }
                                        }
#endif
                                    }
                                } while (type);
                            }
                            else if (skipType(pbf, type) < 0)
                                return -38;
                        }
                        // done, so determine and fold in dataVal
                        int tf = BlockTranslations[typeIndex].translateFlags;
                        switch (tf) {
                        default:
                            // prop defined but not used in list below - just use NO_PROP if the prop does nothing
                            // TODOTODO uncomment when all props implemented:
                            assert(0);

                        case NO_PROP:
                            // these are also ones where nothing needs to be done. They could all be called NO_PROP,
                            // but it's handy to know what blocks have what properties associated with them.
                        case SNOWY_PROP:
                        case AGE_PROP:
                        case FLUID_PROP:
                        case SAPLING_PROP:
                        case LEAF_PROP:
                        case FARMLAND_PROP:
                        case STANDING_SIGN_PROP:
                        case WT_PRESSURE_PROP:
                        case PICKLE_PROP:
                        case LANTERN_PROP:
                        case WIRE_PROP:
                            break;

                        case TRULY_NO_PROP:
                            dataVal = 0x0;
                            break;

                        case SLAB_PROP:
                            // everything is fine if double is false
                            if (doubleSlab) {
                                // turn single slabs into double slabs by using the type ID just before (it's traditional)
                                paletteBlockEntry[entry_index]--;
                            }
                            break;
                        case CANDLE_PROP:
                            if (lit) {
                                // turn candle type into lit candle, which is one above an unlit candle
                                paletteBlockEntry[entry_index]++;
                            }
                            break;
                        case AXIS_PROP:
                            // will get OR'ed in with type of block later
                            dataVal = axis;
                            break;
                        case NETHER_PORTAL_AXIS_PROP:
                            // was 4 and 8, make it 1 and 2
                            dataVal = axis >> 2;
                            break;
                        case CANDLE_CAKE_PROP:
                            // 3 lowest bits is number of bites, which is 0-6. However, 7 bites means there's a regular (non-colored) candle.
                            // 0x10 bit is whether the cake has a COLORED candle or not. If it does, then the four lowest bits are the color
                            // 0x20 bit is whether the candle is lit or not, for all 17 candles.
                            dataVal |= bites | (lit ? BIT_32 : 0x0);
                            // Must reset "lit", as the basic "cake" object does not have this property, but the candle cakes do.
                            // Similarly, "bites" must be reset, as candle cakes always have 0 bites and only care about "lit".
                            lit = false;
                            bites = 0;
                            break;
                        case TORCH_PROP:
                            // if dataVal is not set, i.e. is 0, then set to 5
                            if (dataVal == 0)
                                dataVal = 5;
                            // if this is a redstone torch, use the "lit" property to decide which block
                            if (!lit && (paletteBlockEntry[entry_index] == 76)) {
                                // turn it off
                                paletteBlockEntry[entry_index] = 75;
                            }
                            break;
                        case STAIRS_PROP:
                            dataVal = (dataVal - 1) | (half ? 4 : 0) | stairs;
                            break;
                        case RAIL_PROP:
                            dataVal = rails;
                            if (!(paletteBlockEntry[entry_index] == 66)) {
                                dataVal |= (powered ? 8 : 0);
                            }
                            break;
                        case PRESSURE_PROP:
                            dataVal = powered ? 1 : 0;
                            break;
                        case DOOR_PROP:
                            // if upper door, use hinge and powered, else use facing and open
                            if (half) {
                                // upper
                                dataVal = 8 | hinge | (powered ? 2 : 0);
                            }
                            else {
                                // lower
                                dataVal = open | door_facing;
                            }
                            break;
                        case LEVER_PROP:
                            // which way is face?
                            if (face == 0) {
                                // floor, can only be 5 or 6, test south or north;
                                // but, may need to set dataVal properly.
                                // We don't have the full flexibility of 1.13,
                                // but rather use 1.12 rules here, that things go
                                // south when off, north when on, in order to get
                                // south and north orientations.
                                switch ((dataVal - 1) + (powered ? 4 : 0)) {
                                default:
                                case 0: // east
                                case 5: // west
                                    dataVal = 6 | 8;
                                    break;
                                case 1: // west
                                case 4: // east
                                    dataVal = 6;
                                    break;
                                case 2: // south
                                case 7: // north
                                    dataVal = 5 | 8;
                                    break;
                                case 3: // north
                                case 6: // south
                                    dataVal = 5;
                                    break;
                                }
                            }
                            else if (face == 1) {
                                // side
                                // surprisingly simple, see switch code that follows.
                                dataVal |= (powered ? 8 : 0);
                                /*
                                switch (stairs_facing) {
                                default:
                                case 0: // east
                                dataVal |= 1;
                                break;
                                case 1: // west
                                dataVal |= 2;
                                break;
                                case 2: // south
                                dataVal |= 3;
                                break;
                                case 3: // north
                                dataVal |= 4;
                                break;
                                }
                                */
                            }
                            else {
                                // ceiling, can only be 0 or 7
                                switch ((dataVal - 1) + (powered ? 4 : 0)) {
                                default:
                                case 0: // east
                                case 5: // west
                                    dataVal = 0 | 8;
                                    break;
                                case 1: // west
                                case 4: // east
                                    dataVal = 0;
                                    break;
                                case 2: // south
                                case 7: // north
                                    dataVal = 7 | 8;
                                    break;
                                case 3: // north
                                case 6: // south
                                    dataVal = 7;
                                    break;
                                }
                            }
                            face = 0;
                            break;
                        case CHEST_PROP:
                            dataVal = 6 - dataVal;
                            // two upper bits 0x18 are 1 = single, 2 = left, 3 = right half; note if no bits found, then it's an old-style chest
                            dataVal |= (single << 3);
                            break;
                        case FACING_PROP:
                            dataVal = 6 - dataVal;
                            break;
                        case FURNACE_PROP:
                            dataVal = 6 - dataVal;
                            if (lit) {
                                // light furnace: move type to be the "lit" version.
                                paletteBlockEntry[entry_index] = 62;
                            }
                            break;
                        case BUTTON_PROP:
                            // dataVal is set fron "facing" already (1234), just need face & powered
                            // check for top or bottom facing
                            // if button is on top or bottom, "facing" affects angle, bit 16
                            if (face == 0) {
                                dataVal = 5 | ((dataVal <= 2) ? BIT_16 : 0x0);
                            }
                            else if (face == 2) {
                                dataVal = 0 | ((dataVal <= 3) ? BIT_16 : 0x0);
                            }
                            //else if (face == 1) {
                            // not needed, as dataVal should be set just right at this point for walls
                            dataVal |= powered ? 0x8 : 0;
                            face = 0;
                            break;
                        case TRAPDOOR_PROP:
                            dataVal = (half ? 8 : 0) | (open ? 4 : 0) | (4 - dataVal);
                            break;
                        case TALL_FLOWER_PROP:
                            // Top half of sunflowers, etc., have just the 0x8 bit set, not the flower itself.
                            // Doesn't matter to Mineways per se, but if we export a schematic, we should make
                            // this data the same as Minecraft's. TODO - need to test flowers more
                            dataVal = half ? 0x8 : 0;
                            break;
                        case REDSTONE_ORE_PROP:
                            if (lit) {
                                // light redstone ore or redstone lamp
                                paletteBlockEntry[entry_index]++;
                            }
                            break;
                        case FENCE_GATE_PROP:
                            // strange but true;
                            dataVal = (open ? 0x4 : 0x0) | ((door_facing + 3) % 4) | (in_wall ? 0x20 : 0);
                            break;
                        case SWNE_FACING_PROP:
                            // south/west/north/east == 0/1/2/3
                            dataVal = (door_facing + 3) % 4;
                            break;
                        case BED_PROP:
                            // south/west/north/east == 0/1/2/3
                            // note that "occupied" will not be set if GRAPHICAL_ONLY is defined
                            dataVal = ((door_facing + 3) % 4) + part + occupied;
                            break;
                        case EXTENDED_FACING_PROP:
                            // properties DROPPER_PROP, PISTON_PROP, PISTON_HEAD_PROP, HOPPER_PROP, COMMAND_BLOCK_PROP, 
                            // also WALL_SIGN_PROP, OBSERVER_PROP
                            dataVal = dropper_facing | (extended ? 8 : 0) | sticky | (enabled ? 8 : 0) | (conditional ? 8 : 0) | (open ? 8 : 0) | (powered ? 8 : 0) | (triggered ? 8 : 0);
                            // We have to reset, as this property is used by lots of different blocks, each of which sets its own set of properties.
                            // Normally we don't have to reset, as (for example) a fence gate FENCE_GATE_PROP will always set the "open" property, it's always present, so when a second fence
                            // gate is found in the palette, it is guaranteed to have set this value, i.e., no clearing is needed there.
                            dropper_facing = 0;
                            triggered = false; // non-graphical
                            extended = false;
                            sticky = 0x0;
                            enabled = false;
                            conditional = false;
                            open = false;
                            powered = false;
                            break;
                        case EXTENDED_SWNE_FACING_PROP:
                            // properties GRINDSTONE_PROP, LECTERN_PROP, BELL_PROP, CAMPFIRE_PROP
                            // really, powered and signal_fire have no effect on rendering the objects themselves, but tracked for now anyway
                            dataVal = door_facing | (face << 2) // grindstone
                                | (has_book ? 4 : 0) | (powered ? 8 : 0) // lectern, and bell is powered
                                | (attachment << 4) // bell: 0x30 field (note that bell's 0x04 field is not used
                                | (lit ? 4 : 0)
                                //| (signal_fire ? 8 : 0) - commented out, as we now use 0x8 to mean it's a soul campfire; signal fire has no effect on rendering, AFAIK
                                | (honey_level << 2); // bee_nest, beehive
                            door_facing = face = 0;
                            has_book = false;
                            powered = false;
                            attachment = 0;
                            lit = false;
                            signal_fire = false;
                            honey_level = 0;
                            break;
                        case FENCE_AND_VINE_PROP:
                            // Note that for vines, 0 means there's one "above" (really, underneath).
                            // When there's one above, there (happily) cannot be east/west/n/s, so
                            // no extra bit is needed or used internally.
                            dataVal = (south ? 1 : 0) | (west ? 2 : 0) | (north ? 4 : 0) | (east ? 8 : 0) | (down ? BIT_16 : 0) | (up ? BIT_32 : 0);
                            break;
                        case COCOA_PROP:
                            dataVal = ((door_facing + 3) % 4) + (age << 2);
                            break;
                        case LEAF_SIZE_PROP:
                            dataVal = (leaves << 1) | age;
                            break;
                        case HIGH_FACING_PROP:
                            dataVal = 0xf | (door_facing << 4);
                        case QUARTZ_PILLAR_PROP:
                            // for quartz pillar, change data val based on axis
                            switch (axis) {
                            default:
                            case 0:
                                paletteDataEntry[entry_index] = 2;
                                break;
                            case 4:
                                paletteDataEntry[entry_index] = 3;
                                break;
                            case 8:
                                paletteDataEntry[entry_index] = 4;
                                break;
                            }
                            break;
                        case REPEATER_PROP:
                            // facing is 0x3
                            // delay is 0xC
                            // locked is 0x10
                            dataVal = ((door_facing + 3) % 4) | (delay << 2) | (locked << 4);
                            if (powered) {
                                // use active form
                                paletteBlockEntry[entry_index]++;
                            }
                            break;
                        case COMPARATOR_PROP:
                            dataVal = ((door_facing + 3) % 4) | (mode ? 4 : 0) | (powered ? 8 : 0);
                            break;
                        case MUSHROOM_STEM_PROP:
                            dataVal = up ? 15 : 10;
                            break;
                        case MUSHROOM_PROP:
                            // https://minecraft.gamepedia.com/Java_Edition_data_values#Brown_and_red_mushroom_blocks
                            if (north) {
                                // 1,2,3,14
                                if (south) {
                                    // 14 - all sides
                                    dataVal = 14;
                                }
                                else if (west) {
                                    // top, west, and north:
                                    dataVal = 1;
                                }
                                else if (east) {
                                    // top, north, and east:
                                    dataVal = 3;
                                }
                                else
                                    // top and north
                                    dataVal = 2;
                            }
                            else {
                                // 0,4,5,6,7,8,9 (stem is separate with 10 and 15, above)
                                if (south) {
                                    // 7,8,9
                                    if (west) {
                                        // top, south, west
                                        dataVal = 7;
                                    }
                                    else if (east) {
                                        // top, south, east
                                        dataVal = 9;
                                    }
                                    else
                                        // top, south
                                        dataVal = 8;
                                }
                                else {
                                    // 0,4,5,6
                                    if (west) {
                                        // top, west
                                        dataVal = 4;
                                    }
                                    else if (east) {
                                        // top, east
                                        dataVal = 6;
                                    }
                                    else if (up) {
                                        // top, only
                                        dataVal = 5;
                                    }
                                    else
                                        // nothing: pores on all sides
                                        dataVal = 0;
                                }
                            }
                            break;
                        case ANVIL_PROP:
                            dataVal = (door_facing + 3) % 4;
                            break;
                        case DAYLIGHT_PROP:
                            // change to inverted form if inverted is set
                            if (inverted) {
                                paletteBlockEntry[entry_index] = 178;
                            }
                            break;
                        case TRIPWIRE_PROP:
                            dataVal = (powered ? 1 : 0) | (attached ? 4 : 0) | (disarmed ? 8 : 0);
                            break;
                        case TRIPWIRE_HOOK_PROP:
                            dataVal = ((door_facing + 3) % 4) | (attached ? 4 : 0) | (powered ? 8 : 0);
                            break;
                        case END_PORTAL_PROP:
                            dataVal = ((door_facing + 3) % 4) | (eye ? 4 : 0);
                            break;

                            // If dataVal is 2-5, rotation is not used (head is on a wall) and high bit of head type is off, else it is put on.
                            // 7 | 654 | 3210
                            // bit 7 - is bottom four bits 3210 the rotation on floor? If off, put on wall.
                            // bits 654 - the head. Hopefully Minecraft won't add more than 8 heads...
                            // bits 3210 - depends on bit 7; rotation if on floor, or on which wall (2-5)

                        case HEAD_PROP:
                            // see BLOCK_HEAD in ObjFileManip.cpp for data layout, which is crazed
                            dataVal |= 0x80;	// it's a rotation, so flag it as such
                            break;
                        case HEAD_WALL_PROP:
                            // see BLOCK_HEAD in ObjFileManip.cpp for data layout, which is crazed
                            switch (door_facing) { // starts at 1: south, west, north, east
                            default:
                            case 1: // south
                                dataVal = 3;
                                break;
                            case 2: // west
                                dataVal = 4;	// docs say 5, docs are wrong
                                break;
                            case 3: // north
                                dataVal = 2;
                                break;
                            case 0: // east
                                dataVal = 5;	// docs say 4, docs are wrong
                                break;
                            }
                            break;
                        case FAN_PROP:
                            // low 3 bits are the subtype
                            // fourth bit unused
                            // 2 bits 56 facing for the fan: NESW
                            // next bit waterlogged (done below)
                            dataVal = ((door_facing + 3) % 4) << 4;
                            break;
                        case EGG_PROP:
                            dataVal |= (hatch << 2);
                            break;
                            //case WIRE_PROP:
                            //    dataVal |= redstone_side;
                            //    break;
                        case AMETHYST_PROP:
                            dataVal = dropper_facing << 2;
                            break;
                        case DRIPSTONE_PROP:
                            // up is vertical_direction, and if "down" the value is set to 0x08
                            dataVal = thickness | vertical_direction;
                            break;
                        case BIG_DRIPLEAF_PROP:
                            // bottommost bit is stem or not
                            dataVal = (door_facing | (tilt << 2)) << 1;
                            break;
                        case SMALL_DRIPLEAF_PROP:
                            // note that upper half is 0x0, lower is 0x1
                            dataVal = (door_facing << 1) | ( half ? 0 : 1 );
                            break;
                        case BERRIES_PROP:
                            dataVal = 0x0;
                            // use lit/berries form if berries present
                            if (berries) {
                                paletteBlockEntry[entry_index]++;
                            }
                            break;
                        }
                        // make sure upper bits are not set - they should not be! Well, except for heads. So, comment out this test
                        //if (dataVal > 0x3F) {
                        //	// here's where to put a break for DEBUG
                        //	dataVal &= 0x3F;
                        //}
                        // always check for waterlogged
                        dataVal |= (waterlogged ? WATERLOGGED_BIT : 0x0);

                        paletteDataEntry[entry_index] |= dataVal;
                        entry_index++;
                    }
                }
                if (!ret)
                    if (skipType(pbf, type) < 0)
                        return -39;
            }
            // Now that we have all the data, convert bigbuff layer into buff and data values
            // DEBUG: set a condition of entry_index > 2 - shows just the chunk slices that are not just a single block type & air
            // BlockStates in Sections elements no longer contain values stretching over multiple 64 - bit fields.
            // If the number of bits per block is not power of two (i.e., single 64 - bit value can't fill whole number of blockstates) some bits will not be used.
            //		For example, if a single block state takes 5 bits, the highest 4 bits of every 64 - bit field will be unused.
            //      That also means slight increase in storage size(in case of 5 bits, from 320 to 342 64 - bit fields).
            // This example, explained:
            // Old format:
            // 1234512345123451234512345123451234512345123451234512345123451234 | 512345 - at the 64 - bit mark the bits just continue
            // New format (which we call "uncompressed"):
            // 123451234512345123451234512345123451234512345123451234512345.... | 1234512345 - at the 60 - bit mark the 5 bits won't fit, so they start again.
            if (bigbufflen > 0 && entry_index > 0) {
                // compute number of bits for each palette entry. For example, 21 entries is 5 bits, which can access 17-32 entries.
                int bitlength = bigbufflen / 64;
                // is this the new 1.16 20w17a format?
                bool uncompressed = (bigbufflen > 64 * bitlength);
                unsigned long int bitmask = (1 << bitlength) - 1;

                unsigned char* bout = buff + 16 * 16 * 16 * y;
                unsigned char* dout = data + 16 * 16 * 16 * y;
                int bitpull = 0;
                for (int i = 0; i < 16 * 256; i++, bitpull += bitlength) {
                    // Pull out bits. Here is the lowest bit's index, if the array is thought of as one long string of bits.
                    // That is, if you see "5" here, the bits in the 64-bit long long are in order 
                    // which bb should we access for these bits? Divide by 8
                Restart:
                    int bbindex = bitpull >> 3;
                    // Have to count from top to bottom 8 bytes of each long long. I suspect if I read the long longs as bytes the order might be right.
                    // But, this works.
                    bbindex = (bbindex & 0xfff8) + 7 - (bbindex & 0x7);
                    int bbshift = bitpull & 0x7;
                    // get the top bits out of the topmost byte, on down the row
                    int bits = (bigbuff[bbindex] >> bbshift) & bitmask;
                    // Check if we got enough bits. If we had only a few bits retrieved, need to get more from the next byte.
                    // 'While' is needed only when remainingBitLength > 0, as 3 bytes may be needed
                    int remainingBitLength = bitlength - (8 - bbshift);
                    while (remainingBitLength > 0) {
                        if (bbindex & 0x7) {
                            // one of the middle bytes, not the bottommost one
                            bits |= (bigbuff[bbindex - 1] << (8 - bbshift)) & bitmask;
                        }
                        else {
                            // Bottommost byte, and not enough bits left: need to jump to topmost byte of next long long and restart.
                            // If this is the new format and the length of bigbufflen is greater than expected,
                            // e.g., 5*64 is 320, but might be 342, then we have to add to bitpull (need to make that number
                            // incremental up above) and pull entirely from the next +15 index, as shown here.
                            if (uncompressed) {
                                // start on next long long
                                //bits = bigbuff[bbindex + 15] & bitmask;
                                bitpull += (8 - bbshift);
                                goto Restart;
                                //next iteration it will be: bbshift = 0;
                            }
                            else {
                                bits |= (bigbuff[bbindex + 15] << (8 - bbshift)) & bitmask;
                            }
                        }
                        // a waste 99% of the time - any faster way? TODO - could unwind loops, could properly track bbindex and bbshift without
                        // recomputing them each time. Maybe try some timing tests one day to see if it matters.
                        remainingBitLength -= 8;
                        bbindex--;
                        bbshift = 0;
                    }

                    // sanity check
                    if (bits >= entry_index) {
                        // Should never reach here; means that a stored index value is greater than any value in the palette.
                        assert(0);
#ifdef _DEBUG
                        // maximum value is entry_index - 1; which is useful for debugging - see things go bad
                        bits = entry_index - 1;
#else
                        bits = 0; // which is likely air
#endif
                    }
                    *bout++ = paletteBlockEntry[bits];
                    *dout++ = paletteDataEntry[bits];
                }
            }
        }
    }
    if (!newFormat) {
        if (nbtFindElement(pbf, "TileEntities") != 9)
            // all done, no TileEntities found
            return returnCode;

        {
            type = 0;
            if (bfread(pbf, &type, 1) < 0)
                return -40;
            if (type != 10)
                return returnCode;	// all done

            // tile entity (aka block entity) found, parse it - get number of sections that follow
            nsections = readDword(pbf);

            int numSaved = 0;

            while (nsections--)
            {
                // read all the elements in this section
                bool skipSection = false;
                unsigned char dataType = 0;
                unsigned char dataRot = 0;
                unsigned char dataSkullType = 0;
                unsigned char dataFlowerPotType = 0;
                int dataX = 0;
                int dataY = 0;
                int dataZ = 0;
                int dataData = 0;
                int dataBase = 0;
                for (;;)
                {
                    type = 0;
                    if (bfread(pbf, &type, 1) < 0)
                        return -41;
                    if (type == 0) {
                        // end of list, so process data, if any valid data found
                        if (!skipSection) {
                            // save skulls and flowers
                            BlockEntity* pBE = &entities[numSaved];
                            pBE->type = dataType;
                            pBE->y = (unsigned char)dataY;
                            pBE->zx = (mod16(dataZ) << 4) | mod16(dataX);
                            switch (dataType) {
                            case BLOCK_HEAD:
                                pBE->data = (unsigned char)((dataSkullType << 4) | dataRot);
                                break;
                            case BLOCK_FLOWER_POT:
                                pBE->data = (unsigned char)((dataFlowerPotType << 4) | dataData);
                                break;
                            case BLOCK_STANDING_BANNER:
                                //case BLOCK_WALL_BANNER:
                                    // entity data will be used to convert to full color ID type
                                pBE->data = (unsigned char)dataBase;
                                break;
                            default:
                                // should flag an error!
                                break;
                            }
                            numSaved++;
                        }
                        break;
                    }

                    // always read name of field
                    len = readWord(pbf);
                    if (bfread(pbf, thisName, len) < 0)
                        return -42;

                    // if the id is one we don't care about, skip the rest of the data
                    if (skipSection) {
                        if (skipType(pbf, type) < 0)
                            return -43;
                    }
                    else {
                        thisName[len] = 0;
                        if (strcmp(thisName, "x") == 0 && type == 3)
                        {
                            dataX = readDword(pbf);
                        }
                        else if (strcmp(thisName, "y") == 0 && type == 3)
                        {
                            dataY = readDword(pbf);
                        }
                        else if (strcmp(thisName, "z") == 0 && type == 3)
                        {
                            dataZ = readDword(pbf);
                        }
                        else if (strcmp(thisName, "id") == 0 && type == 8)
                        {
                            len = readWord(pbf);
                            char idName[MAX_NAME_LENGTH];
                            if (bfread(pbf, idName, len) < 0)
                                return -44;
                            idName[len] = 0;

                            // is it a skull or a flowerpot?
                            if (strcmp(idName, "minecraft:skull") == 0)
                            {
                                dataType = BLOCK_HEAD;
                            }
                            else if (strcmp(idName, "minecraft:flower_pot") == 0)
                            {
                                dataType = BLOCK_FLOWER_POT;
                            }
                            else if (strcmp(idName, "minecraft:banner") == 0)
                            {
                                // actually, unknown here - either a standing or a wall banner
                                dataType = BLOCK_STANDING_BANNER;
                            }
                            else {
                                skipSection = true;
                            }
                        }
                        else if (strcmp(thisName, "Item") == 0 && type == 8)
                        {
                            len = readWord(pbf);
                            char idName[MAX_NAME_LENGTH];
                            if (bfread(pbf, idName, len) < 0)
                                return -45;
                            idName[len] = 0;
                            /*
                            Flower Pot Contents
                            Contents		Item			Data
                            empty			air				0
                            poppy			red_flower		0
                            blue orchid		red_flower		1
                            allium			red_flower		2
                            houstonia		red_flower		3
                            red tulip		red_flower		4
                            orange tulip	red_flower		5
                            white tulip		red_flower		6
                            pink tulip		red_flower		7
                            oxeye daisy		red_flower		8
                            dandelion		yellow_flower	0
                            red mushroom	red_mushroom	0
                            brown mushroom	brown_mushroom	0
                            oak sapling		sapling			0
                            spruce sapling	sapling			1
                            birch sapling	sapling			2
                            jungle sapling	sapling			3
                            acacia sapling	sapling			4
                            dark oak sapling	sapling		5
                            dead bush		deadbush		0
                            fern			tallgrass		2
                            cactus			cactus			0
                            (note that bamboo should not be found in this data, since it is in 1.14, which is newer than this scheme)
                            */
                            char* potName[] = {
                                "minecraft:air", "minecraft:red_flower", "minecraft:yellow_flower", "minecraft:red_mushroom", "minecraft:brown_mushroom",
                                "minecraft:sapling", "minecraft:deadbush", "minecraft:tallgrass", "minecraft:cactus" };

                            skipSection = true;
                            for (int pot = 0; pot < 9; pot++) {
                                if (strcmp(idName, potName[pot]) == 0) {
                                    dataFlowerPotType = (unsigned char)pot;
                                    skipSection = false;
                                    break;
                                }
                            }
                        }
                        else if (strcmp(thisName, "Rot") == 0 && type == 1)
                        {
                            if (bfread(pbf, &dataRot, 1) < 0)
                                return -46;
                        }
                        else if (strcmp(thisName, "SkullType") == 0 && type == 1)
                        {
                            if (bfread(pbf, &dataSkullType, 1) < 0)
                                return -47;
                        }
                        else if (strcmp(thisName, "Data") == 0 && type == 3)
                        {
                            dataData = readDword(pbf);
                        }
                        else if (strcmp(thisName, "Base") == 0 && type == 3)
                        {
                            dataBase = readDword(pbf);
                        }
                        else if (strcmp(thisName, "Patterns") == 0 && type == 9)
                        {
                            // skip past the patterns when reading banners
                            if (skipType(pbf, type) < 0)
                                return -43;
                        }
                        else {
                            // unused type, skip it, and skip all rest of object, since it's something we don't care about
                            // (all fields we care about are read above - if we hit one we don't care about, we know we
                            // can ignore the rest).
                            skipType(pbf, type);
                            skipSection = true;
                        }
                    }
                }
            }

            if (numSaved > 0) {
                *numEntities = numSaved;
            }
        }
    }
    return returnCode;
}

int nbtGetSpawn(bfFile* pbf, int* x, int* y, int* z)
{
    int len;
    *x = *y = *z = 0;
    //Data/SpawnX
    // don't really need this first seek to beginning of file
    //bfseek(pbf,0,SEEK_SET);
    if (bfseek(pbf, 1, SEEK_CUR) < 0)
        return -1; //skip type
    len = readWord(pbf); //name length
    if (bfseek(pbf, len, SEEK_CUR) < 0)
        return -2; //skip name ()
    if (nbtFindElement(pbf, "Data") != 10)
        return -3;
    if (nbtFindElement(pbf, "SpawnX") != 3)
        return -4;
    *x = readDword(pbf);

    // Annoyingly, SpawnY can come before SpawnX, so we need to re-find each time.
    // For some reason, seeking to a stored offset does not work.
    // So we seek to beginning of file and find "Data" again.
    if (bfseek(pbf, 0, SEEK_SET) < 0)
        return -5;
    if (bfseek(pbf, 1, SEEK_CUR) < 0)
        return -6; //skip type
    len = readWord(pbf); //name length
    if (bfseek(pbf, len, SEEK_CUR) < 0)
        return -7; //skip name ()
    if (nbtFindElement(pbf, "Data") != 10)
        return -8;
    if (nbtFindElement(pbf, "SpawnY") != 3)
        return -9;
    *y = readDword(pbf);

    // We seek to beginning of file and find "Data" again.
    if (bfseek(pbf, 0, SEEK_SET) < 0)
        return -10;
    if (bfseek(pbf, 1, SEEK_CUR) < 0)
        return -11; //skip type
    len = readWord(pbf); //name length
    if (bfseek(pbf, len, SEEK_CUR) < 0)
        return -12; //skip name ()
    if (nbtFindElement(pbf, "Data") != 10)
        return -13;
    if (nbtFindElement(pbf, "SpawnZ") != 3)
        return -14;
    *z = readDword(pbf);
    return 0;
}

//  The NBT version of the level, 19133. See http://minecraft.gamepedia.com/Level_format#level.dat_format
int nbtGetFileVersion(bfFile* pbf, int* version)
{
    *version = 0x0; // initialize
    int len;
    //Data/version
    if (bfseek(pbf, 1, SEEK_CUR) < 0)
        return -1; //skip type
    len = readWord(pbf); //name length
    if (bfseek(pbf, len, SEEK_CUR) < 0)
        return -2; //skip name ()
    if (nbtFindElement(pbf, "Data") != 10)
        return -3;
    if (nbtFindElement(pbf, "version") != 3)
        return -4;
    *version = readDword(pbf);
    return 0;
}

// From Version, not version, see http://minecraft.gamepedia.com/Level_format#level.dat_format at bottom
// This is a newer tag for 1.9 and on, older worlds do not have them
//  The NBT data version, which tells the MC release. See https://minecraft.gamepedia.com/Data_version
// 1444 is 1.13, 1901 is 1.14
int nbtGetFileVersionId(bfFile* pbf, int* versionId)
{
    *versionId = 0x0; // initialize
    int len;
    //Data/version
    if (bfseek(pbf, 1, SEEK_CUR) < 0)
        return -1; //skip type
    len = readWord(pbf); //name length
    if (bfseek(pbf, len, SEEK_CUR) < 0)
        return -2; //skip name ()
    if (nbtFindElement(pbf, "Data") != 10)
        return -3;
    if (nbtFindElement(pbf, "Version") != 10)
        return -4;
    if (nbtFindElement(pbf, "Id") != 3)
        return -5;
    *versionId = readDword(pbf);
    return 0;
}

/* currently not used:
int nbtGetFileVersionName(bfFile* pbf, char* versionName, int stringLength)
{
    *versionName = '\0'; // initialize to empty string
    int len;
    //Data/version
    if (bfseek(pbf, 1, SEEK_CUR) < 0) return -1; //skip type
    len = readWord(pbf); //name length
    if (bfseek(pbf, len, SEEK_CUR) < 0) return -2; //skip name ()
    if (nbtFindElement(pbf, "Data") != 10) return -3;
    if (nbtFindElement(pbf, "Version") != 10) return -4;
    if (nbtFindElement(pbf, "Name") != 8) return -5;
    len = readWord(pbf);
    if (len < stringLength) {
        if (bfread(pbf, versionName, len) < 0) return -6;
    }
    else {
        // string too long; read some, discard the rest
        if (bfread(pbf, versionName, stringLength - 1) < 0) return -7;	// save last position for string terminator
        if (bfseek(pbf, len - stringLength + 1, SEEK_CUR) < 0) return -8;
        len = stringLength - 1;
    }
    versionName[len] = 0;
    return 0;
}
*/

int nbtGetLevelName(bfFile* pbf, char* levelName, int stringLength)
{
    *levelName = '\0'; // initialize to empty string
    int len;
    //Data/levelName
    if (bfseek(pbf, 1, SEEK_CUR) < 0)
        return -1; //skip type
    len = readWord(pbf); //name length
    if (bfseek(pbf, len, SEEK_CUR) < 0)
        return -2; //skip name ()
    if (nbtFindElement(pbf, "Data") != 10)
        return -3;
    // 8 means a string
    if (nbtFindElement(pbf, "LevelName") != 8)
        return -4;
    len = readWord(pbf);
    if (len < stringLength) {
        if (bfread(pbf, levelName, len) < 0)
            return -5;
    }
    else {
        // string too long; read some, discard the rest
        if (bfread(pbf, levelName, stringLength - 1) < 0)
            return -6;	// save last position for string terminator
        if (bfseek(pbf, len - stringLength + 1, SEEK_CUR) < 0)
            return -7;
        len = stringLength - 1;
    }
    levelName[len] = 0;
    return 0;
}

//void nbtGetRandomSeed(bfFile *pbf,long long *seed)
//{
//    int len;
//    *seed=0;
//    //Data/RandomSeed
//    bfseek(pbf,1,SEEK_CUR); //skip type
//    len=readWord(pbf); //name length
//    bfseek(pbf,len,SEEK_CUR); //skip name ()
//    if (nbtFindElement(pbf,"Data")!=10) return;
//    if (nbtFindElement(pbf,"RandomSeed")!=4) return;
//    *seed=readLong(pbf);
//}
int nbtGetPlayer(bfFile* pbf, int* px, int* py, int* pz)
{
    int len;
    *px = *py = *pz = 0;
    //Data/Player/Pos
    if (bfseek(pbf, 1, SEEK_CUR) < 0)
        return -1; //skip type
    len = readWord(pbf); //name length
    if (bfseek(pbf, len, SEEK_CUR) < 0)
        return -2; //skip name ()
    if (nbtFindElement(pbf, "Data") != 10)
        return -3;
    if (nbtFindElement(pbf, "Player") != 10)
        return -4;
    if (nbtFindElement(pbf, "Pos") != 9)
        return -5;
    if (bfseek(pbf, 5, SEEK_CUR) < 0)
        return -6; //skip subtype and num items
    *px = (int)readDouble(pbf);
    *py = (int)readDouble(pbf);
    *pz = (int)readDouble(pbf);
    return 0;
}

//////////// schematic
//  http://minecraft.gamepedia.com/Schematic_file_format
// return 0 if not found, 1 if all is well
int nbtGetSchematicWord(bfFile* pbf, char* field, int* value)
{
    *value = 0x0; // initialize
    int len;
    //Data/version
    if (bfseek(pbf, 1, SEEK_CUR) < 0)
        return -1; //skip type
    len = readWord(pbf); //name length
    if (bfseek(pbf, len, SEEK_CUR) < 0)
        return -2; //skip name ()
    if (nbtFindElement(pbf, field) != 2)
        return -3;
    *value = readWord(pbf);
    return 1;
}

// return 1 on success
int nbtGetSchematicBlocksAndData(bfFile* pbf, int numBlocks, unsigned char* schematicBlocks, unsigned char* schematicBlockData)
{
    int len;
    //Data/version
    if (bfseek(pbf, 1, SEEK_CUR) < 0)
        return -1; //skip type
    len = readWord(pbf); //name length
    if (bfseek(pbf, len, SEEK_CUR) < 0)
        return -2; //skip name ()

    int found = 0;
    while (found < 2)
    {
        int ret = 0;
        unsigned char type = 0;
        if (bfread(pbf, &type, 1) < 0)
            return -3;
        if (type == 0)
            break;
        len = readWord(pbf);
        char thisName[MAX_NAME_LENGTH];
        if (bfread(pbf, thisName, len) < 0)
            return -4;
        thisName[len] = 0;
        if (strcmp(thisName, "Blocks") == 0)
        {
            //found++;
            ret = 1;
            len = readDword(pbf); //array length
            // check that array is the right size
            if (len != numBlocks)
                return 0;
            if (bfread(pbf, schematicBlocks, len) < 0)
                return -5;
            found++;
        }
        else if (strcmp(thisName, "Data") == 0)
        {
            //found++;
            ret = 1;
            len = readDword(pbf); //array length
            // check that array is the right size
            if (len != numBlocks)
                return 0;
            if (bfread(pbf, schematicBlockData, len) < 0)
                return -6;
            found++;
        }
        if (!ret)
            if (skipType(pbf, type) < 0)
                return -7;
    }
    // Did we find two things? If so, return 1, success
    return ((found == 2) ? 1 : -8);
}


void nbtClose(bfFile* pbf)
{
    // For some reason, this gzclose() call causes Mineways to then shut down with an exception.
    // Inside gzclose it's the line "ret = close(state->fd);" that actually causes the problem.
    // No idea why. The error is:
    //   minkernel\crts\ucrt\src\appcrt\lowio\close.cpp
    //   Line: 49
    //   Expression (_osfile(fh) & FOPEN)
    // It happens in C:\Windows\System32\ucrtbased.dll
    if (pbf->type == BF_GZIP) {
        // if I comment out this gzclose() line, things shut down fine, but that's a bit
        // unnerving - keeping file after file open seems bad. Anyway, it's a fix, if need be.
        // I suspect it has to do with this similar thing: https://github.com/csound/csound/issues/634
        // at the bottom they discuss an incompatibility between ways of accessing files, and point
        // to https://stackoverflow.com/questions/9136127/what-problems-can-appear-when-using-g-compiled-dll-plugin-in-vc-compiled-a
        // It may be the case that if I rebuilt zlib (or perhaps integrated its files into Mineways itself?)
        // this would then go away.
        // No, that's not it, there's some conflict between zlib and MSVC. The fix is to use fclose instead of gzclose. See:
        // https://github.com/OpenImageIO/oiio/issues/1817#issuecomment-439048464
        gzflush(pbf->gz, Z_FINISH);
        //gzclose(pbf->gz);
        fclose(pbf->fptr);
    }
}
