// Copyright 2012 Emilie Gillet.
//
// Beads-local provider adapted from stmlib/utils/random.h. It preserves the
// stmlib LCG sequence while allowing each Beads engine to bind its own state.
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
// See http://creativecommons.org/licenses/MIT/ for more information.

#ifndef BEADS_DSP_RANDOM_H_
#define BEADS_DSP_RANDOM_H_

#include <cstdint>

namespace beads {

class Random {
 public:
  static uint32_t state() { return *state_pointer_; }
  static void Seed(uint32_t seed) { *state_pointer_ = seed; }
  static uint32_t GetWord() {
    *state_pointer_ = *state_pointer_ * 1664525L + 1013904223L;
    return state();
  }
  static int16_t GetSample() { return static_cast<int16_t>(GetWord() >> 16); }
  static float GetFloat() { return static_cast<float>(GetWord()) / 4294967296.0f; }

 private:
  friend class ScopedRandomState;
  static inline thread_local uint32_t fallback_state_ = 0x21u;
  static inline thread_local uint32_t* state_pointer_ = &fallback_state_;
};

class ScopedRandomState {
 public:
  explicit ScopedRandomState(uint32_t& state) : previous_(Random::state_pointer_) {
    Random::state_pointer_ = &state;
  }
  ~ScopedRandomState() { Random::state_pointer_ = previous_; }
  ScopedRandomState(const ScopedRandomState&) = delete;
  ScopedRandomState& operator=(const ScopedRandomState&) = delete;

 private:
  uint32_t* previous_;
};

}  // namespace beads

#endif  // BEADS_DSP_RANDOM_H_
