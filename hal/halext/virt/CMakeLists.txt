
list(APPEND SOURCE
    halextvirt.c)

add_library(halextvirt MODULE
    ${SOURCE}
    halextvirt.rc)

set_module_type(halextvirt kerneldll ENTRYPOINT HalExtensionInit 12)
add_importlibs(halextvirt ntoskrnl)
add_cd_file(TARGET halextvirt DESTINATION reactos/system32 FOR all)
