#include "softrender/scene.h"

#include <algorithm>
#include <cmath>

namespace softrender {

namespace {

/// Fill a convex polygon given in camera space, cutting it off at the near plane
void FillViewPolygon(Canvas& canvas, const Camera& cam, const std::vector<Vec3d>& poly, Rgb colour, std::uint8_t edges) {
  const double near_plane = cam.near_plane;
  bool clipped = false;
  std::vector<Vec3d> out;
  for (std::size_t i = 0; i < poly.size(); ++i) {
    const Vec3d& a = poly[i];
    const Vec3d& b = poly[(i + 1) % poly.size()];
    const bool a_in = a.z >= near_plane;
    const bool b_in = b.z >= near_plane;
    if (a_in) {
      out.push_back(a);
    }
    if (a_in != b_in) {
      const double t = (near_plane - a.z) / (b.z - a.z);
      Vec3d cut = a + (b - a) * t;
      cut.z = near_plane;  // rounding must not leave it behind the plane, or it could not be projected
      out.push_back(cut);
      clipped = true;
    }
  }
  if (out.size() < 3) {
    return;
  }
  for (std::size_t i = 1; i + 1 < out.size(); ++i) {
    float p[3][2];
    float inv[3];
    const Vec3d* corner[3] = {&out[0], &out[i], &out[i + 1]};
    bool ok = true;
    for (int k = 0; k < 3; ++k) {
      ok = ok && cam.Project(*corner[k], &p[k][0], &p[k][1]);
      inv[k] = static_cast<float>(1.0 / corner[k]->z);
    }
    if (ok) {
      canvas.Triangle(p, inv, colour, clipped ? 0 : edges);
    }
  }
}

/// How far the furthest vertex is from the origin of the model
double BoundingRadius(const Model& model) {
  double largest = 0.0;
  for (const Vec3& v : model.vertices) {
    largest = std::max(largest, static_cast<double>(v.x) * v.x + static_cast<double>(v.y) * v.y + static_cast<double>(v.z) * v.z);
  }
  return std::sqrt(largest);
}

Rgb Highlight(Rgb c, float amount) {
  const auto add = [amount](std::uint8_t v) { return static_cast<std::uint8_t>(std::min(255.0f, static_cast<float>(v) + 255.0f * amount)); };
  return {add(c.r), add(c.g), add(c.b)};
}

}  // namespace

void DrawModel(Canvas& canvas, const Camera& cam, const Model& model, const Transform& transform, const Lighting& lighting, Rgb distant) {
  const Vec3d centre = cam.ToCamera(transform.position);
  const double scale = transform.scale;
  const double radius = BoundingRadius(model) * scale;
  if (centre.z < -radius) {
    return;
  }
  const double dist = std::max(Length(centre), 1.0);
  if (cam.focal * radius / dist < 1.6) {
    // Too far to see as a shape: a dot
    float sx = 0.0f;
    float sy = 0.0f;
    if (cam.Project(centre, &sx, &sy) && sx >= 0.0f && sy >= 0.0f) {
      const float limit = static_cast<float>(std::max(canvas.width(), canvas.height()));
      if (sx < limit && sy < limit) {
        const int ix = static_cast<int>(sx);
        const int iy = static_cast<int>(sy);
        if (canvas.InClip(ix, iy) && canvas.depth(ix, iy) < static_cast<float>(1.0 / dist)) {
          canvas.Set(ix, iy, distant);
        }
      }
    }
    return;
  }
  const Vec3d light = Rotate(Conjugate(cam.orientation), Normalized(lighting.direction));
  const Quat r = Conjugate(cam.orientation) * transform.orientation;
  std::vector<Vec3d> verts(model.vertices.size());
  for (std::size_t i = 0; i < verts.size(); ++i) {
    const Vec3& v = model.vertices[i];
    verts[i] = Rotate(r, Vec3d{v.x, v.y, v.z} * scale) + centre;
  }
  for (const Face& face : model.faces) {
    const Vec3d& a = verts[face.v[0]];
    const Vec3d& b = verts[face.v[1]];
    const Vec3d& c = verts[face.v[2]];
    Vec3d n = Cross(b - a, c - a);
    const double length = Length(n);
    if (length < 1e-9) {
      continue;
    }
    n = n * (1.0 / length);
    const Vec3d mid = (a + b + c) * (1.0 / 3.0);
    const Vec3d face_centre = Rotate(r, Vec3d{face.centre.x, face.centre.y, face.centre.z} * scale) + centre;
    if (Dot(n, mid - face_centre) < 0.0) {
      n = -n;
    }
    // Faces turned away from the eye are not drawn
    if (Dot(n, mid) >= 0.0) {
      continue;
    }
    Rgb colour = face.colour;
    if (!face.emissive) {
      const float lit = lighting.ambient + lighting.diffuse * static_cast<float>(std::max(0.0, Dot(n, light)));
      colour = Scale(colour, lit);
      if (lighting.specular > 0.0f) {
        const Vec3d half = Normalized(light - Normalized(mid));
        colour = Highlight(colour, lighting.specular * static_cast<float>(std::pow(std::max(0.0, Dot(n, half)), lighting.shininess)));
      }
    }
    FillViewPolygon(canvas, cam, {a, b, c}, colour, face.edges);
  }
}

void FillPolygon(Canvas& canvas, const Camera& cam, const std::vector<Vec3d>& polygon, Rgb colour, std::uint8_t edges) {
  std::vector<Vec3d> view;
  view.reserve(polygon.size());
  for (const Vec3d& p : polygon) {
    view.push_back(cam.ToCamera(p));
  }
  FillViewPolygon(canvas, cam, view, colour, edges);
}

void DrawLine3D(Canvas& canvas, const Camera& cam, const Vec3d& a, const Vec3d& b, Rgb colour, bool additive) {
  Vec3d p = cam.ToCamera(a);
  Vec3d q = cam.ToCamera(b);
  const double near_plane = cam.near_plane;
  if (p.z < near_plane && q.z < near_plane) {
    return;
  }
  if (p.z < near_plane || q.z < near_plane) {
    const double t = (near_plane - p.z) / (q.z - p.z);
    Vec3d cut = p + (q - p) * t;
    cut.z = near_plane;
    (p.z < near_plane ? p : q) = cut;
  }
  float x0 = 0.0f;
  float y0 = 0.0f;
  float x1 = 0.0f;
  float y1 = 0.0f;
  if (cam.Project(p, &x0, &y0) && cam.Project(q, &x1, &y1)) {
    canvas.DepthLine(x0, y0, static_cast<float>(1.0 / p.z), x1, y1, static_cast<float>(1.0 / q.z), colour, additive);
  }
}

void DrawTexturedQuad(Canvas& canvas, const Camera& cam, const Vec3d& a, const Vec3d& b, const Vec3d& c, const Vec3d& d, const Image& texture) {
  struct Corner {
    Vec3d view;
    float u;
    float v;
  };
  const Corner corners[4] = {{cam.ToCamera(a), 0.0f, 1.0f}, {cam.ToCamera(b), 1.0f, 1.0f}, {cam.ToCamera(c), 1.0f, 0.0f}, {cam.ToCamera(d), 0.0f, 0.0f}};
  // Cut off at the near plane, carrying the picture's coordinates along
  std::vector<Corner> out;
  for (int i = 0; i < 4; ++i) {
    const Corner& s = corners[i];
    const Corner& e = corners[(i + 1) % 4];
    const bool s_in = s.view.z >= cam.near_plane;
    const bool e_in = e.view.z >= cam.near_plane;
    if (s_in) {
      out.push_back(s);
    }
    if (s_in != e_in) {
      const double t = (cam.near_plane - s.view.z) / (e.view.z - s.view.z);
      Corner cut{s.view + (e.view - s.view) * t, 0.0f, 0.0f};
      cut.view.z = cam.near_plane;
      // The coordinates are linear in space, and the near plane is a flat cut, so this is correct in perspective
      cut.u = s.u + (e.u - s.u) * static_cast<float>(t);
      cut.v = s.v + (e.v - s.v) * static_cast<float>(t);
      out.push_back(cut);
    }
  }
  if (out.size() < 3) {
    return;
  }
  std::vector<float> sx(out.size());
  std::vector<float> sy(out.size());
  for (std::size_t i = 0; i < out.size(); ++i) {
    if (!cam.Project(out[i].view, &sx[i], &sy[i])) {
      return;
    }
  }
  const float screen_width = std::hypot(sx[1] - sx[0], sy[1] - sy[0]);
  const float minify = static_cast<float>(texture.width()) / std::max(1.0f, screen_width);
  for (std::size_t i = 1; i + 1 < out.size(); ++i) {
    const std::size_t k[3] = {0, i, i + 1};
    float p[3][2];
    float inv[3];
    float uv[3][2];
    for (int j = 0; j < 3; ++j) {
      p[j][0] = sx[k[j]];
      p[j][1] = sy[k[j]];
      inv[j] = static_cast<float>(1.0 / out[k[j]].view.z);
      uv[j][0] = out[k[j]].u;
      uv[j][1] = out[k[j]].v;
    }
    canvas.TexturedTriangle(p, inv, uv, texture, minify);
  }
}

}  // namespace softrender
