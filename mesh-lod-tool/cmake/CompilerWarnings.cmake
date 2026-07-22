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
