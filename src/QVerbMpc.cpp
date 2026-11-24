// QVerb for Akai MPC standalone (VST2 / AArch64 + ARMv7)
//
// A compact vocal reverb: Dattorro/MVerb core, pre-delay, tone, width and a
// musical Auto Clear ducker with an optional hard tank clear at 100%.
//
// MVerb is Copyright (c) 2010 Martin Eastwood and is distributed under the
// GNU General Public License v3.0 or later. See MVERB-LICENSE.txt.
// The MPC glue is based on the QDelay MPC framework (MIT SDK).
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "mpc_plugin.h"
#include "dsp/biquad.h"
#include "dsp/delay_line.h"
#include "dsp/math.h"
#include "dsp/onepole.h"
#include "dsp/smoother.h"
#include "MVerb.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <utility>
#include <vector>

using namespace mpc::dsp;
using mpc::boolParam;
using mpc::choiceParam; // unused, kept for the framework header symmetry
using mpc::floatParam;
using mpc::logParam;

namespace
{

// ---------------------------------------------------------------------------
// VocalClear - keyed reverb ducker / hard clear
// ---------------------------------------------------------------------------
class VocalClear
{
public:
    void prepare (double sampleRate)
    {
        srate = (float) std::max (8000.0, sampleRate);
        detectorAttack  = 1.0f - std::exp (-1.0f / (0.005f * srate));
        detectorRelease = 1.0f - std::exp (-1.0f / (0.300f * srate));
        attack  = 1.0f - std::exp (-1.0f / (0.010f * srate));
        release = 1.0f - std::exp (-1.0f / (0.400f * srate));
        reset();
    }

    void reset()
    {
        env = 0.0f;
        gain = 1.0f;
        resetDone = false;
    }

    void set (bool enabled_, float amount_)
    {
        enabled = enabled_;
        amount = clamp (amount_, 0.0f, 1.0f);
        if (! enabled || amount <= 0.0001f)
        {
            gain = 1.0f;
            resetDone = false;
        }
    }

    // Soft-knee vocal ducker. Unlike the earlier binary gate this has no
    // active/hold toggle: the duck depth is a smooth function of the vocal
    // envelope, so the end of a phrase releases monotonically instead of
    // fluttering between duck and open.
    float process (float left, float right, bool& resetTank)
    {
        resetTank = false;

        if (! enabled || amount <= 0.0001f)
        {
            env = 0.0f;
            gain = 1.0f;
            resetDone = false;
            return 1.0f;
        }

        const float peak = std::max (std::abs (left), std::abs (right));
        env += (peak > env ? detectorAttack : detectorRelease) * (peak - env);

        // -52 dB .. -38 dB soft knee; zero slope at both ends.
        constexpr float low = 0.0025f;
        constexpr float high = 0.0125f;
        float x = clamp ((env - low) / (high - low), 0.0f, 1.0f);
        x = x * x * (3.0f - 2.0f * x);

        const float target = 1.0f - amount * x;
        gain += (target < gain ? attack : release) * (target - gain);

        // At 100% the tank is wiped once, only when the duck is effectively
        // silent. The re-arm waits for a real quiet gap, never for the end of
        // the release wobble.
        if (amount > 0.999f)
        {
            if (! resetDone && gain < 0.002f && x > 0.80f)
            {
                resetTank = true;
                resetDone = true;
            }
            if (env < low * 0.5f && gain > 0.99f)
                resetDone = false;
        }

        return gain;
    }

private:
    float srate = 44100.0f;
    bool enabled = false;
    float amount = 0.0f;
    float env = 0.0f;
    float gain = 1.0f;
    float attack = 0.01f, release = 0.001f;
    float detectorAttack = 0.01f, detectorRelease = 0.001f;
    bool resetDone = false;
};

inline float softLimit (float x)
{
    constexpr float knee = 0.9f;
    const float a = std::fabs (x);
    if (a <= knee)
        return x;
    const float y = knee + (1.0f - knee) * fastTanh ((a - knee) / (1.0f - knee));
    return x < 0.0f ? -y : y;
}

} // namespace

// ===========================================================================
// The plugin
// ===========================================================================
class QVerbMpc : public mpc::Plugin
{
public:
    enum P
    {
        pMix, pDecay, pTone, pPreDelay, pWidth, pClear, pAutoClear,
        kNumParams
    };

    static mpc::PluginInfo makeInfo()
    {
        mpc::PluginInfo i;
        i.name = "QVerb";
        i.vendor = "Tilr";
        i.uniqueId = MPC_FOURCC ('q', 'v', 'r', 'b');
        i.version = 1000;
        i.isInstrument = false;
        i.numInputs = 2;
        i.numOutputs = 2;
        i.wantsMidi = false; // MPC effects receive no MIDI
        return i;
    }

