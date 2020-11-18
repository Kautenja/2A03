// An echo effect module based on the S-SMP chip from Nintendo SNES.
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
#include "dsp/sony_s_dsp/echo.hpp"

// TODO: attenu-verters for FIR parameters

// ---------------------------------------------------------------------------
// MARK: Module
// ---------------------------------------------------------------------------

/// An echo effect module based on the S-SMP chip from Nintendo SNES.
struct ChipS_SMP_Echo : Module {
 private:
    /// the Sony S-DSP echo effect emulator
    Sony_S_DSP_Echo apu[PORT_MAX_CHANNELS];

    /// a VU meter for measuring the input audio levels
    rack::dsp::VuMeter2 inputVUMeter[StereoSample::CHANNELS];
    /// a VU meter for measuring the output audio levels
    rack::dsp::VuMeter2 outputVUMeter[StereoSample::CHANNELS];
    /// a light divider for updating the LEDs every 512 processing steps
    rack::dsp::ClockDivider lightDivider;

 public:
    /// the indexes of parameters (knobs, switches, etc.) on the module
    enum ParamIds {
        PARAM_BYPASS,
        PARAM_DELAY,
        PARAM_FEEDBACK,
        ENUMS(PARAM_MIX, StereoSample::CHANNELS),
        ENUMS(PARAM_FIR_COEFFICIENT, Sony_S_DSP_Echo::FIR_COEFFICIENT_COUNT),
        NUM_PARAMS
    };

    /// the indexes of input ports on the module
    enum InputIds {
        ENUMS(INPUT_AUDIO, StereoSample::CHANNELS),
        INPUT_DELAY,
        INPUT_FEEDBACK,
        ENUMS(INPUT_MIX, StereoSample::CHANNELS),
        ENUMS(INPUT_FIR_COEFFICIENT, Sony_S_DSP_Echo::FIR_COEFFICIENT_COUNT),
        NUM_INPUTS
    };

    /// the indexes of output ports on the module
    enum OutputIds {
        ENUMS(OUTPUT_AUDIO, StereoSample::CHANNELS),
        NUM_OUTPUTS
    };

    /// the indexes of lights on the module
    enum LightIds {
        ENUMS(VU_LIGHTS_INPUT,  3 * StereoSample::CHANNELS),
        ENUMS(VU_LIGHTS_OUTPUT, 3 * StereoSample::CHANNELS),
        NUM_LIGHTS
    };

    /// @brief Initialize a new S-DSP Chip module.
    ChipS_SMP_Echo() {
        config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
        for (unsigned coeff = 0; coeff < Sony_S_DSP_Echo::FIR_COEFFICIENT_COUNT; coeff++)
            configParam(PARAM_FIR_COEFFICIENT  + coeff, -128, 127, apu[0].getFIR(coeff), "FIR Coefficient " + std::to_string(coeff + 1));
        configParam(PARAM_DELAY, 0, Sony_S_DSP_Echo::DELAY_LEVELS, 0, "Echo Delay", "ms", 0, Sony_S_DSP_Echo::MILLISECONDS_PER_DELAY_LEVEL);
        configParam(PARAM_FEEDBACK, -128, 127, 0, "Echo Feedback");
        configParam(PARAM_MIX + 0, -128, 127, 0, "Echo Mix (Left Channel)");
        configParam(PARAM_MIX + 1, -128, 127, 0, "Echo Mix (Right Channel)");
        configParam(PARAM_BYPASS, 0, 1, 0, "Bypass");
        lightDivider.setDivision(512);
    }

 protected:
    /// @brief Return the value of the delay parameter from the panel.
    ///
    /// @param channel the polyphonic channel to get the delay parameter for
    /// @returns the 8-bit delay parameter after applying CV modulations
    ///
    inline uint8_t getDelay(unsigned channel) {
        const float param = params[PARAM_DELAY].getValue();
        const float cv = inputs[INPUT_DELAY].getVoltage(channel) / 10.f;
        const float mod = Sony_S_DSP_Echo::DELAY_LEVELS * cv;
        const float MAX = static_cast<float>(Sony_S_DSP_Echo::DELAY_LEVELS);
        return clamp(param + mod, 0.f, MAX);
    }

