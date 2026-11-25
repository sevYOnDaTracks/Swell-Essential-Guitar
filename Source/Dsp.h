#pragma once
#include <juce_core/juce_core.h>
#include <array>
#include <cmath>
#include <vector>

// Small, allocation-free (after prepare) effects used by the processor.
namespace essential::dsp {

// RBJ biquad, transposed direct form II.
struct Biquad {
    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    void reset() { z1 = z2 = 0; }
    inline float process(float x)
    {
        float y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }
    void set(double B0, double B1, double B2, double A0, double A1, double A2)
    {
        b0 = (float) (B0 / A0); b1 = (float) (B1 / A0); b2 = (float) (B2 / A0);
        a1 = (float) (A1 / A0); a2 = (float) (A2 / A0);
    }
    void highpass(double sr, double f, double q = 0.7071)
    {
        double w = juce::MathConstants<double>::twoPi * f / sr, c = std::cos(w), a = std::sin(w) / (2 * q);
        set((1 + c) / 2, -(1 + c), (1 + c) / 2, 1 + a, -2 * c, 1 - a);
    }
    void lowpass(double sr, double f, double q = 0.7071)
    {
        double w = juce::MathConstants<double>::twoPi * f / sr, c = std::cos(w), a = std::sin(w) / (2 * q);
        set((1 - c) / 2, 1 - c, (1 - c) / 2, 1 + a, -2 * c, 1 - a);
    }
    void peak(double sr, double f, double q, double gainDb)
    {
        double A = std::pow(10.0, gainDb / 40), w = juce::MathConstants<double>::twoPi * f / sr;
        double c = std::cos(w), a = std::sin(w) / (2 * q);
        set(1 + a * A, -2 * c, 1 - a * A, 1 + a / A, -2 * c, 1 - a / A);
    }
    void highShelf(double sr, double f, double gainDb)
    {
        double A = std::pow(10.0, gainDb / 40), w = juce::MathConstants<double>::twoPi * f / sr;
        double c = std::cos(w), s = std::sin(w), a = s / 2 * std::sqrt(2.0), k = 2 * std::sqrt(A) * a;
        set(A * ((A + 1) + (A - 1) * c + k), -2 * A * ((A - 1) + (A + 1) * c), A * ((A + 1) + (A - 1) * c - k),
            (A + 1) - (A - 1) * c + k, 2 * ((A - 1) - (A + 1) * c), (A + 1) - (A - 1) * c - k);
    }
};

// Circular delay with fractional (linear) reads.
struct DelayLine {
    std::vector<float> buf;
    int write = 0;
    void prepare(int maxSamples) { buf.assign((size_t) juce::jmax(4, maxSamples + 2), 0.0f); write = 0; }
    void clear() { std::fill(buf.begin(), buf.end(), 0.0f); }
    inline void push(float x) { buf[(size_t) write] = x; if (++write >= (int) buf.size()) write = 0; }
    inline float read(float delay) const // delay in samples, >= 1
    {
        const int size = (int) buf.size();
        float pos = (float) write - delay;
        while (pos < 0) pos += (float) size;
        int i0 = (int) pos, i1 = i0 + 1 >= size ? 0 : i0 + 1;
        float f = pos - (float) i0;
        return buf[(size_t) i0] + (buf[(size_t) i1] - buf[(size_t) i0]) * f;
    }
    inline float readInt(int delay) const
    {
        int i = write - delay;
        if (i < 0) i += (int) buf.size();
        return buf[(size_t) i];
    }
};

// Schroeder allpass used for diffusion.
struct Allpass {
    DelayLine line;
    int length = 1;
    float g = 0.6f;
    void prepare(int len, float gain) { length = juce::jmax(1, len); g = gain; line.prepare(length + 1); }
    inline float process(float x)
    {
        float d = line.readInt(length);
        float v = x + g * d;
        line.push(v);
        return d - g * v;
    }
};

// 8-line feedback delay network: dense, wide and smooth. The input is high-passed and
// the tail loses low frequencies faster than mids, so it never piles up low-mid mud.
class Reverb {
public:
    void prepare(double sampleRate)
    {
        sr = sampleRate;
        static const float lengthsMs[8] { 31.7f, 37.9f, 43.3f, 49.1f, 56.9f, 63.7f, 71.3f, 79.9f };
        for (int i = 0; i < 8; ++i) {
            lengths[(size_t) i] = (float) (lengthsMs[i] * 0.001 * sr);
            lines[(size_t) i].prepare((int) lengths[(size_t) i] + 32);
        }
        static const float diffMs[4] { 4.71f, 3.59f, 12.73f, 9.31f };
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < 4; ++i)
                diffusers[(size_t) c][(size_t) i].prepare((int) ((diffMs[i] + c * 0.37f * (float) (i + 1)) * 0.001 * sr), 0.68f);
        predelay.prepare((int) (0.05 * sr));
        for (int c = 0; c < 2; ++c) {
            inHigh[(size_t) c][0].highpass(sr, 260.0); inHigh[(size_t) c][1].highpass(sr, 260.0);
            inMid[(size_t) c].peak(sr, 420.0, 0.8, -4.0);
            inLow[(size_t) c].lowpass(sr, 9500.0);
        }
        for (int i = 0; i < 8; ++i) { damp[(size_t) i] = 0; lowState[(size_t) i] = 0; }
        setDecay(2.6f);
        phase = 0;
    }
    void reset()
    {
        for (auto& l : lines) l.clear();
        for (auto& c : diffusers) for (auto& a : c) a.line.clear();
        predelay.clear();
        for (auto& c : inHigh) for (auto& b : c) b.reset();
        for (auto& b : inMid) b.reset();
        for (auto& b : inLow) b.reset();
        damp.fill(0); lowState.fill(0);
    }
    void setDecay(float seconds)
    {
        for (int i = 0; i < 8; ++i)
            gains[(size_t) i] = std::pow(10.0f, -3.0f * lengths[(size_t) i] / (seconds * (float) sr));
    }
    // Adds the wet signal (scaled by `wet`) into L/R.
    void process(float* L, float* R, int n, float wet)
    {
        const float dampCoef = std::exp(-juce::MathConstants<float>::twoPi * 6500.0f / (float) sr);
        const float lowCoef = std::exp(-juce::MathConstants<float>::twoPi * 180.0f / (float) sr);
        const float lfoInc = (float) (juce::MathConstants<double>::twoPi * 0.31 / sr);
        const int pre = (int) (0.018 * sr);
        for (int s = 0; s < n; ++s) {
            // Input conditioning: mono-compatible stereo feed, cleaned of lows and low-mids.
            float in[2] { L[s], R[s] };
            for (int c = 0; c < 2; ++c) {
                float x = inHigh[(size_t) c][1].process(inHigh[(size_t) c][0].process(in[c]));
                x = inLow[(size_t) c].process(inMid[(size_t) c].process(x));
                for (auto& a : diffusers[(size_t) c]) x = a.process(x);
                in[c] = x;
            }
            predelay.push(0.5f * (in[0] + in[1]));
            const float side = 0.5f * (in[0] - in[1]);
            const float mid = predelay.readInt(pre);

            phase += lfoInc;
            if (phase > juce::MathConstants<float>::twoPi) phase -= juce::MathConstants<float>::twoPi;
            float out[8];
            for (int i = 0; i < 8; ++i) {
                float mod = (i == 1 || i == 6) ? 6.0f * std::sin(phase + (float) i) : 0.0f;
                out[i] = lines[(size_t) i].read(lengths[(size_t) i] + mod);
            }
            // Hadamard 8x8 mixing (fast Walsh-Hadamard), energy preserving.
            float h[8];
            for (int i = 0; i < 8; ++i) h[i] = out[i];
            for (int len = 1; len < 8; len <<= 1)
                for (int i = 0; i < 8; i += len << 1)
                    for (int j = i; j < i + len; ++j) { float a = h[j], b = h[j + len]; h[j] = a + b; h[j + len] = a - b; }
            const float norm = 0.35355339f; // 1/sqrt(8)
            for (int i = 0; i < 8; ++i) {
                float v = h[i] * norm * gains[(size_t) i];
                // High damping (air) and stronger low decay (no boom in the tail).
                damp[(size_t) i] = v + (damp[(size_t) i] - v) * dampCoef;
                lowState[(size_t) i] = damp[(size_t) i] + (lowState[(size_t) i] - damp[(size_t) i]) * lowCoef;
                float fb = damp[(size_t) i] - 0.6f * lowState[(size_t) i];
                float feed = (i & 1) ? mid - side : mid + side;
                lines[(size_t) i].push(fb + feed * ((i & 2) ? -0.5f : 0.5f));
            }
            float wl = out[0] - out[2] + out[4] + out[6] * 0.7f;
            float wr = out[1] + out[3] - out[5] + out[7] * 0.7f;
            L[s] += wet * wl * 0.55f;
            R[s] += wet * wr * 0.55f;
        }
    }
private:
    double sr = 44100;
    std::array<DelayLine, 8> lines;
    std::array<float, 8> lengths {}, gains {}, damp {}, lowState {};
    std::array<std::array<Allpass, 4>, 2> diffusers;
    std::array<std::array<Biquad, 2>, 2> inHigh;
    std::array<Biquad, 2> inMid, inLow;
    DelayLine predelay;
    float phase = 0;
};

