#include "coriolis/json.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <utility>

namespace coriolis {

Json Json::Bool(bool value) {
  Json j;
  j.type_ = Type::kBool;
  j.bool_ = value;
  return j;
}

Json Json::Number(double value) {
  Json j;
  j.type_ = Type::kNumber;
  j.number_ = value;
  return j;
}

Json Json::String(std::string value) {
  Json j;
  j.type_ = Type::kString;
  j.string_ = std::move(value);
  return j;
}

Json Json::MakeArray() {
  Json j;
  j.type_ = Type::kArray;
  return j;
}

Json Json::MakeObject() {
  Json j;
  j.type_ = Type::kObject;
  return j;
}

void Json::Push(Json value) { items_.push_back(std::move(value)); }

void Json::Set(const std::string& key, Json value) {
  for (std::size_t i = 0; i < keys_.size(); ++i) {
    if (keys_[i] == key) {
      items_[i] = std::move(value);
      return;
    }
  }
  keys_.push_back(key);
  items_.push_back(std::move(value));
}

const Json* Json::Find(const std::string& key) const {
  for (std::size_t i = 0; i < keys_.size(); ++i) {
    if (keys_[i] == key) {
      return &items_[i];
    }
  }
  return nullptr;
}

namespace {

void AppendString(std::string& out, const std::string& s) {
  out += '"';
  for (const char c : s) {
    switch (c) {
      case '"':
        out += "\\\"";
        break;
      case '\\':
        out += "\\\\";
        break;
      case '\n':
        out += "\\n";
        break;
      case '\r':
        out += "\\r";
        break;
      case '\t':
        out += "\\t";
        break;
      default:
        if (static_cast<unsigned char>(c) < 0x20) {
          char buffer[8];
          std::snprintf(buffer, sizeof buffer, "\\u%04x", static_cast<unsigned>(static_cast<unsigned char>(c)));
          out += buffer;
        } else {
          out += c;
        }
    }
  }
  out += '"';
}

/// The shortest text that reads back as exactly the same double
void AppendNumber(std::string& out, double value) {
  if (!std::isfinite(value)) {
    out += "null";
    return;
  }
  char buffer[40];
  for (int precision = 1; precision <= 17; ++precision) {
    std::snprintf(buffer, sizeof buffer, "%.*g", precision, value);
    if (std::strtod(buffer, nullptr) == value) {
      break;
    }
  }
  out += buffer;
}

bool IsContainer(const Json& j) { return j.is_array() || j.is_object(); }

bool HoldsOnlyPlainValues(const Json& j) {
  for (const Json& item : j.items()) {
    if (IsContainer(item)) {
      return false;
    }
  }
  return true;
}

void Write(std::string& out, const Json& j, int depth) {
  switch (j.type()) {
    case Json::Type::kNull:
      out += "null";
      return;
    case Json::Type::kBool:
      out += j.bool_value() ? "true" : "false";
      return;
    case Json::Type::kNumber:
      AppendNumber(out, j.number_value());
      return;
    case Json::Type::kString:
      AppendString(out, j.string_value());
      return;
    case Json::Type::kArray:
    case Json::Type::kObject:
      break;
  }

  const bool object = j.is_object();
  if (j.items().empty()) {
    out += object ? "{}" : "[]";
    return;
  }
  const bool one_line = HoldsOnlyPlainValues(j);
  out += object ? '{' : '[';
  for (std::size_t i = 0; i < j.items().size(); ++i) {
    if (i > 0) {
      out += one_line ? ", " : ",";
    }
    if (!one_line) {
      out += '\n';
      out.append(static_cast<std::size_t>(depth + 1) * 2, ' ');
    }
    if (object) {
      AppendString(out, j.keys()[i]);
      out += ": ";
    }
    Write(out, j.items()[i], depth + 1);
  }
  if (!one_line) {
    out += '\n';
    out.append(static_cast<std::size_t>(depth) * 2, ' ');
  }
  out += object ? '}' : ']';
}

constexpr int kMaxDepth = 64;

class Parser {
 public:
  explicit Parser(const std::string& text) : text_(text) {}

  bool Run(Json* out, std::string* error) {
    SkipSpace();
    if (!ParseValue(out, 0)) {
      Report(error);
      return false;
    }
    SkipSpace();
    if (pos_ != text_.size()) {
      Fail("unexpected text after the value");
      Report(error);
      return false;
    }
    return true;
  }

