dd_add_plugin(Contour
    PRODUCT_NAME "Contour"
    PLUGIN_CODE  Cntr
    KIND         EFFECT
    VERSION      1.2.1
    PRESETS_DIR  Presets
    WANTS_KEYBOARD
    SOURCES
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
        Source/Params.cpp
        Source/Analysis.cpp
        Source/Render.cpp
        Source/Engine.cpp
        Source/NoteView.cpp)

target_link_libraries(Contour PRIVATE dd_take)
if(TARGET Contour_Harness)
    target_link_libraries(Contour_Harness PRIVATE dd_take)
endif()
