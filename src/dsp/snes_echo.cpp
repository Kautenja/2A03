// Sony SPC700 emulator.
// Copyright 2020 Christian Kauten
// Copyright 2006 Shay Green
// Copyright 2002 Brad Martin
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.
//
// derived from: Game_Music_Emu 0.5.2
// Based on Brad Martin's OpenSPC DSP emulator
//

#include "snes_echo.hpp"
#include <algorithm>
#include <cstddef>
#include <limits>

/// Clamp an integer to a 16-bit value.
///
/// @param n a 32-bit integer value to clip
/// @returns n clipped to a 16-bit value [-32768, 32767]
///
inline int clamp_16(int n) {
    const int lower = std::numeric_limits<int16_t>::min();
    const int upper = std::numeric_limits<int16_t>::max();
    return std::max(lower, std::min(n, upper));
}

void Sony_S_DSP_Echo::run(int left, int right, int16_t* output_buffer) {
    // get the current feedback sample in the echo buffer
    auto const echo_sample = reinterpret_cast<BufferSample*>(&ram[buffer_head]);
    // increment the echo pointer by the size of the echo buffer sample (4)
    buffer_head += sizeof(BufferSample);
    // check if for the end of the ring buffer and wrap the pointer around
    // the echo delay is clamped in [0, 15] and each delay index requires
    // 2KB of RAM (0x800)
    if (buffer_head >= (delay & DELAY_LEVELS) * DELAY_LEVEL_BYTES)
        buffer_head = 0;
    // cache the feedback value (sign-extended to 32-bit)
    int fb_left = echo_sample->samples[BufferSample::LEFT];
    int fb_right = echo_sample->samples[BufferSample::RIGHT];

    // put samples in history ring buffer
    const int fir_offset = this->fir_offset;
    int16_t (*fir_pos)[2] = &fir_buf[fir_offset];
    this->fir_offset = (fir_offset + 7) & 7;  // move backwards one step
    fir_pos[0][0] = fb_left;
    fir_pos[0][1] = fb_right;
    // duplicate at +8 eliminates wrap checking below
    fir_pos[8][0] = fb_left;
    fir_pos[8][1] = fb_right;

    // FIR left channel
    fb_left =     fb_left * fir_coeff[7] +
            fir_pos[1][0] * fir_coeff[6] +
            fir_pos[2][0] * fir_coeff[5] +
            fir_pos[3][0] * fir_coeff[4] +
            fir_pos[4][0] * fir_coeff[3] +
            fir_pos[5][0] * fir_coeff[2] +
            fir_pos[6][0] * fir_coeff[1] +
            fir_pos[7][0] * fir_coeff[0];
    // FIR right channel
    fb_right =   fb_right * fir_coeff[7] +
            fir_pos[1][1] * fir_coeff[6] +
            fir_pos[2][1] * fir_coeff[5] +
            fir_pos[3][1] * fir_coeff[4] +
            fir_pos[4][1] * fir_coeff[3] +
            fir_pos[5][1] * fir_coeff[2] +
            fir_pos[6][1] * fir_coeff[1] +
            fir_pos[7][1] * fir_coeff[0];

    // put the echo samples into the buffer
    echo_sample->samples[BufferSample::LEFT] =
        clamp_16(left + ((fb_left  * feedback) >> 14));
    echo_sample->samples[BufferSample::RIGHT] =
        clamp_16(right + ((fb_right * feedback) >> 14));

    if (output_buffer) {  // write final samples
        // (1) add the echo to the samples for the left and right channel, (2)
        // clamp the left and right samples, and (3) place them into the buffer
        output_buffer[0] =
            clamp_16(left + ((fb_left  * mixLeft) >> 14));
        output_buffer[1] =
            clamp_16(right + ((fb_right * mixRight) >> 14));
    }
}
