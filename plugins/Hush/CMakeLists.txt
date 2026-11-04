dd_add_plugin(Hush
    PRODUCT_NAME "Hush"
    PLUGIN_CODE  Hush
    KIND         EFFECT
    VERSION      1.1.0
    PRESETS_DIR  Presets
    SOURCES
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
        Source/Params.cpp
        Source/Analysis.cpp
        Source/Render.cpp
        Source/Engine.cpp
        Source/SoundView.cpp)

target_link_libraries(Hush PRIVATE dd_take)
if(TARGET Hush_Harness)
    target_link_libraries(Hush_Harness PRIVATE dd_take)
endif()
