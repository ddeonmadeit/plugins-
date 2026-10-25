/*
 BEGIN_JUCE_MODULE_DECLARATION

  ID:                 dd_dsp
  vendor:             ddeonmadeit
  version:            1.0.0
  name:               DD DSP
  description:        Reusable DSP building blocks (oscillators, filters, envelopes, saturation, effects)
  license:            Proprietary
  dependencies:       juce_dsp
  minimumCppStandard: 20

 END_JUCE_MODULE_DECLARATION
*/

#pragma once

#include <juce_dsp/juce_dsp.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

#include "dsp/dd_Math.h"
#include "dsp/dd_Oscillators.h"
#include "dsp/dd_Filters.h"
#include "dsp/dd_Envelopes.h"
#include "dsp/dd_Saturation.h"
#include "dsp/dd_Effects.h"
