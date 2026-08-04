#include "InstrumentPlayer.h"
#include "../Config/PluginLogger.h"
#include "DrumKitEngine.h"
#include "HiHatTransitionPolicy.h"
#include <cmath>

void InstrumentPlayer::handleMidiEvent(const juce::MidiMessage& midiMessage) {
    if (midiMessage.isNoteOn()) {
        const int note = midiMessage.getNoteNumber();
        this->resetAftertouchChokeGesture(midiMessage.getChannel(), note);
        if (this->type == KitLib::InstrumentSpec::Type::HiHat) {
            const int notePedalOpenCloseValue = this->getHiHatNotePedalOpenCloseValue(note);
            if (notePedalOpenCloseValue >= 0) {
                const float closeCrossfadeSec = this->parent != nullptr ? this->parent->getEngineSettings().hihatCloseCrossfadeSec.load() : 0.020f;
                this->chokeHiHatVoicesByPoc(notePedalOpenCloseValue, closeCrossfadeSec);
            }
        }
        if (this->noteToArticulation[static_cast<size_t>(note)] == -1) return;
        this->playNote(midiMessage.getChannel(), note, midiMessage.getVelocity());
    }
    else if (midiMessage.isController()) {
        auto ccNum = static_cast<uint8_t>(midiMessage.getControllerNumber());
        if (!this->ccToArticulationMask[ccNum]) return;
        this->handleController(midiMessage.getChannel(), ccNum, midiMessage.getControllerValue());
    }
    else if (midiMessage.isAftertouch()) this->handleAftertouch(midiMessage.getChannel(), midiMessage.getNoteNumber(), midiMessage.getAfterTouchValue());

    else if (midiMessage.isAllNotesOff() || midiMessage.isAllSoundOff()) {
        this->resetAftertouchChokeGesture(midiMessage.getChannel(), -1);
        this->allNotesOff(midiMessage.getChannel(), true);
    }

    // else if (midiMessage.isNoteOff()) this->noteOff (channel, midiMessage.getNoteNumber(), midiMessage.getFloatVelocity(), true);
    // else if (midiMessage.isChannelPressure()) this->handleChannelPressure(channel, midiMessage.getChannelPressureValue());
    // else if (midiMessage.isProgramChange()) this->handleProgramChange(channel, midiMessage.getProgramChangeNumber());
    // else if (midiMessage.isPitchWheel()) {
    //     const int wheelPos = midiMessage.getPitchWheelValue();
    //     lastPitchWheelValues[channel - 1] = wheelPos;
    //     this->handlePitchWheel(channel, wheelPos);
    // }

}
