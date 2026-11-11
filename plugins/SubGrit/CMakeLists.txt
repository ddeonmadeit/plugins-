dd_add_plugin(SubGrit
    PRODUCT_NAME "SubGrit"
    PLUGIN_CODE  Sgrt
    KIND         SYNTH
    VERSION      1.14.1
    PRESETS_DIR  Presets
    SOURCES
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
        Source/SubView.cpp
        Source/Params.cpp)