    QVerbMpc() : mpc::Plugin (makeInfo())
    {
        addParam (floatParam ("mix", "Mix", 0.0f, 100.0f, 32.0f, "%", 0));
        addParam (logParam ("decay", "Decay", 0.3f, 12.0f, 2.8f, "s", 2));
        addParam (floatParam ("tone", "Tone", 0.0f, 100.0f, 65.0f, "%", 0));
        addParam (floatParam ("predelay", "Pre-Delay", 0.0f, 120.0f, 25.0f, "ms", 0, 0.6f));
        addParam (floatParam ("width", "Width", 0.0f, 200.0f, 140.0f, "%", 0));
        addParam (floatParam ("clear", "Clear", 0.0f, 100.0f, 55.0f, "%", 0));
        addParam (boolParam ("autoclear", "Auto Clear", true));

        addPrograms();
    }

    void prepare (double sampleRate, int maxBlockSize) override
    {
        srate = (float) sampleRate;
        maxBlock = std::max (1, maxBlockSize);
        preBufL.assign ((size_t) maxBlock, 0.0f);
        preBufR.assign ((size_t) maxBlock, 0.0f);
        wetBufL.assign ((size_t) maxBlock, 0.0f);
        wetBufR.assign ((size_t) maxBlock, 0.0f);
        clearGain.assign ((size_t) maxBlock, 1.0f);

        const int maxPre = (int) (0.2 * sampleRate) + 8;
        preL.prepare (maxPre);
        preR.prepare (maxPre);
        preSamples.setTime (30.0f, sampleRate);
        widthSmooth.setTime (25.0f, sampleRate);
        mixSmooth.setTime (25.0f, sampleRate);

        const auto hp = BiquadCoeffs::highpass (sampleRate, 80.0f, 0.707f);
        hpL.setCoeffs (hp);
        hpR.setCoeffs (hp);
        dcL.prepare (sampleRate, 5.0f);
        dcR.prepare (sampleRate, 5.0f);

        mverb.setSampleRate (sampleRate);
        mverb.setParameter (MVerb<float>::SIZE, 0.85f);
        mverb.setParameter (MVerb<float>::PREDELAY, 0.0f);
        mverb.setParameter (MVerb<float>::MIX, 1.0f);
        mverb.setParameter (MVerb<float>::GAIN, 1.0f);
        mverb.setParameter (MVerb<float>::EARLYMIX, 0.68f);
        mverb.setParameter (MVerb<float>::DENSITY, 0.70f);
        mverb.setParameter (MVerb<float>::DECAY, 0.40f);
        mverb.setParameter (MVerb<float>::DAMPINGFREQ, 0.35f);
        mverb.setParameter (MVerb<float>::BANDWIDTHFREQ, 0.75f);
        mverb.reset();

        vocalClear.prepare (sampleRate);
        reset();
    }

    void reset() override
    {
        preL.reset();
        preR.reset();
        hpL.reset();
        hpR.reset();
        dcL.reset();
        dcR.reset();
        mverb.reset();
        vocalClear.reset();

        preSamples.snap (msToSamples (get (pPreDelay), srate));
        widthSmooth.snap (clamp (get (pWidth) * 0.01f, 0.0f, 2.0f));
        mixSmooth.snap (clamp (get (pMix) * 0.01f, 0.0f, 1.0f));
    }

