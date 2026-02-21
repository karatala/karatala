#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

class AftertouchChokeProcessor {
public:
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

            this->targetGain = 1.0f - pa * this->maxDuckDepth;

            if (this->state != State::Muted) {
                this->state = State::Ducking;
                this->duckElapsed = 0.0f;
            }
        } else {
            if (this->state == State::Ducking) {
                this->state = State::Releasing;
                this->releaseElapsed = 0.0f;

                float boosted = juce::jmin(1.0f, this->currentGain + releaseBoost);
                this->targetGain = boosted;
            }
        }
    }

    float getNextBlockGain() {
        const float dt = 1.0f / static_cast<float>(this->sampleRate);

        switch (this->state) {
            case State::Ducking: {
                this->duckElapsed += dt;
                this->moveTowards(this->targetGain, duckTimeSec);

                if (this->duckElapsed >= duckTimeSec) {
                    if (this->currentPA >= this->maxPA * this->muteThresholdRatio) {
                        this->state = State::Muted;
                        this->muteElapsed = 0.0f;
                        // small tail instead of hard zero
                        this->targetGain = 0.0f;
                    } else {
                        this->state = State::Idle; //FIXME: why never reached?
                    }
                }
                break;
            }

            case State::Releasing: {
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
                this->moveTowards(0.0f, muteTailSec);
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
    enum class State { Idle, Ducking, Releasing, Muted };

    float applyCurve(float x) const {
        return x * std::sqrt(x);
    }

    void moveTowards(float newTarget, float timeSec) {
        if (timeSec <= 0.0f) {
            this->currentGain = newTarget; //FIXME: why never reached?
            return;
        }
        const float step = (newTarget - this->currentGain) / (timeSec * static_cast<float>(this->sampleRate));
        this->currentGain += step;
    }

    State state = State::Idle;

    double sampleRate = 44100.0;

    float currentGain = 1.0f;
    float targetGain = 1.0f;

    float currentPA = 0.0f;
    float maxPA = 0.0f;

    float duckElapsed = 0.0f;
    float releaseElapsed = 0.0f;
    float muteElapsed = 0.0f;

    static constexpr float maxDuckDepth = 0.7f;          // 70%
    static constexpr float duckTimeSec = 0.2f;           // 200 ms
    static constexpr float releaseTimeSec = 0.05f;       // 50 ms
    static constexpr float muteTailSec = 0.04f;          // 40 ms
    static constexpr float releaseBoost = 0.1f;          // +10%
    static constexpr float muteThresholdRatio = 0.5f;    // 50% of maxPA
};
