// Copyright 2017 Emilie Gillet.
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
// Delay line.

#ifndef BEADS_DSP_DELAY_LINE_H_
#define BEADS_DSP_DELAY_LINE_H_

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstring>
#include <type_traits>

#include "stmlib/stmlib.h"

#include "stmlib/dsp/dsp.h"
#include "stmlib/utils/dsp.h"

#include "beads/dsp/frame.h"
#include "beads/dsp/mu_law.h"

#include "beads/dsp/stmlib_compat.h"

namespace beads {

struct DelayBufferState {
  int write_ptr;
  int duration;
  int tail_write_ptr;
  int tail_xfade_duration;
  int written_since_last_frozen;
  bool frozen;
};

enum Resolution {
  RESOLUTION_8_BIT,
  RESOLUTION_12_BIT,
  RESOLUTION_16_BIT,
  RESOLUTION_LAST
};

template<Resolution resolution, int num_channels>
struct Item { };

template<>
struct Item<RESOLUTION_16_BIT, 1> {
  void Store(int i, FloatFrame s) {
    lr = stmlib::Clip16(static_cast<int32_t>((s.l + s.r) * 16384.0f));
  }
  inline FloatFrame value(int i) const {
    FloatFrame result;
    result.l = result.r = static_cast<float>(lr) / 32768.0f;
    return result;
  }
  enum { bundle_size = 1 };
  int16_t lr;
};

template<>
struct Item<RESOLUTION_16_BIT, 2> {
  void Store(int i, FloatFrame s) {
    l = stmlib::Clip16(static_cast<int32_t>(s.l * 32768.0f));
    r = stmlib::Clip16(static_cast<int32_t>(s.r * 32768.0f));
  }
  inline FloatFrame value(int i) const {
    FloatFrame result;
    result.l = static_cast<float>(l) / 32768.0f;
    result.r = static_cast<float>(r) / 32768.0f;
    return result;
  }
  enum { bundle_size = 1 };
  int16_t l;
  int16_t r;
};

// Explicit little-endian packing avoids compiler-specific signed bitfield
// layout (notably GCC's Windows MS bitfield ABI). Both formats occupy 3 bytes.
inline uint16_t Encode12(int32_t sample) {
  const int32_t clipped = stmlib::Clip16(sample);
  const int32_t quantized = (clipped + 32768) / 16 - 2048;
  return static_cast<uint16_t>(quantized) & 0x0fff;
}

inline float Decode12(uint16_t value) {
  const int32_t signed_value = value < 2048 ? value : int32_t(value) - 4096;
  return static_cast<float>(signed_value) / 2048.0f;
}

template<>
struct Item<RESOLUTION_12_BIT, 2> {
  void Store(int i, FloatFrame s) {
    const uint16_t l = Encode12(static_cast<int32_t>(s.l * 32768.0f));
    const uint16_t r = Encode12(static_cast<int32_t>(s.r * 32768.0f));
    data[0] = l & 0xff;
    data[1] = (l >> 8) | ((r & 0x0f) << 4);
    data[2] = r >> 4;
  }
  inline FloatFrame value(int i) const {
    return {Decode12(uint16_t(data[0]) | ((uint16_t(data[1]) & 0x0f) << 8)),
            Decode12((uint16_t(data[1]) >> 4) | (uint16_t(data[2]) << 4))};
  }
  enum { bundle_size = 1 };
  uint8_t data[3];
};

template<>
struct Item<RESOLUTION_12_BIT, 1> {
  void Store(int i, FloatFrame s) {
    const uint16_t value = Encode12(static_cast<int32_t>((s.l + s.r) * 16384.0f));
    if (i) {
      data[0] = value & 0xff;
      data[1] = (data[1] & 0xf0) | (value >> 8);
    } else {
      data[1] = (data[1] & 0x0f) | ((value & 0x0f) << 4);
      data[2] = value >> 4;
    }
  }
  inline FloatFrame value(int i) const {
    const float sample = Decode12(i
        ? uint16_t(data[0]) | ((uint16_t(data[1]) & 0x0f) << 8)
        : (uint16_t(data[1]) >> 4) | (uint16_t(data[2]) << 4));
    return {sample, sample};
  }
  enum { bundle_size = 2 };
  uint8_t data[3];
};

static_assert(sizeof(Item<RESOLUTION_12_BIT, 1>) == 3, "12-bit mono storage size");
static_assert(sizeof(Item<RESOLUTION_12_BIT, 2>) == 3, "12-bit stereo storage size");

template<>
struct Item<RESOLUTION_8_BIT, 1> {
  void Store(int i, FloatFrame s) {
    lr = Lin2MuLaw(
        stmlib::Clip16(static_cast<int32_t>((s.l + s.r) * 16384.0f)));
  }
  inline FloatFrame value(int i) const {
    FloatFrame result;
    result.l = result.r = MuLaw2Lin(lr);
    return result;
  }
  enum { bundle_size = 1 };
  uint8_t lr;
};

template<>
struct Item<RESOLUTION_8_BIT, 2> {
  void Store(int i, FloatFrame s) {
    l = Lin2MuLaw(stmlib::Clip16(static_cast<int32_t>(s.l * 32768.0f)));
    r = Lin2MuLaw(stmlib::Clip16(static_cast<int32_t>(s.r * 32768.0f)));
  }
  inline FloatFrame value(int i) const {
    FloatFrame result;
    result.l = MuLaw2Lin(l);
    result.r = MuLaw2Lin(r);
    return result;
  }
  enum { bundle_size = 1 };
  uint8_t l;
  uint8_t r;
};

enum Interpolation {
  INTERPOLATION_ZOH,
  INTERPOLATION_LINEAR,
  INTERPOLATION_HERMITE
};

template<
    Resolution resolution,
    int max_delay,
    int num_channels,
    size_t bank_size=262144,
    size_t num_banks=3>
class DelayLine {
 public:
  DelayLine() { }
  ~DelayLine() { }
  