    /// @brief Return the value of the feedback parameter from the panel.
    ///
    /// @param channel the feedback channel to get the delay parameter for
    /// @returns the 8-bit feedback parameter after applying CV modulations
    ///
    inline int8_t getFeedback(unsigned channel) {
        const float param = params[PARAM_FEEDBACK].getValue();
        const float cv = inputs[INPUT_FEEDBACK].getVoltage(channel) / 10.f;
        const float mod = std::numeric_limits<int8_t>::max() * cv;
        static constexpr float MIN = std::numeric_limits<int8_t>::min();
        static constexpr float MAX = std::numeric_limits<int8_t>::max();
        return clamp(param + mod, MIN, MAX);
    }

    /// @brief Return the value of the mix parameter from the panel.
    ///
    /// @param channel the polyphonic channel to get the mix parameter for
    /// @param lane the stereo delay lane to get the mix level parameter for
    /// @returns the 8-bit mix parameter after applying CV modulations
    ///
    inline int8_t getMix(unsigned channel, unsigned lane) {
        const float param = params[PARAM_MIX + lane].getValue();
        // get the normal voltage from the left/right pair
        const auto normal = lane ? inputs[INPUT_MIX + lane - 1].getVoltage(channel) : 0.f;
        const float voltage = inputs[INPUT_MIX + lane].getNormalVoltage(normal, channel);
        // get the mod value and clamp within finite precision
        const float mod = std::numeric_limits<int8_t>::max() * voltage / 10.f;
        static constexpr float MIN = std::numeric_limits<int8_t>::min();
        static constexpr float MAX = std::numeric_limits<int8_t>::max();
        return clamp(param + mod, MIN, MAX);
    }

    /// @brief Return the value of the FIR filter parameter from the panel.
    ///
    /// @param channel the polyphonic channel to get the FIR coefficient for
    /// @param index the index of the FIR filter coefficient to get
    /// @returns the 8-bit FIR filter parameter for coefficient at given index
    ///
    inline int8_t getFIRCoefficient(unsigned channel, unsigned index) {
        // get the normal voltage from the previous channel. if the index is
        // 0, use a default voltage of 0V
        const auto normal = index ? inputs[INPUT_FIR_COEFFICIENT + index - 1].getVoltage(channel) : 0.f;
        const float voltage = inputs[INPUT_FIR_COEFFICIENT + index].getNormalVoltage(normal, channel);
        // normal the voltage forward by updating the voltage on the port
        inputs[INPUT_FIR_COEFFICIENT + index].setVoltage(voltage, channel);
        const float mod = std::numeric_limits<int8_t>::max() * voltage / 10.f;
        static constexpr float MIN = std::numeric_limits<int8_t>::min();
        static constexpr float MAX = std::numeric_limits<int8_t>::max();
        // get the parameter value from the knob and return the clamped parameter
        const float param = params[PARAM_FIR_COEFFICIENT + index].getValue();
        return clamp(param + mod, MIN, MAX);
    }

    /// @brief Return the value of the stereo input from the panel.
    ///
    /// @param args the sample arguments (sample rate, sample time, etc.)
    /// @param channel the polyphonic channel to get the audio input for
    /// @param lane the stereo delay lane to get the input voltage for
    /// @returns the 8-bit stereo input for the given lane
    ///
    inline int16_t getInput(const ProcessArgs &args, unsigned channel, unsigned lane) {
        static constexpr float MAX = std::numeric_limits<int16_t>::max();
        // get the normal voltage from the left/right pair
        const auto normal = lane ? inputs[INPUT_AUDIO + lane - 1].getVoltage(channel) : 0.f;
        const auto input = inputs[INPUT_AUDIO + lane].getNormalVoltage(normal, channel) / 5.f;
        // process the input on the VU meter
        inputVUMeter[lane].process(args.sampleTime, input);
        // clamp the value to finite precision and scale to the integer type
        return MAX * math::clamp(input, -1.f, 1.f);
    }

