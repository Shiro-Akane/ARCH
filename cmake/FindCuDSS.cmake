# Optional library discovery only. Production route registration belongs to
# the executor and must not be enabled merely because a library was found.
find_path(CuDSS_INCLUDE_DIR cudss.h
    HINTS "${CUDSS_ROOT}" ENV CUDSS_ROOT ENV CUDSS_DIR
    PATH_SUFFIXES include)
find_library(CuDSS_LIBRARY NAMES cudss libcudss.so.0
    HINTS "${CUDSS_ROOT}" ENV CUDSS_ROOT ENV CUDSS_DIR
    PATH_SUFFIXES lib lib64)
include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(CuDSS
    REQUIRED_VARS CuDSS_LIBRARY CuDSS_INCLUDE_DIR)
if(CuDSS_FOUND AND NOT TARGET CuDSS::cudss)
    add_library(CuDSS::cudss UNKNOWN IMPORTED)
    set_target_properties(CuDSS::cudss PROPERTIES
        IMPORTED_LOCATION "${CuDSS_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${CuDSS_INCLUDE_DIR}"
        INTERFACE_LINK_LIBRARIES "CUDA::cudart;CUDA::cublas")
endif()
mark_as_advanced(CuDSS_INCLUDE_DIR CuDSS_LIBRARY)