  typedef Item<resolution, num_channels> ItemType;

  enum {
    NUM_CHANNELS = num_channels,
    MAX_DELAY = max_delay,
    
    interpolation_tail = 4,
    freeze_tail_size = 256,
  };
  
  void Init(uint8_t* buffer, size_t allocated_size) {
    assert(
        allocated_size >= bank_size * num_banks + freeze_tail_size_in_bytes());
    uint8_t* buffers[num_banks];
    for (size_t i = 0; i < num_banks; ++i) {
      buffers[i] = buffer;
      buffer += bank_size;
    }
    Init(buffers);
  }

  void Init(uint8_t* buffers[]) {
    /*assert((delay_line_size_in_bytes() + freeze_tail_size_in_bytes()) <= \
           bank_size * num_banks);
    if (resolution == RESOLUTION_16_BIT && num_channels == 2) {
      assert(sizeof(ItemType) == 4);
    }
    if (resolution == RESOLUTION_16_BIT && num_channels == 1) {
      assert(sizeof(ItemType) == 2);
    }
    if (resolution == RESOLUTION_12_BIT && num_channels == 2) {
      assert(sizeof(ItemType) == 3);
    }
    if (resolution == RESOLUTION_12_BIT && num_channels == 1) {
      assert(sizeof(ItemType) == 3);
    }
    if (resolution == RESOLUTION_8_BIT && num_channels == 2) {
      assert(sizeof(ItemType) == 2);
    }
    if (resolution == RESOLUTION_8_BIT && num_channels == 1) {
      assert(sizeof(ItemType) == 1);
    }*/

    static_assert(std::is_trivially_copyable<ItemType>::value,
                  "Delay storage requires trivially copyable items");
    static_assert(std::is_trivially_copyable<FloatFrame>::value,
                  "Freeze storage requires trivially copyable frames");
    freeze_tail_ = &buffers[num_banks - 1][bank_size - freeze_tail_size_in_bytes()];
    for (size_t i = 0; i < num_banks; ++i) {
      items_[i] = buffers[i];
    }
    
    Reset();
  }
  
