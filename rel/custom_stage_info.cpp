#include "custom_stage_info.h"
#include <cstdint>

#include "config.h"
#include "heap.h"
#include "log.h"
#include "mathutils.h"
#include "mkb2_ghidra.h"
#include "patch.h"

namespace custom_stage_info {

// Append-only buffer that precomputes the allocation size.
//
// You first simulate writes to compute the size to allocate, then perform your writes again with
// the newly-allocated space.
class PreallocBuffer {
 public:
    u32 write(const void* ptr, u32 size) {
        if (m_buf != nullptr) {
            MOD_ASSERT(m_buf_pos + size <= m_capacity);
            mkb::memcpy(reinterpret_cast<void*>(reinterpret_cast<u32>(m_buf) + m_buf_pos),
                        const_cast<void*>(ptr), size);
        }
        m_buf_pos += size;
        return m_buf_pos;
    }

    u32 write(const char* str) {
        u32 additional_size = mkb::strlen(const_cast<char*>(str)) + 1;
        return write(str, additional_size);
    }

    // Write to separately allocated buffer
    void alloc(void* buf) {
        // Idempotent
        if (m_buf != nullptr) return;

        m_buf = buf;
        m_capacity = m_buf_pos;
        m_buf_pos = 0;
    }

    // Write to heap allocated buffer
    void alloc() {
        // Idempotent
        if (m_buf != nullptr) return;

        alloc(heap::alloc(m_buf_pos));
    }

    void* buf() const { return m_buf; }
    u32 pos() const { return m_buf_pos; }

