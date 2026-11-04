/*
 BEGIN_JUCE_MODULE_DECLARATION

  ID:                 dd_take
  vendor:             ddeonmadeit
  version:            1.0.0
  name:               DD Take
  description:        Capturing a track against the host timeline (Melodyne-style transfer), take storage on disk,
                      offline pitch tracking, and lock-free hand-over of rendered data to the audio thread
  license:            Proprietary
  dependencies:       juce_audio_processors juce_audio_formats
  minimumCppStandard: 20

 END_JUCE_MODULE_DECLARATION
*/

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>

#include <array>
#include <atomic>
#include <memory>
#include <vector>

#include "take/dd_TakeStore.h"
#include "take/dd_CaptureFifo.h"
#include "take/dd_Published.h"
#include "take/dd_Ranges.h"
#include "take/dd_PitchTrack.h"
#include "take/dd_TakeFiles.h"
#include "take/dd_TakeEngine.h"
