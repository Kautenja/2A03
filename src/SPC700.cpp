// A Sony SPC700 chip (from Nintendo SNES) emulator module.
// Copyright 2020 Christian Kauten
//
// Author: Christian Kauten (kautenja@auburn.edu)
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

#include "plugin.hpp"
#include "componentlibrary.hpp"
#include "engine/chip_module.hpp"
#include "dsp/sony_spc700.hpp"

// ---------------------------------------------------------------------------
// MARK: Module
// ---------------------------------------------------------------------------

/// A Sony SPC700 chip (from Nintendo SNES) emulator module.
struct ChipSPC700 : Module {
 public:
    /// the indexes of parameters (knobs, switches, etc.) on the module
    enum ParamIds {
        NUM_PARAMS
    };
    /// the indexes of input ports on the module
    enum InputIds {
        NUM_INPUTS
    };
    /// the indexes of output ports on the module
    enum OutputIds {
        NUM_OUTPUTS
    };
    /// the indexes of lights on the module
    enum LightIds {
        NUM_LIGHTS
    };

    /// @brief Initialize a new SPC700 Chip module.
    ChipSPC700() {
        config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
    }

 protected:
    /// @brief Process the CV inputs for the given channel.
    ///
    /// @param args the sample arguments (sample rate, sample time, etc.)
    /// @param channel the polyphonic channel to process the CV inputs to
    ///
    inline void process(const ProcessArgs &args) final {

    }
};

// ---------------------------------------------------------------------------
// MARK: Widget
// ---------------------------------------------------------------------------

/// The panel widget for SPC700.
struct ChipSPC700Widget : ModuleWidget {
    /// @brief Initialize a new widget.
    ///
    /// @param module the back-end module to interact with
    ///
    explicit ChipSPC700Widget(ChipSPC700 *module) {
        setModule(module);
        static constexpr auto panel = "res/SPC700.svg";
        setPanel(APP->window->loadSvg(asset::plugin(plugin_instance, panel)));
        // panel screws
        addChild(createWidget<ScrewBlack>(Vec(RACK_GRID_WIDTH, 0)));
        addChild(createWidget<ScrewBlack>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
        addChild(createWidget<ScrewBlack>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
        addChild(createWidget<ScrewBlack>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
    }
};

/// the global instance of the model
Model *modelChipSPC700 = createModel<ChipSPC700, ChipSPC700Widget>("SPC700");
