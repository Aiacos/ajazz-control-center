# Common warning flags applied to our own targets.
# Usage: target_link_libraries(<tgt> PRIVATE ajazz::warnings)

add_library(ajazz_warnings INTERFACE)
add_library(ajazz::warnings ALIAS ajazz_warnings)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
    target_compile_options(
        ajazz_warnings
        INTERFACE -Wall
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
    # GCC 16.x's -Wnull-dereference turned over-eager on std::vector element access inlined through
    # lambdas (e.g. the install() reply handler in plugin_catalog_model.cpp): it reports false
    # positives originating in <bits/stl_vector.h> that -Werror then makes fatal, and they are not
    # silenceable from the call site (even an explicit bounds check before the access doesn't
    # convince it). The CI toolchains (older GCC / Clang / MSVC) don't hit this. Neutralise the flag
    # on GCC >= 16 ONLY — appended last so it wins over -Wnull-dereference — keeping the warning
    # active everywhere else.
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_GREATER_EQUAL 16)
        target_compile_options(ajazz_warnings INTERFACE -Wno-null-dereference)
    endif()
elseif(MSVC)
    # /wd4702 (unreachable code): Qt's own headers (qmetatype.h, qvariant.h, qjsengine.h) emit C4702
    # under /W4, which /WX then promotes to a hard error when the qmlcache-generated translation
    # units include them. It is never our code, and there is no targeted pragma we can place in
    # generated files, so suppress it project-wide. Seen on both Qt 6.8.3 (CI) and the 6.11.x local
    # toolchain.
    target_compile_options(
        ajazz_warnings INTERFACE /W4 /permissive- /Zc:__cplusplus /Zc:preprocessor /wd4702
    )
    if(AJAZZ_ENABLE_WERROR)
        target_compile_options(ajazz_warnings INTERFACE /WX)
    endif()
endif()