  inline size_t freeze_tail_size_in_bytes() const {
    return freeze_tail_size * sizeof(FloatFrame);
  }
  
  inline size_t delay_line_size_in_bytes() {
    return (max_delay + interpolation_tail) * sizeof(ItemType) / ItemType::bundle_size;
  }
  
  inline size_t items_per_bank() const {
    return bank_size / sizeof(ItemType);
  }
  
  inline uint8_t* raw_bytes(size_t offset) {
    return items_[offset / bank_size] + (offset % bank_size);
  }
  
  inline int* mutable_write_ptr() { return &write_ptr_; }
  
  inline void Reset(bool clear) {
    write_ptr_ = 0;
    tail_write_ptr_ = 0;
    tail_xfade_duration_ = 0;
    frozen_ = false;
    written_since_last_frozen_ = 0;
    duration_ = max_delay;
    
    if (clear) {
      FloatFrame zero;
      zero.l = zero.r = 0.0f;

      ItemType zero_item{};
      zero_item.Store(0, zero);
      zero_item.Store(1, zero);
      for (size_t bank = 0; bank < num_banks; ++bank) {
        for (size_t i = 0; i < items_per_bank(); ++i) {
          std::memcpy(items_[bank] + i * sizeof(ItemType), &zero_item, sizeof(zero_item));
        }
      }
      for (size_t i = 0; i < freeze_tail_size; ++i) {
        std::memcpy(freeze_tail_ + i * sizeof(FloatFrame), &zero, sizeof(zero));
      }
    }
  }
  
  inline void Reset() {
    Reset(true);
  }
  
  inline void Write(FloatFrame in) {
    Store(write_ptr_, in);
    if (write_ptr_ < interpolation_tail) {
      Store(write_ptr_ + max_delay, in);
    }
    if (write_ptr_ == 0) {
      write_ptr_ = max_delay - 1;
    } else {
      --write_ptr_;
    }
    ++written_since_last_frozen_;
    if (written_since_last_frozen_ >= max_delay) {
      written_since_last_frozen_ = 0;
    }
    ++duration_;
    if (duration_ >= max_delay) {
      duration_ = max_delay;
    }
  }
  
  inline void Trim() {
    tail_write_ptr_ = 0;
    duration_ = 0;
  }
  
  inline void Write(FloatFrame in, bool frozen) {
    if (frozen) {
      if (!frozen_) {
        tail_write_ptr_ = 0;
      }
      if (tail_write_ptr_ < freeze_tail_size) {
        std::memcpy(freeze_tail_ + size_t(tail_write_ptr_) * sizeof(in), &in, sizeof(in));
        ++tail_write_ptr_;
      }
      written_since_last_frozen_ = 0;
    } else {
      if (frozen_ && tail_write_ptr_) {
        tail_xfade_duration_ = tail_write_ptr_;
      }
      if (tail_write_ptr_) {
        const int tail_position = tail_xfade_duration_ - tail_write_ptr_;
        const float t = float(tail_write_ptr_) / float(tail_xfade_duration_);
        const float gain = beads::SmoothStep(t);
        const FloatFrame tail = freeze_sample(tail_position);
        in.l += (tail.l - in.l) * gain;
        in.r += (tail.r - in.r) * gain;

        --tail_write_ptr_;
      }
      Write(in);
    }
    frozen_ = frozen;
  }
  
  template<Interpolation interpolation>
  inline FloatFrame Read(int32_t delay) const {
    int32_t delay_integral = delay >> 10;
    float delay_fractional = float(delay & 1023) / 1024.0f;
    return Read<interpolation>(delay_integral, delay_fractional);
  }
  
