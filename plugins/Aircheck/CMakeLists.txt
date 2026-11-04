dd_add_plugin(Aircheck
    PRODUCT_NAME "Aircheck"
    PLUGIN_CODE  Achk
    KIND         EFFECT
    VERSION      1.2.0
    PRESETS_DIR  Presets
    SOURCES
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
        Source/Views.cpp
        Source/Params.cpp)
