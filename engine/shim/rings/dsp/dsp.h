// FM1 Rack's copy of rings/dsp/dsp.h (pichenettes/eurorack, MIT), found before the original by the include path
// (engine/shim first). One change: with RACK_FM1 (the FM-1 build), Rings runs at 24 kHz in blocks of 24 (FM1 Rack's
// engine feeds it every other sample and fills in between, rack.cc run_rings_shared): half its time, which the
// fifth benchmark (10 Nov 2026) found it needed. Every time constant in Rings is written against kSampleRate, so its
// decays, tunings and filters stay as they were; what goes is the top octave (above 12 kHz) of the resonators.
// Without RACK_FM1, this is the original.
//
// Copyright 2015 Emilie Gillet.
//
// Author: Emilie Gillet (emilie.o.gillet@gmail.com)
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.
//
// See http://creativecommons.org/licenses/MIT/ for more information.
//
// -----------------------------------------------------------------------------
//
// Utility DSP routines.

#ifndef RINGS_DSP_DSP_H_
#define RINGS_DSP_DSP_H_

#include "stmlib/stmlib.h"

// #define MIC_W
#define BRYAN_CHORDS

namespace rings {

#ifdef RACK_FM1
static const float kSampleRate = 24000.0f;
const float a3 = 440.0f / kSampleRate;
const size_t kMaxBlockSize = 24;   // half the engine's block of 48 (rack.cc BLOCK)
#else
static const float kSampleRate = 48000.0f;
const float a3 = 440.0f / kSampleRate;
const size_t kMaxBlockSize = 24;
#endif

}  // namespace rings

#endif  // RINGS_DSP_DSP_H_
