# Note: this bitness check does not work for universal builds.
if (CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(_openvr_bitness 64)
else ()
    set(_openvr_bitness 32)
endif ()

if (CMAKE_SYSTEM_NAME MATCHES "Darwin")
    set(_openvr_platform_base osx)
    set(OpenVR_PLATFORM osx32)
elseif (CMAKE_SYSTEM_NAME MATCHES "Linux")
    set(_openvr_platform_base linux)
    set(OpenVR_PLATFORM "${_openvr_platform_base}${_openvr_bitness}")
elseif (WIN32)
    set(_openvr_platform_base win)
    set(OpenVR_PLATFORM "${_openvr_platform_base}${_openvr_bitness}")
endif ()

find_path(OpenVR_INCLUDE_DIR
    NAMES
        openvr.h
        openvr_driver.h
    PATH_SUFFIXES
        headers)
mark_as_advanced(OpenVR_INCLUDE_DIR)

set(_openvr_lib_name "")
if (WIN32)
    set(_openvr_lib_name openvr_api)
elseif (CMAKE_SYSTEM_NAME MATCHES "Linux")
    set(_openvr_lib_name openvr_api)
elseif (CMAKE_SYSTEM_NAME MATCHES "Darwin")
    set(_openvr_lib_name openvr_api)
endif()

find_library(OpenVR_LIBRARY
    NAMES ${_openvr_lib_name}
    PATHS ${OpenVR_ROOT}
    PATH_SUFFIXES "lib/${OpenVR_PLATFORM}")
mark_as_advanced(OpenVR_LIBRARY)

if (WIN32)
    find_file(OpenVR_BINARY
        NAMES openvr_api.dll
        PATHS ${OpenVR_ROOT}
        PATH_SUFFIXES "bin/${OpenVR_PLATFORM}")
    mark_as_advanced(OpenVR_BINARY)
else()
    set(OpenVR_BINARY ${OpenVR_LIBRARY})
endif()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(OpenVR
    REQUIRED_VARS OpenVR_LIBRARY OpenVR_INCLUDE_DIR)

if (OpenVR_FOUND)
    set(OpenVR_INCLUDE_DIRS "${OpenVR_INCLUDE_DIR}")
    set(OpenVR_LIBRARIES "${OpenVR_LIBRARY}")

    if (NOT TARGET OpenVR::API)
        add_library(OpenVR::API SHARED IMPORTED)
        if (WIN32)
            set_target_properties(OpenVR::API
                PROPERTIES
                    IMPORTED_IMPLIB "${OpenVR_LIBRARY}"
                    IMPORTED_LOCATION "${OpenVR_BINARY}"
                    INTERFACE_INCLUDE_DIRECTORIES "${OpenVR_INCLUDE_DIR}")
        else()
            set_target_properties(OpenVR::API
                PROPERTIES
                    IMPORTED_LOCATION "${OpenVR_LIBRARY}"
                    INTERFACE_INCLUDE_DIRECTORIES "${OpenVR_INCLUDE_DIR}")
        endif()
    endif ()

    if (NOT TARGET OpenVR::Driver)
        add_library(OpenVR::Driver INTERFACE IMPORTED)
        set_target_properties(OpenVR::Driver
            PROPERTIES
                INTERFACE_INCLUDE_DIRECTORIES "${OpenVR_INCLUDE_DIR}")
    endif ()
endif ()

unset(_openvr_bitness)
unset(_openvr_platform_base)
unset(_openvr_lib_name)