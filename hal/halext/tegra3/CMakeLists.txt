
list(APPEND SOURCE
    halexttegra3.c)

add_library(halexttegra3 MODULE
    ${SOURCE}
    halexttegra3.rc)

set_module_type(halexttegra3 kerneldll ENTRYPOINT HalExtensionInit 12)
add_importlibs(halexttegra3 ntoskrnl)
add_cd_file(TARGET halexttegra3 DESTINATION reactos/system32 FOR all)
