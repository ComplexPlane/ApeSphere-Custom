#include "relutil.h"
#include "log.h"

#include <mkb.h>

namespace relutil {

enum class ModuleId {
    Dol = 0,
    MainLoop = 1,
    MainGame = 2,
    SelNgc = 3,
    WorkshopMod = 100,
    PracticeMod = 101,
};

// Start of a loaded DOL, REL, or REL BSS
struct Region {
    ModuleId id;
    void* vanilla_ptr;
    u32 size;
    bool is_bss;
};

struct RelEntry {
    u16 offset;
    u8 type;
    u8 section;
    u32 addend;
};
static_assert(sizeof(RelEntry) == 0x8);

struct Imp {
    u32 module_id;
    RelEntry* rel_offset;
};
static_assert(sizeof(Imp) == 0x8);

struct SectionInfo {
    u32 addr_and_2_flags;
    u32 size;
};
static_assert(sizeof(SectionInfo) == 0x8);

struct RelHeader {
    u32 id;
    RelHeader* next;
    RelHeader* prev;
    u32 num_sections;
    SectionInfo* section_info_offset;
    char* name_offset;
    u32 name_size;
    u32 version;
    u32 bss_size;
    RelEntry* rel_offset;
    Imp* imp_offset;
    u32 imp_size;
    u8 prolog_section;
    u8 epilog_section;
    u8 unresolved_section;
    u8 bss_section;
    void* prolog;
    void* epilog;
    void* unresolved;
    u32 align;
    u32 bssAlign;
    u32 fixSize;
};
static_assert(sizeof(RelHeader) == 0x4C);

static Region s_vanilla_regions[] = {
    { ModuleId::Dol, reinterpret_cast<void*>(0x80000000), 0x199F84, false },
    { ModuleId::MainLoop, reinterpret_cast<void*>(0x80270100), 0x2DC7CC, false },
    { ModuleId::MainLoop, reinterpret_cast<void*>(0x8054C8e0), 0xDDA4C, true },
    { ModuleId::MainGame, reinterpret_cast<void*>(0x808F3FE0), 0x8B484, false },
    { ModuleId::MainGame, reinterpret_cast<void*>(0x8097F4A0), 0x65F0, true },
    { ModuleId::SelNgc, reinterpret_cast<void*>(0x808F3FE0), 0x55C87, false },
    { ModuleId::SelNgc, reinterpret_cast<void*>(0x80949CA0), 0x8BD4, true },
};

void* compute_mainloop_reldata_boundary() {
    RelHeader* module = *reinterpret_cast<RelHeader**>(0x800030C8);
    for (u32 imp_idx = 0; imp_idx * sizeof(Imp) < module->imp_size; imp_idx++) {
        Imp& imp = module->imp_offset[imp_idx];
        // Look for end of relocation data against main_loop.rel itself
        if (imp.module_id != 1) continue;
        u32 rel_idx = 0;
        for (; imp.rel_offset[rel_idx].type != 203; rel_idx++)
            ;
        return &imp.rel_offset[rel_idx + 1];
    }
    return nullptr;
}

static RelHeader* find_loaded_rel(ModuleId id) {
    RelHeader* module = *reinterpret_cast<RelHeader**>(0x800030C8);
    while (module != nullptr) {
        if (module->id == static_cast<u32>(id)) {
            return module;
        }
        module = module->next;
    }
    return nullptr;
}

void* relocate_addr(u32 vanilla_addr) {
    for (const auto& region : s_vanilla_regions) {

        u32 region_addr = reinterpret_cast<u32>(region.vanilla_ptr);
        if (vanilla_addr >= region_addr && vanilla_addr < (region_addr + region.size)) {
            // Vanilla pointer can be treated as absolute address
            if (region.id == ModuleId::Dol) {
                return reinterpret_cast<void*>(vanilla_addr);
            }

            // Find the rel location, if it's loaded at all
            RelHeader* module = find_loaded_rel(region.id);
            if (module != nullptr) {
                u32 live_addr;
                if (region.is_bss) {
                    live_addr = module->section_info_offset[module->bss_section].addr_and_2_flags & 0xFFFFFFFC;
                } else {
                    live_addr = reinterpret_cast<u32>(module);
                }

                u32 relocated_addr = live_addr + (vanilla_addr - region_addr);
                return reinterpret_cast<void*>(relocated_addr);
            }
        }
    }

    return nullptr;
}

}  // namespace relutil
