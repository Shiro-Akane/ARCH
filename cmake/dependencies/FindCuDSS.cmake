# Optional library discovery only. Production route registration belongs to
# the executor and must not be enabled merely because a library was found.
find_path(CuDSS_INCLUDE_DIR cudss.h
    HINTS "${CUDSS_ROOT}" ENV CUDSS_ROOT ENV CUDSS_DIR
    PATH_SUFFIXES include)
find_library(CuDSS_LIBRARY NAMES cudss libcudss.so.0
    HINTS "${CUDSS_ROOT}" ENV CUDSS_ROOT ENV CUDSS_DIR
    PATH_SUFFIXES lib lib64)
include(FindPackageHandleStandardArgs)
if(WIN32)
    # Import libraries identify the ABI, not the DLL implementation. Preserve
    # an explicit runtime binding for the shared validation evidence reader.
    find_file(CuDSS_RUNTIME_LIBRARY NAMES cudss64_0.dll
        HINTS "${CUDSS_ROOT}" ENV CUDSS_ROOT ENV CUDSS_DIR
        PATH_SUFFIXES bin lib)
endif()
find_package_handle_standard_args(CuDSS
    REQUIRED_VARS CuDSS_LIBRARY CuDSS_INCLUDE_DIR)
mark_as_advanced(CuDSS_INCLUDE_DIR CuDSS_LIBRARY CuDSS_RUNTIME_LIBRARY)