  template<Interpolation interpolation>
  inline FloatFrame Read(int delay_integral, float delay_fractional) const {
    if (interpolation == INTERPOLATION_HERMITE) {
      delay_integral += max_delay - 1; // Fix latency.
    }
    int i = (write_ptr_ + delay_integral) % max_delay;
    
    FloatFrame value;
    
    if (interpolation == INTERPOLATION_ZOH) {
      value = Load(i);
    } else if (interpolation == INTERPOLATION_LINEAR) {
      const FloatFrame x0 = Load(i);
      const FloatFrame x1 = Load(i + 1);
      value = x0 + (x1 - x0) * delay_fractional;
    } else {
      FloatFrame x[4];
      LoadQuad(i, x);
      
      const FloatFrame c = (x[2] - x[0]) * 0.5f;
      const FloatFrame v = x[1] - x[2];
      const FloatFrame w = c + v;
      const FloatFrame a = w + v + (x[3] - x[1]) * 0.5f;
      const FloatFrame b_neg = w + a;
      const float t = delay_fractional;
      value = (((a * t) - b_neg) * t + c) * delay_fractional + x[1];
    }
    return value;
  }
  
  template<Interpolation interpolation>
  inline FloatFrame Read(float delay) const {
    MAKE_INTEGRAL_FRACTIONAL(delay)
    return Read<interpolation>(delay_integral, delay_fractional);
  }
  
  inline bool frozen() const { return frozen_; }
  
  inline int duration() const { return duration_; }

  inline int buffer_capacity() const { return max_delay; }

  inline DelayBufferState buffer_state() const {
    DelayBufferState state = {
      write_ptr_, duration_, tail_write_ptr_, tail_xfade_duration_,
      written_since_last_frozen_, frozen_
    };
    return state;
  }

  inline bool valid_buffer_state(const DelayBufferState& state) const {
    return state.write_ptr >= 0 && state.write_ptr < max_delay &&
        state.duration >= 0 && state.duration <= max_delay &&
        state.tail_write_ptr >= 0 && state.tail_write_ptr <= freeze_tail_size &&
        state.tail_xfade_duration >= 0 && state.tail_xfade_duration <= freeze_tail_size &&
        state.written_since_last_frozen >= 0 &&
        state.written_since_last_frozen < max_delay;
  }

  inline bool restore_buffer_state(const DelayBufferState& state) {
    if (!valid_buffer_state(state)) {
      return false;
    }
    write_ptr_ = state.write_ptr;
    duration_ = state.duration;
    tail_write_ptr_ = state.tail_write_ptr;
    tail_xfade_duration_ = state.tail_xfade_duration;
    written_since_last_frozen_ = state.written_since_last_frozen;
    frozen_ = state.frozen;
    return true;
  }

  inline int export_buffer(FloatFrame* out, int capacity) const {
    if (!out || capacity <= 0) {
      return 0;
    }
    const int count = duration_ < capacity ? duration_ : capacity;
    for (int i = 0; i < count; ++i) {
      int index = (write_ptr_ + duration_ - i) % max_delay;
      if (index < 0) {
        index += max_delay;
      }
      out[i] = Load(index);
    }
    return count;
  }

  inline bool import_buffer(const FloatFrame* in, int count) {
    if (count < 0 || count > max_delay || (count && !in)) {
      return false;
    }
    for (int i = 0; i < count; ++i) {
      if (!std::isfinite(in[i].l) || !std::isfinite(in[i].r)) return false;
    }
    Reset(true);
    Trim();
    for (int i = 0; i < count; ++i) {
      const FloatFrame sample = {
          std::max(-1.0f, std::min(1.0f, in[i].l)),
          std::max(-1.0f, std::min(1.0f, in[i].r))
      };
      Write(sample);
    }
    // DelayGranularPlayer subtracts a 514-frame safety margin from duration.
    // Keep short imports usable by exposing that minimum, with the reset-zero
    // samples preceding the imported audio in chronological order.
    const int minimum_duration = max_delay < 515 ? max_delay : 515;
    if (duration_ < minimum_duration) {
      duration_ = minimum_duration;
    }
    tail_write_ptr_ = 0;
    tail_xfade_duration_ = 0;
    written_since_last_frozen_ = 0;
    frozen_ = true;
    return true;
  }

