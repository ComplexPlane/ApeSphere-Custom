#include "config.h"

#define ARDUINOJSON_ENABLE_COMMENTS 1
#include <ArduinoJson/ArduinoJson.h>

#include <mkb.h>
#include "heap.h"
#include "log.h"
#include "relpatches.h"
#include "mathutils.h"

// TODO
// Parse to fixed-size heap at end of main heap to avoid fragmentation?

namespace config {

//
// Load/parse raw json config
//

struct Allocator {
    void* allocate(size_t size) { return heap::alloc(size); }
    void deallocate(void* ptr) { heap::free(ptr); }
};

static mkb::DVDFileInfo s_dvd_file_info;

static void* read_file(const char* path) {
    if (!mkb::DVDOpen(const_cast<char*>(path), &s_dvd_file_info)) {
        log::abort("[wsmod] Failed to open %s\n", path);
    }

    // Round up to a multiple of 32, necessary for DVDReadAsyncPrio
    u32 rounded_up_size = mkb::OSRoundUp32B(s_dvd_file_info.length);
    void* file_buf = heap::alloc(rounded_up_size);
    u32 read_size = mkb::read_entire_file_using_dvdread_prio_async(&s_dvd_file_info, file_buf,
                                                                   rounded_up_size, 0);
    MOD_ASSERT(read_size > 0);
    mkb::DVDClose(&s_dvd_file_info);
    return file_buf;
}

//
// Parse access trace / parse stack for better errors
//

class ParseStack {
 public:
    void push(const char* key) { push_access(Accessor{.type = AccessorType::Key, .key = key}); }
    void push(size_t index) { push_access(Accessor{.type = AccessorType::Index, .index = index}); }
    void pop() {
        MOD_ASSERT(m_parse_stack_ptr > 0);
        m_parse_stack_ptr--;
    }

    void print_parse_trace_prefix() {
        mkb::OSReport("[wsmod] Error parsing config: config");
        for (u32 i = 0; i < m_parse_stack_ptr; i++) {
            if (m_parse_stack[i].type == AccessorType::Key) {
                mkb::OSReport("[\"%s\"]", m_parse_stack[i].key);
            } else {
                mkb::OSReport("[%d]", m_parse_stack[i].index);
            }
        }
    }

    [[noreturn]] void abort_with_trace(const char* suffix) {
        print_parse_trace_prefix();
        log::abort(suffix);
    }

 private:
    enum class AccessorType { Key, Index };

    struct Accessor {
        AccessorType type;
        union {
            size_t index;
            const char* key;
        };
    };

    static constexpr u32 MAX_PARSE_DEPTH = 6;

    Accessor m_parse_stack[MAX_PARSE_DEPTH];
    u32 m_parse_stack_ptr = 0;