 private:
  bool Fail(const char* message) {
    if (message_.empty()) {
      message_ = message;
      failed_at_ = pos_;
    }
    return false;
  }

  void Report(std::string* error) const {
    if (error == nullptr) {
      return;
    }
    std::size_t line = 1;
    std::size_t column = 1;
    for (std::size_t i = 0; i < failed_at_ && i < text_.size(); ++i) {
      if (text_[i] == '\n') {
        ++line;
        column = 1;
      } else {
        ++column;
      }
    }
    *error = "line " + std::to_string(line) + ", column " + std::to_string(column) + ": " + message_;
  }

  void SkipSpace() {
    while (pos_ < text_.size() &&
           (text_[pos_] == ' ' || text_[pos_] == '\t' || text_[pos_] == '\n' || text_[pos_] == '\r')) {
      ++pos_;
    }
  }

  bool Match(const char* word) {
    std::size_t i = 0;
    while (word[i] != '\0') {
      if (pos_ + i >= text_.size() || text_[pos_ + i] != word[i]) {
        return false;
      }
      ++i;
    }
    pos_ += i;
    return true;
  }

  bool ParseValue(Json* out, int depth) {
    if (depth > kMaxDepth) {
      return Fail("nested too deeply");
    }
    if (pos_ >= text_.size()) {
      return Fail("unexpected end of text");
    }
    const char c = text_[pos_];
    if (c == '{') {
      return ParseObject(out, depth);
    }
    if (c == '[') {
      return ParseArray(out, depth);
    }
    if (c == '"') {
      std::string s;
      if (!ParseString(&s)) {
        return false;
      }
      *out = Json::String(std::move(s));
      return true;
    }
    if (Match("true")) {
      *out = Json::Bool(true);
      return true;
    }
    if (Match("false")) {
      *out = Json::Bool(false);
      return true;
    }
    if (Match("null")) {
      *out = Json();
      return true;
    }
    if (c == '-' || (c >= '0' && c <= '9')) {
      return ParseNumber(out);
    }
    return Fail("unexpected character");
  }

  bool ParseNumber(Json* out) {
    const std::size_t start = pos_;
    if (text_[pos_] == '-') {
      ++pos_;
    }
    if (pos_ >= text_.size() || !IsDigit(text_[pos_])) {
      return Fail("bad number");
    }
    if (text_[pos_] == '0') {
      ++pos_;
    } else {
      while (pos_ < text_.size() && IsDigit(text_[pos_])) {
        ++pos_;
      }
    }
    if (pos_ < text_.size() && text_[pos_] == '.') {
      ++pos_;
      if (pos_ >= text_.size() || !IsDigit(text_[pos_])) {
        return Fail("bad number");
      }
      while (pos_ < text_.size() && IsDigit(text_[pos_])) {
        ++pos_;
      }
    }
    if (pos_ < text_.size() && (text_[pos_] == 'e' || text_[pos_] == 'E')) {
      ++pos_;
      if (pos_ < text_.size() && (text_[pos_] == '+' || text_[pos_] == '-')) {
        ++pos_;
      }
      if (pos_ >= text_.size() || !IsDigit(text_[pos_])) {
        return Fail("bad number");
      }
      while (pos_ < text_.size() && IsDigit(text_[pos_])) {
        ++pos_;
      }
    }
    *out = Json::Number(std::strtod(text_.substr(start, pos_ - start).c_str(), nullptr));
    return true;
  }

  static bool IsDigit(char c) { return c >= '0' && c <= '9'; }

  bool ParseHex4(unsigned* value) {
    if (pos_ + 4 > text_.size()) {
      return Fail("bad \\u escape");
    }
    unsigned v = 0;
    for (int i = 0; i < 4; ++i) {
      const char c = text_[pos_ + static_cast<std::size_t>(i)];
      v <<= 4;
      if (c >= '0' && c <= '9') {
        v |= static_cast<unsigned>(c - '0');
      } else if (c >= 'a' && c <= 'f') {
        v |= static_cast<unsigned>(c - 'a' + 10);
      } else if (c >= 'A' && c <= 'F') {
        v |= static_cast<unsigned>(c - 'A' + 10);
      } else {
        return Fail("bad \\u escape");
      }
    }
    pos_ += 4;
    *value = v;
    return true;
  }

