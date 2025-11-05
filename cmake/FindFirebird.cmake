# FindFirebird.cmake
# Locate Firebird client headers and libraries on both Linux and Windows.

set(_FIREBIRD_ROOT_HINTS
    ${Firebird_ROOT}
    ${FIREBIRD_ROOT}
    $ENV{Firebird_ROOT}
    $ENV{FIREBIRD_ROOT}
    $ENV{FIREBIRD}
)

set(_FIREBIRD_DEFAULT_PREFIXES
    "C:/fb50"
    "C:/Program Files/Firebird"
    "C:/Program Files/Firebird/Firebird_2_5"
    "C:/Program Files/Firebird/Firebird_3_0"
    "C:/Program Files/Firebird/Firebird_4_0"
    "C:/Program Files/Firebird/Firebird_5_0"
    "C:/Program Files (x86)/Firebird"
    /opt/firebird
    /usr/local
    /usr
)

if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    list(APPEND _FIREBIRD_DEFAULT_PREFIXES $ENV{ProgramFiles}/Firebird $ENV{ProgramFiles\(x86\)}/Firebird)
endif()

list(APPEND _FIREBIRD_SEARCH_PREFIXES ${_FIREBIRD_ROOT_HINTS} ${_FIREBIRD_DEFAULT_PREFIXES})
list(REMOVE_DUPLICATES _FIREBIRD_SEARCH_PREFIXES)

find_path(FIREBIRD_INCLUDE_DIR
    NAMES firebird/Interface.h
    HINTS ${_FIREBIRD_SEARCH_PREFIXES}
    PATH_SUFFIXES include include/firebird Firebird/include
    NO_DEFAULT_PATH
)

# Also fall back to the default search paths so CMake can find system installs.
if(NOT FIREBIRD_INCLUDE_DIR)
    find_path(FIREBIRD_INCLUDE_DIR
        NAMES firebird/Interface.h
        PATHS
            /usr/include
            /usr/local/include
            /opt/firebird/include
    )
endif()

find_library(FIREBIRD_LIBRARY
    NAMES fbclient fbclient_ms fbclient64
    HINTS ${_FIREBIRD_SEARCH_PREFIXES}
    PATH_SUFFIXES lib lib64 msvc/lib msbuild/x64
    NO_DEFAULT_PATH
)

if(NOT FIREBIRD_LIBRARY)
    find_library(FIREBIRD_LIBRARY
        NAMES fbclient
        PATHS
            /usr/lib
            /usr/lib/x86_64-linux-gnu
            /usr/local/lib
            /opt/firebird/lib
    )
endif()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(Firebird DEFAULT_MSG
    FIREBIRD_LIBRARY FIREBIRD_INCLUDE_DIR
)

if(FIREBIRD_FOUND)
    set(FIREBIRD_LIBRARIES ${FIREBIRD_LIBRARY})
    set(FIREBIRD_INCLUDE_DIRS ${FIREBIRD_INCLUDE_DIR})
endif()

mark_as_advanced(FIREBIRD_INCLUDE_DIR FIREBIRD_LIBRARY)
