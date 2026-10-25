/*
 BEGIN_JUCE_MODULE_DECLARATION

  ID:                 dd_gui
  vendor:             ddeonmadeit
  version:            1.0.0
  name:               DD GUI
  description:        Shared look and feel and widgets (knobs, choices, toggles, panels, preset bar, scope)
  license:            Proprietary
  dependencies:       juce_gui_basics juce_audio_processors dd_core
  minimumCppStandard: 20

 END_JUCE_MODULE_DECLARATION
*/

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <dd_core/dd_core.h>

#include "gui/dd_Theme.h"
#include "gui/dd_Widgets.h"
#include "gui/dd_PresetBar.h"
#include "gui/dd_Scope.h"