  static void AppendUtf8(std::string& out, unsigned code) {
    if (code < 0x80) {
      out += static_cast<char>(code);
    } else if (code < 0x800) {
      out += static_cast<char>(0xC0 | (code >> 6));
      out += static_cast<char>(0x80 | (code & 0x3F));
    } else if (code < 0x10000) {
      out += static_cast<char>(0xE0 | (code >> 12));
      out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
      out += static_cast<char>(0x80 | (code & 0x3F));
    } else {
      out += static_cast<char>(0xF0 | (code >> 18));
      out += static_cast<char>(0x80 | ((code >> 12) & 0x3F));
      out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
      out += static_cast<char>(0x80 | (code & 0x3F));
    }
  }

  bool ParseString(std::string* out) {
    ++pos_;  // opening quote
    while (true) {
      if (pos_ >= text_.size()) {
        return Fail("unterminated string");
      }
      const char c = text_[pos_++];
      if (c == '"') {
        return true;
      }
      if (static_cast<unsigned char>(c) < 0x20) {
        --pos_;
        return Fail("control character in string");
      }
      if (c != '\\') {
        *out += c;
        continue;
      }
      if (pos_ >= text_.size()) {
        return Fail("unterminated string");
      }
      const char e = text_[pos_++];
      switch (e) {
        case '"':
        case '\\':
        case '/':
          *out += e;
          break;
        case 'b':
          *out += '\b';
          break;
        case 'f':
          *out += '\f';
          break;
        case 'n':
          *out += '\n';
          break;
        case 'r':
          *out += '\r';
          break;
        case 't':
          *out += '\t';
          break;
        case 'u': {
          unsigned code = 0;
          if (!ParseHex4(&code)) {
            return false;
          }
          if (code >= 0xD800 && code <= 0xDBFF) {
            unsigned low = 0;
            if (!Match("\\u") || !ParseHex4(&low) || low < 0xDC00 || low > 0xDFFF) {
              return Fail("bad surrogate pair");
            }
            code = 0x10000 + ((code - 0xD800) << 10) + (low - 0xDC00);
          } else if (code >= 0xDC00 && code <= 0xDFFF) {
            return Fail("bad surrogate pair");
          }
          AppendUtf8(*out, code);
          break;
        }
        default:
          --pos_;
          return Fail("bad escape");
      }
    }
  }

  bool ParseArray(Json* out, int depth) {
    ++pos_;  // [
    *out = Json::MakeArray();
    SkipSpace();
    if (pos_ < text_.size() && text_[pos_] == ']') {
      ++pos_;
      return true;
    }
    while (true) {
      SkipSpace();
      Json item;
      if (!ParseValue(&item, depth + 1)) {
        return false;
      }
      out->Push(std::move(item));
      SkipSpace();
      if (pos_ >= text_.size()) {
        return Fail("unterminated array");
      }
      if (text_[pos_] == ',') {
        ++pos_;
        continue;
      }
      if (text_[pos_] == ']') {
        ++pos_;
        return true;
      }
      return Fail("expected , or ]");
    }
  }

  bool ParseObject(Json* out, int depth) {
    ++pos_;  // {
    *out = Json::MakeObject();
    SkipSpace();
    if (pos_ < text_.size() && text_[pos_] == '}') {
      ++pos_;
      return true;
    }
    while (true) {
      SkipSpace();
      if (pos_ >= text_.size() || text_[pos_] != '"') {
        return Fail("expected a member name");
      }
      std::string key;
      if (!ParseString(&key)) {
        return false;
      }
      SkipSpace();
      if (pos_ >= text_.size() || text_[pos_] != ':') {
        return Fail("expected :");
      }
      ++pos_;
      SkipSpace();
      Json value;
      if (!ParseValue(&value, depth + 1)) {
        return false;
      }
      out->Set(key, std::move(value));
      SkipSpace();
      if (pos_ >= text_.size()) {
        return Fail("unterminated object");
      }
      if (text_[pos_] == ',') {
        ++pos_;
        continue;
      }
      if (text_[pos_] == '}') {
        ++pos_;
        return true;
      }
      return Fail("expected , or }");
    }
  }

  const std::string& text_;
  std::size_t pos_ = 0;
  std::size_t failed_at_ = 0;
  std::string message_;
};

}  // namespace

std::string Json::Dump() const {
  std::string out;
  Write(out, *this, 0);
  out += '\n';
  return out;
}

bool Json::Parse(const std::string& text, Json* out, std::string* error) { return Parser(text).Run(out, error); }

}  // namespace coriolis