    void push_access(const Accessor& accessor) {
        MOD_ASSERT(m_parse_stack_ptr < MAX_PARSE_DEPTH);
        m_parse_stack[m_parse_stack_ptr++] = accessor;
    }
};

static ParseStack s_parse_stack;

//
// Type-checked primitive parsing utils
//

static JsonObject parse_object_field(JsonObject parent, const char* field_name) {
    s_parse_stack.push(field_name);
    JsonObject child_obj = parent[field_name];
    if (!child_obj) {
        s_parse_stack.abort_with_trace(" is missing or isn't an object\n");
    }
    s_parse_stack.pop();
    return child_obj;
}

static bool parse_bool_field(JsonObject parent, const char* field_name) {
    s_parse_stack.push(field_name);
    JsonVariant variant = parent[field_name];
    if (!variant.is<bool>()) {
        s_parse_stack.abort_with_trace(" is missing or isn't a bool\n");
    }
    s_parse_stack.pop();
    return variant;
}

static int parse_int_field(JsonObject parent, const char* field_name) {
    s_parse_stack.push(field_name);
    JsonVariant variant = parent[field_name];
    if (!variant.is<int>()) {
        s_parse_stack.abort_with_trace(" is missing or isn't an int\n");
    }
    s_parse_stack.pop();
    return variant;
}

static float parse_float_field(JsonObject parent, const char* field_name) {
    s_parse_stack.push(field_name);
    JsonVariant variant = parent[field_name];
    if (!variant.is<float>()) {
        s_parse_stack.abort_with_trace(" is missing or isn't a float\n");
    }
    s_parse_stack.pop();
    return variant;
}

static const char* parse_str_field(JsonObject parent, const char* field_name) {
    s_parse_stack.push(field_name);
    const char* str = parent[field_name];
    if (str == nullptr) {
        s_parse_stack.abort_with_trace(" is missing or isn't a string\n");
    }
    s_parse_stack.pop();
    return str;
}

static JsonArray parse_array_field(JsonObject parent, const char* field_name) {
    s_parse_stack.push(field_name);
    JsonArray arr = parent[field_name];
    if (arr.isNull()) {
        s_parse_stack.abort_with_trace(" is missing or isn't an array");
    }
    s_parse_stack.pop();
    return arr;
}

//
// Validate and parse stuff from loaded json config
//

struct StageInfo {
    u16 stage_id;
    char name[32];
    u16 theme_id;
    u16 music_id;
    u32 time_limit_frames;
};

template <typename T>
struct FixedArray {
    T* elems;
    size_t size;
};

struct CmStage {
    u16 stage_id;
    u16 blue_goal_jump;
    u16 green_goal_jump;
    u16 red_goal_jump;
};

struct CmLayout {
    FixedArray<CmStage> beginner;
    FixedArray<CmStage> beginner_extra;
    FixedArray<CmStage> advanced;
    FixedArray<CmStage> advanced_extra;
    FixedArray<CmStage> expert;
    FixedArray<CmStage> expert_extra;
    FixedArray<CmStage> master;
    FixedArray<CmStage> master_extra;
};

static const char* party_game_names[] = {
    "race", "fight", "target",   "billiards", "bowling",  "golf",
    "boat", "shot",  "dogfight", "soccer",    "baseball", "tennis",
};

static u16 s_story_mode_layout[10][10];
static CmLayout s_cm_layout;

static void parse_stage_info(JsonObject stage_info, StageInfo& out_stage_info) {
    int stage_id = parse_int_field(stage_info, "stage_id");
    const char* name = parse_str_field(stage_info, "name");
    int theme_id = parse_int_field(stage_info, "theme_id");
    int music_id = parse_int_field(stage_info, "music_id");
    // int music_id = 4;
    float time_limit = parse_float_field(stage_info, "time_limit");

    out_stage_info.stage_id = stage_id;
    mkb::strncpy(out_stage_info.name, const_cast<char*>(name), sizeof(out_stage_info.name));
    out_stage_info.name[sizeof(out_stage_info.name) - 1] = '\0';
    out_stage_info.theme_id = theme_id;
    out_stage_info.music_id = music_id;
    // This is not a correct way to do rounding, but we don't have a round function and it'll work
    // for the values we'll see
    out_stage_info.time_limit_frames = static_cast<u32>(time_limit * 60 + 0.5);
}

static FixedArray<CmStage> parse_cm_course(JsonObject layout_obj, const char *course_name) {
    JsonArray stage_list_j = parse_array_field(layout_obj, course_name);
    s_parse_stack.push(course_name);

    FixedArray<CmStage> stage_list{
        .elems = static_cast<CmStage*>(heap::alloc(sizeof(CmStage) * stage_list_j.size())),
        .size = stage_list_j.size(),
    };

    size_t i = 0;
    for (JsonObject stage_j : stage_list_j) {
        s_parse_stack.push(i);
        if (stage_j.isNull()) {
            s_parse_stack.abort_with_trace(" isn't an object\n");
        }

        CmStage& out_stage = stage_list.elems[i];
        out_stage.stage_id = parse_int_field(stage_j, "stage_id");
        out_stage.blue_goal_jump = parse_int_field(stage_j, "blue_goal_jump");
        out_stage.green_goal_jump = parse_int_field(stage_j, "green_goal_jump");
        out_stage.red_goal_jump = parse_int_field(stage_j, "red_goal_jump");

        i++;
        s_parse_stack.pop();
    }

    s_parse_stack.pop();
    return stage_list;
}

static void parse_cm_layout(JsonObject layout_obj, CmLayout& out_layout) {
    out_layout.beginner = parse_cm_course(layout_obj, "beginner");
    out_layout.beginner_extra = parse_cm_course(layout_obj, "beginner_extra");
    out_layout.advanced = parse_cm_course(layout_obj, "advanced");
    out_layout.advanced_extra = parse_cm_course(layout_obj, "advanced_extra");
    out_layout.expert = parse_cm_course(layout_obj, "expert");
    out_layout.expert_extra = parse_cm_course(layout_obj, "expert_extra");
    out_layout.master = parse_cm_course(layout_obj, "master");
    out_layout.master_extra = parse_cm_course(layout_obj, "master_extra");
}

static void parse_patches(JsonObject root_obj) {
    JsonObject patches_obj = parse_object_field(root_obj, "patches");
    s_parse_stack.push("patches");

    // Enable patches we know about, removing them as we go
    for (u32 i = 0; i < relpatches::PATCH_COUNT; i++) {
        auto& patch = relpatches::patches[i];
        patch.status = parse_bool_field(patches_obj, patch.name);
        patches_obj.remove(patch.name);
    }

    // Other patches specified? Throw error instead of failing silently
    if (patches_obj.size() > 0) {
        for (JsonPair pair : patches_obj) {
            s_parse_stack.push(pair.key().c_str());
            s_parse_stack.print_parse_trace_prefix();
            s_parse_stack.pop();
            mkb::OSReport(" is an unknown patch\n");
        }
        log::abort();
    }

    s_parse_stack.pop();
}

static void parse_story_layout(JsonObject root_obj) {
    JsonArray worlds_j = parse_array_field(root_obj, "story_mode_layout");
    s_parse_stack.push("story_mode_layout");

    size_t world_idx = 0;
    for (JsonArray world_j : worlds_j) {
        s_parse_stack.push(world_idx);
        if (world_j.isNull()) {
            s_parse_stack.abort_with_trace(" isn't an object\n");
        }
        if (world_j.size() != 10) {
            s_parse_stack.abort_with_trace(" has incorrect stage count\n");
        }

        size_t stage_idx = 0;
        for (JsonVariant stage_id_j : world_j) {
            s_parse_stack.push(stage_idx);
            if (!stage_id_j.is<int>()) {
                s_parse_stack.abort_with_trace(" is not an integer stage ID\n");
            }

            s_story_mode_layout[world_idx][stage_idx] = stage_id_j;

            stage_idx++;
            s_parse_stack.pop();
        }

        world_idx++;
        s_parse_stack.pop();
    }

    s_parse_stack.pop();
}

static void parse_party_game_toggles(JsonObject root_obj) {
    JsonObject party_games_obj = parse_object_field(root_obj, "party_game_toggles");
    s_parse_stack.push("party_game_toggles");

    relpatches::party_game_toggle::party_game_bitflag = 0;
    for (u32 i = 0; i < LEN(party_game_names); i++) {
        bool enabled = parse_bool_field(party_games_obj, party_game_names[i]);
        if (enabled) {
            relpatches::party_game_toggle::party_game_bitflag |= (1 << i);
        }
    }

    s_parse_stack.pop();
}

static void parse_stage_info(JsonObject root_obj) {
    JsonArray stage_info_arr = parse_array_field(root_obj, "stage_info");
    s_parse_stack.push("stage_info");

    StageInfo stage_info;
    size_t stage_info_idx = 0;
    for (JsonObject stage_info_obj : stage_info_arr) {
        s_parse_stack.push(stage_info_idx);
        parse_stage_info(stage_info_obj, stage_info);
        s_parse_stack.pop();
        stage_info_idx++;
    }

    s_parse_stack.pop();
}

void parse() {
    // ArduinoJson uses less memory given a mutable input
    char* json_text = static_cast<char*>(read_file("config.json"));
    BasicJsonDocument<Allocator> doc(2048);
    DeserializationError err = deserializeJson(doc, json_text);
    if (err) {
        mkb::OSReport("[wsmod] Error parsing config: %s\n", err.c_str());
        if (err == DeserializationError::Code::IncompleteInput ||
            err == DeserializationError::Code::InvalidInput) {
            mkb::OSReport(
                "[wsmod] This is a JSON syntax error. Consider opening config.json in an editor "
                "such as VSCode using the \"JSON with Comments\" language to look for issues.\n");
        }
        log::abort();
    }

    JsonObject root_obj = doc.as<JsonObject>();
    if (root_obj.isNull()) {
        log::abort("[wsmod] Error parsing config: root value is not an object\n");
    }

    parse_patches(root_obj);
    parse_party_game_toggles(root_obj);
    parse_stage_info(root_obj);
    parse_story_layout(root_obj);
    parse_cm_layout(root_obj, s_cm_layout);

    heap::free(json_text);
}

}  // namespace newconf
