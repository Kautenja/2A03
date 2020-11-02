// A Ricoh 2A03 Chip module.
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

#include "plugin.hpp"
#include "engine/chip_module.hpp"
#include "dsp/ricoh_2a03.hpp"

// TODO: discrete DMC sampler module
// TODO: volume control for the triangle waveform
// TODO: hard sync for triangle and pulse waveforms

// ---------------------------------------------------------------------------
// MARK: Module
// ---------------------------------------------------------------------------

/// A Ricoh 2A03 chip emulator module.
struct Terracillator : ChipModule<Ricoh2A03> {
 private:
    /// Schmitt Triggers for handling inputs to the LFSR port
    dsp::SchmittTrigger lfsr[PORT_MAX_CHANNELS];

 public:
    /// the indexes of parameters (knobs, switches, etc.) on the module
    enum ParamIds {
        ENUMS(PARAM_FREQ,  Ricoh2A03::OSC_COUNT - 1),
        PARAM_NOISE_PERIOD,
        ENUMS(PARAM_FM,    Ricoh2A03::OSC_COUNT - 1),
        PARAM_LFSR,
        ENUMS(PARAM_LEVEL, Ricoh2A03::OSC_COUNT),
        ENUMS(PARAM_PW, 2),
        NUM_PARAMS
    };

    /// the indexes of input ports on the module
    enum InputIds {
        ENUMS(INPUT_VOCT,  Ricoh2A03::OSC_COUNT - 1),
        INPUT_NOISE_PERIOD,
        ENUMS(INPUT_FM,    Ricoh2A03::OSC_COUNT - 1),
        INPUT_LFSR,
        ENUMS(INPUT_LEVEL, Ricoh2A03::OSC_COUNT),
        ENUMS(INPUT_PW, 2),
        INPUT_SYNC,
        NUM_INPUTS
    };

    /// the indexes of output ports on the module
    enum OutputIds {
        ENUMS(OUTPUT_OSCILLATOR, Ricoh2A03::OSC_COUNT),
        NUM_OUTPUTS
    };

    /// the indexes of lights on the module
    enum LightIds {
        ENUMS(LIGHTS_LEVEL, 3 * Ricoh2A03::OSC_COUNT),
        NUM_LIGHTS
    };

    /// @brief Initialize a new 2A03 Chip module.
    Terracillator() : ChipModule<Ricoh2A03>(6.f) {
        normal_outputs = true;
        config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
        for (unsigned i = 0; i < Ricoh2A03::OSC_COUNT; i++) {
            // get the name of the channel
            std::string name;
            switch (i) {
                case 0:
                case 1: name = "Pulse " + std::to_string(i + 1); break;
                case 2: name = "Triangle"; break;
                case 3: name = "Noise"; break;
            }
            if (i < 2) {  // only pulse channels have duty cycle
                configParam(PARAM_PW + i, 0, 3, 2, name + " Duty Cycle");
            }
            if (i < 3) {  // only pulse and triangle channels have frequency
                configParam(PARAM_FREQ + i,   -2.5f, 2.5f, 0.f, name + " Frequency", " Hz", 2, dsp::FREQ_C4);
                configParam(PARAM_FM + i, -1.f, 1.f, 0.f, name + " FM");
            } else {  // noise channel has a period and LFSR setting
                configParam(PARAM_NOISE_PERIOD, 0, 15, 7, "Noise Period");
                configParam(PARAM_LFSR, 0, 1, 0, "Noise LFSR");
            }
            // all channels have a volume setting
            configParam(PARAM_LEVEL + i, 0, 15, 10, name + " Volume");
        }
    }

 protected:
    /// trigger for handling inputs to the sync port for the saw wave
    rack::dsp::SchmittTrigger syncTriggers[PORT_MAX_CHANNELS];

