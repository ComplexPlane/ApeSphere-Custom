#pragma once

#include <mkb.h>

namespace config {

struct StageInfo {
    u16 stage_id;
    char name[32]; // TODO store stage names in compact format in memory a-la stgname file
    u16 theme_id;
    u16 music_id;
    u16 time_limit_frames;
};

struct CmStageInfo : StageInfo {
    u16 blue_goal_jump;
    u16 green_goal_jump;
    u16 red_goal_jump;
    bool is_bonus_stage;
};

template <typename T>
struct FixedArray {
    T* elems;
    u32 size;
};

typedef StageInfo WorldLayout[10];
typedef FixedArray<WorldLayout> StoryLayout;

struct CmLayout {
    FixedArray<CmStageInfo> beginner;
    FixedArray<CmStageInfo> beginner_extra;
    FixedArray<CmStageInfo> advanced;
    FixedArray<CmStageInfo> advanced_extra;
    FixedArray<CmStageInfo> expert;
    FixedArray<CmStageInfo> expert_extra;
    FixedArray<CmStageInfo> master;
    FixedArray<CmStageInfo> master_extra;
};

// Parsed/validated config allocated in "parse heap", which is freed after all init_main_loop()
// functions have run
struct Config {
    u16 party_game_bitfield;
    StoryLayout story_layout; 
    CmLayout cm_layout;
};

Config *parse();

}
