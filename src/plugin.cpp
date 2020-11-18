// The Potato Chips VCVRack plug-in.
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

/// the global instance of the plug-in
Plugin* plugin_instance;

/// Initialize an instance of the plug-in.
///
/// @param instance a fresh instance of the plug-in to initialize
///
void init(Plugin* instance) {
    plugin_instance = instance;
    // current releases
    instance->addModel(modelJairasullator);
    instance->addModel(modelInfiniteStairs);
    instance->addModel(modelPotKeys);
    instance->addModel(modelStepSaw);
    instance->addModel(modelPulses);
    instance->addModel(modelNameCorpOctalWaveGenerator);
    instance->addModel(modelPalletTownWavesSystem);
    instance->addModel(modelMegaTone);
    instance->addModel(modelBossFight);
    // blanks
    instance->addModel(modelChipS_SMP_Blank1);
    instance->addModel(modelBossFight_Blank1);
    // beta versions / WIPs
    instance->addModel(modelSuperEcho);
    instance->addModel(modelChipS_SMP);
    instance->addModel(modelChipS_SMP_ADSR);
    instance->addModel(modelChipS_SMP_BRR);
    instance->addModel(modelChipS_SMP_Gauss);
    instance->addModel(modelChipSCC);
    instance->addModel(modelChipTurboGrafx16);
}