    void process (mpc::ProcessContext& ctx) override
    {
        int n = ctx.numFrames;
        if (n <= 0)
            return;
        n = std::min (n, maxBlock);

        const float* inL = ctx.numInputs > 0 ? ctx.inputs[0] : nullptr;
        const float* inR = ctx.numInputs > 1 ? ctx.inputs[1] : inL;
        float* outL = ctx.numOutputs > 0 ? ctx.outputs[0] : nullptr;
        float* outR = ctx.numOutputs > 1 ? ctx.outputs[1] : outL;
        if (! outL)
            return;

        const float sr = srate;

        const float mixTarget = clamp (get (pMix) * 0.01f, 0.0f, 1.0f);
        const float widthTarget = clamp (get (pWidth) * 0.01f, 0.0f, 2.0f);
        mixSmooth.setTarget (mixTarget);
        widthSmooth.setTarget (widthTarget);
        preSamples.setTarget (msToSamples (get (pPreDelay), sr));

        const float decaySeconds = get (pDecay);
        const float decayNorm = clamp ((decaySeconds - 0.3f) / (12.0f - 0.3f), 0.0f, 1.0f);
        const float tone = clamp (get (pTone) * 0.01f, 0.0f, 1.0f);
        mverb.setParameter (MVerb<float>::DECAY, 0.25f + 0.70f * decayNorm);
        mverb.setParameter (MVerb<float>::DAMPINGFREQ, 1.0f - tone);
        mverb.setParameter (MVerb<float>::BANDWIDTHFREQ, 0.30f + 0.70f * tone);

        vocalClear.set (getBool (pAutoClear),
                        clamp (get (pClear) * 0.01f, 0.0f, 1.0f));

        bool resetTank = false;

        // Pre-delay + input high-pass, and the vocal-clear detector, all on
        // the original dry signal so the dry vocal is never filtered.
        for (int i = 0; i < n; ++i)
        {
            const float dryL = inL ? inL[i] : 0.0f;
            const float dryR = inR ? inR[i] : 0.0f;

            const float hpOutL = hpL.process (dryL);
            const float hpOutR = hpR.process (dryR);

            preL.write (hpOutL);
            preR.write (hpOutR);
            const float preDelay = clamp (preSamples.process(), 1.0f, (float) preL.maxDelay());
            preBufL[(size_t) i] = preL.readLinear (preDelay);
            preBufR[(size_t) i] = preR.readLinear (preDelay);

            bool r = false;
            clearGain[(size_t) i] = vocalClear.process (dryL, dryR, r);
            if (r)
                resetTank = true;
        }

        float* inPtrs[2] = { preBufL.data(), preBufR.data() };
        float* outPtrs[2] = { wetBufL.data(), wetBufR.data() };
        mverb.process (inPtrs, outPtrs, n);

        for (int i = 0; i < n; ++i)
        {
            const float dryL = inL ? inL[i] : 0.0f;
            const float dryR = inR ? inR[i] : 0.0f;

            float wetL = dcL.process (wetBufL[(size_t) i]) * 1.2f;
            float wetR = dcR.process (wetBufR[(size_t) i]) * 1.2f;

            const float w = widthSmooth.process();
            if (std::fabs (w - 1.0f) > 0.0001f)
            {
                const float mid = 0.5f * (wetL + wetR);
                const float side = 0.5f * (wetL - wetR) * w;
                wetL = mid + side;
                wetR = mid - side;
            }

            const float cg = clearGain[(size_t) i];
            wetL *= cg;
            wetR *= cg;

            const float m = mixSmooth.process();
            const float dryGain = std::cos (m * kHalfPi);
            const float wetGain = std::sin (m * kHalfPi);

            float yL = dryL * dryGain + wetL * wetGain;
            float yR = dryR * dryGain + wetR * wetGain;
            outL[i] = softLimit (flushDenormal (yL));
            if (outR && outR != outL)
                outR[i] = softLimit (flushDenormal (yR));
        }

        if (resetTank)
        {
            mverb.reset();
            preL.reset();
            preR.reset();
        }
    }

private:
    // -----------------------------------------------------------------------
    // Factory programs
    // -----------------------------------------------------------------------
    std::vector<float> defaultValues() const
    {
        std::vector<float> v ((size_t) kNumParams);
        for (int i = 0; i < kNumParams; ++i)
            v[(size_t) i] = param (i).def;
        return v;
    }

    void program (const char* name, std::initializer_list<std::pair<int, float>> overrides)
    {
        auto v = defaultValues();
        for (const auto& o : overrides)
            v[(size_t) o.first] = o.second;
        addProgram (name, std::move (v));
    }

    void addPrograms()
    {
        // Program 0 is the default the MPC selects after loading.
        program ("Vocal Air", {});
        program ("Close Vocal", {
            { pMix, 26.0f }, { pDecay, 1.4f }, { pTone, 55.0f },
            { pPreDelay, 10.0f }, { pWidth, 110.0f }, { pClear, 65.0f },
        });
        program ("Hazy Pad", {
            { pMix, 40.0f }, { pDecay, 4.5f }, { pTone, 40.0f },
            { pPreDelay, 35.0f }, { pWidth, 170.0f }, { pClear, 0.0f },
            { pAutoClear, 0.0f },
        });
        program ("Dub Vocal", {
            { pMix, 38.0f }, { pDecay, 3.2f }, { pTone, 50.0f },
            { pPreDelay, 30.0f }, { pWidth, 130.0f }, { pClear, 80.0f },
        });
        program ("Wide Hall", {
            { pMix, 35.0f }, { pDecay, 6.0f }, { pTone, 60.0f },
            { pPreDelay, 50.0f }, { pWidth, 180.0f }, { pClear, 20.0f },
        });
        program ("Room", {
            { pMix, 28.0f }, { pDecay, 1.0f }, { pTone, 55.0f },
            { pPreDelay, 5.0f }, { pWidth, 100.0f }, { pClear, 50.0f },
        });
        program ("Init", {
            { pMix, 30.0f }, { pDecay, 2.0f }, { pTone, 50.0f },
            { pPreDelay, 20.0f }, { pWidth, 120.0f }, { pClear, 0.0f },
            { pAutoClear, 0.0f },
        });
    }

    MVerb<float> mverb;

    std::vector<float> preBufL, preBufR, wetBufL, wetBufR, clearGain;
    DelayLine preL, preR;
    Biquad hpL, hpR;
    DCBlocker dcL, dcR;
    Smoother preSamples, widthSmooth, mixSmooth;
    VocalClear vocalClear;

    float srate = 44100.0f;
    int maxBlock = 1;
};

MPC_PLUGIN_ENTRY (QVerbMpc)
