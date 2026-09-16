# Keep runtime image eligibility tied to the same explicit list used by CMake
# for CUDA compilation. This module contains no physical-feature requirements.
function(arch_resolve_cuda_code_images output)
    set(images "${CMAKE_CUDA_ARCHITECTURES}")
    if(images STREQUAL "native")
        set(images "${CMAKE_CUDA_ARCHITECTURES_NATIVE}")
    elseif(images STREQUAL "all")
        set(images "${CMAKE_CUDA_ARCHITECTURES_ALL}")
    elseif(images STREQUAL "all-major")
        set(images "${CMAKE_CUDA_ARCHITECTURES_ALL_MAJOR}")
    endif()
    if(NOT images)
        message(FATAL_ERROR
            "ARCH needs an explicit CUDA image list. Set CMAKE_CUDA_ARCHITECTURES "
            "to numeric targets (for example 80;86), or use a CMake/compiler "
            "that resolves native/all/all-major. OFF and manual -gencode bypass "
            "the startup compatibility contract.")
    endif()
    foreach(image IN LISTS images)
        if(NOT image MATCHES "^[1-9][0-9]+(-(real|virtual))?$")
            message(FATAL_ERROR
                "ARCH cannot classify CUDA image '${image}'. Use ordinary numeric "
                "CMAKE_CUDA_ARCHITECTURES targets, optionally -real or -virtual. "
                "Architecture/family-specific a/f targets need a separate compatibility rule.")
        endif()
    endforeach()
    foreach(config "" _DEBUG _RELEASE _RELWITHDEBINFO _MINSIZEREL)
        if(CMAKE_CUDA_FLAGS${config} MATCHES "(-gencode|--generate-code|-arch[ =]|--gpu-architecture|-code[ =]|--gpu-code)")
            message(FATAL_ERROR
                "Set CUDA code images through CMAKE_CUDA_ARCHITECTURES, not "
                "CMAKE_CUDA_FLAGS${config}; startup must describe the compiled images.")
        endif()
    endforeach()
    list(REMOVE_DUPLICATES images)
    set(${output} "${images}" PARENT_SCOPE)
endfunction()
