# Optional ASan/UBSan helpers. Enabled via -DAJAZZ_ENABLE_SANITIZERS=ON.

add_library(ajazz_sanitizers INTERFACE)
add_library(ajazz::sanitizers ALIAS ajazz_sanitizers)

if(AJAZZ_ENABLE_SANITIZERS)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
        target_compile_options(
            ajazz_sanitizers INTERFACE -fsanitize=address,undefined -fno-omit-frame-pointer
                                       -fno-sanitize-recover=all
        )
        target_link_options(ajazz_sanitizers INTERFACE -fsanitize=address,undefined)
    elseif(MSVC)
        target_compile_options(ajazz_sanitizers INTERFACE /fsanitize=address)
    endif()
endif()

if(AJAZZ_ENABLE_COVERAGE AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
    target_compile_options(ajazz_sanitizers INTERFACE --coverage -O0 -g)
    target_link_options(ajazz_sanitizers INTERFACE --coverage)
endif()