    /// @brief Get the frequency for the given oscillator and polyphony channel
    ///
    /// @param oscillator the oscillator to return the frequency for
    /// @param channel the polyphonic channel to return the frequency for
    /// @param freq_min the minimal value for the frequency register to
    /// produce sound
    /// @param freq_max the maximal value for the frequency register
    /// @param clock_division the clock division of the oscillator relative
    /// to the CPU
    /// @returns the 11 bit frequency value from the panel
    /// @details
    /// parameters for pulse wave:
    /// freq_min = 8, freq_max = 1023, clock_division = 16
    /// parameters for triangle wave:
    /// freq_min = 2, freq_max = 2047, clock_division = 32
    ///
    inline uint16_t getFrequency(
        unsigned oscillator,
        unsigned channel,
        float freq_min,
        float freq_max,
        float clock_division
    ) {
        // get the pitch from the parameter and control voltage
        float pitch = params[PARAM_FREQ + oscillator].getValue();
        // get the normalled input voltage based on the voice index. Voice 0
        // has no prior voltage, and is thus normalled to 0V. Reset this port's
        // voltage afterward to propagate the normalling chain forward.
        const auto normalPitch = oscillator ? inputs[INPUT_VOCT + oscillator - 1].getVoltage(channel) : 0.f;
        const auto pitchCV = inputs[INPUT_VOCT + oscillator].getNormalVoltage(normalPitch, channel);
        inputs[INPUT_VOCT + oscillator].setVoltage(pitchCV, channel);
        pitch += pitchCV;
        // get the attenuverter parameter value
        const auto att = params[PARAM_FM + oscillator].getValue();
        // get the normalled input voltage based on the voice index. Voice 0
        // has no prior voltage, and is thus normalled to 5V. Reset this port's
        // voltage afterward to propagate the normalling chain forward.
        const auto normalMod = oscillator ? inputs[INPUT_FM + oscillator - 1].getVoltage(channel) : 5.f;
        const auto mod = inputs[INPUT_FM + oscillator].getNormalVoltage(normalMod, channel);
        inputs[INPUT_FM + oscillator].setVoltage(mod, channel);
        pitch += att * mod / 5.f;
        // convert the pitch to frequency based on standard exponential scale
        float freq = rack::dsp::FREQ_C4 * powf(2.0, pitch);
        freq = rack::clamp(freq, 0.0f, 20000.0f);
        // convert the frequency to an 11-bit value
        freq = (buffers[channel][oscillator].get_clock_rate() / (clock_division * freq)) - 1;
        return rack::clamp(freq, freq_min, freq_max);
    }

    /// @brief Get the PW for the given oscillator and polyphony channel
    ///
    /// @param oscillator the oscillator to return the pulse width for
    /// @param channel the polyphony channel of the given oscillator
    /// @returns the pulse width value coded in an 8-bit container
    ///
    inline uint8_t getPulseWidth(unsigned oscillator, unsigned channel) {
        // the minimal value for the pulse width register
        static constexpr float PW_MIN = 0;
        // the maximal value for the pulse width register
        static constexpr float PW_MAX = 3;
        // get the pulse width from the parameter knob
        auto param = params[PARAM_PW + oscillator].getValue();
        // get the normalled input voltage based on the voice index. Voice 0
        // has no prior voltage, and is thus normalled to 5V. Reset this port's
        // voltage afterward to propagate the normalling chain forward.
        const auto normalMod = oscillator ? inputs[INPUT_PW + oscillator - 1].getVoltage(channel) : 0.f;
        const auto mod = inputs[INPUT_PW + oscillator].getNormalVoltage(normalMod, channel);
        inputs[INPUT_PW + oscillator].setVoltage(mod, channel);
        // get the 8-bit pulse width clamped within legal limits
        uint8_t pw = rack::clamp(param + rescale(mod, 0.f, 7.f, 0, 4), PW_MIN, PW_MAX);
        // shift the pulse width over into the high 2 bits
        return pw << 6;
    }

    /// @brief Return the period of the noise oscillator from the panel controls.
    ///
    /// @param channel the polyphony channel of the given oscillator
    ///
    inline uint8_t getNoisePeriod(unsigned channel) {
        // the minimal value for the frequency register to produce sound
        static constexpr float FREQ_MIN = 0;
        // the maximal value for the frequency register
        static constexpr float FREQ_MAX = 15;
        // get the attenuation from the parameter knob
        float freq = params[PARAM_NOISE_PERIOD].getValue();
        // apply the control voltage to the attenuation
        if (inputs[INPUT_NOISE_PERIOD].isConnected())
            freq += inputs[INPUT_NOISE_PERIOD].getPolyVoltage(channel) / 2.f;
        return FREQ_MAX - rack::clamp(floorf(freq), FREQ_MIN, FREQ_MAX);
    }

