
include_directories(${REACTOS_SOURCE_DIR}/sdk/include/reactos/drivers)

list(APPEND SOURCE
    blue.c
    font.c
    blue.h)

add_library(blue MODULE ${SOURCE} blue.rc)
set_module_type(blue kernelmodedriver)
if(ARCH STREQUAL "arm")
    # no VGA, the text screen is drawn on the boot framebuffer
    add_importlibs(blue ntoskrnl hal bootvid)
else()
    add_importlibs(blue ntoskrnl hal)
endif()
add_pch(blue blue.h SOURCE)
add_cd_file(TARGET blue DESTINATION reactos/system32/drivers NO_CAB FOR all)
