#ifndef SOFTRENDER_PNG_H_
#define SOFTRENDER_PNG_H_

#include <string>

#include "softrender/types.h"

namespace softrender {

/// Write an image as a PNG file (uncompressed, so no dependencies)
///
/// \return true if the file was written
bool WritePng(const Image& image, const std::string& path);

}  // namespace softrender

#endif  // SOFTRENDER_PNG_H_
