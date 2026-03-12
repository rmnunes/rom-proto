# Strict compiler warnings

function(protocoll_set_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /WX)
    else()
        target_compile_options(${target} PRIVATE
            -Wall -Wextra -Wpedantic -Werror
            -Wno-unused-parameter
        )
    endif()
endfunction()
