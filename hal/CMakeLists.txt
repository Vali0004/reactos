
project(HAL)
if((ARCH STREQUAL "i386") OR (ARCH STREQUAL "amd64"))
    add_subdirectory(halx86)
elseif((ARCH STREQUAL "arm") OR (ARCH STREQUAL "arm64"))
    # the ARM64 HAL also builds for 32-bit ARM, GIC, generic timer and PSCI alike
    add_subdirectory(halarm64)
endif()

if(ARCH STREQUAL "arm")
    add_subdirectory(halext/omap3)
    add_subdirectory(halext/virt)
endif()
