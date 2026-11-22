#pragma once

#include <cmath>
#include <algorithm>
#include <glm/gtc/constants.hpp>

#include "DelayLine.h"
#include "SpatialMath.h"

//==============================================================================
// Per-source binaural model for a fixed head. One instance lives in every AudioObject.
//
//   mono source -> [ITD delay per ear] -> [head-shadow / air-absorption low-pass per ear]
//               -> [level pan * distance attenuation * volume] -> L / R
//
// Every parameter is ramped linearly across each processed chunk, so moving a source
// (gizmo drag or animation) doesn't click.
//==============================================================================
class BinauralPanner
{
public:
    BinauralPanner() { prepare(44100.0); }

    // message thread only (allocates)
    void prepare(double sampleRate)
    {
        sr = sampleRate > 0.0 ? sampleRate : 44100.0;
        delay.prepare((int) std::ceil(maxItdSeconds * sr) + 8);
        reset();
    }

    // call on seek / clip start (audio thread safe: no allocation)
    void reset()
    {
        delay.reset();
        zL = zR = 0.0f;
        initialised = false;
    }

    // In-place. L and R hold the source's stereo signal (a mono source has L == R).
    // The source is summed to mono, then spatialised.
    void process(float* L, float* R, int n, const SpatialMath::SourceAngles& a, float volume)
    {
        if (n <= 0)
            return;

        //----------------------------------------------------------------------
        // 1. targets from the angles
        //----------------------------------------------------------------------
        const float lat     = a.lateral;                     // -pi/2 (left) .. +pi/2 (right)
        const float s       = std::sin(lat);                 // -1 .. 1
        const float absLat  = std::abs(lat);
        const float absS    = std::abs(s);

        // Woodworth ITD, applied to the ear FAR from the source
        const float itdSamples = (float) ((headRadius / speedOfSound)
                                          * (absLat + std::sin(absLat)) * sr);

        const float tDelayL = lat > 0.0f ? itdSamples : 0.0f;   // source on the right -> left ear is far
        const float tDelayR = lat < 0.0f ? itdSamples : 0.0f;

        // head shadow: far ear loses highs; air absorption: everything gets duller with distance
        const float shadowL = lat > 0.0f ? absS : 0.0f;
        const float shadowR = lat < 0.0f ? absS : 0.0f;

        const float airFc   = std::max(3000.0f, 20000.0f / (1.0f + 0.04f * a.distance));
        const float nyqCap  = 0.45f * (float) sr;

        const float fcL = std::min(airFc * std::pow(farEarFloorRatio, shadowL), nyqCap);
        const float fcR = std::min(airFc * std::pow(farEarFloorRatio, shadowR), nyqCap);

        const float tCoefL = 1.0f - std::exp(-glm::two_pi<float>() * fcL / (float) sr);
        const float tCoefR = 1.0f - std::exp(-glm::two_pi<float>() * fcR / (float) sr);

        // level: mild equal-power pan (the filter + delay do most of the work) * distance * volume
        const float pan   = levelPanAmount * s;
        const float theta = (pan + 1.0f) * glm::pi<float>() / 4.0f;
        const float att   = SpatialMath::getAttenuation(a.distance);

        const float tGainL = std::cos(theta) * att * volume;
        const float tGainR = std::sin(theta) * att * volume;

        if (! initialised)                                   // first block after reset: snap, don't ramp
        {
            curDelayL = tDelayL;  curDelayR = tDelayR;
            curCoefL  = tCoefL;   curCoefR  = tCoefR;
            curGainL  = tGainL;   curGainR  = tGainR;
            initialised = true;
        }

        //----------------------------------------------------------------------
        // 2. per-sample ramps
        //----------------------------------------------------------------------
        const float inv = 1.0f / (float) n;

        const float dDelayL = (tDelayL - curDelayL) * inv;
        const float dDelayR = (tDelayR - curDelayR) * inv;
        const float dCoefL  = (tCoefL  - curCoefL)  * inv;
        const float dCoefR  = (tCoefR  - curCoefR)  * inv;
        const float dGainL  = (tGainL  - curGainL)  * inv;
        const float dGainR  = (tGainR  - curGainR)  * inv;

        for (int i = 0; i < n; ++i)
        {
            const float mono = 0.5f * (L[i] + R[i]);
            delay.push(mono);

            // ITD
            float xL = delay.read(curDelayL);
            float xR = delay.read(curDelayR);

            // one-pole low-pass (head shadow + air)
            zL += curCoefL * (xL - zL);
            zR += curCoefR * (xR - zR);

            L[i] = zL * curGainL;
            R[i] = zR * curGainR;

            curDelayL += dDelayL;  curDelayR += dDelayR;
            curCoefL  += dCoefL;   curCoefR  += dCoefR;
            curGainL  += dGainL;   curGainR  += dGainR;
        }

        // land exactly on the target (avoids float drift)
        curDelayL = tDelayL;  curDelayR = tDelayR;
        curCoefL  = tCoefL;   curCoefR  = tCoefR;
        curGainL  = tGainL;   curGainR  = tGainR;
    }

    //--------------------------------------------------------------------------
    // Tweakable constants - these are the knobs to tune by ear
    //--------------------------------------------------------------------------
    float levelPanAmount    = 0.6f;     // 0 = no level difference, 1 = full equal-power pan
    float farEarFloorRatio  = 0.075f;   // far ear cutoff at 90 deg = 20 kHz * this = 1.5 kHz
    float headRadius        = 0.0875f;  // metres
    float speedOfSound      = 343.0f;   // m/s

private:
    static constexpr double maxItdSeconds = 0.0008;   // ~0.66 ms max for an 8.75 cm head

    double    sr = 44100.0;
    DelayLine delay;

    float zL = 0.0f, zR = 0.0f;
    float curDelayL = 0.0f, curDelayR = 0.0f;
    float curCoefL  = 1.0f, curCoefR  = 1.0f;
    float curGainL  = 1.0f, curGainR  = 1.0f;
    bool  initialised = false;
};