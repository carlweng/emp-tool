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

# emp-tool exports PUBLIC cxx_std_20 (the BooleanContext surface requires it), so
# every test already compiles as C++20. This wrapper is a self-documenting marker
# for the context/typed-layer tests.
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
add_test_case(test_boolean_program)
add_test_case_cxx20(test_bit)        # per-type value headers over ClearCtx
add_test_case_cxx20(test_bitvec)
add_test_case_cxx20(test_uint)
add_test_case_cxx20(test_int)
add_test_case_cxx20(test_float)
add_test_case_cxx20(test_sort)       # data-oblivious sort over circuit values
add_test_case_cxx20(test_runtime_int) # runtime-width UInt_T<Ctx,0> / Int_T<Ctx,0>
add_test_case(test_halfgate)         # half-gate garble/eval free functions
add_test_case_cxx20(test_circuit_fn) # C++20: compile-once / run-on-any-context frontend (rec:: value types)
add_test_case_cxx20(test_context)    # C++20: BooleanContext concept + contexts cross-check
add_test_case_cxx20(test_session_concepts)   # C++20: Session/DirectSession/SessionIO + WireValue (incl. synthetic value family)
add_test_case_cxx20(test_builtin_circuits)   # C++20: AES/SHA/SHA3 IR-replay == live kernels
add_test_case_cxx20(test_typed)              # C++20: typed values (Bit/BitVec/UInt/Int/Float) over Ctx
add_test_case_cxx20(test_crypto_aes)         # C++20: AES-128 block over the value layer (vector/record/count)
add_test_case_cxx20(test_crypto_sha256)      # C++20: SHA-256 compression + full-hash wrapper
add_test_case_cxx20(test_crypto_keccak)      # C++20: Keccak-f[1600] + SHA3-256 wrapper

# Circuit-function contract probes. The positive case compiles + runs; the eight
# negative cases must fail to compile with the EXPECTED contract diagnostic.
# Each test builds the case and its output must match the per-case message
# (PASS_REGULAR_EXPRESSION) — so a build that succeeds (contract stopped
# rejecting) OR fails with a different/changed message both fail the test, which
# bare WILL_FAIL could not catch. Serial: the cases share one build tree.
add_test_case_cxx20(circuit_fn_contract_probes)   # positive (NEG_CASE undefined)
set(_neg_msg
    "not callable with prvalue circuit-value arguments"   # 1  run + lvalue-ref
    "not callable with prvalue circuit-value arguments"   # 2  compile + lvalue-ref
    "must RETURN BY VALUE"                                 # 3  run + ref return
    "must RETURN BY VALUE"                                 # 4  compile + ref return
    "each arg must be a circuit value"                     # 5  compile<int> (not a circuit value)
    "not a plain value"                                    # 6  run + plain return
    "not void"                                             # 7  run + void return
    "callable in BOTH"                                     # 8  compile + both-forms ambiguity
    "circuit value over RecordCtx")                        # 9  compile arg not over RecordCtx
foreach(neg RANGE 1 9)
    add_executable(circuit_fn_contract_neg_${neg} EXCLUDE_FROM_ALL circuit_fn_contract_probes.cpp)
    target_link_libraries(circuit_fn_contract_neg_${neg} PRIVATE emp-tool::emp-tool)
    target_compile_features(circuit_fn_contract_neg_${neg} PRIVATE cxx_std_20)
    target_compile_definitions(circuit_fn_contract_neg_${neg} PRIVATE NEG_CASE=${neg})
    math(EXPR _idx "${neg} - 1")
    list(GET _neg_msg ${_idx} _expect)
    add_test(NAME circuit_fn_contract_neg_${neg}
             COMMAND ${CMAKE_COMMAND} --build . --target circuit_fn_contract_neg_${neg}
             WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
    set_tests_properties(circuit_fn_contract_neg_${neg} PROPERTIES
        PASS_REGULAR_EXPRESSION "${_expect}" RUN_SERIAL TRUE)
endforeach()
