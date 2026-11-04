# Unit tests for the shared modules (no plugin wrappers). Run: build/tools/tests/dd_tests_artefacts/Release/dd_tests
juce_add_console_app(dd_tests PRODUCT_NAME "dd_tests")
target_sources(dd_tests PRIVATE PresetManagerTest.cpp)
target_compile_definitions(dd_tests PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(dd_tests PRIVATE dd_core juce::juce_audio_processors juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)

# Contour's note analysis and grain renderer (plugin sources, no wrappers).
# Run: build/tools/tests/contour_tests_artefacts/Release/contour_tests
juce_add_console_app(contour_tests PRODUCT_NAME "contour_tests")
target_sources(contour_tests PRIVATE ContourRenderTest.cpp
    ${CMAKE_SOURCE_DIR}/plugins/Contour/Source/Analysis.cpp
    ${CMAKE_SOURCE_DIR}/plugins/Contour/Source/Render.cpp
    ${CMAKE_SOURCE_DIR}/plugins/Contour/Source/Engine.cpp
    ${CMAKE_SOURCE_DIR}/plugins/Contour/Source/Params.cpp)
target_include_directories(contour_tests PRIVATE ${CMAKE_SOURCE_DIR}/plugins/Contour/Source)
target_compile_definitions(contour_tests PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(contour_tests PRIVATE dd_take juce::juce_audio_processors juce::juce_audio_formats
    juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)


# Hush's sound detection, per-sound rendering, live de-esser and save/restore.
# Run: build/tools/tests/hush_tests_artefacts/Release/hush_tests
juce_add_console_app(hush_tests PRODUCT_NAME "hush_tests")
target_sources(hush_tests PRIVATE HushTest.cpp
    ${CMAKE_SOURCE_DIR}/plugins/Hush/Source/Analysis.cpp
    ${CMAKE_SOURCE_DIR}/plugins/Hush/Source/Render.cpp
    ${CMAKE_SOURCE_DIR}/plugins/Hush/Source/Engine.cpp
    ${CMAKE_SOURCE_DIR}/plugins/Hush/Source/Params.cpp)
target_include_directories(hush_tests PRIVATE ${CMAKE_SOURCE_DIR}/plugins/Hush/Source)
target_compile_definitions(hush_tests PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(hush_tests PRIVATE dd_take juce::juce_audio_processors juce::juce_audio_formats juce::juce_dsp
    juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)

