option(HA_BUNDLE_RUNTIME_TOOLS "Copy pinned Git/7-Zip tools beside the application" ON)
if(HA_BUNDLE_RUNTIME_TOOLS)
    find_program(HA_TOOLS_PWSH pwsh REQUIRED)
    set(_ha_tool_source "${CMAKE_CURRENT_SOURCE_DIR}/tools")
    # Run on every build so changed tools/wrappers are copied without relinking.
    add_custom_target(Hibernation_RuntimeTools ALL
        COMMAND "${HA_TOOLS_PWSH}" -NoProfile -File "${_ha_tool_source}/VerifyRuntimeTools.ps1"
        COMMAND "${CMAKE_COMMAND}" -E copy_directory "${_ha_tool_source}/runtime" "${CMAKE_BINARY_DIR}/tools/runtime"
        COMMAND "${CMAKE_COMMAND}" -E copy_directory "${_ha_tool_source}/bin" "${CMAKE_BINARY_DIR}/tools/bin"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${_ha_tool_source}/UseTools.ps1" "${_ha_tool_source}/VerifyRuntimeTools.ps1"
            "${CMAKE_BINARY_DIR}/tools/"
        VERBATIM COMMENT "Verify and stage portable Git and 7-Zip")
    add_dependencies(hibernation-agent Hibernation_RuntimeTools)
    if(BUILD_TESTING)
        add_test(NAME Hibernation_RuntimeTools
            COMMAND "${HA_TOOLS_PWSH}" -NoProfile -File "${CMAKE_CURRENT_SOURCE_DIR}/tests/RuntimeTools.ps1"
                -ToolsRoot "${CMAKE_BINARY_DIR}/tools" -ScratchRoot "${CMAKE_BINARY_DIR}/integration")
        set_tests_properties(Hibernation_RuntimeTools PROPERTIES TIMEOUT 90)
    endif()
endif()
