# FindFirebird.cmake
# Find Firebird client library and headers

find_path(FIREBIRD_INCLUDE_DIR
    NAMES firebird/Interface.h
    HINTS
        /usr/include
        /usr/local/include
        /opt/firebird/include
        $ENV{FIREBIRD}/include
)

find_library(FIREBIRD_LIBRARY
    NAMES fbclient
    HINTS
        /usr/lib
        /usr/lib/x86_64-linux-gnu
        /usr/local/lib
        /opt/firebird/lib
        $ENV{FIREBIRD}/lib
)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(Firebird DEFAULT_MSG
    FIREBIRD_LIBRARY FIREBIRD_INCLUDE_DIR
)

if(FIREBIRD_FOUND)
    set(FIREBIRD_LIBRARIES ${FIREBIRD_LIBRARY})
    set(FIREBIRD_INCLUDE_DIRS ${FIREBIRD_INCLUDE_DIR})
endif()

mark_as_advanced(FIREBIRD_INCLUDE_DIR FIREBIRD_LIBRARY)