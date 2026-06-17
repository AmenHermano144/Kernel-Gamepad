#pragma once

#include <wdm.h>

#define GAMEPAD_POOL_TAG 'GpCr'

inline void* __cdecl operator new(size_t size, POOL_TYPE pool)
{
    return ExAllocatePool2(POOL_FLAG_NON_PAGED, size, GAMEPAD_POOL_TAG);
}

inline void* __cdecl operator new(size_t size)
{
    return ExAllocatePool2(POOL_FLAG_NON_PAGED, size, GAMEPAD_POOL_TAG);
}

inline void __cdecl operator delete(void* ptr)
{
    if (ptr) ExFreePoolWithTag(ptr, GAMEPAD_POOL_TAG);
}

inline void __cdecl operator delete(void* ptr, size_t)
{
    if (ptr) ExFreePoolWithTag(ptr, GAMEPAD_POOL_TAG);
}

inline void* __cdecl operator new[](size_t size)
{
    return ExAllocatePool2(POOL_FLAG_NON_PAGED, size, GAMEPAD_POOL_TAG);
}

inline void __cdecl operator delete[](void* ptr)
{
    if (ptr) ExFreePoolWithTag(ptr, GAMEPAD_POOL_TAG);
}

inline void __cdecl operator delete[](void* ptr, size_t)
{
    if (ptr) ExFreePoolWithTag(ptr, GAMEPAD_POOL_TAG);
}