 private:
    void* m_buf = nullptr;
    u32 m_buf_pos = 0;
    u32 m_capacity = 0;
};

static constexpr u32 div_power_2_round_up(u32 v, u32 div) {
    return ((v + (div - 1)) & ~(div - 1)) / div;
}

static constexpr u16 STAGE_COUNT = 421;

static u16 s_bgm_id_lookup[STAGE_COUNT] = {};
static u16 s_time_limit_lookup[STAGE_COUNT] = {};
static u16 s_stage_name_offset_lookup[STAGE_COUNT] = {};
static char* s_stage_name_buffer;
static u8 s_is_bonus_stage_bitfield[div_power_2_round_up(STAGE_COUNT, 8)] = {};

// Replication of vanilla function, but using our own theme lookup tables
static void g_handle_world_bgm_hook(u32 g_volume) {
    mkb::BgmTrack bgm_id;
    short theme_id;
    int iVar2;
    s32 sVar3;
    int iVar4;

    if (mkb::world_theme < 0) {
        theme_id = 0;
    } else {
        theme_id = mkb::world_theme;
        if (0x2a < mkb::world_theme) {
            theme_id = 0x2a;
        }
    }

    // Use our own per-stage lookup table instead of the game's per-theme-id table
    bgm_id = s_bgm_id_lookup[mkb::g_current_stage_id];

    if (bgm_id == -1) {
        mkb::g_fade_track_volume((int)mkb::world_theme, '\0');
    } else {
        iVar2 = mkb::g_maybe_related_to_music_crossfading((int)(short)bgm_id);
        if ((short)iVar2 < 0) {
            iVar4 = 0;
            for (iVar2 = 0; iVar2 < 10; iVar2 += 1) {
                if ((-1 < mkb::g_active_music_tracks[iVar2]) &&
                    ((&mkb::g_something_related_to_bgm_track_id)[iVar2] == '\0')) {
                    iVar4 += 1;
                }
            }
            iVar2 = mkb::g_maybe_related_to_music_crossfading((int)(short)bgm_id);
            if ((short)iVar2 < 0) {
                if (iVar4 < 1) {
                    mkb::SoftStreamStart(0, bgm_id, g_volume);
                } else {
                    sVar3 = mkb::SoftStreamStart(0, bgm_id, 0);
                    if ((short)sVar3 != -1) {
                        mkb::g_change_music_volume((int)(short)bgm_id, 0xf, (u8)g_volume);
                        for (theme_id = 0; theme_id < 10; theme_id += 1) {
                            if (((((short)sVar3 != theme_id) &&
                                  (-1 < mkb::g_active_music_tracks[theme_id])) &&
                                 ((&mkb::g_something_related_to_bgm_track_id)[theme_id] == '\0')) &&
                                (((&mkb::g_some_music_status_array)
                                      [mkb::g_active_music_tracks[theme_id] * 0x14] &
                                  4) == 0)) {
                                mkb::g_change_music_volume(
                                    (int)mkb::g_active_music_tracks[theme_id], 0x1e, '\0');
                            }
                        }
                    }
                }
            } else {
                mkb::g_change_music_volume((int)(short)bgm_id, 0x1e, 'd');
            }
        } else {
            mkb::g_change_music_volume(-1, 0x78, '\0');
            mkb::g_change_music_volume((int)(short)bgm_id, 0x78, (u8)g_volume);
        }
    }
}

static int get_storymode_stage_time_limit_hook(int world, int world_stage) {
    int stage_id = mkb::sm_world_info[world].stages[world_stage].stage_id;
    return s_time_limit_lookup[stage_id];
}

static bool is_bonus_stage_hook(int stage_id) {
    return s_is_bonus_stage_bitfield[stage_id / 8] & (1 << (stage_id % 8));
}

static void write_stage_info_arrays(const config::Config& config) {
    for (u32 world = 0; world < config.story_layout.size; world++) {
        const auto& stage_infos = config.story_layout.elems[world];
        for (u32 world_stage = 0; world_stage < LEN(stage_infos); world_stage++) {
            const auto& stage = stage_infos[world_stage];
            mkb::STAGE_WORLD_THEMES[stage.stage_id] = stage.theme_id;
            s_bgm_id_lookup[stage.stage_id] = stage.music_id;
            s_time_limit_lookup[stage.stage_id] = stage.time_limit_frames;
        }
    }

    for (u32 layout_idx = 0; layout_idx < config.cm_courses.size; layout_idx++) {
        const auto& layout = config.cm_courses.elems[layout_idx];
        for (u32 i = 0; i < layout.size; i++) {
            const auto& stage = layout.elems[i];
            mkb::STAGE_WORLD_THEMES[stage.stage_id] = stage.theme_id;
            s_bgm_id_lookup[stage.stage_id] = stage.music_id;
            s_time_limit_lookup[stage.stage_id] = stage.time_limit_frames;
            if (stage.is_bonus_stage) {
                s_is_bonus_stage_bitfield[stage.stage_id / 8] |= 1 << (stage.stage_id % 8);
            }
        }
    }

    // Write stage names

    PreallocBuffer name_prebuf;

    // Prealloc first, then do it for real
    for (u32 i = 0; i < 2; i++) {
        for (u32 world = 0; world < config.story_layout.size; world++) {
            const auto& stage_infos = config.story_layout.elems[world];
            for (u32 world_stage = 0; world_stage < LEN(stage_infos); world_stage++) {
                const auto& stage = stage_infos[world_stage];
                s_stage_name_offset_lookup[stage.stage_id] = name_prebuf.write(stage.name);
            }
        }

        for (u32 course_idx = 0; course_idx < config.cm_courses.size; course_idx++) {
            const auto& course = config.cm_courses.elems[course_idx];
            for (u32 i = 0; i < course.size; i++) {
                const auto& stage = course.elems[i];
                s_stage_name_offset_lookup[stage.stage_id] = name_prebuf.write(stage.name);
            }
        }

        name_prebuf.alloc();
    }

    s_stage_name_buffer = static_cast<char*>(name_prebuf.buf());
}

static void write_storymode_entries(const config::Config& config) {
    for (u32 world = 0; world < config.story_layout.size; world++) {
        const auto& stage_infos = config.story_layout.elems[world];
        for (u32 world_stage = 0; world_stage < LEN(stage_infos); world_stage++) {
            const auto& stage = stage_infos[world_stage];
            mkb::sm_world_info[world].stages[world_stage].stage_id = stage.stage_id;
            mkb::sm_world_info[world].stages[world_stage].difficulty = stage.difficulty;
        }
    }
}

// CourseCommand enums lifted from SMB1 decomp

enum {
    CMD_IF = 0,
    CMD_THEN = 1,
    CMD_FLOOR = 2,
    CMD_COURSE_END = 3,
};

enum  // CMD_IF conditions
{
    IF_FLOOR_CLEAR = 0,
    IF_GOAL_TYPE = 2,
};

enum  // CMD_THEN actions
{
    THEN_JUMP_FLOOR = 0,
    THEN_FINISH_COURSE = 2,
};

enum  // CMD_FLOOR value types
{
    FLOOR_STAGE_ID = 0,
    FLOOR_TIME = 1,
};

// Use our own type without implicit padding for convenience
struct CourseCommand {
    u8 opcode;
    u8 type;
    s32 value;
    u8 filler8[0x1C - 0x8];  // unused filler?
};
static_assert(sizeof(CourseCommand) == sizeof(mkb::CourseCommand));

static void do_course_write_pass(const config::CmCourseLayout& course, PreallocBuffer& buf) {
    auto write_cmd = [&](const CourseCommand& cmd) { buf.write(&cmd, sizeof(cmd)); };

    auto write_explicit_jump = [&](int goal_type, int jump_distance) {
        write_cmd({CMD_IF, IF_FLOOR_CLEAR});
        write_cmd({CMD_IF, IF_GOAL_TYPE, goal_type});
        write_cmd({CMD_THEN, THEN_JUMP_FLOOR, jump_distance});
    };

    auto write_default_jump = [&](int jump_distance) {
        write_cmd({CMD_IF, IF_FLOOR_CLEAR});
        write_cmd({CMD_THEN, THEN_JUMP_FLOOR, jump_distance});
    };

    for (u32 stage_idx = 0; stage_idx < course.size; stage_idx++) {
        const auto& stage = course.elems[stage_idx];

        // Write stage ID
        write_cmd({CMD_FLOOR, FLOOR_STAGE_ID, stage.stage_id});

        // Write time limit
        if (stage.time_limit_frames != 60 * 60) {
            write_cmd({CMD_FLOOR, FLOOR_TIME, stage.time_limit_frames});
        }

        // Write goal conditions+actions

        // Jump distances are irrelevant, just finish course
        if (stage_idx == course.size - 1) {
            write_cmd({CMD_IF, IF_FLOOR_CLEAR});
            write_cmd({CMD_THEN, THEN_FINISH_COURSE});

            // All jump distances are the same
        } else if (stage.blue_goal_jump == stage.green_goal_jump &&
                   stage.green_goal_jump == stage.red_goal_jump) {
            write_default_jump(stage.blue_goal_jump);

            // All jump distances differ
        } else if (stage.blue_goal_jump != stage.green_goal_jump &&
                   stage.blue_goal_jump != stage.red_goal_jump &&
                   stage.green_goal_jump != stage.red_goal_jump) {
            write_explicit_jump(0, stage.blue_goal_jump);
            write_explicit_jump(1, stage.green_goal_jump);
            write_explicit_jump(2, stage.red_goal_jump);

            // Only blue jump differs
        } else if (stage.green_goal_jump == stage.red_goal_jump) {
            write_explicit_jump(0, stage.blue_goal_jump);
            write_default_jump(stage.green_goal_jump);

            // Only green jump differs
        } else if (stage.blue_goal_jump == stage.red_goal_jump) {
            write_explicit_jump(1, stage.green_goal_jump);
            write_default_jump(stage.blue_goal_jump);

            // Only red jump differs
        } else {
            write_explicit_jump(2, stage.red_goal_jump);
            write_default_jump(stage.blue_goal_jump);
        }
    }

    write_cmd({CMD_COURSE_END});
}

// Keeps track of how much of the vanilla game's course command space we've used. If we run out, we
// must allocate course in wsmod's heap.
//
// TODO sort courses by length? Could sort by course stage count instead of course command list size
// as a heuristic
struct VanillaCmdBuffer {
    void* ptr;
    u32 pos;
    u32 capacity;
};

static mkb::CourseCommand* write_cm_course(const config::CmCourseLayout& course,
                                           VanillaCmdBuffer& vanilla_buf) {
    PreallocBuffer buf;

    do_course_write_pass(course, buf);
    u32 course_size = buf.pos();
    if (course_size > vanilla_buf.capacity - vanilla_buf.pos) {
        // Allocate course in wsmod heap
        buf.alloc();
    } else {
        // Allocate course in vanilla game's course region
        buf.alloc(
            reinterpret_cast<void*>(reinterpret_cast<u32>(vanilla_buf.ptr) + vanilla_buf.pos));
        vanilla_buf.pos += course_size;
    }
    do_course_write_pass(course, buf);

    return static_cast<mkb::CourseCommand*>(buf.buf());
}

static void write_cm_courses(const config::Config& config) {
    VanillaCmdBuffer vanilla_buf = {
        .ptr = mkb::beginner_noex_cm_entries,
        .pos = 0,
        .capacity = 0x3e3c,
    };

    mkb::cm_courses[0] = write_cm_course(config.cm_layout.beginner, vanilla_buf);
    mkb::cm_courses[1] = write_cm_course(config.cm_layout.advanced, vanilla_buf);
    mkb::cm_courses[2] = write_cm_course(config.cm_layout.expert, vanilla_buf);
    mkb::cm_courses[3] = write_cm_course(config.cm_layout.beginner_extra, vanilla_buf);
    mkb::cm_courses[4] = write_cm_course(config.cm_layout.advanced_extra, vanilla_buf);
    mkb::cm_courses[5] = write_cm_course(config.cm_layout.expert_extra, vanilla_buf);
    mkb::cm_courses[6] = write_cm_course(config.cm_layout.master, vanilla_buf);
    mkb::cm_courses[7] = write_cm_course(config.cm_layout.master_extra, vanilla_buf);
    mkb::cm_courses[8] = mkb::cm_courses[7];
    // I don't know what these courses are but they might point to some unused course commands in
    // vanilla
    mkb::cm_courses[9] = mkb::cm_courses[0];
    mkb::cm_courses[10] = mkb::cm_courses[0];
    mkb::cm_courses[11] = mkb::cm_courses[0];
}

void init_main_loop(const config::Config& config) {
    // TODO challenge mode layout
    // TODO disable stgname machinery, hook into our own thing
    // TODO authors
    // TODO custom world count hook

    patch::hook_function(mkb::g_handle_world_bgm, g_handle_world_bgm_hook);
    patch::hook_function(mkb::get_storymode_stage_time_limit, get_storymode_stage_time_limit_hook);
    patch::hook_function(mkb::is_bonus_stage, is_bonus_stage_hook);

    write_stage_info_arrays(config);
    write_storymode_entries(config);
    write_cm_courses(config);
}

}  // namespace custom_stage_info