    /// @brief Return the clean value of the stereo input from the panel.
    ///
    /// @param args the sample arguments (sample rate, sample time, etc.)
    /// @param channel the polyphonic channel to get the audio input for
    /// @param lane the stereo delay lane to get the input voltage for
    /// @returns the 8-bit stereo input for the given lane
    ///
    inline void bypassInput(const ProcessArgs &args, unsigned channel, unsigned lane) {
        // get the normal voltage from the left/right pair
        const auto normal = lane ? inputs[INPUT_AUDIO + lane - 1].getVoltage(channel) : 0.f;
        const auto voltage = inputs[INPUT_AUDIO + lane].getNormalVoltage(normal, channel);
        // process the input on the VU meter
        inputVUMeter[lane].process(args.sampleTime, voltage / 5.f);
        outputVUMeter[lane].process(args.sampleTime, voltage / 5.f);
        outputs[OUTPUT_AUDIO + lane].setVoltage(voltage, channel);
    }

    /// @brief Process the CV inputs for the given channel.
    ///
    /// @param args the sample arguments (sample rate, sample time, etc.)
    /// @param channel the polyphonic channel to process the CV inputs to
    ///
    inline void processChannel(const ProcessArgs &args, unsigned channel) {
        // update the delay parameters
        apu[channel].setDelay(getDelay(channel));
        apu[channel].setFeedback(getFeedback(channel));
        apu[channel].setMixLeft(getMix(channel, StereoSample::LEFT));
        apu[channel].setMixRight(getMix(channel, StereoSample::RIGHT));
        // update the FIR Coefficients
        for (unsigned i = 0; i < Sony_S_DSP_Echo::FIR_COEFFICIENT_COUNT; i++) {
            apu[channel].setFIR(i, getFIRCoefficient(channel, i));
        }
        // run a stereo sample through the echo buffer + filter
        auto output = apu[channel].run(
            getInput(args, channel, StereoSample::LEFT),
            getInput(args, channel, StereoSample::RIGHT)
        );
        // write the stereo output to the ports
        for (unsigned i = 0; i < StereoSample::CHANNELS; i++) {
            // get the sample in [0, 1] (clipped by the finite precision of the
            // emulation)
            const auto sample = output.samples[i] / static_cast<float>(std::numeric_limits<int16_t>::max());
            // approximate the VU meter by scaling the sample slightly
            outputVUMeter[i].process(args.sampleTime, 1.2 * sample);
            // set the output
            outputs[OUTPUT_AUDIO + i].setVoltage(5.f * sample, channel);
        }
    }

    /// @brief Set the given VU meter light based on given VU meter.
    ///
    /// @param vuMeter the VU meter to get the data from
    /// @param light the light to update from the VU meter data
    ///
    inline void setLight(rack::dsp::VuMeter2& vuMeter, rack::engine::Light* light) {
        // get the global brightness scale from -12 to 3
        auto brightness = vuMeter.getBrightness(-12, 3);
        // set the red light based on total brightness and
        // brightness from 0dB to 3dB
        (light + 0)->setBrightness(brightness * vuMeter.getBrightness(0, 3));
        // set the red light based on inverted total brightness and
        // brightness from -12dB to 0dB
        (light + 1)->setBrightness((1 - brightness) * vuMeter.getBrightness(-12, 0));
        // set the blue light to off
        (light + 2)->setBrightness(0);
    }

    /// @brief Process the inputs and outputs to/from the module.
    ///
    /// @param args the sample arguments (sample rate, sample time, etc.)
    ///
    inline void process(const ProcessArgs &args) final {
        // get the number of polyphonic channels (defaults to 1 for monophonic).
        // also set the channels on the output ports based on the number of
        // channels
        unsigned channels = 1;
        for (unsigned port = 0; port < NUM_INPUTS; port++)
            channels = std::max(inputs[port].getChannels(), static_cast<int>(channels));
        // set the number of polyphony channels for output ports
        for (unsigned port = 0; port < NUM_OUTPUTS; port++)
            outputs[port].setChannels(channels);
        if (params[PARAM_BYPASS].getValue()) {  // bypass the chip emulator
            for (unsigned channel = 0; channel < channels; channel++) {
                for (unsigned i = 0; i < StereoSample::CHANNELS; i++)
                    bypassInput(args, channel, i);
            }
        } else {  // process audio samples on the chip engine.
            for (unsigned channel = 0; channel < channels; channel++)
                processChannel(args, channel);
        }
        // process the lights based on the VU meter readings
        if (lightDivider.process()) {
            setLight(inputVUMeter[0], &lights[VU_LIGHTS_INPUT]);
            setLight(inputVUMeter[1], &lights[VU_LIGHTS_INPUT + 3]);
            setLight(outputVUMeter[0], &lights[VU_LIGHTS_OUTPUT]);
            setLight(outputVUMeter[1], &lights[VU_LIGHTS_OUTPUT + 3]);
        }
    }
};

