if(ENABLE_CJSON_TEST)
    add_library(unity unity/src/unity.c)

    # Disable -Werror for Unity
    list(FIND custom_compiler_flags "-Werror" werror_found)
    if (werror_found)
        target_compile_options(unity PRIVATE "-Wno-error")
    endif()
    # Disable -fvisibility=hidden for Unity
    list(FIND custom_compiler_flags "-fvisibility=hidden" visibility_found)
    if (visibility_found)
        target_compile_options(unity PRIVATE "-fvisibility=default")
    endif()

    #copy test files
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/inputs")
    file(GLOB test_files "inputs/*")
    file(COPY ${test_files} DESTINATION "${CMAKE_CURRENT_BINARY_DIR}/inputs/")

    set(unity_tests
        parse_examples
        parse_number
        parse_hex4
        parse_string
        parse_array
        parse_object
        parse_value
        print_string
        print_number
        print_array
        print_object
        print_value
        misc_tests
    )

    add_library(test-common common.c)

    option(ENABLE_VALGRIND OFF "Enable the valgrind memory checker for the tests.")
    if (ENABLE_VALGRIND)
        find_program(MEMORYCHECK_COMMAND valgrind)
        if ("${MEMORYCHECK_COMMAND}" MATCHES "MEMORYCHECK_COMMAND-NOTFOUND")
            message(WARNING "Valgrind couldn't be found.")
            unset(MEMORYCHECK_COMMAND)
        else()
            set(MEMORYCHECK_COMMAND_OPTIONS --trace-children=yes --leak-check=full --error-exitcode=1)
        endif()
    endif()

    #"check" target that automatically builds everything and runs the tests
    add_custom_target(check 
        COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure
        DEPENDS ${unity_tests})

    foreach(unity_test ${unity_tests})
        add_executable("${unity_test}" "${unity_test}.c")
        target_link_libraries("${unity_test}" "${CJSON_LIB}" unity test-common)
        if(MEMORYCHECK_COMMAND)
            add_test(NAME "${unity_test}"
                COMMAND "${MEMORYCHECK_COMMAND}" ${MEMORYCHECK_COMMAND_OPTIONS} "./${unity_test}")
        else()
            add_test(NAME "${unity_test}"
                COMMAND "./${unity_test}")
        endif()
    endforeach()
endif()
