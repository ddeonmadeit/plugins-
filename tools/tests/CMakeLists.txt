# Unit tests for the shared modules (no plugin wrappers). Run: build/tools/tests/dd_tests_artefacts/Release/dd_tests
juce_add_console_app(dd_tests PRODUCT_NAME "dd_tests")
target_sources(dd_tests PRIVATE PresetManagerTest.cpp)
target_compile_definitions(dd_tests PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(dd_tests PRIVATE dd_core juce::juce_audio_processors juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
