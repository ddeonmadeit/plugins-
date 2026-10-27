dd_add_plugin(Afterglow
    PRODUCT_NAME "Afterglow"
    PLUGIN_CODE  Aglw
    KIND         SYNTH
    VERSION      1.4.0
    PRESETS_DIR  Presets
    SOURCES
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
        Source/Params.cpp
        Source/HorizonView.cpp)
