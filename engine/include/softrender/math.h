#ifndef SOFTRENDER_MATH_H_
#define SOFTRENDER_MATH_H_

#include <cmath>

namespace softrender {

/// A vector in double precision, for positions in space. The simulation works in metres with +x right, +y up and +z
/// forward, as a model does.
struct Vec3d {
  double x = 0.0;
  double y = 0.0;
  double z = 0.0;
};

inline Vec3d operator+(const Vec3d& a, const Vec3d& b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline Vec3d operator-(const Vec3d& a, const Vec3d& b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline Vec3d operator-(const Vec3d& a) { return {-a.x, -a.y, -a.z}; }
inline Vec3d operator*(const Vec3d& a, double s) { return {a.x * s, a.y * s, a.z * s}; }
inline Vec3d operator*(double s, const Vec3d& a) { return a * s; }
inline Vec3d& operator+=(Vec3d& a, const Vec3d& b) {
  a = a + b;
  return a;
}
inline Vec3d& operator-=(Vec3d& a, const Vec3d& b) {
  a = a - b;
  return a;
}

inline double Dot(const Vec3d& a, const Vec3d& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline Vec3d Cross(const Vec3d& a, const Vec3d& b) {
  return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
inline double Length(const Vec3d& a) { return std::sqrt(Dot(a, a)); }
inline double Distance(const Vec3d& a, const Vec3d& b) { return Length(a - b); }

/// The vector scaled to length 1, or a zero vector if it has no length
inline Vec3d Normalized(const Vec3d& a) {
  const double length = Length(a);
  return length > 1e-12 ? a * (1.0 / length) : Vec3d{};
}

inline double Clamp(double v, double low, double high) { return v < low ? low : (v > high ? high : v); }

/// A rotation. The orientation of an object is the rotation that turns its own axes (front +z, top +y, right
/// +x) into world directions.
struct Quat {
  double w = 1.0;
  double x = 0.0;
  double y = 0.0;
  double z = 0.0;
};

inline Quat operator*(const Quat& a, const Quat& b) {
  return {a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z, a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
          a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x, a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w};
}

inline Quat Conjugate(const Quat& q) { return {q.w, -q.x, -q.y, -q.z}; }

inline Quat Normalized(const Quat& q) {
  const double length = std::sqrt(q.w * q.w + q.x * q.x + q.y * q.y + q.z * q.z);
  return length > 1e-12 ? Quat{q.w / length, q.x / length, q.y / length, q.z / length} : Quat{};
}

/// A turn of `angle` radians about a unit axis
inline Quat FromAxisAngle(const Vec3d& axis, double angle) {
  const double s = std::sin(angle / 2.0);
  return {std::cos(angle / 2.0), axis.x * s, axis.y * s, axis.z * s};
}

inline Vec3d Rotate(const Quat& q, const Vec3d& v) {
  const Vec3d u{q.x, q.y, q.z};
  const Vec3d t = 2.0 * Cross(u, v);
  return v + q.w * t + Cross(u, t);
}

/// Directions of an object's own axes in the world
inline Vec3d Forward(const Quat& q) { return Rotate(q, {0.0, 0.0, 1.0}); }
inline Vec3d Up(const Quat& q) { return Rotate(q, {0.0, 1.0, 0.0}); }
inline Vec3d Right(const Quat& q) { return Rotate(q, {1.0, 0.0, 0.0}); }

/// Turn an object by angles about its own axes, applied in its own frame. Positive pitch lifts the front, positive roll
/// drops the right side and positive yaw swings the front to the right.
inline Quat Turned(const Quat& q, double pitch, double roll, double yaw) {
  const Quat local = FromAxisAngle({0.0, 1.0, 0.0}, yaw) * FromAxisAngle({1.0, 0.0, 0.0}, -pitch) *
                     FromAxisAngle({0.0, 0.0, 1.0}, -roll);
  return Normalized(q * local);
}

/// The orientation that points the front along `forward`, with the top as near `up` as can be
inline Quat LookAlong(const Vec3d& forward, const Vec3d& up_hint) {
  const Vec3d f = Normalized(forward);
  Vec3d r = Cross(up_hint, f);
  if (Length(r) < 1e-9) {
    r = Cross(Vec3d{0.0, 0.0, 1.0}, f);
    if (Length(r) < 1e-9) {
      r = {1.0, 0.0, 0.0};
    }
  }
  r = Normalized(r);
  const Vec3d u = Cross(f, r);
  // Rotation matrix with columns r, u, f, turned into a quaternion
  const double m00 = r.x, m01 = u.x, m02 = f.x;
  const double m10 = r.y, m11 = u.y, m12 = f.y;
  const double m20 = r.z, m21 = u.z, m22 = f.z;
  Quat q;
  const double trace = m00 + m11 + m22;
  if (trace > 0.0) {
    const double s = std::sqrt(trace + 1.0) * 2.0;
    q = {0.25 * s, (m21 - m12) / s, (m02 - m20) / s, (m10 - m01) / s};
  } else if (m00 > m11 && m00 > m22) {
    const double s = std::sqrt(1.0 + m00 - m11 - m22) * 2.0;
    q = {(m21 - m12) / s, 0.25 * s, (m01 + m10) / s, (m02 + m20) / s};
  } else if (m11 > m22) {
    const double s = std::sqrt(1.0 + m11 - m00 - m22) * 2.0;
    q = {(m02 - m20) / s, (m01 + m10) / s, 0.25 * s, (m12 + m21) / s};
  } else {
    const double s = std::sqrt(1.0 + m22 - m00 - m11) * 2.0;
    q = {(m10 - m01) / s, (m02 + m20) / s, (m12 + m21) / s, 0.25 * s};
  }
  return Normalized(q);
}

}  // namespace softrender

#endif  // SOFTRENDER_MATH_H_
