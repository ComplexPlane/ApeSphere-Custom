#pragma once

#include <mkb.h>

namespace config {

struct StageInfo {
    u16 stage_id;
    const char *name;
    u16 theme_id;
    u16 music_id;
    u16 time_limit_frames;
};

struct SmStageInfo : StageInfo {
    u8 difficulty;
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

typedef SmStageInfo WorldLayout[10];
typedef FixedArray<WorldLayout> StoryLayout;
typedef FixedArray<CmStageInfo> CmCourseLayout;

struct CmLayout {
    CmCourseLayout beginner;
    CmCourseLayout beginner_extra;
    CmCourseLayout advanced;
    CmCourseLayout advanced_extra;
    CmCourseLayout expert;
    CmCourseLayout expert_extra;
    CmCourseLayout master;
    CmCourseLayout master_extra;
};

// Parsed/validated config allocated in "parse heap", which is freed after all init_main_loop()
// functions have run
struct Config {
    u16 party_game_bitfield;
    StoryLayout story_layout; 
    CmLayout cm_layout;
    FixedArray<CmCourseLayout> cm_courses; // Convenient list of all courses
};

Config *parse();

}
