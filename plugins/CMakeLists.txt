function(ha_role_plugin id name source)
    add_library(${name} SHARED ${source})
    target_include_directories(${name} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../include")
    target_link_libraries(${name} PRIVATE AYPlugin AYNetworkHttp nlohmann_json::nlohmann_json)
    target_compile_definitions(${name} PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    set_target_properties(${name} PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins/${id}")
    # Track manifest edits as configure inputs; changing labels/settings must
    # not require an unrelated C++ edit just to refresh the installed metadata.
    configure_file("${CMAKE_CURRENT_SOURCE_DIR}/${id}/plugin.json" "${CMAKE_BINARY_DIR}/plugins/${id}/plugin.json" COPYONLY)
    add_dependencies(HibernationPlugins ${name})
endfunction()
add_custom_target(HibernationPlugins)
ha_role_plugin(weather Hibernation_Weather weather/Weather.cpp)
ha_role_plugin(deepseek-pricing Hibernation_DeepSeekPricing deepseek-pricing/Pricing.cpp)
target_link_libraries(Hibernation_DeepSeekPricing PRIVATE AYInference)
target_link_libraries(Hibernation_DeepSeekPricing PRIVATE AYInference)
ha_role_plugin(widget Hibernation_Widget widget/Widget.cpp)
target_link_libraries(Hibernation_Widget PRIVATE AY2DFrameSequence user32 gdi32 ole32 windowscodecs)
# Resource-only animation edits must also reach the installed pack when the
# widget DLL is already up to date.
add_custom_target(Hibernation_WidgetAssets
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/widget/characters" "${CMAKE_BINARY_DIR}/plugins/widget/characters")
add_dependencies(Hibernation_Widget Hibernation_WidgetAssets)
ha_role_plugin(habitat Hibernation_Habitat habitat/Habitat.cpp)
target_link_libraries(Hibernation_Habitat PRIVATE user32 gdi32)
ha_role_plugin(rhythm Hibernation_Rhythm rhythm/Rhythm.cpp)
ha_role_plugin(atelier Hibernation_Atelier atelier/Atelier.cpp)
ha_role_plugin(pocket Hibernation_Pocket pocket/Pocket.cpp)
ha_role_plugin(reading Hibernation_Reading reading/Reading.cpp)
ha_role_plugin(mentor Hibernation_Mentor mentor/Mentor.cpp)
target_link_libraries(Hibernation_Mentor PRIVATE AYPlatform AYIO AYSerializer winhttp)
target_link_libraries(Hibernation_Atelier PRIVATE AYIO)
ha_role_plugin(tts Hibernation_Speech tts/Speech.cpp)
target_link_libraries(Hibernation_Speech PRIVATE AYPlatform AYAudioPlayback sapi ole32 uuid winmm crypt32)
add_custom_command(TARGET Hibernation_Speech POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_CURRENT_SOURCE_DIR}/tts/kana_worker.py"
    "${CMAKE_CURRENT_SOURCE_DIR}/tts/kana_reading.py"
    "$<TARGET_FILE_DIR:Hibernation_Speech>" COMMAND_EXPAND_LISTS)
ha_role_plugin(television Hibernation_Television television/Television.cpp)
target_link_libraries(Hibernation_Television PRIVATE AYVideoPlayback user32 gdi32 ole32 windowscodecs crypt32 winmm comctl32)
add_subdirectory(napcat)
