dd_add_plugin(Kitroom
    PRODUCT_NAME "Kitroom"
    PLUGIN_CODE  Ktrm
    KIND         SYNTH
    VERSION      1.5.0
    PRESETS_DIR  Presets
    SOURCES
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
        Source/Params.cpp
        Source/Library.cpp
        Source/DrumEngine.cpp
        Source/KitView.cpp
        Source/ElementPanel.cpp)