    /// @brief Return the volume level from the panel controls for a given oscillator and polyphony channel.
    ///
    /// @param oscillator the oscillator to return the volume level of
    /// @param channel the polyphony channel of the given oscillator
    /// @returns the volume level of the given oscillator
    /// @details
    /// oscillator can be one of 0, 1, or 3. the triangle oscillator (2) has no
    /// volume control.
    ///
    inline uint8_t getVolume(unsigned oscillator, unsigned channel) {
        // the minimal value for the volume width register
        static constexpr float MIN = 0;
        // the maximal value for the volume width register
        static constexpr float MAX = 15;
        // get the level from the parameter knob
        auto level = params[PARAM_LEVEL + oscillator].getValue();
        // get the normalled input voltage based on the voice index. Voice 0
        // has no prior voltage, and is thus normalled to 10V. Reset this port's
        // voltage afterward to propagate the normalling chain forward.
        const auto normal = oscillator ? inputs[INPUT_LEVEL + oscillator - 1].getVoltage(channel) : 10.f;
        const auto voltage = inputs[INPUT_LEVEL + oscillator].getNormalVoltage(normal, channel);
        inputs[INPUT_LEVEL + oscillator].setVoltage(voltage, channel);
        // apply the control voltage to the level. Normal to a constant
        // 10V source instead of checking if the cable is connected
        level = roundf(level * voltage / 10.f);
        // get the 8-bit attenuation by inverting the level and clipping
        // to the legal bounds of the parameter
        return rack::clamp(level, MIN, MAX);
    }

    /// @brief Process the audio rate inputs for the given channel.
    ///
    /// @param args the sample arguments (sample rate, sample time, etc.)
    /// @param channel the polyphonic channel to process the audio inputs to
    ///
    inline void processAudio(const ProcessArgs &args, unsigned channel) final {
        // ---------------------------------------------------------------
        // pulse oscillator (2)
        // ---------------------------------------------------------------
        for (unsigned oscillator = 0; oscillator < 2; oscillator++) {
            // write the frequency to the low and high registers
            // - there are 4 registers per pulse oscillator, multiply oscillator by 4 to
            //   produce an offset between registers based on oscillator index
            uint16_t freq = getFrequency(oscillator, channel, 8, 1023, 16);
            auto lo =  freq & 0b0000000011111111;
            apu[channel].write(Ricoh2A03::PULSE0_LO + 4 * oscillator, lo);
            auto hi = (freq & 0b0000011100000000) >> 8;
            apu[channel].write(Ricoh2A03::PULSE0_HI + 4 * oscillator, hi);
        }
        // ---------------------------------------------------------------
        // triangle oscillator
        // ---------------------------------------------------------------
        // write the frequency to the low and high registers
        uint16_t freq = getFrequency(2, channel, 2, 2047, 32);
        apu[channel].write(Ricoh2A03::TRIANGLE_LO,  freq & 0b0000000011111111);
        apu[channel].write(Ricoh2A03::TRIANGLE_HI, (freq & 0b0000011100000000) >> 8);
        // sync input
        const float sync = inputs[INPUT_SYNC].getVoltage(channel);
        if (syncTriggers[channel].process(rescale(sync, 0.f, 2.f, 0.f, 1.f)))
            apu[channel].reset_phase(2);
    }

    /// @brief Process the CV inputs for the given channel.
    ///
    /// @param args the sample arguments (sample rate, sample time, etc.)
    /// @param channel the polyphonic channel to process the CV inputs to
    ///
    inline void processCV(const ProcessArgs &args, unsigned channel) final {
        lfsr[channel].process(rescale(inputs[INPUT_LFSR].getVoltage(channel), 0.f, 2.f, 0.f, 1.f));
        bool is_lfsr = params[PARAM_LFSR].getValue() - lfsr[channel].state;
        // ---------------------------------------------------------------
        // pulse oscillator (2)
        // ---------------------------------------------------------------
        for (unsigned oscillator = 0; oscillator < 2; oscillator++) {
            // set the pulse width of the pulse wave (high 3 bits) and set
            // the volume (low 4 bits). the 5th bit controls the envelope,
            // high sets constant volume.
            auto volume = getPulseWidth(oscillator, channel) | 0b00010000 | getVolume(oscillator, channel);
            apu[channel].write(Ricoh2A03::PULSE0_VOL + 4 * oscillator, volume);
        }
        // ---------------------------------------------------------------
        // triangle oscillator
        // ---------------------------------------------------------------
        // write the linear register to enable the oscillator
        apu[channel].write(Ricoh2A03::TRIANGLE_LINEAR, 0b01111111);
        // ---------------------------------------------------------------
        // noise oscillator
        // ---------------------------------------------------------------
        apu[channel].write(Ricoh2A03::NOISE_LO, (is_lfsr << 7) | getNoisePeriod(channel));
        apu[channel].write(Ricoh2A03::NOISE_HI, 0);
        // get volume for triangle to normal voltages
        auto volumeTriangle = getVolume(2, channel);
        apu[channel].write(Ricoh2A03::NOISE_VOL, 0b00010000 | getVolume(3, channel));
        // enable all four oscillators
        apu[channel].write(Ricoh2A03::SND_CHN, 0b00001111);
    }

