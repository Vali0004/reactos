/*
 * PROJECT:     ReactOS SDK
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Visual C++ ARM intrinsics clang-cl lacks for 32-bit ARM
 * COPYRIGHT:   Copyright 2026 Jonathan McAvin <mcavinjohn@gmail.com>
 */

#pragma once

/* intrin.h declared these as functions already, macros take over from here */

#define _ReturnAddress() (__builtin_return_address(0))

#define _ReadWriteBarrier() __asm__ __volatile__("" : : : "memory")

#define _disable() __asm__ __volatile__("cpsid i" : : : "memory")
#define _enable() __asm__ __volatile__("cpsie i" : : : "memory")

/* zero has no leading one, Visual C++ returns the full width */
#define _CountLeadingZeros(Mask) \
    ({ unsigned long __clz_m = (Mask); __clz_m ? (unsigned int)__builtin_clzl(__clz_m) : 32u; })
#define _CountLeadingZeros64(Mask) \
    ({ unsigned __int64 __clz_m = (Mask); __clz_m ? (unsigned int)__builtin_clzll(__clz_m) : 64u; })

/* _InterlockedAdd returns the new value, ExchangeAdd the old one */
#define _InterlockedAdd(Addend, Value) __atomic_add_fetch((Addend), (Value), __ATOMIC_SEQ_CST)
#define _InterlockedAdd64(Addend, Value) __atomic_add_fetch((Addend), (Value), __ATOMIC_SEQ_CST)
