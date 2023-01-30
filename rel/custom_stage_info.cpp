#include "custom_stage_info.h"
#include <cstdint>

#include "config.h"
#include "heap.h"
#include "log.h"
#include "mathutils.h"
#include "mkb2_ghidra.h"
#include "patch.h"

namespace custom_stage_info {

class PreallocBuffer {
 public:
    u32 write(const char* str) {
        u32 additional_size = mkb::strlen(const_cast<char*>(str)) + 1;
        if (m_write_mode) {
            MOD_ASSERT(m_buffer_pos + additional_size <= m_capacity);
            mkb::memcpy(reinterpret_cast<void*>(reinterpret_cast<u32>(m_buf) + m_buffer_pos),
                        const_cast<char*>(str), additional_size);
        }
        m_buffer_pos += additional_size;
        return m_buffer_pos;
    }

    void alloc() {
        // Idempotent
        if (m_write_mode) return;

        m_write_mode = true;
        m_capacity = m_buffer_pos;
        m_buffer_pos = 0;
        m_buf = heap::alloc(m_capacity);
    }

    void* buf() { return m_buf; }

 private:
    void* m_buf = nullptr;
    u32 m_buffer_pos = 0;
    u32 m_capacity = 0;
    bool m_write_mode = false;
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

void init_main_loop(const config::Config& config) {
    // TODO disable stgname machinery, hook into our own thing
    // TODO authors
    // TODO custom world count hook
    // TODO challenge mode layout

    patch::hook_function(mkb::g_handle_world_bgm, g_handle_world_bgm_hook);
    patch::hook_function(mkb::get_storymode_stage_time_limit, get_storymode_stage_time_limit_hook);
    patch::hook_function(mkb::is_bonus_stage, is_bonus_stage_hook);

    write_stage_info_arrays(config);
    write_storymode_entries(config);
}

}  // namespace custom_stage_info
