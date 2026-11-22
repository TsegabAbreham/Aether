#pragma once

#include <JuceHeader.h>
#include <glm/glm.hpp>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

//==============================================================================
// A set of measured head-related impulse responses (one left/right FIR pair per direction).
//
// File format (written by the Colab converter, little endian):
//     "HRIR"  | int32 M | int32 N | float32 sampleRate
//     float32 position[M][3]     azimuth deg, elevation deg, radius m   (SOFA convention:
//                                azimuth 0 = front, +90 = LEFT; elevation +90 = up)
//     float32 ir[M][2][N]        measurement, ear (0 = left, 1 = right), tap
//
// Usage (message thread):   load()  ->  prepare(deviceSampleRate)
// After prepare() the object is read-only, so the audio thread can share it safely.
//
// prepare() resamples the filters to the device rate, scales them so an average filter has unit
// energy, and stores every filter time-REVERSED so the audio thread can convolve with a plain
// forward dot product.
//==============================================================================
class HrirDatabase
{
public:
    // set to -1 if the left/right check shows the file's azimuth runs the other way
    float azimuthSign = 1.0f;

    bool load(const juce::File& file, juce::String& error)
    {
        juce::FileInputStream in(file);

        if (! in.openedOk())
        {
            error = "cannot open " + file.getFullPathName();
            return false;
        }

        char magic[4] = {};

        if (in.read(magic, 4) != 4 || std::memcmp(magic, "HRIR", 4) != 0)
        {
            error = "not an .hrir file (bad magic bytes)";
            return false;
        }

        const int   m  = in.readInt();
        const int   n  = in.readInt();
        const float fs = in.readFloat();

        if (m < 1 || m > 200000 || n < 8 || n > 16384 || fs < 8000.0f || fs > 400000.0f)
        {
            error = "bad header (M=" + juce::String(m) + ", N=" + juce::String(n)
                    + ", fs=" + juce::String(fs) + ")";
            return false;
        }

        std::vector<float> pos((size_t) m * 3);
        std::vector<float> data((size_t) m * 2 * (size_t) n);

        const juce::int64 posBytes  = (juce::int64) pos.size()  * (juce::int64) sizeof(float);
        const juce::int64 dataBytes = (juce::int64) data.size() * (juce::int64) sizeof(float);

        if (dataBytes > 0x7fffffff)
        {
            error = "file too large";
            return false;
        }

        if (in.read(pos.data(), (int) posBytes) != (int) posBytes
            || in.read(data.data(), (int) dataBytes) != (int) dataBytes)
        {
            error = "file truncated";
            return false;
        }

        numMeasurements = m;
        numTaps         = n;
        sampleRate      = (double) fs;
        irs             = std::move(data);
        prepared        = false;

        azimuth.resize((size_t) m);
        elevation.resize((size_t) m);
        dirs.resize((size_t) m);

        for (int i = 0; i < m; ++i)
        {
            const float azDeg = pos[(size_t) i * 3 + 0];
            const float elDeg = pos[(size_t) i * 3 + 1];

            azimuth[(size_t) i]   = azDeg;
            elevation[(size_t) i] = elDeg;

            const float a = juce::degreesToRadians(azDeg);
            const float e = juce::degreesToRadians(elDeg);

            // SOFA axes: x forward, y left, z up
            dirs[(size_t) i] = glm::vec3(std::cos(e) * std::cos(a),
                                         std::cos(e) * std::sin(a),
                                         std::sin(e));
        }

        return true;
    }

    // Resample to the device rate, normalise, reverse the taps. Call once, before sharing.
    void prepare(double targetSampleRate)
    {
        if (prepared || numMeasurements == 0)
            return;

        if (std::abs(targetSampleRate - sampleRate) >= 1.0)
            resampleTo(targetSampleRate);

        normaliseAndReverse();
        prepared = true;
    }

    bool   isPrepared() const              { return prepared; }
    double getSampleRate() const           { return sampleRate; }
    int    getNumTaps() const              { return numTaps; }
    int    getNumMeasurements() const      { return numMeasurements; }

    float  azimuthDeg(int i) const         { return azimuth[(size_t) i]; }
    float  elevationDeg(int i) const       { return elevation[(size_t) i]; }

