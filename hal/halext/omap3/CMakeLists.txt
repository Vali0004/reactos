
list(APPEND SOURCE
    halextomap3.c)

add_library(halextomap3 MODULE
    ${SOURCE}
    halextomap3.rc)

set_module_type(halextomap3 kerneldll ENTRYPOINT HalExtensionInit 12)
add_importlibs(halextomap3 ntoskrnl)
add_cd_file(TARGET halextomap3 DESTINATION reactos/system32 FOR all)
