# Test cases — one file per crypto/core/io/group header (see docs/test_conventions.md).
# Tests are fast examples + correctness/regression checks; throughput lives in
# ../bench and is not registered with ctest.

function(add_test_case name)
    add_executable(${name} ${name}.cpp)
    target_link_libraries(${name} PRIVATE emp-tool::emp-tool)
    set_target_properties(${name} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
    add_test(NAME ${name}
             COMMAND ${name}
             WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
endfunction()

function(add_test_case_with_run name)
    add_executable(${name} ${name}.cpp)
    target_link_libraries(${name} PRIVATE emp-tool::emp-tool)
    set_target_properties(${name} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
    add_test(NAME ${name}
             COMMAND ${PROJECT_SOURCE_DIR}/run $<TARGET_FILE:${name}>
             WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
endfunction()

# Same as add_test_case but compiles just this binary as C++20 (e.g. for
# template-lambda circuit bodies). The library stays C++17; cxx_std_17 there is
# a floor, so CMake resolves this one target to C++20 with no wider effect.
function(add_test_case_cxx20 name)
    add_executable(${name} ${name}.cpp)
    target_link_libraries(${name} PRIVATE emp-tool::emp-tool)
    target_compile_features(${name} PRIVATE cxx_std_20)
    set_target_properties(${name} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
    add_test(NAME ${name}
             COMMAND ${name}
             WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
endfunction()

# utils/* tests
add_test_case(test_aes)
add_test_case(test_block)
add_test_case(test_ccrh)
add_test_case(test_f2k)
add_test_case(test_hash)
add_test_case(test_mitccrh)
add_test_case(test_prg)
add_test_case(test_prp)
add_test_case(test_utils)

# group/EC
add_test_case(test_ecc)
add_test_case(test_ro)

# circuits / IO / higher-level — not utils/* but kept here for convenience
add_test_case_with_run(test_netio)
add_test_case_with_run(test_tlsio)
add_test_case(test_bit)
add_test_case(test_bitvec)
add_test_case(test_uint)
add_test_case(test_int)
add_test_case(test_float)
add_test_case(test_gen_circuit)
add_test_case(test_circuit_file)
add_test_case(test_halfgate)
add_test_case(test_aes_circuit)
add_test_case(test_aes_128_ctr)
add_test_case(test_sha3_circuit)
add_test_case(test_sha3_256)
add_test_case(test_sha256_circuit)
add_test_case(test_circuit_types)
add_test_case_cxx20(test_frontend)   # C++20: also demonstrates template-lambda bodies

# Frontend contract probes. Positive case (C++17) compiles + runs; the seven
# negative cases must fail to compile with the EXPECTED contract diagnostic.
# Each test builds the case and its output must match the per-case message
# (PASS_REGULAR_EXPRESSION) — so a build that succeeds (contract stopped
# rejecting) OR fails with a different/changed message both fail the test, which
# bare WILL_FAIL could not catch. Serial: the cases share one build tree.
add_test_case(frontend_contract_probes)   # positive (NEG_CASE undefined)
set(_neg_msg
    "not callable with prvalue circuit-value arguments"   # 1  run + lvalue-ref
    "not callable with prvalue circuit-value arguments"   # 2  compile + lvalue-ref
    "must RETURN BY VALUE"                                 # 3  run + ref return
    "must RETURN BY VALUE"                                 # 4  compile + ref return
    "each input TYPE must be a circuit value"              # 5  compile<int>
    "not a plain value"                                    # 6  run + plain return
    "not void")                                            # 7  run + void return
foreach(neg RANGE 1 7)
    add_executable(frontend_contract_neg_${neg} EXCLUDE_FROM_ALL frontend_contract_probes.cpp)
    target_link_libraries(frontend_contract_neg_${neg} PRIVATE emp-tool::emp-tool)
    target_compile_definitions(frontend_contract_neg_${neg} PRIVATE NEG_CASE=${neg})
    math(EXPR _idx "${neg} - 1")
    list(GET _neg_msg ${_idx} _expect)
    add_test(NAME frontend_contract_neg_${neg}
             COMMAND ${CMAKE_COMMAND} --build . --target frontend_contract_neg_${neg}
             WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
    set_tests_properties(frontend_contract_neg_${neg} PROPERTIES
        PASS_REGULAR_EXPRESSION "${_expect}" RUN_SERIAL TRUE)
endforeach()
