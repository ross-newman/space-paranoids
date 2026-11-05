#include "coriolis/obj.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <map>
#include <sstream>

#include "coriolis/model_builder.h"

namespace coriolis {

namespace {

constexpr std::uintmax_t kMaxFileBytes = 64U * 1024U * 1024U;

struct Material {
  Rgb colour{184, 186, 196};
  bool emissive = false;
};

std::uint8_t Channel(float v) {
  return static_cast<std::uint8_t>(std::lround(std::fmin(1.0f, std::fmax(0.0f, v)) * 255.0f));
}

bool Fail(std::string* error, const std::string& message) {
  if (error != nullptr) {
    *error = message;
  }
  return false;
}

std::string TrimmedRest(std::istringstream& line) {
  std::string rest;
  std::getline(line, rest);
  const std::size_t first = rest.find_first_not_of(" \t\r");
  if (first == std::string::npos) {
    return "";
  }
  return rest.substr(first, rest.find_last_not_of(" \t\r") - first + 1);
}

/// Kd and Ke by material name. A missing or unreadable library just leaves every face the default grey.
std::map<std::string, Material> ReadMaterials(const std::string& path) {
  std::map<std::string, Material> materials;
  std::ifstream in(path);
  if (!in) {
    return materials;
  }
  std::string text;
  Material* current = nullptr;
  while (std::getline(in, text)) {
    std::istringstream line(text);
    std::string key;
    line >> key;
    if (key == "newmtl") {
      current = &materials[TrimmedRest(line)];
    } else if (current != nullptr && key == "Kd") {
      float r = 0.0f;
      float g = 0.0f;
      float b = 0.0f;
      if (line >> r >> g >> b) {
        current->colour = {Channel(r), Channel(g), Channel(b)};
      }
    } else if (current != nullptr && key == "Ke") {
      float r = 0.0f;
      float g = 0.0f;
      float b = 0.0f;
      if (line >> r >> g >> b) {
        current->emissive = r > 0.5f || g > 0.5f || b > 0.5f;
        if (current->emissive) {
          current->colour = {Channel(r), Channel(g), Channel(b)};
        }
      }
    }
  }
  return materials;
}

/// The leading vertex index of a face corner (v, v/t, v//n or v/t/n), made absolute, or -1 if it is not valid
long VertexIndex(const std::string& corner, std::size_t vertex_count) {
  char* end = nullptr;
  const long raw = std::strtol(corner.c_str(), &end, 10);
  if (end == corner.c_str() || raw == 0) {
    return -1;
  }
  const long index = raw > 0 ? raw - 1 : static_cast<long>(vertex_count) + raw;
  if (index < 0 || index >= static_cast<long>(vertex_count)) {
    return -1;
  }
  return index;
}

}  // namespace

bool ParseObj(const std::string& text, const std::string& directory, const ObjOptions& options, Model* model,
              std::string* error) {
  if (model == nullptr) {
    return Fail(error, "no model to write to");
  }
  std::vector<Vec3> positions;
  std::map<std::string, Material> materials;
  Material material;
  ModelBuilder builder(options.name.empty() ? "OBJ MODEL" : options.name, options.spin);
  std::vector<long> mapped;  // OBJ vertex -> builder vertex, or -1 while unused
  std::size_t faces = 0;

  const auto vertex = [&](std::size_t i) {
    if (mapped.size() <= i) {
      mapped.resize(positions.size(), -1);
    }
    if (mapped[i] < 0) {
      mapped[i] = static_cast<long>(builder.Vertex(positions[i].x, positions[i].y, positions[i].z));
    }
    return static_cast<std::size_t>(mapped[i]);
  };

  std::istringstream in(text);
  std::string raw;
  std::size_t line_number = 0;
  while (std::getline(in, raw)) {
    ++line_number;
    std::istringstream line(raw);
    std::string key;
    line >> key;
    if (key == "v") {
      float x = 0.0f;
      float y = 0.0f;
      float z = 0.0f;
      if (!(line >> x >> y >> z) || !std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z)) {
        return Fail(error, "line " + std::to_string(line_number) + ": bad vertex");
      }
      positions.push_back({x, y, options.flip_z ? -z : z});
    } else if (key == "mtllib" && !directory.empty()) {
      const std::string file = TrimmedRest(line);
      // Only a library next to the model: a name that climbs out of the directory is not followed
      if (!file.empty() && file.find("..") == std::string::npos && file.front() != '/') {
        for (const auto& entry : ReadMaterials(directory + "/" + file)) {
          materials[entry.first] = entry.second;
        }
      }
    } else if (key == "usemtl") {
      const auto found = materials.find(TrimmedRest(line));
      material = found != materials.end() ? found->second : Material{};
    } else if (key == "f") {
      std::vector<std::size_t> corners;
      std::string corner;
      while (line >> corner) {
        const long index = VertexIndex(corner, positions.size());
        if (index < 0) {
          return Fail(error, "line " + std::to_string(line_number) + ": bad face index \"" + corner + "\"");
        }
        corners.push_back(static_cast<std::size_t>(index));
      }
      if (corners.size() < 3) {
        return Fail(error, "line " + std::to_string(line_number) + ": a face needs three corners");
      }
      // The winding of a right handed file is the wrong way round once z is negated
      if (options.flip_z != options.flip_winding) {
        std::reverse(corners.begin(), corners.end());
      }
      const std::size_t n = corners.size();
      for (std::size_t i = 1; i + 1 < n; ++i) {
        const Vec3& a = positions[corners[0]];
        const Vec3& b = positions[corners[i]];
        const Vec3& c = positions[corners[i + 1]];
        const Vec3 normal{(b.y - a.y) * (c.z - a.z) - (b.z - a.z) * (c.y - a.y),
                          (b.z - a.z) * (c.x - a.x) - (b.x - a.x) * (c.z - a.z),
                          (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x)};
        const float length = std::sqrt(normal.x * normal.x + normal.y * normal.y + normal.z * normal.z);
        if (length < 1e-12f) {
          continue;  // a sliver with no area
        }
        // The renderer points a normal away from the face's centre, so put the centre just behind the face along the
        // normal that the winding gives: the winding then decides which side is the front.
        const Vec3 middle{(a.x + b.x + c.x) / 3.0f, (a.y + b.y + c.y) / 3.0f, (a.z + b.z + c.z) / 3.0f};
        builder.Centre(middle.x - normal.x / length, middle.y - normal.y / length, middle.z - normal.z / length);
        // As ModelBuilder::Poly does: outline the edge of the polygon, not the cuts across it
        std::uint8_t edges = n == 3 ? 0x7 : 0x2;
        if (n > 3 && i == 1) {
          edges |= 0x1;
        }
        if (n > 3 && i + 2 == n) {
          edges |= 0x4;
        }
        builder.Tri(vertex(corners[0]), vertex(corners[i]), vertex(corners[i + 1]), material.colour, material.emissive,
                    edges);
        ++faces;
      }
    }
  }
  if (faces == 0) {
    return Fail(error, "no faces");
  }
  *model = builder.Finish(3.2f);
  return true;
}

bool LoadObj(const std::string& path, const ObjOptions& options, Model* model, std::string* error) {
  std::ifstream in(path, std::ios::binary | std::ios::ate);
  if (!in) {
    return Fail(error, "cannot open " + path);
  }
  const std::streamoff size = in.tellg();
  if (size < 0 || static_cast<std::uintmax_t>(size) > kMaxFileBytes) {
    return Fail(error, path + " is too big");
  }
  in.seekg(0);
  std::string text(static_cast<std::size_t>(size), '\0');
  if (!in.read(&text[0], size)) {
    return Fail(error, "cannot read " + path);
  }
  const std::size_t slash = path.find_last_of('/');
  const std::string directory = slash == std::string::npos ? "." : path.substr(0, slash);
  ObjOptions named = options;
  if (named.name.empty()) {
    std::string base = slash == std::string::npos ? path : path.substr(slash + 1);
    const std::size_t dot = base.find_last_of('.');
    if (dot != std::string::npos && dot > 0) {
      base.resize(dot);
    }
    for (char& c : base) {
      c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    named.name = base;
  }
  return ParseObj(text, directory, named, model, error);
}

}  // namespace coriolis
