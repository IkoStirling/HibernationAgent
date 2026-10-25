function(ha_role_plugin id name source)
    add_library(${name} SHARED ${source})
    target_include_directories(${name} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../include")
    target_link_libraries(${name} PRIVATE AYPlugin AYNetworkHttp nlohmann_json::nlohmann_json)
    target_compile_definitions(${name} PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    set_target_properties(${name} PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins/${id}")
    add_custom_command(TARGET ${name} POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/${id}/plugin.json" "$<TARGET_FILE_DIR:${name}>/plugin.json")
    add_dependencies(HibernationPlugins ${name})
endfunction()
add_custom_target(HibernationPlugins)
ha_role_plugin(weather Hibernation_Weather weather/Weather.cpp)
ha_role_plugin(widget Hibernation_Widget widget/Widget.cpp)
target_link_libraries(Hibernation_Widget PRIVATE user32 gdi32)
ha_role_plugin(tts Hibernation_Speech tts/Speech.cpp)
target_link_libraries(Hibernation_Speech PRIVATE AYPlatform AYAudioPlayback sapi ole32 uuid winmm crypt32)
add_custom_command(TARGET Hibernation_Speech POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_CURRENT_SOURCE_DIR}/tts/kana_worker.py"
    "${CMAKE_CURRENT_SOURCE_DIR}/tts/kana_reading.py"
    "$<TARGET_FILE_DIR:Hibernation_Speech>" COMMAND_EXPAND_LISTS)
ha_role_plugin(television Hibernation_Television television/Television.cpp)
target_link_libraries(Hibernation_Television PRIVATE AYVideoPlayback user32 gdi32 ole32 windowscodecs crypt32 winmm)
