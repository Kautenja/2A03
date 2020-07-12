// A VCV Rack for viewing and editing a wave-table using the mouse pointer.
// Copyright 2020 Christian Kauten
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

#include "rack.hpp"
#include <cstdint>
#include <vector>
#include <functional>

#ifndef WIDGETS_WAVETABLE_EDITOR_HPP_
#define WIDGETS_WAVETABLE_EDITOR_HPP_

/// A widget that displays / edits a wave-table.
struct WaveTableEditor : rack::OpaqueWidget {
 public:
    /// a callback function for passing update events to a listener
    typedef std::function<void(uint32_t, uint64_t)> Callback;

 private:
    /// the background color for the widget
    NVGcolor background;
    /// the fill color for the widget
    NVGcolor fill;
    /// the border color for the widget
    NVGcolor border;
    /// the state of the drag operation
    struct {
        /// whether a drag is currently active
        bool is_active = false;
        /// whether the drag operation is being modified
        bool is_modified = false;
        /// the current position of the mouse pointer during the drag
        rack::Vec position = {0, 0};
    } drag_state;
    /// the length of the wave-table to edit
    uint32_t length;
    /// the bit depth of the waveform
    uint64_t bit_depth;
    /// the vector containing the waveform
    std::vector<uint64_t> waveform;
    /// the callback to pass wave-table update events to
    Callback callback;

 public:
    /// @brief Initialize a new wave-table editor widget.
    ///
    /// @param position the position of the screen on the module
    /// @param size the output size of the display to render
    /// @param background_ the background color for the widget
    /// @param fill_ the fill color for the widget
    /// @param fill_ the border color for the widget
    /// @param length_ the length of the wave-table to edit
    /// @param bit_depth_ the bit-depth of the waveform samples to generate
    ///
    explicit WaveTableEditor(
        rack::Vec position,
        rack::Vec size,
        NVGcolor background_,
        NVGcolor fill_,
        NVGcolor border_,
        uint32_t length_,
        uint64_t bit_depth_,
        Callback callback_
    ) :
        OpaqueWidget(),
        background(background_),
        fill(fill_),
        border(border_),
        length(length_),
        bit_depth(bit_depth_),
        callback(callback_) {
        setPosition(position);
        setSize(size);
        waveform.resize(length, 0);
    }

    /// Respond to a button event on this widget.
    void onButton(const rack::event::Button &e) override {
        // consume the event to prevent it from propagating
        e.consume(this);
        // setup the drag state
        drag_state.is_active = e.button == GLFW_MOUSE_BUTTON_LEFT;
        drag_state.is_modified = e.mods & GLFW_MOD_CONTROL;
        if (e.button == GLFW_MOUSE_BUTTON_RIGHT) {  // right click event
            // TODO: show menu with basic waveforms
        }
        // return if the drag operation is not active
        if (!drag_state.is_active) return;
        // set the position of the drag operation to the position of the mouse
        drag_state.position = e.pos;
        // calculate the normalized x position in [0, 1]
        float x = drag_state.position.x / box.size.x;
        x = rack::math::clamp(x, 0.f, 1.f);
        // calculate the position in the wave-table
        uint32_t index = x * length;
        // calculate the normalized y position in [0, 1]
        // y increases downward in pixel space, so invert about 1
        float y = 1.f - drag_state.position.y / box.size.y;
        y = rack::math::clamp(y, 0.f, 1.f);
        // calculate the value of the wave-table at this index
        uint64_t value = y * bit_depth;
        waveform[index] = value;
        callback(index, value);
    }

    /// Respond to drag move event on this widget.
    void onDragMove(const rack::event::DragMove &e) override {
        // consume the event to prevent it from propagating
        e.consume(this);
        // if the drag operation is not active, return early
        if (!drag_state.is_active) return;
        // update the drag state based on the change in position from the mouse
        uint32_t index = length * rack::math::clamp(drag_state.position.x / box.size.x, 0.f, 1.f);
        drag_state.position.x += e.mouseDelta.x / APP->scene->rackScroll->zoomWidget->zoom;
        uint32_t next_index = length * rack::math::clamp(drag_state.position.x / box.size.x, 0.f, 1.f);
        drag_state.position.y += e.mouseDelta.y / APP->scene->rackScroll->zoomWidget->zoom;
        // calculate the normalized y position in [0, 1]
        // y increases downward in pixel space, so invert about 1
        float y = 1.f - drag_state.position.y / box.size.y;
        y = rack::math::clamp(y, 0.f, 1.f);
        // calculate the value of the wave-table at this index
        uint64_t value = y * bit_depth;
        if (next_index < index)  // swap next index if it's less the current
            (index ^= next_index), (next_index ^= index), (index ^= next_index);
        for (; index < next_index; index++) {  // update the positions
            waveform[index] = value;
            callback(index, value);
        }
    }

    /// @brief Draw the display on the main context.
    ///
    /// @param args the arguments for the draw context for this widget
    ///
    void draw(const DrawArgs& args) override {
        // the x position of the widget
        static constexpr int x = 0;
        // the y position of the widget
        static constexpr int y = 0;
        // the radius for the corner on the rectangle
        static constexpr int corner_radius = 3;
        // arbitrary padding
        static constexpr int pad = 1;
        // -------------------------------------------------------------------
        // draw the background
        // -------------------------------------------------------------------
        nvgBeginPath(args.vg);
        nvgRoundedRect(args.vg, x, y, box.size.x, box.size.y, corner_radius);
        nvgFillColor(args.vg, background);
        nvgFill(args.vg);
        nvgClosePath(args.vg);
        // -------------------------------------------------------------------
        // draw the waveform
        // -------------------------------------------------------------------
        nvgSave(args.vg);
        nvgBeginPath(args.vg);
        nvgScissor(args.vg, x + pad, y + pad, box.size.x - pad, box.size.y - pad);
        nvgMoveTo(args.vg, 0, box.size.y);
        for (uint32_t i = 0; i < length; i++) {
            auto pixelX = box.size.x * i / static_cast<float>(length);
            auto pixelY = box.size.y * (bit_depth - waveform[i]) / static_cast<float>(bit_depth);
            nvgLineTo(args.vg, pixelX, pixelY);
        }
        nvgMoveTo(args.vg, 0, box.size.y);
        nvgStrokeColor(args.vg, fill);
        nvgStroke(args.vg);
        nvgClosePath(args.vg);
        nvgRestore(args.vg);
        // -------------------------------------------------------------------
        // draw the border
        // -------------------------------------------------------------------
        nvgBeginPath(args.vg);
        nvgRoundedRect(args.vg, x, y, box.size.x, box.size.y, corner_radius);
        nvgStrokeColor(args.vg, border);
        nvgStroke(args.vg);
        nvgClosePath(args.vg);
    }
};

#endif  // WIDGETS_WAVETABLE_EDITOR_HPP_
