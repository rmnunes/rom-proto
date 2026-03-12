# FindMbedTLS.cmake — Locate mbedTLS (Mbed TLS) on the system.
#
# This module defines:
#   MbedTLS_FOUND        - True if mbedTLS was found
#   MbedTLS_INCLUDE_DIRS - Include directories
#   MbedTLS_LIBRARIES    - Libraries to link
#
# Imported targets:
#   MbedTLS::mbedtls     - The mbedtls library
#   MbedTLS::mbedx509    - The mbedx509 library
#   MbedTLS::mbedcrypto  - The mbedcrypto library
#
# Hints:
#   MBEDTLS_ROOT_DIR     - Root directory of mbedTLS installation

find_path(MbedTLS_INCLUDE_DIR
    NAMES mbedtls/ssl.h
    PATHS
        ${MBEDTLS_ROOT_DIR}/include
        /usr/include
        /usr/local/include
        /opt/homebrew/include
)

find_library(MbedTLS_LIBRARY
    NAMES mbedtls
    PATHS
        ${MBEDTLS_ROOT_DIR}/lib
        /usr/lib
        /usr/local/lib
        /opt/homebrew/lib
)

find_library(MbedX509_LIBRARY
    NAMES mbedx509
    PATHS
        ${MBEDTLS_ROOT_DIR}/lib
        /usr/lib
        /usr/local/lib
        /opt/homebrew/lib
)

find_library(MbedCrypto_LIBRARY
    NAMES mbedcrypto
    PATHS
        ${MBEDTLS_ROOT_DIR}/lib
        /usr/lib
        /usr/local/lib
        /opt/homebrew/lib
)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(MbedTLS
    REQUIRED_VARS MbedTLS_INCLUDE_DIR MbedTLS_LIBRARY MbedX509_LIBRARY MbedCrypto_LIBRARY
)

if(MbedTLS_FOUND)
    set(MbedTLS_INCLUDE_DIRS ${MbedTLS_INCLUDE_DIR})
    set(MbedTLS_LIBRARIES ${MbedTLS_LIBRARY} ${MbedX509_LIBRARY} ${MbedCrypto_LIBRARY})

    if(NOT TARGET MbedTLS::mbedcrypto)
        add_library(MbedTLS::mbedcrypto UNKNOWN IMPORTED)
        set_target_properties(MbedTLS::mbedcrypto PROPERTIES
            IMPORTED_LOCATION "${MbedCrypto_LIBRARY}"
            INTERFACE_INCLUDE_DIRECTORIES "${MbedTLS_INCLUDE_DIR}"
        )
    endif()

    if(NOT TARGET MbedTLS::mbedx509)
        add_library(MbedTLS::mbedx509 UNKNOWN IMPORTED)
        set_target_properties(MbedTLS::mbedx509 PROPERTIES
            IMPORTED_LOCATION "${MbedX509_LIBRARY}"
            INTERFACE_INCLUDE_DIRECTORIES "${MbedTLS_INCLUDE_DIR}"
            INTERFACE_LINK_LIBRARIES MbedTLS::mbedcrypto
        )
    endif()

    if(NOT TARGET MbedTLS::mbedtls)
        add_library(MbedTLS::mbedtls UNKNOWN IMPORTED)
        set_target_properties(MbedTLS::mbedtls PROPERTIES
            IMPORTED_LOCATION "${MbedTLS_LIBRARY}"
            INTERFACE_INCLUDE_DIRECTORIES "${MbedTLS_INCLUDE_DIR}"
            INTERFACE_LINK_LIBRARIES MbedTLS::mbedx509
        )
    endif()
endif()

mark_as_advanced(MbedTLS_INCLUDE_DIR MbedTLS_LIBRARY MbedX509_LIBRARY MbedCrypto_LIBRARY)