    /// @brief Process the lights on the module.
    ///
    /// @param args the sample arguments (sample rate, sample time, etc.)
    /// @param channels the number of active polyphonic channels
    ///
    inline void processLights(const ProcessArgs &args, unsigned channels) final {
        for (unsigned voice = 0; voice < Ricoh2A03::OSC_COUNT; voice++) {
            // get the global brightness scale from -12 to 3
            auto brightness = vuMeter[voice].getBrightness(-12, 3);
            // set the red light based on total brightness and
            // brightness from 0dB to 3dB
            lights[LIGHTS_LEVEL + voice * 3 + 0].setBrightness(brightness * vuMeter[voice].getBrightness(0, 3));
            // set the red light based on inverted total brightness and
            // brightness from -12dB to 0dB
            lights[LIGHTS_LEVEL + voice * 3 + 1].setBrightness((1 - brightness) * vuMeter[voice].getBrightness(-12, 0));
            // set the blue light to off
            lights[LIGHTS_LEVEL + voice * 3 + 2].setBrightness(0);
        }
    }
};

// ---------------------------------------------------------------------------
// MARK: Widget
// ---------------------------------------------------------------------------

/// The panel widget for 2A03.
struct TerracillatorWidget : ModuleWidget {
    /// @brief Initialize a new widget.
    ///
    /// @param module the back-end module to interact with
    ///
    explicit TerracillatorWidget(Terracillator *module) {
        setModule(module);
        static constexpr auto panel = "res/Terracillator.svg";
        setPanel(APP->window->loadSvg(asset::plugin(plugin_instance, panel)));
        // panel screws
        addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, 0)));
        addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
        addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
        addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
        for (unsigned i = 0; i < Ricoh2A03::OSC_COUNT; i++) {
            // Frequency / Noise Period
            auto freq = createParam<Trimpot>(  Vec(12 + 35 * i, 32),  module, Terracillator::PARAM_FREQ        + i);
            freq->snap = i == 3;
            addParam(freq);
            addInput(createInput<PJ301MPort>(  Vec(10 + 35 * i, 71),  module, Terracillator::INPUT_VOCT        + i));
            // FM / LFSR
            addInput(createInput<PJ301MPort>(  Vec(10 + 35 * i, 99), module, Terracillator::INPUT_FM          + i));
            if (i < 3)
                addParam(createParam<Trimpot>( Vec(12 + 35 * i, 144), module, Terracillator::PARAM_FM          + i));
            else
                addParam(createParam<CKSS>(    Vec(120, 141), module, Terracillator::PARAM_FM                  + i));
            // Level
            addParam(createSnapParam<Trimpot>( Vec(12 + 35 * i, 170), module, Terracillator::PARAM_LEVEL       + i));
            addInput(createInput<PJ301MPort>(  Vec(10 + 35 * i, 210), module, Terracillator::INPUT_LEVEL       + i));
            // Pulse Width / Sync
            if (i < 2) {
                addParam(createSnapParam<Trimpot>(Vec(12 + 35 * i, 241), module, Terracillator::PARAM_PW + i));
                addInput(createInput<PJ301MPort>(Vec(10 + 35 * i, 281), module, Terracillator::INPUT_PW + i));
            } else if (i == 2) {
                addInput(createInput<PJ301MPort>(Vec(12 + 35 * i, 264), module, Terracillator::INPUT_SYNC));
            }
            // Output
            addChild(createLight<SmallLight<RedGreenBlueLight>>(Vec(29 + 35 * i, 319), module, Terracillator::LIGHTS_LEVEL + 3 * i));
            addOutput(createOutput<PJ301MPort>(Vec(10 + 35 * i, 324), module, Terracillator::OUTPUT_OSCILLATOR + i));
        }
    }
};

/// the global instance of the model
Model *modelTerracillator = createModel<Terracillator, TerracillatorWidget>("2A03");