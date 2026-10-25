/*
 BEGIN_JUCE_MODULE_DECLARATION

  ID:                 dd_core
  vendor:             ddeonmadeit
  version:            1.0.0
  name:               DD Core
  description:        Preset management, parameter helpers and audio->UI data sharing
  license:            Proprietary
  dependencies:       juce_audio_processors
  minimumCppStandard: 20

 END_JUCE_MODULE_DECLARATION
*/

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>

#include "core/dd_PresetManager.h"
#include "core/dd_ScopeBuffer.h"
