dd_add_plugin(Orbit
    PRODUCT_NAME "Orbit"
    PLUGIN_CODE  Orbt
    KIND         EFFECT
    VERSION      1.1.0
    PRESETS_DIR  Presets
    SOURCES
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
        Source/Params.cpp
        Source/Hrtf.cpp
        Source/Spatializer.cpp
        Source/RoomView.cpp)

# Head-related impulse responses (built by hrtf/make_hrtf.py from the Viking HRTF dataset, CC BY 4.0).
juce_add_binary_data(Orbit_Hrtf
    HEADER_NAME OrbitHrtf.h
    NAMESPACE   OrbitHrtf
    SOURCES     Resources/hrtf.bin)
set_target_properties(Orbit_Hrtf PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_link_libraries(Orbit PRIVATE Orbit_Hrtf)
if (TARGET Orbit_Harness)
    target_link_libraries(Orbit_Harness PRIVATE Orbit_Hrtf)
endif()
