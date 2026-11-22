#pragma once

#include <vector>
#include <cmath>
#include <algorithm>

// Ring buffer with fractional (linearly interpolated) read.
// push() writes the newest sample, read(d) returns the sample d samples ago (d may be fractional).
class DelayLine
{
public:
    DelayLine() { prepare(256); }

    // allocates -> call from the message thread only
    void prepare(int maxDelaySamples)
    {
        int n = 4;
        while (n < maxDelaySamples + 4)
            n <<= 1;

        buffer.assign((size_t) n, 0.0f);
        mask = n - 1;
        pos  = 0;
    }

    void reset()
    {
        std::fill(buffer.begin(), buffer.end(), 0.0f);
        pos = 0;
    }

    inline void push(float x)
    {
        pos = (pos + 1) & mask;
        buffer[(size_t) pos] = x;
    }

    inline float read(float delaySamples) const
    {
        delaySamples = std::min(std::max(delaySamples, 0.0f), (float) (mask - 2));

        const float rp = (float) pos - delaySamples;
        const float fl = std::floor(rp);
        const int   i0 = (int) fl;
        const float fr = rp - fl;

        const float a = buffer[(size_t) (i0 & mask)];         // older
        const float b = buffer[(size_t) ((i0 + 1) & mask)];   // newer

        return a + (b - a) * fr;
    }

private:
    std::vector<float> buffer;
    int mask = 0;
    int pos  = 0;
};