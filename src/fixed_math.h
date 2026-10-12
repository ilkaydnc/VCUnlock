// =====================================================================================================================
//
//      _    __________  __      __           __
//     | |  / / ____/ / / /___  / /___  _____/ /__
//     | | / / /   / / / / __ \/ / __ \/ ___/ //_/        GRAND THEFT AUTO: VICE CITY
//     | |/ / /___/ /_/ / / / / / /_/ / /__/ ,<           Uncapped frame rate.
//     |___/\____/\____/_/ /_/_/\____/\___/_/|_|
//
// =====================================================================================================================
//
//   FILE      fixed_math.h
//   MODULE    fixed
//   ROLE      Interpolation and scheduling math
//
// ---------------------------------------------------------------------------------------------------------------------
//
//   Original math, written for VCUnlock: no game code and no RenderWare implementation is included.
//
//   - Vec, Matrix and Quaternion with the operations the presentation needs.
//   - Blend, BlendSpin, BlendTwist, BlendFollow, MoveVisual and TurnLook: poses between two ticks.
//   - Accumulator: schedules 30 Hz ticks from real time and drops what exceeds its catch-up cap. In lockstep (a frame
//     cap of 30 or below) every frame runs its tick and shows it unblended, as the original did.
//
// ---------------------------------------------------------------------------------------------------------------------
//
//   LICENCE   MIT. Copyright (c) 2026 İlkay Dinç. The full text is in LICENSE.
//             SPDX-License-Identifier: MIT
//   NOTICE    Unofficial. Not affiliated with or endorsed by Rockstar Games or Take-Two
//             Interactive. Requires an original copy of the game.
//
// =====================================================================================================================

#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace fixedmath {
constexpr double TickMs = 1000.0 / 30.0;  // one 30 Hz tick in ms

// ---------------------------------------------------------------------------------------------------------------------
//  VECTORS
// ---------------------------------------------------------------------------------------------------------------------

struct Vec {
    float x, y, z;
};

