
list(APPEND SOURCE
    halextqsd8x50.c)

add_library(halextqsd8x50 MODULE
    ${SOURCE}
    halextqsd8x50.rc)

set_module_type(halextqsd8x50 kerneldll ENTRYPOINT HalExtensionInit 12)
add_importlibs(halextqsd8x50 ntoskrnl)
add_cd_file(TARGET halextqsd8x50 DESTINATION reactos/system32 FOR all)
