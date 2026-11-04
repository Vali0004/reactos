
add_definitions(
    -D_NTHALDLL_
    -D_NTHAL_)

include_directories(
    include
    ${REACTOS_SOURCE_DIR}/hal/halx86/include
    ${REACTOS_SOURCE_DIR}/ntoskrnl/include)

spec2def(hal.dll ../hal.spec ADD_IMPORTLIB)

list(APPEND SOURCE
    generic/acpi.c
    generic/dtb.c
    generic/gic.c
    generic/extintr.c
    generic/exttimer.c
    generic/halext.c
    generic/halinit.c
    generic/halpnp.c
    generic/irql.c
    generic/memory.c
    generic/misc.c
    generic/pci.c
    generic/timer.c
    ${REACTOS_SOURCE_DIR}/hal/halx86/acpi/halpnpdd.c
    ${REACTOS_SOURCE_DIR}/hal/halx86/acpi/mmconfig.c
    ${REACTOS_SOURCE_DIR}/hal/halx86/generic/dma.c)

if(ARCH STREQUAL "arm")
    # the uniprocessor spinlocks and IRQL wrappers the 32-bit HAL exports
    list(APPEND SOURCE
        ${REACTOS_SOURCE_DIR}/hal/halarm/generic/spinlock.c
        generic/arm/portio.c)
    list(APPEND ASM_SOURCE
        generic/arm/cache.s)
else()
    list(APPEND ASM_SOURCE
        generic/cache.s)
endif()

add_asm_files(hal_asm ${ASM_SOURCE})

add_library(hal MODULE
    ${SOURCE}
    ${hal_asm}
    halarm64.rc
    ${CMAKE_CURRENT_BINARY_DIR}/hal.def)

target_link_libraries(hal libcntpr fdtlib ${PSEH_LIB})
add_importlibs(hal ntoskrnl)
add_dependencies(hal psdk asm)
set_module_type(hal kerneldll ENTRYPOINT HalInitSystem 8)
add_cd_file(TARGET hal DESTINATION reactos/system32 NO_CAB FOR all)
target_link_options(hal PRIVATE /ignore:4216 /SECTION:INIT,ERWD)
