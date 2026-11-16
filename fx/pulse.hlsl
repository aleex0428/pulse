// SPDX-License-Identifier: MIT
// PULSE's screen effect: what the driver's heart does to what they see. The plugin gives the pass 48 bytes every
// frame (native/src/session.hpp, FxData); with no data, `active` at 0 or every channel at 0, the picture passes
// through untouched.
//
//   0 active   4 tunnel   8 beat   12 soft   16 g_side   20 g_brake   24 flash   28 blur   32 desat   36 time
//   40 grain   44 dim
//
// Everything works in linear light, so it looks the same in SDR and HDR. Every effect is at the edges or brief: the
// centre of the picture, where the driver looks, stays clear. The formula itself is in pulse_core.hlsli, which the
// tests compile as C++.
#include "swofty/postfx.hlsli"

#define PULSE_SCENE(uv) to_linear(scene(uv).rgb)
#define PULSE_GRAIN(pixel) (random(pixel, 7) - 0.5f)
#define PULSE_SIZE float2(swofty_size)
#define PULSE_LUMA(c) luminance(c)
#define PULSE_SPLAT3(v) float3((v), (v), (v))
#define PULSE_UNROLL [unroll]
#include "pulse_core.hlsli"

float4 effect(float2 uv, uint2 pixel)
{
    float4 c = scene_at(int2(pixel));
    if (data_size() < 48 || data_float(0) < 0.5)
        return c;
    PulseFx f;
    f.tunnel = saturate(data_float(4));
    f.beat = saturate(data_float(8));
    f.soft = saturate(data_float(12));
    f.g_side = clamp(data_float(16), -1.0, 1.0);
    f.g_brake = saturate(data_float(20));
    f.flash = saturate(data_float(24));
    f.blur = saturate(data_float(28));
    f.desat = saturate(data_float(32));
    f.grain = saturate(data_float(40));
    f.dim = saturate(data_float(44));
    // nothing to do: the picture as it came, with no trip through linear light
    if (pulse_idle(f))
        return c;
    float3 rgb = pulse_apply(to_linear(c.rgb), uv, pixel, f);
    return float4(from_linear(max(rgb, 0.0)), c.a);
}
