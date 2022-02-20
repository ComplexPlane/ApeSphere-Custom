#include "relutil.h"
#include "log.h"
#include "mkb2_ghidra.h"
#include "patch.h"

#include "mkb.h"

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

static Region s_vanilla_regions[] = {
    { ModuleId::Dol, reinterpret_cast<void*>(0x80000000), 0x199F84, false },
    { ModuleId::MainLoop, reinterpret_cast<void*>(0x80270100), 0x2DC7CC, false },
    { ModuleId::MainLoop, reinterpret_cast<void*>(0x8054C8E0), 0xDDA4C, true },
    { ModuleId::MainGame, reinterpret_cast<void*>(0x808F3FE0), 0x8B484, false },
    { ModuleId::MainGame, reinterpret_cast<void*>(0x8097F4A0), 0x65F0, true },
    { ModuleId::SelNgc, reinterpret_cast<void*>(0x808F3FE0), 0x55C87, false },
    { ModuleId::SelNgc, reinterpret_cast<void*>(0x80949CA0), 0x8BD4, true },
};

void* compute_mainloop_reldata_boundary() {
    mkb::OSModuleHeader* module = *reinterpret_cast<mkb::OSModuleHeader**>(0x800030C8);
    for (u32 imp_idx = 0; imp_idx * sizeof(mkb::OSImportInfo) < module->impSize; imp_idx++) {
        mkb::OSImportInfo& imp = reinterpret_cast<mkb::OSImportInfo*>(module->impOffset)[imp_idx];
        // Look for end of relocation data against main_loop.rel itself
        if (imp.id != 1) continue;
        u32 rel_idx = 0;
        for (; reinterpret_cast<mkb::OSRel*>(imp.offset)[rel_idx].type != 203; rel_idx++)
            ;
        return &reinterpret_cast<mkb::OSRel*>(imp.offset)[rel_idx + 1];
    }
    return nullptr;
}

static mkb::OSModuleHeader* find_loaded_rel(ModuleId id) {
    mkb::OSModuleHeader* module = *reinterpret_cast<mkb::OSModuleHeader**>(0x800030C8);
    while (module != nullptr) {
        if (module->info.id == static_cast<u32>(id)) {
            return module;
        }
        module = reinterpret_cast<mkb::OSModuleHeader*>(module->info.link.next);
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
            mkb::OSModuleHeader* module = find_loaded_rel(region.id);
            if (module != nullptr) {
                u32 live_addr;
                if (region.is_bss) {
                    if (region.id == ModuleId::MainLoop) {
                        live_addr = reinterpret_cast<u32>(mkb::mainloop_rel_buffer_info.bss_buffer);
                    } else if (region.id == ModuleId::MainGame || region.id == ModuleId::SelNgc) {
                        live_addr = reinterpret_cast<u32>(mkb::additional_rel_buffer_info.bss_buffer);
                    } else {
                        // Sorry, we don't know where the BSS for that REL is
                        return nullptr;
                    }
                } else {
                    live_addr = reinterpret_cast<u32>(module);
                }

                u32 relocated_addr = live_addr + (vanilla_addr - region_addr);

                // TODO remove
                if (relocated_addr != vanilla_addr) {
                    mkb::OSReport("Vanilla addr: 0x%08X, relocated addr: 0x%08X, live section addr: 0x%08X, bss: %d\n", vanilla_addr, relocated_addr, live_addr, region.is_bss);
                    MOD_ASSERT(relocated_addr == vanilla_addr);
                }

                return reinterpret_cast<void*>(relocated_addr);
            }
        }
    }

    return nullptr;
}

}  // namespace relutil
