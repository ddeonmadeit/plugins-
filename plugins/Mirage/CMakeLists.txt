dd_add_plugin(Mirage
    PRODUCT_NAME "Mirage"
    PLUGIN_CODE  Mrge
    KIND         EFFECT
    VERSION      1.1.0
    PRESETS_DIR  Presets
    SOURCES
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
        Source/Params.cpp
        Source/MirageView.cpp)
