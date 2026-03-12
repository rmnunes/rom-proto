# FindDPDK.cmake — Locate Intel DPDK (Data Plane Development Kit).
#
# This module defines:
#   DPDK_FOUND          - True if DPDK was found
#   DPDK_INCLUDE_DIRS   - Include directories
#   DPDK_LIBRARIES      - Libraries to link
#
# Imported target:
#   DPDK::DPDK          - The DPDK libraries
#
# Hints:
#   DPDK_ROOT_DIR       - Root directory of DPDK installation
#   RTE_SDK             - DPDK SDK path (traditional env var)
#
# DPDK is typically installed via meson/pkg-config. This module
# first tries pkg-config, then falls back to manual search.

# Try pkg-config first (most reliable for DPDK)
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(_DPDK QUIET libdpdk)
endif()

if(_DPDK_FOUND)
    set(DPDK_FOUND TRUE)
    set(DPDK_INCLUDE_DIRS ${_DPDK_INCLUDE_DIRS})
    set(DPDK_LIBRARIES ${_DPDK_LIBRARIES})
    set(DPDK_LIBRARY_DIRS ${_DPDK_LIBRARY_DIRS})
else()
    # Manual search
    set(_dpdk_search_paths
        ${DPDK_ROOT_DIR}
        $ENV{RTE_SDK}/$ENV{RTE_TARGET}
        $ENV{RTE_SDK}/build
        /usr
        /usr/local
        /opt/dpdk
    )

    find_path(DPDK_INCLUDE_DIR
        NAMES rte_eal.h
        PATH_SUFFIXES include dpdk include/dpdk
        PATHS ${_dpdk_search_paths}
    )

    find_library(DPDK_rte_eal_LIBRARY
        NAMES rte_eal
        PATH_SUFFIXES lib lib64 lib/x86_64-linux-gnu
        PATHS ${_dpdk_search_paths}
    )

    find_library(DPDK_rte_ethdev_LIBRARY
        NAMES rte_ethdev
        PATH_SUFFIXES lib lib64 lib/x86_64-linux-gnu
        PATHS ${_dpdk_search_paths}
    )

    find_library(DPDK_rte_mbuf_LIBRARY
        NAMES rte_mbuf
        PATH_SUFFIXES lib lib64 lib/x86_64-linux-gnu
        PATHS ${_dpdk_search_paths}
    )

    find_library(DPDK_rte_mempool_LIBRARY
        NAMES rte_mempool
        PATH_SUFFIXES lib lib64 lib/x86_64-linux-gnu
        PATHS ${_dpdk_search_paths}
    )

    include(FindPackageHandleStandardArgs)
    find_package_handle_standard_args(DPDK
        REQUIRED_VARS DPDK_INCLUDE_DIR DPDK_rte_eal_LIBRARY
    )

    if(DPDK_FOUND)
        set(DPDK_INCLUDE_DIRS ${DPDK_INCLUDE_DIR})
        set(DPDK_LIBRARIES
            ${DPDK_rte_eal_LIBRARY}
            ${DPDK_rte_ethdev_LIBRARY}
            ${DPDK_rte_mbuf_LIBRARY}
            ${DPDK_rte_mempool_LIBRARY}
        )
    endif()
endif()

if(DPDK_FOUND AND NOT TARGET DPDK::DPDK)
    add_library(DPDK::DPDK INTERFACE IMPORTED)
    set_target_properties(DPDK::DPDK PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES "${DPDK_INCLUDE_DIRS}"
        INTERFACE_LINK_LIBRARIES "${DPDK_LIBRARIES}"
    )
    if(DPDK_LIBRARY_DIRS)
        set_target_properties(DPDK::DPDK PROPERTIES
            INTERFACE_LINK_DIRECTORIES "${DPDK_LIBRARY_DIRS}"
        )
    endif()
endif()

mark_as_advanced(DPDK_INCLUDE_DIR DPDK_rte_eal_LIBRARY DPDK_rte_ethdev_LIBRARY
                 DPDK_rte_mbuf_LIBRARY DPDK_rte_mempool_LIBRARY)
