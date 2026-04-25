# Optional ASan/UBSan/TSan helpers.
#  -DAJAZZ_ENABLE_SANITIZERS=ON   -> address + undefined behavior
#  -DAJAZZ_ENABLE_TSAN=ON         -> thread sanitizer (mutually exclusive with ASan)

add_library(ajazz_sanitizers INTERFACE)
add_library(ajazz::sanitizers ALIAS ajazz_sanitizers)

if(AJAZZ_ENABLE_SANITIZERS AND AJAZZ_ENABLE_TSAN)
    message(
        FATAL_ERROR
            "AJAZZ_ENABLE_SANITIZERS (ASan/UBSan) and AJAZZ_ENABLE_TSAN (TSan) cannot be combined; "
            "run them in separate CI jobs."
    )
endif()

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

if(AJAZZ_ENABLE_TSAN)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
        target_compile_options(
            ajazz_sanitizers INTERFACE -fsanitize=thread -fno-omit-frame-pointer -O1 -g
        )
        target_link_options(ajazz_sanitizers INTERFACE -fsanitize=thread)
    else()
        message(WARNING "AJAZZ_ENABLE_TSAN: thread sanitizer not supported by this compiler")
    endif()
endif()

if(AJAZZ_ENABLE_COVERAGE AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
    target_compile_options(ajazz_sanitizers INTERFACE --coverage -O0 -g)
    target_link_options(ajazz_sanitizers INTERFACE --coverage)
endif()
