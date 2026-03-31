// VulkanMemoryAllocator implementation unit.
//
// VMA is a header-only library but requires exactly ONE translation unit to
// define VMA_IMPLEMENTATION before the include so that function bodies are
// emitted.  All other files must include vk_mem_alloc.h WITHOUT the define.
//
// SETUP: download vk_mem_alloc.h from
//   https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator/releases
// and place it at:
//   include/vma/vk_mem_alloc.h

#define VMA_IMPLEMENTATION
#include "vma/vk_mem_alloc.h"
