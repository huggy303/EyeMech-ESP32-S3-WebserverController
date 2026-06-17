#ifndef EYE_MATH_H
#define EYE_MATH_H
// ─────────────────────────────────────────────────────────────────────────────
//  Pure, hardware-independent math for the EyeMech motion engine.
//  Deliberately free of Arduino types and globals so it compiles and unit-tests
//  on a host (see test/test_eyemath.cpp). The firmware (EyeMech.ino) includes
//  this and uses these definitions directly — single source of truth.
// ─────────────────────────────────────────────────────────────────────────────

#include <math.h>

// Easing curve identifiers. Order is load-bearing: indices 0..2 are the only
// eases exposed through the /seq choreography API (the keyframe `ease` field is
// clamped to 0..2). EASE_OUT_BACK is internal-only (ALIVE large-saccade overshoot)
// and must stay last so the public indices never shift.
enum EaseType { EASE_OUT, EASE_IN_OUT, EASE_LINEAR, EASE_OUT_BACK };

// Clamp v to [lo, hi]. Tolerates inverted ranges (some eyelids are mechanically
// inverted, so their limit pair runs high->low).
inline int clamp(int v, int lo, int hi) {
    if (lo > hi) { int t = lo; lo = hi; hi = t; }
    return v < lo ? lo : (v > hi ? hi : v);
}

// Map normalised gaze x in [-1,1] to a servo angle, piecewise around centre so
// asymmetric travel (centre->lo != centre->hi) is honoured.
//   x<0 -> interpolates centre..lo,  x>0 -> centre..hi,  x==0 -> centre.
inline int gazeAngle(float x, int lo, int center, int hi) {
    if (x < 0.0f) return (int)lroundf(center + (center - lo) * x);
    return (int)lroundf(center + (hi - center) * x);
}

// Map normalised tween progress t (0..1) through the chosen easing curve.
// Returns the eased fraction. EASE_OUT_BACK overshoots above 1.0 mid-curve but
// lands exactly at 1.0 when t == 1.
inline float applyEase(EaseType ease, float t) {
    switch (ease) {
        case EASE_OUT: {
            // cubic ease-out: 1 - (1-t)^3
            float inv = 1.0f - t;
            return 1.0f - (inv * inv * inv);
        }
        case EASE_IN_OUT:
            // standard cubic in-out
            return (t < 0.5f)
                ? 4.0f * t * t * t
                : 1.0f - (-2.0f * t + 2.0f) * (-2.0f * t + 2.0f) * (-2.0f * t + 2.0f) / 2.0f;
        case EASE_OUT_BACK: {
            // mild overshoot-settle: peaks slightly above 1.0 then lands at 1.0
            const float c1 = 0.9f; const float c3 = c1 + 1.0f; float p = t - 1.0f;
            return 1.0f + c3 * p * p * p + c1 * p * p;
        }
        default: // EASE_LINEAR
            return t;
    }
}

#endif // EYE_MATH_H
