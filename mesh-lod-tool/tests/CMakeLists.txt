add_executable(mesh-lod-tool-tests tool_tests.cpp)
target_link_libraries(mesh-lod-tool-tests PRIVATE mesh-lod-tool-core)
# White-box access to internal converter headers (cli.h and friends).
target_include_directories(mesh-lod-tool-tests PRIVATE ${PROJECT_SOURCE_DIR}/src)
# Checked-in ingestion fixtures resolved by absolute path at test time.
target_compile_definitions(mesh-lod-tool-tests PRIVATE
    MLOD_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")
mlod_set_project_warnings(mesh-lod-tool-tests)

# In-process smoke assertions: exit codes, help contents, and the exact pinned
# dependency revisions reported by --version.
add_test(NAME tool_smoke COMMAND mesh-lod-tool-tests)

# End-to-end assertions against the built executable so the shipped binary is
# exercised exactly as documented.
add_test(NAME cli_help COMMAND mesh-lod-tool --help)
set_tests_properties(cli_help PROPERTIES PASS_REGULAR_EXPRESSION "--input")

add_test(NAME cli_version COMMAND mesh-lod-tool --version)
set_tests_properties(cli_version PROPERTIES
    PASS_REGULAR_EXPRESSION "format_version=1\\.0")

add_test(NAME cli_version_meshoptimizer_pin COMMAND mesh-lod-tool --version)
set_tests_properties(cli_version_meshoptimizer_pin PROPERTIES
    PASS_REGULAR_EXPRESSION "meshoptimizer_revision=f843aae0b3070306bd2aeef43ffcf09509fee526")

add_test(NAME cli_version_cgltf_pin COMMAND mesh-lod-tool --version)
set_tests_properties(cli_version_cgltf_pin PROPERTIES
    PASS_REGULAR_EXPRESSION "cgltf_revision=85cd62382dfea638278962690cf515023f33ed00")

add_test(NAME cli_unknown_arg COMMAND mesh-lod-tool --nope)
set_tests_properties(cli_unknown_arg PROPERTIES WILL_FAIL TRUE)
