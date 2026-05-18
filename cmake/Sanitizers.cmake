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
        # MSVC has no /fsanitize=thread equivalent. Previously this was a
        # WARNING which let the configure succeed and silently produced a
        # binary with NO TSan instrumentation — defeating the purpose of
        # explicitly opting in. Fail fast instead so contributors aren't
        # surprised by green-but-meaningless TSan CI runs on Windows.
        message(
            FATAL_ERROR
                "AJAZZ_ENABLE_TSAN is set but the current compiler "
                "(${CMAKE_CXX_COMPILER_ID}) has no thread-sanitizer equivalent. "
                "TSan is supported only on GNU / Clang / AppleClang. "
                "Run TSan in a separate Linux or macOS CI job; use "
                "AJAZZ_ENABLE_SANITIZERS=ON for ASan/UBSan on MSVC instead."
        )
    endif()
endif()

if(AJAZZ_ENABLE_COVERAGE AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
    target_compile_options(ajazz_sanitizers INTERFACE --coverage -O0 -g)
    target_link_options(ajazz_sanitizers INTERFACE --coverage)
endif()
