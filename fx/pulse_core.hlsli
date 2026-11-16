// SPDX-License-Identifier: MIT
// PULSE's screen effect, the per-pixel part: shared by fx/pulse.hlsl (the pass) and native/tests/fx_test.cpp (the
// same formula compiled as C++, so the tests check the shader itself). Written in the subset both languages read:
// float2/float3/uint2, f-suffixed literals, no swizzles but .x/.y/.z.
//
// The includer defines:
//   PULSE_SCENE(uv)     the picture at uv, linear float3
//   PULSE_GRAIN(pixel)  noise for the pixel, -0.5..0.5
//   PULSE_SIZE          the picture's size, float2
//   PULSE_LUMA(rgb)     luminance of a linear float3
//   PULSE_SPLAT3(v)     float3(v, v, v)
//   PULSE_UNROLL        [unroll] in HLSL, nothing in C++
//
// Every effect is multiplied by its own channel and adds nothing of its own: with a channel at 0 its effect leaves
// the pixel exactly as it was, and with every channel at 0 the pixel is returned untouched (pulse_idle).

struct PulseFx {
    float tunnel;  // tunnel vision, 0..1
    float beat;    // the heartbeat's throb now, 0..1
    float soft;    // fatigue's softness, 0..1
    float g_side;  // lateral G greyout, -1 (left edge) .. +1 (right edge)
    float g_brake; // braking G, the top edge, 0..1
    float flash;   // the impact's white flash, 0..1
    float blur;    // the impact's radial blur, 0..1
    float desat;   // colour drained at the edges, 0..1
    float grain;   // fatigue's grain, 0..1
    float dim;     // fatigue's dimming, 0..1
};

bool pulse_idle(PulseFx f)
{
    const float e = 0.001f;
    return f.tunnel <= e && f.beat <= e && f.soft <= e && abs(f.g_side) <= e && f.g_brake <= e && f.flash <= e &&
           f.blur <= e && f.desat <= e && f.grain <= e && f.dim <= e;
}

// 0 in the middle, 1 at the corners, for a frame of this aspect
float pulse_edge(float2 uv)
{
    float aspect = PULSE_SIZE.x / PULSE_SIZE.y;
    float2 d = uv - 0.5f;
    d.x = d.x * aspect;
    return length(d) / length(float2(0.5f * aspect, 0.5f));
}

// rgb: the pixel's linear colour. Returns the new linear colour.
float3 pulse_apply(float3 rgb, float2 uv, uint2 pixel, PulseFx f)
{
    if (pulse_idle(f))
        return rgb;
    float r = pulse_edge(uv);

    // an impact: a radial blur towards the centre and a little colour split, fading in about a second
    if (f.blur > 0.001f) {
        float2 dir = uv - 0.5f;
        float reach = 0.045f * f.blur;
        float3 sum = rgb;
        PULSE_UNROLL for (int i = 1; i <= 8; i++) {
            float t = float(i) / 8.0f;
            sum = sum + PULSE_SCENE(uv - dir * (reach * t));
        }
        rgb = lerp(rgb, sum / 9.0f, saturate(f.blur * 1.2f));
        float split = 0.004f * f.blur;
        rgb.x = lerp(rgb.x, PULSE_SCENE(uv + dir * split).x, f.blur);
        rgb.z = lerp(rgb.z, PULSE_SCENE(uv - dir * split).z, f.blur);
    }

    // fatigue: a softer picture, some grain, a little dimmer
    if (f.soft > 0.001f) {
        float2 px = float2(1.5f / PULSE_SIZE.x, 1.5f / PULSE_SIZE.y);
        float3 around = (PULSE_SCENE(uv + float2(px.x, 0.0f)) + PULSE_SCENE(uv - float2(px.x, 0.0f)) +
                         PULSE_SCENE(uv + float2(0.0f, px.y)) + PULSE_SCENE(uv - float2(0.0f, px.y))) * 0.25f;
        rgb = lerp(rgb, around, f.soft * 0.35f);
    }
    if (f.grain > 0.001f)
        rgb = rgb * (1.0f + PULSE_GRAIN(pixel) * 0.07f * f.grain);
    if (f.dim > 0.001f)
        rgb = rgb * (1.0f - 0.12f * f.dim);

    // tunnel vision and the heartbeat: the edges close in and lose their colour, and throb with each beat
    if (f.tunnel > 0.001f || f.desat > 0.001f || f.beat > 0.001f) {
        float inner = lerp(0.95f, 0.42f, f.tunnel);
        float ring = smoothstep(inner, inner + 0.55f, r);
        float throb = f.beat * smoothstep(0.55f, 1.1f, r);
        float dark = saturate(ring * 0.85f * saturate(f.tunnel * 1.5f) + throb * 0.35f);
        rgb = lerp(rgb, PULSE_SPLAT3(PULSE_LUMA(rgb)), saturate(ring * f.desat * 0.9f));
        rgb = rgb * (1.0f - dark);
        rgb = lerp(rgb, rgb * float3(1.0f, 0.82f, 0.82f), throb * 0.5f); // a hint of red in the beat
    }

    // G: the outside edge greys in a fast corner, the top edge under hard braking
    if (abs(f.g_side) > 0.001f) {
        float side = f.g_side > 0.0f ? smoothstep(0.62f, 1.0f, uv.x) : smoothstep(0.38f, 0.0f, uv.x);
        float k = abs(f.g_side) * side;
        rgb = lerp(rgb, PULSE_SPLAT3(PULSE_LUMA(rgb)), k * 0.6f);
        rgb = rgb * (1.0f - k * 0.45f);
    }
    if (f.g_brake > 0.001f) {
        float top = smoothstep(0.35f, 0.0f, uv.y) * f.g_brake;
        rgb = rgb * (1.0f - top * 0.35f);
        rgb = lerp(rgb, rgb * float3(1.0f, 0.85f, 0.85f), top * 0.4f);
    }

    // the impact's flash: white, strongest at the edges
    if (f.flash > 0.001f)
        rgb = lerp(rgb, PULSE_SPLAT3(max(1.0f, PULSE_LUMA(rgb))), f.flash * (0.35f + 0.4f * r));

    return rgb;
}
