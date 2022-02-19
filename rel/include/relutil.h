#pragma once

#include "mkb.h"

namespace relutil {

/**
 * Returns one past the last address of relocation data in mainloop.rel which we are free to
 * overwrite.
 */
void* compute_mainloop_reldata_boundary();

/**
 * Adjusts a pointer to account for differences in REL load locations compared to vanilla.
 * 
 * RELs will be loaded at different addresses when using the merge-heaps patch for example.
 * It is not necessary to "relocate" symbols which appear in mkb2.us.lst .
 */
void* relocate_addr(u32 vanilla_addr);
void* relocate_ptr(void* vanilla_ptr);

}  // namespace relutil