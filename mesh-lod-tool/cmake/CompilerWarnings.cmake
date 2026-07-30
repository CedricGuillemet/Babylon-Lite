# High, portable warning levels for the converter's own targets only. Third-party
# dependencies keep their own settings. Warnings are not promoted to errors so a
# future compiler revision cannot break reproducible builds over a new diagnostic.
function(mlod_set_project_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive-)
    else()
        target_compile_options(${target} PRIVATE
            -Wall
            -Wextra
            -Wpedantic
            -Wshadow
            -Wconversion
            -Wsign-conversion)
    endif()
endfunction()

# Deterministic floating-point settings keep output stable across native
# compiler targets. MSVC uses strict IEEE behavior; Clang/GCC disable
# contraction so FMA availability cannot silently change rounding.
function(mlod_set_deterministic_floating_point target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /fp:strict)
    else()
        target_compile_options(${target} PRIVATE -ffp-contract=off)
    endif()
endfunction()