  inline int remaining() const {
    return max_delay - written_since_last_frozen_;
  }
  
 private:
  inline void Store(int i, FloatFrame value) {
    const int index = i / ItemType::bundle_size;
    ItemType stored = item(index);
    stored.Store(i % ItemType::bundle_size, value);
    const size_t bank = num_banks == 1 ? 0 : size_t(index) / items_per_bank();
    const size_t offset = num_banks == 1 ? size_t(index) : size_t(index) % items_per_bank();
    std::memcpy(items_[bank] + offset * sizeof(ItemType), &stored, sizeof(stored));
  }
  
  inline FloatFrame Load(int i) const {
    return item(i / ItemType::bundle_size).value(i % ItemType::bundle_size);
  }
  
  inline void LoadQuad(int i, FloatFrame* x) const {
    // Try to find optimizations if there are several banks and if the
    // memory layout is not too gnarly.
    if (num_banks != 1) {
      if (ItemType::bundle_size == 1) {
        // Load the first item.
        size_t offset = size_t(i) % items_per_bank();
        size_t bank = size_t(i) / items_per_bank();
        if ((offset + 3) < items_per_bank()) {
          // Optimization found! All items belong in the same bank!
          x[0] = item_at(bank, offset).value(0);
          x[1] = item_at(bank, offset + 1).value(0);
          x[2] = item_at(bank, offset + 2).value(0);
          x[3] = item_at(bank, offset + 3).value(0);
          return;
        }
      } else if (ItemType::bundle_size == 2) {
        // Load the first item.
        size_t offset = size_t(i >> 1) % items_per_bank();
        size_t bank = size_t(i >> 1) / items_per_bank();

        if ((offset + 2) < items_per_bank()) {
          // Optimization found! All items belong in the same bank!
          if ((i % 2) == 0) {
            x[0] = item_at(bank, offset).value(0);
            x[1] = item_at(bank, offset).value(1);
            x[2] = item_at(bank, offset + 1).value(0);
            x[3] = item_at(bank, offset + 1).value(1);
          } else {
            x[0] = item_at(bank, offset).value(1);
            x[1] = item_at(bank, offset + 1).value(0);
            x[2] = item_at(bank, offset + 1).value(1);
            x[3] = item_at(bank, offset + 2).value(0);
          }
          return;
        }
      }
    }
    
    // No optmization was found, use the slow code.
    x[0] = Load(i);
    x[1] = Load(i + 1);
    x[2] = Load(i + 2);
    x[3] = Load(i + 3);
  }
   
  // The banks are shared by several media formats. Keep them as byte arrays
  // and copy to live objects rather than aliasing storage with incompatible types.
  inline ItemType item_at(size_t bank, size_t offset) const {
    ItemType result{};
    std::memcpy(&result, items_[bank] + offset * sizeof(ItemType), sizeof(result));
    return result;
  }

  inline ItemType item(int i) const {
    return num_banks == 1
        ? item_at(0, size_t(i))
        : item_at(size_t(i) / items_per_bank(), size_t(i) % items_per_bank());
  }

  inline FloatFrame freeze_sample(int i) const {
    FloatFrame result{};
    std::memcpy(&result, freeze_tail_ + size_t(i) * sizeof(result), sizeof(result));
    return result;
  }

  uint8_t* items_[num_banks];
  uint8_t* freeze_tail_;

  int write_ptr_;
  int tail_write_ptr_;
  int tail_xfade_duration_;
  int written_since_last_frozen_;
  int duration_;
  
  bool frozen_;

  DISALLOW_COPY_AND_ASSIGN(DelayLine);
};

}  // namespace beads

#endif  // BEADS_DSP_DELAY_LINE_H_
