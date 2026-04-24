# Common warning flags applied to our own targets.
# Usage: target_link_libraries(<tgt> PRIVATE ajazz::warnings)

add_library(ajazz_warnings INTERFACE)
add_library(ajazz::warnings ALIAS ajazz_warnings)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
    target_compile_options(ajazz_warnings INTERFACE
        -Wall
        -Wextra
        -Wpedantic
        -Wshadow
        -Wold-style-cast
        -Wcast-align
        -Wconversion
        -Wsign-conversion
        -Wnull-dereference
        -Wdouble-promotion
        -Wformat=2
        -Wimplicit-fallthrough
    )
    if(AJAZZ_ENABLE_WERROR)
        target_compile_options(ajazz_warnings INTERFACE -Werror)
    endif()
elseif(MSVC)
    target_compile_options(ajazz_warnings INTERFACE
        /W4
        /permissive-
        /Zc:__cplusplus
        /Zc:preprocessor
    )
    if(AJAZZ_ENABLE_WERROR)
        target_compile_options(ajazz_warnings INTERFACE /WX)
    endif()
endif()