inline Vec operator+(Vec a, Vec b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline Vec operator-(Vec a, Vec b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline Vec operator*(Vec a, float b) { return {a.x * b, a.y * b, a.z * b}; }
inline float Dot(Vec a, Vec b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline Vec Cross(Vec a, Vec b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
inline float Length(Vec a) { return std::sqrt(Dot(a, a)); }
inline Vec Lerp(Vec a, Vec b, float t) { return a + (b - a) * t; }
inline bool Finite(Vec a) { return std::isfinite(a.x) && std::isfinite(a.y) && std::isfinite(a.z); }

// ---------------------------------------------------------------------------------------------------------------------
//  MATRICES
// ---------------------------------------------------------------------------------------------------------------------

// Verified 64-byte matrix storage: three padded columns followed by a padded translation.
struct Matrix {
    Vec right;
    uint32_t flags;
    Vec forward;
    uint32_t pad1;
    Vec up;
    uint32_t pad2;
    Vec pos;
    uint32_t pad3;
};
static_assert(sizeof(Matrix) == 64);

inline Matrix Identity() { return {{1, 0, 0}, 0, {0, 1, 0}, 0, {0, 0, 1}, 0, {0, 0, 0}, 0}; }

// Finite, with every axis between 0.001 and 100 long.
inline bool Valid(const Matrix& m) {
    if (!Finite(m.right) || !Finite(m.forward) || !Finite(m.up) || !Finite(m.pos)) return false;
    float a = Length(m.right), b = Length(m.forward), c = Length(m.up);
    return a > 0.001f && b > 0.001f && c > 0.001f && a < 100 && b < 100 && c < 100;
}

// ---------------------------------------------------------------------------------------------------------------------
//  QUATERNIONS
// ---------------------------------------------------------------------------------------------------------------------

struct Quaternion {
    float x, y, z, w;
};

inline Quaternion Normalize(Quaternion q) {
    float n = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
    return n > 1e-9f ? Quaternion{q.x / n, q.y / n, q.z / n, q.w / n} : Quaternion{0, 0, 0, 1};
}

inline Quaternion Rotation(const Matrix& m) {
    Vec r = m.right * (1 / Length(m.right)), f = m.forward * (1 / Length(m.forward)), u = m.up * (1 / Length(m.up));
    float t = r.x + f.y + u.z;
    Quaternion q;
    if (t > 0) {
        float s = std::sqrt(t + 1) * 2;
        q = {(f.z - u.y) / s, (u.x - r.z) / s, (r.y - f.x) / s, s / 4};
    } else if (r.x > f.y && r.x > u.z) {
        float s = std::sqrt(1 + r.x - f.y - u.z) * 2;
        q = {s / 4, (f.x + r.y) / s, (u.x + r.z) / s, (f.z - u.y) / s};
    } else if (f.y > u.z) {
        float s = std::sqrt(1 + f.y - r.x - u.z) * 2;
        q = {(f.x + r.y) / s, s / 4, (u.y + f.z) / s, (u.x - r.z) / s};
    } else {
        float s = std::sqrt(1 + u.z - r.x - f.y) * 2;
        q = {(u.x + r.z) / s, (u.y + f.z) / s, s / 4, (r.y - f.x) / s};
    }
    return Normalize(q);
}

inline Quaternion Slerp(Quaternion a, Quaternion b, float t) {
    float d = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
    if (d < 0) {
        b = {-b.x, -b.y, -b.z, -b.w};
        d = -d;
    }
    if (d > 0.9995f)
        return Normalize({a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t});
    float theta = std::acos(std::clamp(d, -1.0f, 1.0f)), s = std::sin(theta);
    float x = std::sin((1 - t) * theta) / s, y = std::sin(t * theta) / s;
    return Normalize({a.x * x + b.x * y, a.y * x + b.y * y, a.z * x + b.z * y, a.w * x + b.w * y});
}

inline Quaternion Conjugate(Quaternion q) { return {-q.x, -q.y, -q.z, q.w}; }

inline Quaternion Multiply(Quaternion a, Quaternion b) {
    return {a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y, a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
            a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w, a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z};
}

// ---------------------------------------------------------------------------------------------------------------------
//  POSE BLENDING
// ---------------------------------------------------------------------------------------------------------------------

// A right-handed basis: a rotation, possibly scaled, but not mirrored.
inline bool Proper(const Matrix& m) { return Dot(Cross(m.right, m.forward), m.up) > 0; }

// Rotation q with axis lengths (scale) interpolated between a and b.
inline void SetRotation(Matrix& m, Quaternion q, const Matrix& a, const Matrix& b, float t) {
    float x = q.x, y = q.y, z = q.z, w = q.w;
    m.right = Vec{1 - 2 * (y * y + z * z), 2 * (x * y + z * w), 2 * (x * z - y * w)} *
              (Length(a.right) + (Length(b.right) - Length(a.right)) * t);
    m.forward = Vec{2 * (x * y - z * w), 1 - 2 * (x * x + z * z), 2 * (y * z + x * w)} *
                (Length(a.forward) + (Length(b.forward) - Length(a.forward)) * t);
    m.up = Vec{2 * (x * z + y * w), 2 * (y * z - x * w), 1 - 2 * (x * x + y * y)} *
           (Length(a.up) + (Length(b.up) - Length(a.up)) * t);
}

inline Matrix Blend(const Matrix& a, const Matrix& b, float t) {
    Matrix m = b;
    m.pos = Lerp(a.pos, b.pos, t);
    // Reflected/scaled models without a proper rotation retain their current orientation.
    if (!Proper(a) || !Proper(b)) return m;
    SetRotation(m, Slerp(Rotation(a), Rotation(b), t), a, b, t);
    return m;
}

// ---------------------------------------------------------------------------------------------------------------------
//  SPIN TRACKING
// ---------------------------------------------------------------------------------------------------------------------

// Per-tick turn of a part relative to its previous pose. The angle is unwrapped against the previous
// tick's, so parts turning more than half a revolution per tick (wheels, rotors) keep their direction
// instead of being blended backwards along the short arc. Spin-up is gradual, so the branch is tracked.
struct Spin {
    Vec axis{1, 0, 0};
    float angle = 0;
    bool valid = false;
};

inline void TrackSpin(const Matrix& a, const Matrix& b, Spin& s) {
    if (!Proper(a) || !Proper(b)) {
        s = Spin{};
        return;
    }
    Quaternion r = Multiply(Conjugate(Rotation(a)), Rotation(b));
    if (r.w < 0) r = {-r.x, -r.y, -r.z, -r.w};
    float sinHalf = std::sqrt(r.x * r.x + r.y * r.y + r.z * r.z);
    if (sinHalf < 1e-6f) {  // no turn: keep the axis for the next tick
        s.angle = 0;
        return;
    }
    Vec axis = {r.x / sinHalf, r.y / sinHalf, r.z / sinHalf};
    float angle = 2 * std::atan2(sinHalf, r.w);  // 0..pi on the short arc
    if (s.valid && Dot(axis, s.axis) < 0) {
        axis = axis * -1;
        angle = -angle;
    }
    if (s.valid) {
        constexpr float kPi = 3.14159265f;
        while (angle - s.angle > kPi) angle -= 2 * kPi;
        while (angle - s.angle < -kPi) angle += 2 * kPi;
    }
    s.axis = axis;
    s.angle = angle;
    s.valid = true;
}

// Like Blend, but turns along the tracked per-tick rotation. Equals a at t=0 and b's rotation at t=1.
inline Matrix BlendSpin(const Matrix& a, const Matrix& b, float t, const Spin& s) {
    if (!s.valid || std::fabs(s.angle) < 1e-6f || !Proper(a) || !Proper(b)) return Blend(a, b, t);
    Matrix m = b;
    m.pos = Lerp(a.pos, b.pos, t);
    float h = s.angle * t * 0.5f, sn = std::sin(h);
    SetRotation(m, Normalize(Multiply(Rotation(a), {s.axis.x * sn, s.axis.y * sn, s.axis.z * sn, std::cos(h)})),
                a, b, t);
    return m;
}

// ---------------------------------------------------------------------------------------------------------------------
//  WHEELS: SWING AND TWIST
// ---------------------------------------------------------------------------------------------------------------------

// Wheels are posed as Rz(steer) * Rx(roll). Split the part-local turn from a to b into
// swing * twist about the local x axis (the axle): the swing is the steering change and the twist
// the roll. Blending the combined turn about a single axis instead tilts the axle out of its plane
// mid-tick, increasingly so as the roll approaches a full revolution per tick.
struct Twist {
    Quaternion swing{0, 0, 0, 1};
    float angle = 0;
    bool valid = false;
};

constexpr float kTwoPi = 6.28318531f;

// The angle wrapped to [-pi, pi].
inline float WrapPi(float a) { return a - kTwoPi * std::round(a / kTwoPi); }

inline Twist SplitTwistX(const Matrix& a, const Matrix& b) {
    Twist s;
    if (!Proper(a) || !Proper(b)) return s;
    Quaternion r = Multiply(Conjugate(Rotation(a)), Rotation(b));
    float n = std::sqrt(r.x * r.x + r.w * r.w);  // cos(swing/2)
    if (n < 0.7071f) return s;                   // swing beyond 90 degrees: not a steered wheel
    Quaternion twist = {r.x / n, 0, 0, r.w / n};
    s.swing = Multiply(r, Conjugate(twist));
    if (s.swing.w < 0) s.swing = {-s.swing.x, -s.swing.y, -s.swing.z, -s.swing.w};
    s.angle = WrapPi(2 * std::atan2(twist.x, twist.w));
    s.valid = true;
    return s;
}

// The roll congruent to the measured twist (modulo a full turn) closest to the expected roll.
inline float NearestTurn(float measured, float expected) {
    return measured + kTwoPi * std::round((expected - measured) / kTwoPi);
}

// Equals a at t=0 and b at t=1 when roll is congruent to s.angle.
inline Matrix BlendTwist(const Matrix& a, const Matrix& b, float t, const Twist& s, float roll) {
    Matrix m = b;
    m.pos = Lerp(a.pos, b.pos, t);
    float h = roll * t * 0.5f;
    Quaternion q = Multiply(Multiply(Rotation(a), Slerp({0, 0, 0, 1}, s.swing, t)), {std::sin(h), 0, 0, std::cos(h)});
    SetRotation(m, Normalize(q), a, b, t);
    return m;
}

// ---------------------------------------------------------------------------------------------------------------------
//  COMPARISON AND SPACE CONVERSION
// ---------------------------------------------------------------------------------------------------------------------

inline bool NearlyEqual(const Matrix& a, const Matrix& b, float eps = 1e-5f) {
    const float* x = &a.right.x;
    const float* y = &b.right.x;
    for (int i = 0; i < 16; i++)
        if ((i & 3) != 3 && std::fabs(x[i] - y[i]) > eps) return false;
    return true;
}

// p from m's axes to world space (no translation).
inline Vec TransformVector(const Matrix& m, Vec p) { return m.right * p.x + m.forward * p.y + m.up * p.z; }

// The inverse of TransformVector for orthogonal (possibly scaled) axes.
inline Vec LocalVector(const Matrix& m, Vec p) {
    return {Dot(m.right, p) / Dot(m.right, m.right), Dot(m.forward, p) / Dot(m.forward, m.forward),
            Dot(m.up, p) / Dot(m.up, m.up)};
}

// ---------------------------------------------------------------------------------------------------------------------
//  FOLLOW CAMERA AND MODEL ROOTS
// ---------------------------------------------------------------------------------------------------------------------

// A follow camera rotates around its target, rather than along a world-space chord.
// Keeping the target offset in view space also retains deliberate off-centre framing.
inline Matrix BlendFollow(const Matrix& previous, const Matrix& current, Vec previousOffset, Vec currentOffset,
                          Vec drawTarget, float alpha) {
    Matrix draw = Blend(previous, current, alpha);
    draw.pos = drawTarget - TransformVector(draw, Lerp(previousOffset, currentOffset, alpha));
    return draw;
}

// Preserve visual offsets/lean applied by PreRender instead of replacing the model root outright.
inline Matrix MoveVisual(const Matrix& from, const Matrix& to, const Matrix& visual) {
    Matrix m = visual;
    m.right = TransformVector(to, LocalVector(from, visual.right));
    m.forward = TransformVector(to, LocalVector(from, visual.forward));
    m.up = TransformVector(to, LocalVector(from, visual.up));
    m.pos = to.pos + TransformVector(to, LocalVector(from, visual.pos - from.pos));
    return m;
}

// ---------------------------------------------------------------------------------------------------------------------
//  MOUSE LOOK
// ---------------------------------------------------------------------------------------------------------------------

// v turned by angle a about the unit axis k (right-handed).
inline Vec RotateAround(Vec v, Vec k, float a) {
    float c = std::cos(a), s = std::sin(a);
    return v * c + Cross(k, v) * s + k * (Dot(k, v) * (1 - c));
}

// Turns a RenderWare camera (look = the up column) by yaw about world z and by pitch in its vertical plane.
// The pitch stops at lo/hi elevation (or where the look already is, if outside). With a pivot on the view ray
// the camera orbits it, otherwise it turns in place.
inline Matrix TurnLook(const Matrix& m, float yaw, float pitch, float lo, float hi, const Vec* pivot) {
    float n = Length(m.up);
    if (n < 1e-6f) return m;
    Vec look = m.up * (1 / n);
    float e0 = std::asin(std::clamp(look.z, -1.0f, 1.0f));
    float ep = pitch == 0 ? 0 : std::clamp(e0 + pitch, std::min(lo, e0), std::max(hi, e0)) - e0;
    Vec h = Cross(look, {0, 0, 1});
    float hl = Length(h);
    auto turn = [&](Vec v) {
        if (hl > 1e-4f) v = RotateAround(v, h * (1 / hl), ep);
        return RotateAround(v, {0, 0, 1}, yaw);
    };
    Matrix r = m;
    r.right = turn(m.right);
    r.forward = turn(m.forward);
    r.up = turn(m.up);
    if (pivot) r.pos = *pivot + turn(m.pos - *pivot);
    return r;
}

// The inverse of a rigid transform (orthonormal axes).
inline Matrix InverseRigid(const Matrix& m) {
    Matrix n = Identity();
    n.right = {m.right.x, m.forward.x, m.up.x};
    n.forward = {m.right.y, m.forward.y, m.up.y};
    n.up = {m.right.z, m.forward.z, m.up.z};
    n.pos = {-Dot(m.right, m.pos), -Dot(m.forward, m.pos), -Dot(m.up, m.pos)};
    return n;
}

// ---------------------------------------------------------------------------------------------------------------------
//  TICK SCHEDULING
// ---------------------------------------------------------------------------------------------------------------------

// Real time in, 30 Hz ticks out. Over 250 ms in one frame is dropped (counted in `dropped`) rather than caught
// up; Alpha is how far the drawn frame lies between the last tick and the next.
// Lockstep, for frame rates capped at 30 or below: a tick is due once half of it has passed, so at a 30 fps cap
// every frame runs exactly one tick, and the frame shows that tick as it is (Alpha 1), as the original game did.
struct Accumulator {
    double remainder = 0, dropped = 0;
    bool lockstep = false;
    void Reset() { remainder = 0; }
    void Add(double ms) {
        if (!std::isfinite(ms) || ms <= 0) return;
        if (ms > 250) {
            dropped += ms - 250;
            ms = 250;
        }
        remainder += ms;
    }
    bool Take() {
        if (remainder + 1e-8 < (lockstep ? TickMs / 2 : TickMs)) return false;
        remainder = lockstep ? remainder - TickMs : std::max(0.0, remainder - TickMs);
        return true;
    }
    float Alpha() const { return lockstep ? 1.0f : (float)std::clamp(remainder / TickMs, 0.0, 1.0); }
};
}  // namespace fixedmath