// Wide stereo chorus: one modulated voice per side, in quadrature.
class Chorus {
public:
    void prepare(double sampleRate) { sr = sampleRate; line[0].prepare((int) (0.05 * sr)); line[1].prepare((int) (0.05 * sr)); phase = 0; }
    void reset() { line[0].clear(); line[1].clear(); }
    void process(float* L, float* R, int n, float amount)
    {
        if (amount <= 0.0005f) { for (int i = 0; i < n; ++i) { line[0].push(L[i]); line[1].push(R[i]); } return; }
        const float inc = (float) (juce::MathConstants<double>::twoPi * 0.42 / sr);
        const float base = (float) (0.013 * sr), depth = (float) (0.0035 * sr);
        for (int i = 0; i < n; ++i) {
            line[0].push(L[i]);
            line[1].push(R[i]);
            phase += inc;
            if (phase > juce::MathConstants<float>::twoPi) phase -= juce::MathConstants<float>::twoPi;
            float dl = line[1].read(base + depth * (1.0f + std::sin(phase)));          // crossed for width
            float dr = line[0].read(base + depth * (1.0f + std::cos(phase)) + 7.0f);
            L[i] = L[i] * (1.0f - 0.25f * amount) + dl * 0.7f * amount;
            R[i] = R[i] * (1.0f - 0.25f * amount) + dr * 0.7f * amount;
        }
    }
private:
    double sr = 44100;
    DelayLine line[2];
    float phase = 0;
};

} // namespace essential::dsp
