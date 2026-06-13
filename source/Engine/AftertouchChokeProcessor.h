#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "EngineSettings.h"

/**
 *  realistic cymbal choke using Polyphonic Aftertouch: with mute timeout, if aftertouch value became less in that timeout, volume return back by some percent
 */
class AftertouchChokeProcessor {
public:
    const EngineSettings* engineSettings = nullptr;
    
    enum class State { Idle, Ducking, Releasing, Muted };

    void prepare(double sr) {
        this->sampleRate = sr;
        this->reset();
    }

    void reset() {
        this->state = State::Idle;
        this->currentGain = 1.0f;
        this->targetGain = 1.0f;
        this->maxPA = 0.0f;
        this->duckElapsed = 0.0f;
        this->releaseElapsed = 0.0f;
        this->muteElapsed = 0.0f;
    }

    void noteStarted() {
        this->reset();
    }

    void handlePolyAftertouch(int value) {
        float pa = juce::jlimit(0.0f, 1.0f, static_cast<float>(value) / 127.0f);
        pa = applyCurve(pa);
        this->currentPA = pa;

        if (pa > 0.0f) {
            this->maxPA = juce::jmax(this->maxPA, pa);

            this->targetGain = 1.0f - pa * this->engineSettings->chokeMaxDuckDepth.load();

            if (this->state != State::Muted) {
                this->state = State::Ducking;
                this->duckElapsed = 0.0f;
            }
        } else {
            if (this->state == State::Ducking) {
                this->state = State::Releasing;
                this->releaseElapsed = 0.0f;

                const float boosted = juce::jmin(1.0f, this->currentGain + this->engineSettings->chokeReleaseBoost.load());
                this->targetGain = boosted;
            }
        }
    }

    float getNextBlockGain() {
        const float dt = 1.0f / static_cast<float>(this->sampleRate);

        switch (this->state) {
            case State::Ducking: {
                const float duckTimeSec = this->engineSettings->chokeDuckTimeSec.load();
                this->duckElapsed += dt;
                this->moveTowards(this->targetGain, duckTimeSec);

                if (this->duckElapsed >= duckTimeSec) {
                    if (this->currentPA >= this->maxPA * this->engineSettings->chokeMuteThresholdRatio.load()) {
                        this->state = State::Muted;
                        this->muteElapsed = 0.0f;
                        // small tail instead of hard zero
                        this->targetGain = 0.0f;
                    } else {
                        this->state = State::Idle; //FIXME: why never reached?
                        jassertfalse;
                    }
                }
                break;
            }

            case State::Releasing: {
                const float releaseTimeSec = this->engineSettings->chokeReleaseTimeSec.load();
                this->releaseElapsed += dt;
                this->moveTowards(this->targetGain, releaseTimeSec);

                if (this->releaseElapsed >= releaseTimeSec) {
                    this->state = State::Idle;
                    this->maxPA = 0.0f;
                }
                break;
            }

            case State::Muted: {
                this->muteElapsed += dt;
                // smooth short tail
                this->moveTowards(0.0f, this->engineSettings->chokeMuteTailSec.load());
                break;
            }

            case State::Idle:
            default:
                break;
        }

        return this->currentGain;
    }

    bool isMuted() const {
        return (this->state == State::Muted && this->currentGain < 0.0005f);
    }

private:
    State state = State::Idle;

    double sampleRate = 44100.0;

    float currentGain = 1.0f;
    float targetGain = 1.0f;

    float currentPA = 0.0f;
    float maxPA = 0.0f;

    float duckElapsed = 0.0f;
    float releaseElapsed = 0.0f;
    float muteElapsed = 0.0f;

    float applyCurve(float x) const {
        return x * std::sqrt(x);
    }

    void moveTowards(float newTarget, float timeSec) {
        if (timeSec <= 0.0f) {
            this->currentGain = newTarget; //FIXME: why never reached?
            jassertfalse;
            return;
        }
        const float step = (newTarget - this->currentGain) / (timeSec * static_cast<float>(this->sampleRate));
        this->currentGain += step;
    }
};
