
list(APPEND SOURCE
    dtb.c
    pcihost.c
    dtb.h)

add_library(dtb MODULE ${SOURCE} dtb.rc)
set_module_type(dtb kernelmodedriver)
target_link_libraries(dtb fdtlib wdmguid libcntpr)
add_importlibs(dtb ntoskrnl hal)
add_cd_file(TARGET dtb DESTINATION reactos/system32/drivers NO_CAB FOR all)
