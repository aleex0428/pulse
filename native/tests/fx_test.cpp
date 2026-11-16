// SPDX-License-Identifier: MIT
// The screen effect's formula (fx/pulse_core.hlsli, the same file the pass compiles) as C++, on a made-up picture:
// with every channel at 0 the picture comes out bit for bit the same; a channel at 0 adds nothing of its own (the
// old vignette that stayed with the tunnel at 0 is gone); each effect stays where it belongs (the centre stays
// clear); and nothing ever comes out of range.
//
//   fx_test.exe
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <random>
#include <vector>

namespace hlsl {

struct float2 {
    float x = 0, y = 0;
    float2() = default;
    float2(float a, float b) : x(a), y(b) {}
};
struct float3 {
    float x = 0, y = 0, z = 0;
    float3() = default;
    float3(float a, float b, float c) : x(a), y(b), z(c) {}
};
struct uint2 {
    uint32_t x = 0, y = 0;
};

inline float2 operator+(float2 a, float2 b) { return {a.x + b.x, a.y + b.y}; }
inline float2 operator-(float2 a, float2 b) { return {a.x - b.x, a.y - b.y}; }
inline float2 operator-(float2 a, float b) { return {a.x - b, a.y - b}; }
inline float2 operator*(float2 a, float b) { return {a.x * b, a.y * b}; }
inline float3 operator+(float3 a, float3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline float3 operator-(float3 a, float3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline float3 operator*(float3 a, float b) { return {a.x * b, a.y * b, a.z * b}; }
inline float3 operator*(float3 a, float3 b) { return {a.x * b.x, a.y * b.y, a.z * b.z}; }
inline float3 operator/(float3 a, float b) { return {a.x / b, a.y / b, a.z / b}; }

inline float abs(float v) { return std::fabs(v); }
inline float max(float a, float b) { return a > b ? a : b; }
inline float saturate(float v) { return std::clamp(v, 0.0f, 1.0f); }
// HLSL's lerp: x + s * (y - x)
inline float lerp(float a, float b, float t) { return a + t * (b - a); }
inline float3 lerp(float3 a, float3 b, float t) { return a + (b - a) * t; }
inline float smoothstep(float e0, float e1, float x)
{
    float t = saturate((x - e0) / (e1 - e0));
    return t * t * (3 - 2 * t);
}
inline float length(float2 v) { return std::sqrt(v.x * v.x + v.y * v.y); }

// the made-up picture, in linear light
int g_w = 96, g_h = 54;
std::vector<float3> g_img;

inline float3 at(int x, int y)
{
    x = std::clamp(x, 0, g_w - 1);
    y = std::clamp(y, 0, g_h - 1);
    return g_img[size_t(y) * g_w + x];
}
inline float3 sample(float2 uv) { return at(int(uv.x * g_w), int(uv.y * g_h)); }
inline float luma(float3 c) { return 0.2126f * c.x + 0.7152f * c.y + 0.0722f * c.z; }
inline float grain(uint2 p) { return float((p.x * 73856093u ^ p.y * 19349663u) % 1000u) / 1000.0f - 0.5f; }

#define PULSE_SCENE(uv) sample(uv)
#define PULSE_GRAIN(pixel) grain(pixel)
#define PULSE_SIZE float2(float(g_w), float(g_h))
#define PULSE_LUMA(c) luma(c)
#define PULSE_SPLAT3(v) float3((v), (v), (v))
#define PULSE_UNROLL
#include "../../fx/pulse_core.hlsli"

} // namespace hlsl

using namespace hlsl;

namespace {

int g_checks = 0, g_failed = 0;

void check(bool ok, const char *what, double got = NAN)
{
    g_checks++;
    if (!ok) {
        g_failed++;
        if (std::isnan(got))
            std::printf("FAIL: %s\n", what);
        else
            std::printf("FAIL: %s (got %.4f)\n", what, got);
    }
}

void make_picture()
{
    g_img.resize(size_t(g_w) * g_h);
    for (int y = 0; y < g_h; y++)
        for (int x = 0; x < g_w; x++) {
            float u = (x + 0.5f) / g_w, v = (y + 0.5f) / g_h;
            g_img[size_t(y) * g_w + x] = float3(0.1f + 0.8f * u, 0.2f + 0.6f * v, 0.5f + 0.4f * std::sin(u * 9 + v * 5));
        }
}

PulseFx none()
{
    PulseFx f;
    std::memset(&f, 0, sizeof f);
    return f;
}

std::vector<float3> render(const PulseFx &f)
{
    std::vector<float3> out(g_img.size());
    for (int y = 0; y < g_h; y++)
        for (int x = 0; x < g_w; x++) {
            float2 uv((x + 0.5f) / g_w, (y + 0.5f) / g_h);
            uint2 p;
            p.x = uint32_t(x);
            p.y = uint32_t(y);
            out[size_t(y) * g_w + x] = pulse_apply(g_img[size_t(y) * g_w + x], uv, p, f);
        }
    return out;
}

bool same(const float3 &a, const float3 &b) { return std::memcmp(&a, &b, sizeof a) == 0; }

// how many pixels changed, in a region (u0..u1, v0..v1)
int changed(const std::vector<float3> &out, float u0, float u1, float v0, float v1)
{
    int n = 0;
    for (int y = 0; y < g_h; y++)
        for (int x = 0; x < g_w; x++) {
            float u = (x + 0.5f) / g_w, v = (y + 0.5f) / g_h;
            if (u >= u0 && u <= u1 && v >= v0 && v <= v1 && !same(out[size_t(y) * g_w + x], g_img[size_t(y) * g_w + x]))
                n++;
        }
    return n;
}

float brightness(const std::vector<float3> &img, float u, float v) { return luma(img[size_t(int(v * g_h)) * g_w + int(u * g_w)]); }

void untouched()
{
    PulseFx f = none();
    check(pulse_idle(f), "every channel at 0: idle");
    auto out = render(f);
    check(changed(out, 0, 1, 0, 1) == 0, "every channel at 0: the picture comes out bit for bit the same");
    // the tunnel at 0 with G on: the old fixed vignette darkened the corners whatever the tunnel said
    f.g_side = 0.8f;
    out = render(f);
    check(changed(out, 0, 0.6f, 0, 1) == 0, "tunnel at 0, G to the right: the left side and the corners stay the same");
    check(changed(out, 0.8f, 1, 0, 1) > 0, "G to the right: the right edge greys");
    f = none();
    f.g_side = -0.8f;
    out = render(f);
    check(changed(out, 0.4f, 1, 0, 1) == 0 && changed(out, 0, 0.2f, 0, 1) > 0, "G to the left: only the left edge");
    f = none();
    f.g_brake = 0.9f;
    out = render(f);
    check(changed(out, 0, 1, 0.4f, 1) == 0 && changed(out, 0, 1, 0, 0.1f) > 0, "braking: only the top edge");
}

void tunnel_and_beat()
{
    PulseFx f = none();
    f.tunnel = 0.7f;
    f.desat = 0.56f;
    auto out = render(f);
    check(changed(out, 0.4f, 0.6f, 0.4f, 0.6f) == 0, "tunnel: the centre stays clear");
    check(brightness(out, 0.02f, 0.02f) < brightness(g_img, 0.02f, 0.02f) * 0.7f, "tunnel: the corners darken",
          brightness(out, 0.02f, 0.02f) / brightness(g_img, 0.02f, 0.02f));
    PulseFx weak = f;
    weak.tunnel = 0.2f;
    weak.desat = 0.16f;
    auto light = render(weak);
    check(brightness(light, 0.02f, 0.02f) > brightness(out, 0.02f, 0.02f), "a weaker tunnel darkens less");
    f = none();
    f.beat = 1;
    out = render(f);
    check(changed(out, 0.35f, 0.65f, 0.35f, 0.65f) == 0, "the heartbeat: the centre stays clear");
    check(changed(out, 0, 0.05f, 0, 0.05f) > 0, "the heartbeat: the corners throb");
}

void fatigue_and_impact()
{
    PulseFx f = none();
    f.dim = 0.5f;
    auto out = render(f);
    float ratio = brightness(out, 0.5f, 0.5f) / brightness(g_img, 0.5f, 0.5f);
    check(ratio < 0.97f && ratio > 0.9f, "dim alone: a little darker everywhere, nothing else", ratio);
    f = none();
    f.flash = 1;
    out = render(f);
    check(brightness(out, 0.02f, 0.5f) > brightness(g_img, 0.02f, 0.5f), "the flash brightens");
    f = none();
    f.blur = 1;
    out = render(f);
    check(changed(out, 0, 1, 0, 1) > 0, "the blur moves pixels");
}

void in_range()
{
    std::mt19937 rng(7);
    std::uniform_real_distribution<float> u(0, 1);
    bool ok = true;
    for (int i = 0; i < 60; i++) {
        PulseFx f;
        f.tunnel = u(rng);
        f.beat = u(rng);
        f.soft = u(rng);
        f.g_side = u(rng) * 2 - 1;
        f.g_brake = u(rng);
        f.flash = u(rng);
        f.blur = u(rng);
        f.desat = u(rng);
        f.grain = u(rng);
        f.dim = u(rng);
        for (const float3 &c : render(f))
            ok = ok && std::isfinite(c.x) && std::isfinite(c.y) && std::isfinite(c.z) && c.x >= -0.001f &&
                 c.y >= -0.001f && c.z >= -0.001f && c.x < 2 && c.y < 2 && c.z < 2;
    }
    check(ok, "any mix of channels: finite and in range");
}

} // namespace

int main()
{
    make_picture();
    untouched();
    tunnel_and_beat();
    fatigue_and_impact();
    in_range();
    if (g_failed) {
        std::printf("fx_test: %d of %d checks FAILED\n", g_failed, g_checks);
        return 1;
    }
    std::printf("fx_test: all %d checks passed\n", g_checks);
    return 0;
}
