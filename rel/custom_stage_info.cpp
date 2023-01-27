#include "custom_stage_info.h"

#include "patch.h"

namespace custom_stage_info {

static constexpr u16 STAGE_COUNT = 421;

static u16 s_bgm_id_lookup[STAGE_COUNT] = {};
static u16 s_time_limit_lookup[STAGE_COUNT] = {};

// Replication of vanilla function, but using our own theme lookup tables
static void g_handle_world_bgm(u32 g_volume) {
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
                            if (((((short)sVar3 != theme_id) && (-1 < mkb::g_active_music_tracks[theme_id])) &&
                                 ((&mkb::g_something_related_to_bgm_track_id)[theme_id] == '\0')) &&
                                (((&mkb::g_some_music_status_array)[mkb::g_active_music_tracks[theme_id] * 0x14] & 4) == 0)) {
                                mkb::g_change_music_volume((int)mkb::g_active_music_tracks[theme_id], 0x1e,
                                                      '\0');
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
    return;
}

static int get_storymode_stage_time_limit(int world, int world_stage) {
    int stage_id = mkb::sm_world_info[world].stages[world_stage].stage_id;
    return s_time_limit_lookup[stage_id];
}

void init_main_loop(const config::Config& config) {
    patch::hook_function(mkb::g_handle_world_bgm, g_handle_world_bgm);
    patch::hook_function(mkb::get_storymode_stage_time_limit, get_storymode_stage_time_limit);
}

}  // namespace custom_stage_info
