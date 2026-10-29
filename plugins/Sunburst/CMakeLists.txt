dd_add_plugin(Sunburst
    PRODUCT_NAME "Sunburst"
    PLUGIN_CODE  Sbst
    KIND         SYNTH
    VERSION      1.0.0
    PRESETS_DIR  Presets
    SOURCES
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
        Source/Params.cpp
        Source/Library.cpp
        Source/GuitarEngine.cpp
        Source/GuitarView.cpp)