    // time-reversed taps (see class comment)
    const float* leftReversed(int i) const  { return &irs[((size_t) i * 2 + 0) * (size_t) numTaps]; }
    const float* rightReversed(int i) const { return &irs[((size_t) i * 2 + 1) * (size_t) numTaps]; }

    // dir must be a unit vector in SOFA axes (x forward, y left, z up)
    int findNearest(const glm::vec3& dir) const
    {
        int   best  = -1;
        float bestD = -2.0f;

        for (int i = 0; i < numMeasurements; ++i)
        {
            const glm::vec3& v = dirs[(size_t) i];
            const float d = v.x * dir.x + v.y * dir.y + v.z * dir.z;

            if (d > bestD)
            {
                bestD = d;
                best  = i;
            }
        }

        return best;
    }

    float dotWith(int i, const glm::vec3& dir) const
    {
        const glm::vec3& v = dirs[(size_t) i];
        return v.x * dir.x + v.y * dir.y + v.z * dir.z;
    }

private:
    // Windowed-sinc resampler (Hann window, 8 lobes). Low-passes when going down in rate.
    // Output taps are scaled by 1/ratio so the filter's DC gain stays the same.
    void resampleTo(double targetSR)
    {
        const double pi     = juce::MathConstants<double>::pi;
        const double ratio  = targetSR / sampleRate;                       // output samples per input sample
        const int    newN   = std::max(16, (int) std::ceil((double) numTaps * ratio));
        const double cutoff = std::min(1.0, ratio);                        // relative to the input Nyquist
        const int    half   = (int) std::ceil(8.0 / cutoff);
        const int    width  = 2 * half;

        // the kernel only depends on the output index, so compute it once for all filters
        std::vector<float> kern((size_t) newN * (size_t) width);
        std::vector<int>   first((size_t) newN);

        for (int j = 0; j < newN; ++j)
        {
            const double x  = (double) j / ratio;
            const int    k0 = (int) std::floor(x) - half + 1;
            first[(size_t) j] = k0;

            for (int t = 0; t < width; ++t)
            {
                const double d = x - (double) (k0 + t);
                const double s = std::abs(d) < 1.0e-9 ? cutoff : std::sin(pi * cutoff * d) / (pi * d);
                const double w = std::abs(d) >= (double) half ? 0.0 : 0.5 + 0.5 * std::cos(pi * d / (double) half);

                kern[(size_t) j * (size_t) width + (size_t) t] = (float) (s * w / ratio);
            }
        }

        const size_t numFilters = (size_t) numMeasurements * 2;
        std::vector<float> out(numFilters * (size_t) newN);

        for (size_t f = 0; f < numFilters; ++f)
        {
            const float* src = &irs[f * (size_t) numTaps];
            float*       dst = &out[f * (size_t) newN];

            for (int j = 0; j < newN; ++j)
            {
                const int    k0     = first[(size_t) j];
                const float* kw     = &kern[(size_t) j * (size_t) width];
                const int    tStart = std::max(0, -k0);
                const int    tEnd   = std::min(width, numTaps - k0);

                float acc = 0.0f;

                for (int t = tStart; t < tEnd; ++t)
                    acc += src[k0 + t] * kw[t];

                dst[j] = acc;
            }
        }

        irs        = std::move(out);
        numTaps    = newN;
        sampleRate = targetSR;
    }

    void normaliseAndReverse()
    {
        // scale so the average filter has unit energy (a white-noise input keeps its loudness)
        double energy = 0.0;

        for (float v : irs)
            energy += (double) v * (double) v;

        energy /= (double) numMeasurements * 2.0;

        const float g = energy > 1.0e-12 ? (float) (1.0 / std::sqrt(energy)) : 1.0f;

        for (float& v : irs)
            v *= g;

        const size_t numFilters = (size_t) numMeasurements * 2;

        for (size_t f = 0; f < numFilters; ++f)
            std::reverse(irs.begin() + (std::ptrdiff_t) (f * (size_t) numTaps),
                         irs.begin() + (std::ptrdiff_t) ((f + 1) * (size_t) numTaps));
    }

    int    numMeasurements = 0;
    int    numTaps         = 0;
    double sampleRate      = 48000.0;
    bool   prepared        = false;

    std::vector<float>     irs;          // [measurement][ear][tap]
    std::vector<float>     azimuth, elevation;
    std::vector<glm::vec3> dirs;         // unit vectors, SOFA axes
};