// ---------------------------------------------------------------------------
// MARK: Widget
// ---------------------------------------------------------------------------

/// The panel widget for SPC700.
struct ChipS_SMP_EchoWidget : ModuleWidget {
    /// @brief Initialize a new widget.
    ///
    /// @param module the back-end module to interact with
    ///
    explicit ChipS_SMP_EchoWidget(ChipS_SMP_Echo *module) {
        setModule(module);
        static constexpr auto panel = "res/S-SMP-Echo-Light.svg";
        setPanel(APP->window->loadSvg(asset::plugin(plugin_instance, panel)));
        // Panel Screws
        addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, 0)));
        addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
        addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
        addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
        // bypass switch
        addParam(createParam<CKSS>(Vec(10, 10), module, ChipS_SMP_Echo::PARAM_BYPASS));
        for (unsigned i = 0; i < StereoSample::CHANNELS; i++) {
            // Echo Parameter (0 = delay, 1 = Feedback)
            auto echoParam = createParam<Rogan2PBlue>(Vec(20 + 44 * i, 51), module, ChipS_SMP_Echo::PARAM_DELAY + i);
            echoParam->snap = true;
            addParam(echoParam);
            addInput(createInput<PJ301MPort>(Vec(25 + 44 * i, 100), module, ChipS_SMP_Echo::INPUT_DELAY + i));
            // Echo Mix
            auto echoIdx = ChipS_SMP_Echo::PARAM_MIX + i;
            auto echoPos = Vec(20 + 44 * i, 163);
            Knob* echoMix;
            if (i)  // i == 1 -> right lane -> red knob
                echoMix = createParam<Rogan2PRed>(echoPos, module, echoIdx);
            else  // i == 0 -> left lane -> white knob
                echoMix = createParam<Rogan2PWhite>(echoPos, module, echoIdx);
            echoMix->snap = true;
            addParam(echoMix);
            addInput(createInput<PJ301MPort>(Vec(25 + 44 * i, 212), module, ChipS_SMP_Echo::INPUT_MIX + i));
            // Stereo Input Ports
            addInput(createInput<PJ301MPort>(Vec(25 + 44 * i, 269), module, ChipS_SMP_Echo::INPUT_AUDIO + i));
            addChild(createLightCentered<MediumLight<RedGreenBlueLight>>(Vec(20 + 44 * i, 289), module, ChipS_SMP_Echo::VU_LIGHTS_INPUT + 3 * i));
            // Stereo Output Ports
            addOutput(createOutput<PJ301MPort>(Vec(25 + 44 * i, 324), module, ChipS_SMP_Echo::OUTPUT_AUDIO + i));
            addChild(createLightCentered<MediumLight<RedGreenBlueLight>>(Vec(20 + 44 * i, 344), module, ChipS_SMP_Echo::VU_LIGHTS_OUTPUT + 3 * i));
        }
        // FIR Coefficients
        for (unsigned i = 0; i < Sony_S_DSP_Echo::FIR_COEFFICIENT_COUNT; i++) {
            addInput(createInput<PJ301MPort>(Vec(120, 28 + i * 43), module, ChipS_SMP_Echo::INPUT_FIR_COEFFICIENT + i));
            auto param = createParam<Rogan1PGreen>(Vec(162, 25 + i * 43), module, ChipS_SMP_Echo::PARAM_FIR_COEFFICIENT + i);
            param->snap = true;
            addParam(param);
        }
    }
};

/// the global instance of the model
rack::Model *modelChipS_SMP_Echo = createModel<ChipS_SMP_Echo, ChipS_SMP_EchoWidget>("S_SMP_Echo");
