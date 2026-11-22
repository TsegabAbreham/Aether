#pragma once

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>
#include <glm/gtc/constants.hpp>

#include "HrirDatabase.h"
#include "../Spatialization/SpatialMath.h"

//==============================================================================
// Per-source HRIR renderer. One instance lives in every AudioObject.
//
//   stereo source -> mono -> air-absorption low-pass -> FIR convolution with the HRIR pair of the
//   nearest measured direction (crossfaded when that direction changes) -> distance gain * volume
//
// The measured filters already contain the interaural time and level differences and the pinna
// filtering, so none of the BinauralPanner stages (ITD delay, head-shadow filter, level pan) are
// used here. Distance gain and air absorption are applied on top, because the HRIRs were all
// measured at one fixed radius.
//
// Not usable until a prepared HrirDatabase with the same sample rate is attached (see isReady()).
// Release builds recommended: the FIR loop is 2 ears x N taps per sample.
//==============================================================================
class HrirPanner
{
public:
    // ---- message thread (Timeline calls these under its lock) ---------------------------------
    void setDatabase(const HrirDatabase* d, double sampleRate)
    {
        db = d;
        sr = sampleRate > 0.0 ? sampleRate : 48000.0;
        allocate();
    }

    void prepare(double sampleRate)
    {
        sr = sampleRate > 0.0 ? sampleRate : 48000.0;
        allocate();
    }

    bool                hasDatabase() const  { return db != nullptr; }
    const HrirDatabase* getDatabase() const  { return db; }
    bool                isReady() const      { return ready; }
    int                 getLastIndex() const { return curIdx; }   // debug read only (benign race)

    // ---- audio thread --------------------------------------------------------------------------
    void reset()
    {
        std::fill(work.begin(), work.end(), 0.0f);
        z           = 0.0f;
        curIdx      = -1;
        initialised = false;
    }

    // In-place. L and R hold the source's stereo signal (a mono source has L == R).
    void process(float* L, float* R, int n, const SpatialMath::SourceAngles& a, float volume)
    {
        if (! ready)
            return;

        while (n > 0)
        {
            const int m = std::min(n, maxBlock);
            processChunk(L, R, m, a, volume);
            L += m;
            R += m;
            n -= m;
        }
    }

private:
    static constexpr int maxBlock = 4096;     // must be >= the chunk size the Timeline passes in

    void allocate()
    {
        ready = false;

        if (db == nullptr || ! db->isPrepared())
            return;

        const int taps = db->getNumTaps();

        work.assign((size_t) (taps - 1 + maxBlock), 0.0f);
        yL.assign((size_t) maxBlock, 0.0f);
        yR.assign((size_t) maxBlock, 0.0f);
        oL.assign((size_t) maxBlock, 0.0f);
        oR.assign((size_t) maxBlock, 0.0f);

        ready = std::abs(db->getSampleRate() - sr) < 1.0;     // wrong rate -> not ready, Timeline falls back
        reset();
    }

    // out[i] = sum_k h[k] * x[i-k], using the time-reversed taps: sum_j hr[j] * w[i+j]
    void convolve(int measurement, int n, float* outL, float* outR) const
    {
        const int    taps = db->getNumTaps();
        const float* hl   = db->leftReversed(measurement);
        const float* hr   = db->rightReversed(measurement);

        for (int i = 0; i < n; ++i)
        {
            const float* x = work.data() + i;
            float aL = 0.0f, aR = 0.0f;

            for (int k = 0; k < taps; ++k)
            {
                aL += hl[k] * x[k];
                aR += hr[k] * x[k];
            }

            outL[i] = aL;
            outR[i] = aR;
        }
    }

    void processChunk(float* L, float* R, int n, const SpatialMath::SourceAngles& a, float volume)
    {
        const int taps = db->getNumTaps();

        //----------------------------------------------------------------------
        // 1. direction in the database's axes (SOFA: x forward, y left, z up)
        //    our angles: azimuth + = right, elevation + = up
        //----------------------------------------------------------------------
        const float ce = std::cos(a.elevation);
        const glm::vec3 dir(ce * std::cos(a.azimuth),
                            -db->azimuthSign * ce * std::sin(a.azimuth),
                            std::sin(a.elevation));

        // nearest measurement, with a little hysteresis so it doesn't flicker on a boundary
        int idx = curIdx;
        const int cand = db->findNearest(dir);

        if (idx < 0 || idx >= db->getNumMeasurements())
            idx = cand;
        else if (cand != idx && db->dotWith(cand, dir) > db->dotWith(idx, dir) + 1.0e-4f)
            idx = cand;

        //----------------------------------------------------------------------
        // 2. distance: gain + air absorption (the HRIRs themselves are distance-free)
        //----------------------------------------------------------------------
        const float dist   = std::max(a.distance, 0.3f);
        const float airFc  = std::min(0.45f * (float) sr, std::max(3000.0f, 20000.0f / (1.0f + 0.04f * dist)));
        const float tCoef  = 1.0f - std::exp(-glm::two_pi<float>() * airFc / (float) sr);
        const float tGain  = volume * SpatialMath::getAttenuation(dist);

        if (! initialised)
        {
            curCoef     = tCoef;
            curGain     = tGain;
            curIdx      = idx;
            initialised = true;
        }

        const float inv   = 1.0f / (float) n;
        const float dCoef = (tCoef - curCoef) * inv;
        const float dGain = (tGain - curGain) * inv;

        //----------------------------------------------------------------------
        // 3. mono + low-pass into the history buffer
        //----------------------------------------------------------------------
        float* w = work.data() + (taps - 1);
        float  c = curCoef;

        for (int i = 0; i < n; ++i)
        {
            const float mono = 0.5f * (L[i] + R[i]);
            z += c * (mono - z);
            w[i] = z;
            c += dCoef;
        }

        //----------------------------------------------------------------------
        // 4. convolve (crossfade old -> new filters if the direction changed)
        //----------------------------------------------------------------------
        convolve(idx, n, yL.data(), yR.data());

        if (idx != curIdx)
        {
            convolve(curIdx, n, oL.data(), oR.data());

            for (int i = 0; i < n; ++i)
            {
                const float t = (float) (i + 1) * inv;
                yL[(size_t) i] = oL[(size_t) i] + (yL[(size_t) i] - oL[(size_t) i]) * t;
                yR[(size_t) i] = oR[(size_t) i] + (yR[(size_t) i] - oR[(size_t) i]) * t;
            }
        }

        //----------------------------------------------------------------------
        // 5. output with ramped gain
        //----------------------------------------------------------------------
        float g = curGain;

        for (int i = 0; i < n; ++i)
        {
            L[i] = yL[(size_t) i] * g;
            R[i] = yR[(size_t) i] * g;
            g += dGain;
        }

        curCoef = tCoef;
        curGain = tGain;
        curIdx  = idx;

        // keep the last (taps - 1) input samples for the next chunk
        std::memmove(work.data(), work.data() + n, (size_t) (taps - 1) * sizeof(float));
    }

    const HrirDatabase* db = nullptr;
    double sr    = 48000.0;
    bool   ready = false;

    std::vector<float> work;                 // [taps-1 history | new samples]
    std::vector<float> yL, yR, oL, oR;       // per-chunk outputs (new / old filters)

    float curCoef = 1.0f, curGain = 1.0f, z = 0.0f;
    int   curIdx  = -1;
    bool  initialised = false;
};