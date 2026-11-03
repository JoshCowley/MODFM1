// FM1 Rack's copy of rings/dsp/string.h (pichenettes/eurorack, MIT), found before the original by the include path
// (engine/shim first). One change: with RACK_RINGS16 (the FM-1 build), the strings' delay lines hold 16-bit samples
// (DelayLine16 below); without it, this is the original.
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
// Comb filter / KS string.

#ifndef RINGS_DSP_STRING_H_
#define RINGS_DSP_STRING_H_

#include "stmlib/stmlib.h"

#include <algorithm>

#include "stmlib/dsp/delay_line.h"
#include "stmlib/dsp/filter.h"

#include "rings/dsp/dsp.h"

namespace rings {

const size_t kDelayLineSize = 2048;

class DampingFilter {
 public:
  DampingFilter() { }
  ~DampingFilter() { }
  
  void Init() {
    x_ = 0.0f;
    x__ = 0.0f;
    brightness_ = 0.0f;
    brightness_increment_ = 0.0f;
    damping_ = 0.0f;
    damping_increment_ = 0.0f;
  }
   
  inline void Configure(float damping, float brightness, size_t size) {
    if (!size) {
      damping_ = damping;
      brightness_ = brightness;
      damping_increment_ = 0.0f;
      brightness_increment_ = 0.0f;
    } else {
      float step = 1.0f / static_cast<float>(size);
      damping_increment_ = (damping - damping_) * step;
      brightness_increment_ = (brightness - brightness_) * step;
    }
  }
   
  inline float Process(float x) {
    float h0 = (1.0f + brightness_) * 0.5f;
    float h1 = (1.0f - brightness_) * 0.25f;
    float y = damping_ * (h0 * x_ + h1 * (x + x__));
    x__ = x_;
    x_ = x;
    brightness_ += brightness_increment_;
    damping_ += damping_increment_;
    return y;
  }
 private:
  float x_;
  float x__;
  float brightness_;
  float brightness_increment_;
  float damping_;
  float damping_increment_;
  
  DISALLOW_COPY_AND_ASSIGN(DampingFilter);
};

#ifdef RACK_RINGS16
// FM1 Rack: the strings' delay lines as 16-bit samples (stmlib::DelayLine's interface, the same arithmetic in float
// on what is read back): half the memory of Rings' 8 strings per voice (99.6 KB -> 49.8 KB). Full scale is
// +/-RINGS16_RANGE; a sample past it is held at the limit.
#ifndef RINGS16_RANGE
#define RINGS16_RANGE 4.0f
#endif
#ifdef RINGS16_PROBE
extern float rings16_peak;
#endif
template<size_t max_delay>
class DelayLine16 {
 public:
  DelayLine16() { }
  ~DelayLine16() { }
  void Init() { Reset(); }
  void Reset() { std::fill(&line_[0], &line_[max_delay], int16_t(0)); delay_ = 1; write_ptr_ = 0; }
  inline void set_delay(size_t delay) { delay_ = delay; }
  inline void Write(const float sample) {
#ifdef RINGS16_PROBE
    if (sample > rings16_peak) rings16_peak = sample; else if (-sample > rings16_peak) rings16_peak = -sample;
#endif
    line_[write_ptr_] = q(sample);
    write_ptr_ = (write_ptr_ - 1 + max_delay) % max_delay;
  }
  inline const float Allpass(const float sample, size_t delay, const float coefficient) {
    float read = g(line_[(write_ptr_ + delay) % max_delay]);
    float write = sample + coefficient * read;
    Write(write);
    return -write * coefficient + read;
  }
  inline const float WriteRead(const float sample, float delay) { Write(sample); return Read(delay); }
  inline const float Read() const { return g(line_[(write_ptr_ + delay_) % max_delay]); }
  inline const float Read(size_t delay) const { return g(line_[(write_ptr_ + delay) % max_delay]); }
  inline const float Read(float delay) const {
    MAKE_INTEGRAL_FRACTIONAL(delay)
    const float a = g(line_[(write_ptr_ + delay_integral) % max_delay]);
    const float b = g(line_[(write_ptr_ + delay_integral + 1) % max_delay]);
    return a + (b - a) * delay_fractional;
  }
  inline const float ReadHermite(float delay) const {
    MAKE_INTEGRAL_FRACTIONAL(delay)
    int32_t t = (write_ptr_ + delay_integral + max_delay);
    const float xm1 = g(line_[(t - 1) % max_delay]);
    const float x0 = g(line_[(t) % max_delay]);
    const float x1 = g(line_[(t + 1) % max_delay]);
    const float x2 = g(line_[(t + 2) % max_delay]);
    const float c = (x1 - xm1) * 0.5f;
    const float v = x0 - x1;
    const float w = c + v;
    const float a = w + v + (x2 - x0) * 0.5f;
    const float b_neg = w + a;
    const float f = delay_fractional;
    return (((a * f) - b_neg) * f + c) * f + x0;
  }
 private:
  static inline int16_t q(float x) {
    x *= 32767.0f / RINGS16_RANGE;
    x = x > 32767.0f ? 32767.0f : x < -32767.0f ? -32767.0f : x;
    return (int16_t)(x + (x >= 0.0f ? 0.5f : -0.5f));
  }
  static inline float g(int16_t v) { return v * (RINGS16_RANGE / 32767.0f); }
  size_t write_ptr_;
  size_t delay_;
  int16_t line_[max_delay];
  DISALLOW_COPY_AND_ASSIGN(DelayLine16);
};
typedef DelayLine16<kDelayLineSize> StringDelayLine;
typedef DelayLine16<kDelayLineSize / 2> StiffnessDelayLine;
#else
typedef stmlib::DelayLine<float, kDelayLineSize> StringDelayLine;
typedef stmlib::DelayLine<float, kDelayLineSize / 2> StiffnessDelayLine;
#endif

class String {
 public:
  String() { }
  ~String() { }
  
  void Init(bool enable_dispersion);
  void Process(const float* in, float* out, float* aux, size_t size);
  
  inline void set_frequency(float frequency) {
    frequency_ = frequency;
  }

  inline void set_frequency(float frequency, float coefficient) {
    frequency_ += coefficient * (frequency - frequency_);
  }

  inline void set_dispersion(float dispersion) {
    dispersion_ = dispersion;
  }
  
  inline void set_brightness(float brightness) {
    brightness_ = brightness;
  }
  
  inline void set_damping(float damping) {
    damping_ = damping;
  }
  
  inline void set_position(float position) {
    position_ = position;
  }
  
  inline StringDelayLine* mutable_string() { return &string_; }
  
 private:
  template<bool enable_dispersion>
  void ProcessInternal(const float* in, float* out, float* aux, size_t size);
   
  float frequency_;
  float dispersion_;
  float brightness_;
  float damping_;
  float position_;
  
  float delay_;
  float clamped_position_;
  float previous_dispersion_;
  float previous_damping_compensation_;
  
  bool enable_dispersion_;
  bool enable_iir_damping_;
  float dispersion_noise_;
  
  // Very crappy linear interpolation upsampler used for low pitches that
  // do not fit the delay line. Rarely used.
  float src_phase_;
  float out_sample_[2];
  float aux_sample_[2];
  
  float curved_bridge_;
  
  StringDelayLine string_;
  StiffnessDelayLine stretch_;
  
  DampingFilter fir_damping_filter_;
  stmlib::Svf iir_damping_filter_;
  stmlib::DCBlocker dc_blocker_;
  
  DISALLOW_COPY_AND_ASSIGN(String);
};

}  // namespace rings

#endif  // RINGS_DSP_STRING_H_
