/*
 * PROJECT:     ReactOS msvcrt.dll
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     ARM C++ MSVC aliases for cpp.c
 * COPYRIGHT:   Copyright 2014-2024 Wine team
 *              Copyright 2026 Jonathan McAvin <mcavinjohn@gmail.com>
 */

#include <kxarm.h>

// generated from the arm and arch independent C++ entries in msvcrt.spec

    IMPORT MANGLED(??0__non_rtti_object@@QAA@ABV0@@Z), WEAK MANGLED(__non_rtti_object_copy_ctor)
    IMPORT MANGLED(??0__non_rtti_object@@QAA@PBD@Z), WEAK MANGLED(__non_rtti_object_ctor)
    IMPORT MANGLED(??0bad_cast@@AAA@PBQBD@Z), WEAK MANGLED(bad_cast_ctor)
    IMPORT MANGLED(??0bad_cast@@QAA@ABQBD@Z), WEAK MANGLED(bad_cast_ctor)
    IMPORT MANGLED(??0bad_cast@@QAA@ABV0@@Z), WEAK MANGLED(bad_cast_copy_ctor)
    IMPORT MANGLED(??0bad_cast@@QAA@PBD@Z), WEAK MANGLED(bad_cast_ctor_charptr)
    IMPORT MANGLED(??0bad_typeid@@QAA@ABV0@@Z), WEAK MANGLED(bad_typeid_copy_ctor)
    IMPORT MANGLED(??0bad_typeid@@QAA@PBD@Z), WEAK MANGLED(bad_typeid_ctor)
    IMPORT MANGLED(??0exception@@QAA@ABQBD@Z), WEAK MANGLED(exception_ctor)
    IMPORT MANGLED(??0exception@@QAA@ABQBDH@Z), WEAK MANGLED(exception_ctor_noalloc)
    IMPORT MANGLED(??0exception@@QAA@ABV0@@Z), WEAK MANGLED(exception_copy_ctor)
    IMPORT MANGLED(??0exception@@QAA@XZ), WEAK MANGLED(exception_default_ctor)
    IMPORT MANGLED(??1__non_rtti_object@@UAA@XZ), WEAK MANGLED(__non_rtti_object_dtor)
    IMPORT MANGLED(??1bad_cast@@UAA@XZ), WEAK MANGLED(bad_cast_dtor)
    IMPORT MANGLED(??1bad_typeid@@UAA@XZ), WEAK MANGLED(bad_typeid_dtor)
    IMPORT MANGLED(??1exception@@UAA@XZ), WEAK MANGLED(exception_dtor)
    IMPORT MANGLED(??1type_info@@UAA@XZ), WEAK MANGLED(type_info_dtor)
    IMPORT MANGLED(??2@YAPAXI@Z), WEAK MANGLED(operator_new)
    IMPORT MANGLED(??2@YAPAXIHPBDH@Z), WEAK MANGLED(operator_new_dbg)
    IMPORT MANGLED(??3@YAXPAX@Z), WEAK MANGLED(operator_delete)
    IMPORT MANGLED(??4__non_rtti_object@@QAAAAV0@ABV0@@Z), WEAK MANGLED(__non_rtti_object_opequals)
    IMPORT MANGLED(??4bad_cast@@QAAAAV0@ABV0@@Z), WEAK MANGLED(bad_cast_opequals)
    IMPORT MANGLED(??4bad_typeid@@QAAAAV0@ABV0@@Z), WEAK MANGLED(bad_typeid_opequals)
    IMPORT MANGLED(??4exception@@QAAAAV0@ABV0@@Z), WEAK MANGLED(exception_opequals)
    IMPORT MANGLED(??8type_info@@QBAHABV0@@Z), WEAK MANGLED(type_info_opequals_equals)
    IMPORT MANGLED(??9type_info@@QBAHABV0@@Z), WEAK MANGLED(type_info_opnot_equals)
    IMPORT MANGLED(??_7__non_rtti_object@@6B@), WEAK MANGLED(__non_rtti_object_vtable)
    IMPORT MANGLED(??_7bad_cast@@6B@), WEAK MANGLED(bad_cast_vtable)
    IMPORT MANGLED(??_7bad_typeid@@6B@), WEAK MANGLED(bad_typeid_vtable)
    IMPORT MANGLED(??_7exception@@6B@), WEAK MANGLED(exception_vtable)
    IMPORT MANGLED(??_Fbad_cast@@QAAXXZ), WEAK MANGLED(bad_cast_default_ctor)
    IMPORT MANGLED(??_Fbad_typeid@@QAAXXZ), WEAK MANGLED(bad_typeid_default_ctor)
    IMPORT MANGLED(??_U@YAPAXI@Z), WEAK MANGLED(operator_new)
    IMPORT MANGLED(??_U@YAPAXIHPBDH@Z), WEAK MANGLED(operator_new_dbg)
    IMPORT MANGLED(??_V@YAXPAX@Z), WEAK MANGLED(operator_delete)
    IMPORT MANGLED(?_query_new_handler@@YAP6AHI@ZXZ), WEAK MANGLED(_query_new_handler)
    IMPORT MANGLED(?_query_new_mode@@YAHXZ), WEAK MANGLED(_query_new_mode)
    IMPORT MANGLED(?_set_new_handler@@YAP6AHI@ZP6AHI@Z@Z), WEAK MANGLED(_set_new_handler)
    IMPORT MANGLED(?_set_new_mode@@YAHH@Z), WEAK MANGLED(_set_new_mode)
    IMPORT MANGLED(?_set_se_translator@@YAP6AXIPAU_EXCEPTION_POINTERS@@@ZP6AXI0@Z@Z), WEAK MANGLED(_set_se_translator)
    IMPORT MANGLED(?before@type_info@@QBAHABV1@@Z), WEAK MANGLED(type_info_before)
    IMPORT MANGLED(?name@type_info@@QBAPBDXZ), WEAK MANGLED(type_info_name)
    IMPORT MANGLED(?raw_name@type_info@@QBAPBDXZ), WEAK MANGLED(type_info_raw_name)
    IMPORT MANGLED(?set_new_handler@@YAP6AXXZP6AXXZ@Z), WEAK MANGLED(set_new_handler)
    IMPORT MANGLED(?set_terminate@@YAP6AXXZP6AXXZ@Z), WEAK MANGLED(set_terminate)
    IMPORT MANGLED(?set_unexpected@@YAP6AXXZP6AXXZ@Z), WEAK MANGLED(set_unexpected)
    IMPORT MANGLED(?terminate@@YAXXZ), WEAK MANGLED(terminate)
    IMPORT MANGLED(?unexpected@@YAXXZ), WEAK MANGLED(unexpected)
    IMPORT MANGLED(?what@exception@@UBAPBDXZ), WEAK MANGLED(exception_what)

    END
