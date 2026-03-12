# Strict compiler warnings

function(protocoll_set_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /WX)
    else()
        target_compile_options(${target} PRIVATE
            -Wall -Wextra -Wpedantic -Werror
            -Wno-unused-parameter
        )
        # GCC 12 emits a false positive -Wstringop-overread on std::vector<uint8_t> comparisons
        if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
            target_compile_options(${target} PRIVATE -Wno-stringop-overread)
        endif()
    endif()
endfunction()
