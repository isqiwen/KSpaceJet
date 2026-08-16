#include "kspacejet/recon/graph/canonical_json.hpp"

#include <nlohmann/json.hpp>

#include <array>
#include <bit>
#include <cstdint>
#include <iomanip>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace ksj::recon::graph {
namespace {

using Json = nlohmann::json;

[[nodiscard]] Status validate_parse_limits(const JsonParseLimits& limits) {
  if (limits.max_document_bytes == 0U || limits.max_depth == 0U || limits.max_array_elements == 0U ||
      limits.max_object_members == 0U || limits.max_string_bytes == 0U) {
    return Status::InvalidArgument("all JSON parse limits must be greater than zero");
  }
  return Status::Ok();
}

// nlohmann::json's normal DOM parser intentionally accepts duplicate object
// keys and retains a single value.  Artifact identity cannot be based on that
// ambiguous representation, so build the DOM through a small SAX adapter that
// performs all admission bounds and duplicate checks before a value is stored.
class BoundedJsonSax final : public nlohmann::json_sax<Json> {
public:
  explicit BoundedJsonSax(const JsonParseLimits& limits) : limits_(limits) {}

  [[nodiscard]] const Status& status() const noexcept { return status_; }
  [[nodiscard]] bool has_root() const noexcept { return root_.has_value(); }
  [[nodiscard]] Json take_root() { return std::move(root_).value(); }

  bool null() override { return append_value(Json(nullptr)); }
  bool boolean(const bool value) override { return append_value(Json(value)); }

  bool number_integer(const number_integer_t value) override {
    if (value < -static_cast<number_integer_t>(kMaxCanonicalJsonInteger) ||
        value > static_cast<number_integer_t>(kMaxCanonicalJsonInteger)) {
      return fail_validation("JSON integer exceeds the exact current range");
    }
    return append_value(Json(value));
  }

  bool number_unsigned(const number_unsigned_t value) override {
    if (value > static_cast<number_unsigned_t>(kMaxCanonicalJsonInteger)) {
      return fail_validation("JSON integer exceeds the exact current range");
    }
    return append_value(Json(value));
  }

  bool number_float(number_float_t /*value*/, const string_t& /*raw*/) override {
    return fail_validation("floating-point JSON values are not permitted in current artifacts");
  }

  bool string(string_t& value) override {
    if (value.size() > limits_.max_string_bytes) {
      return fail_validation("JSON string exceeds the configured maximum length");
    }
    return append_value(Json(std::move(value)));
  }

  bool binary(binary_t& /*value*/) override {
    return fail_validation("binary JSON values are not permitted in textual current artifacts");
  }

  bool start_object(const std::size_t elements) override {
    if (elements != static_cast<std::size_t>(-1) && elements > limits_.max_object_members) {
      return fail_validation("JSON object exceeds the configured member limit");
    }
    return start_container(Json::value_t::object);
  }

  bool key(string_t& value) override {
    if (frames_.empty() || !frames_.back().value.is_object()) {
      return fail_parse("JSON object key appeared outside an object");
    }
    if (value.size() > limits_.max_string_bytes) {
      return fail_validation("JSON object key exceeds the configured maximum length");
    }

    auto& frame = frames_.back();
    if (frame.pending_key.has_value()) {
      return fail_parse("JSON object key was not followed by a value");
    }
    if (frame.member_count == limits_.max_object_members) {
      return fail_validation("JSON object exceeds the configured member limit");
    }

    std::string key_value = std::move(value);
    if (!frame.keys.insert(key_value).second) {
      return fail_validation("JSON object contains a duplicate key: '" + key_value + "'");
    }
    ++frame.member_count;
    frame.pending_key = std::move(key_value);
    return true;
  }

  bool end_object() override { return finish_container(Json::value_t::object); }

  bool start_array(const std::size_t elements) override {
    if (elements != static_cast<std::size_t>(-1) && elements > limits_.max_array_elements) {
      return fail_validation("JSON array exceeds the configured element limit");
    }
    return start_container(Json::value_t::array);
  }

  bool end_array() override { return finish_container(Json::value_t::array); }

  bool parse_error(const std::size_t position, const std::string& /*last_token*/,
                   const nlohmann::detail::exception& exception) override {
    if (status_.ok()) {
      status_ = Status::ParseError("invalid JSON artifact at byte " + std::to_string(position) + ": " +
                                   std::string(exception.what()));
    }
    return false;
  }

private:
  struct Frame {
    Json value;
    std::unordered_set<std::string> keys;
    std::optional<std::string> pending_key;
    std::size_t member_count{0};
    std::size_t element_count{0};
  };

  bool fail_validation(std::string message) {
    if (status_.ok()) {
      status_ = Status::ValidationError(std::move(message));
    }
    return false;
  }

  bool fail_parse(std::string message) {
    if (status_.ok()) {
      status_ = Status::ParseError(std::move(message));
    }
    return false;
  }

  bool start_container(const Json::value_t kind) {
    if (frames_.size() == limits_.max_depth) {
      return fail_validation("JSON nesting exceeds the configured maximum depth");
    }
    Frame frame;
    frame.value = Json(kind);
    frames_.push_back(std::move(frame));
    return true;
  }

  bool finish_container(const Json::value_t expected_kind) {
    if (frames_.empty() || frames_.back().value.type() != expected_kind) {
      return fail_parse("JSON container close does not match its opener");
    }
    if (frames_.back().pending_key.has_value()) {
      return fail_parse("JSON object ended before a member value");
    }

    Json value = std::move(frames_.back().value);
    frames_.pop_back();
    return append_value(std::move(value));
  }

  bool append_value(Json value) {
    if (frames_.empty()) {
      if (root_.has_value()) {
        return fail_parse("JSON document contains more than one root value");
      }
      root_ = std::move(value);
      return true;
    }

    auto& parent = frames_.back();
    if (parent.value.is_array()) {
      if (parent.element_count == limits_.max_array_elements) {
        return fail_validation("JSON array exceeds the configured element limit");
      }
      ++parent.element_count;
      parent.value.push_back(std::move(value));
      return true;
    }
    if (!parent.value.is_object() || !parent.pending_key.has_value()) {
      return fail_parse("JSON object value has no preceding key");
    }

    parent.value.emplace(std::move(parent.pending_key).value(), std::move(value));
    parent.pending_key.reset();
    return true;
  }

  const JsonParseLimits& limits_;
  std::vector<Frame> frames_;
  std::optional<Json> root_;
  Status status_;
};

[[nodiscard]] Result<Json> parse_bounded_json(const std::string_view document, const JsonParseLimits& limits) {
  const auto limits_status = validate_parse_limits(limits);
  if (!limits_status.ok()) {
    return limits_status;
  }
  if (document.size() > limits.max_document_bytes) {
    return Status::ValidationError("JSON document exceeds the configured maximum byte size");
  }

  BoundedJsonSax sax(limits);
  try {
    const auto parsed =
      Json::sax_parse(document.begin(), document.end(), &sax, Json::input_format_t::json, true, false);
    if (!parsed || !sax.status().ok()) {
      return sax.status().ok() ? Status::ParseError("invalid JSON artifact") : sax.status();
    }
  } catch (const Json::exception& exception) {
    return Status::ParseError("invalid JSON artifact: " + std::string(exception.what()));
  }
  if (!sax.has_root()) {
    return Status::ParseError("JSON document contains no root value");
  }
  return sax.take_root();
}

[[nodiscard]] Result<Json> canonicalize_value(const Json& value, const std::string_view path) {
  if (value.is_null() || value.is_boolean() || value.is_string()) {
    return value;
  }
  if (value.is_number_unsigned()) {
    const auto number = value.get<Quantity>();
    if (number > kMaxCanonicalJsonInteger) {
      return ksj::base::Status::ValidationError(std::string(path) + " exceeds the current canonical integer range");
    }
    return Json(number);
  }
  if (value.is_number_integer()) {
    const auto number = value.get<std::int64_t>();
    if (number < -static_cast<std::int64_t>(kMaxCanonicalJsonInteger) ||
        number > static_cast<std::int64_t>(kMaxCanonicalJsonInteger)) {
      return ksj::base::Status::ValidationError(std::string(path) + " exceeds the current canonical integer range");
    }
    return Json(number);
  }
  if (value.is_number_float()) {
    return ksj::base::Status::ValidationError(
      std::string(path) + " is a floating-point JSON value; current artifacts require exact values");
  }
  if (value.is_array()) {
    Json result = Json::array();
    for (std::size_t index = 0; index < value.size(); ++index) {
      auto element = canonicalize_value(value[index], std::string(path) + "[" + std::to_string(index) + "]");
      if (!element.ok()) {
        return element.status();
      }
      result.push_back(std::move(element).value());
    }
    return result;
  }
  if (value.is_object()) {
    Json result = Json::object();
    // nlohmann::json uses a sorted object container by default.  Rebuilding
    // through it makes the serialized bytes independent of input member order.
    for (auto iterator = value.begin(); iterator != value.end(); ++iterator) {
      auto member = canonicalize_value(iterator.value(), std::string(path) + "." + iterator.key());
      if (!member.ok()) {
        return member.status();
      }
      result[iterator.key()] = std::move(member).value();
    }
    return result;
  }
  return ksj::base::Status::ValidationError(std::string(path) + " has an unsupported JSON value kind");
}

constexpr std::array<std::uint32_t, 64> kRoundConstants{
  0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U, 0x3956c25bU, 0x59f111f1U, 0x923f82a4U, 0xab1c5ed5U,
  0xd807aa98U, 0x12835b01U, 0x243185beU, 0x550c7dc3U, 0x72be5d74U, 0x80deb1feU, 0x9bdc06a7U, 0xc19bf174U,
  0xe49b69c1U, 0xefbe4786U, 0x0fc19dc6U, 0x240ca1ccU, 0x2de92c6fU, 0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU,
  0x983e5152U, 0xa831c66dU, 0xb00327c8U, 0xbf597fc7U, 0xc6e00bf3U, 0xd5a79147U, 0x06ca6351U, 0x14292967U,
  0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU, 0x53380d13U, 0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U,
  0xa2bfe8a1U, 0xa81a664bU, 0xc24b8b70U, 0xc76c51a3U, 0xd192e819U, 0xd6990624U, 0xf40e3585U, 0x106aa070U,
  0x19a4c116U, 0x1e376c08U, 0x2748774cU, 0x34b0bcb5U, 0x391c0cb3U, 0x4ed8aa4aU, 0x5b9cca4fU, 0x682e6ff3U,
  0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U, 0x90befffaU, 0xa4506cebU, 0xbef9a3f7U, 0xc67178f2U,
};

[[nodiscard]] constexpr std::uint32_t rotr(const std::uint32_t value, const unsigned int shift) noexcept {
  return std::rotr(value, shift);
}

void transform(std::array<std::uint32_t, 8>& state, const std::uint8_t* block) {
  std::array<std::uint32_t, 64> words{};
  for (std::size_t index = 0; index < 16; ++index) {
    const auto offset = index * 4U;
    words[index] =
      (static_cast<std::uint32_t>(block[offset]) << 24U) | (static_cast<std::uint32_t>(block[offset + 1U]) << 16U) |
      (static_cast<std::uint32_t>(block[offset + 2U]) << 8U) | static_cast<std::uint32_t>(block[offset + 3U]);
  }
  for (std::size_t index = 16; index < words.size(); ++index) {
    const auto sigma0 = rotr(words[index - 15U], 7U) ^ rotr(words[index - 15U], 18U) ^ (words[index - 15U] >> 3U);
    const auto sigma1 = rotr(words[index - 2U], 17U) ^ rotr(words[index - 2U], 19U) ^ (words[index - 2U] >> 10U);
    words[index] = words[index - 16U] + sigma0 + words[index - 7U] + sigma1;
  }

  auto a = state[0];
  auto b = state[1];
  auto c = state[2];
  auto d = state[3];
  auto e = state[4];
  auto f = state[5];
  auto g = state[6];
  auto h = state[7];
  for (std::size_t index = 0; index < words.size(); ++index) {
    const auto sigma1 = rotr(e, 6U) ^ rotr(e, 11U) ^ rotr(e, 25U);
    const auto choose = (e & f) ^ (~e & g);
    const auto temp1 = h + sigma1 + choose + kRoundConstants[index] + words[index];
    const auto sigma0 = rotr(a, 2U) ^ rotr(a, 13U) ^ rotr(a, 22U);
    const auto majority = (a & b) ^ (a & c) ^ (b & c);
    const auto temp2 = sigma0 + majority;
    h = g;
    g = f;
    f = e;
    e = d + temp1;
    d = c;
    c = b;
    b = a;
    a = temp1 + temp2;
  }
  state[0] += a;
  state[1] += b;
  state[2] += c;
  state[3] += d;
  state[4] += e;
  state[5] += f;
  state[6] += g;
  state[7] += h;
}

[[nodiscard]] std::array<std::uint8_t, 32> sha256_bytes(const std::string_view input) {
  std::array<std::uint32_t, 8> state{
    0x6a09e667U, 0xbb67ae85U, 0x3c6ef372U, 0xa54ff53aU, 0x510e527fU, 0x9b05688cU, 0x1f83d9abU, 0x5be0cd19U,
  };
  const auto* bytes = reinterpret_cast<const std::uint8_t*>(input.data());
  std::size_t offset = 0;
  while (input.size() - offset >= 64U) {
    transform(state, bytes + offset);
    offset += 64U;
  }
  std::array<std::uint8_t, 128> tail{};
  const auto remaining = input.size() - offset;
  for (std::size_t index = 0; index < remaining; ++index) {
    tail[index] = bytes[offset + index];
  }
  tail[remaining] = 0x80U;
  const auto padding_block_count = remaining >= 56U ? 2U : 1U;
  const auto bit_length = static_cast<std::uint64_t>(input.size()) * 8U;
  const auto length_offset = (padding_block_count * 64U) - 8U;
  for (std::size_t index = 0; index < 8U; ++index) {
    tail[length_offset + index] = static_cast<std::uint8_t>(bit_length >> ((7U - index) * 8U));
  }
  transform(state, tail.data());
  if (padding_block_count == 2U) {
    transform(state, tail.data() + 64U);
  }

  std::array<std::uint8_t, 32> digest{};
  for (std::size_t index = 0; index < state.size(); ++index) {
    const auto word = state[index];
    digest[index * 4U] = static_cast<std::uint8_t>(word >> 24U);
    digest[index * 4U + 1U] = static_cast<std::uint8_t>(word >> 16U);
    digest[index * 4U + 2U] = static_cast<std::uint8_t>(word >> 8U);
    digest[index * 4U + 3U] = static_cast<std::uint8_t>(word);
  }
  return digest;
}

[[nodiscard]] std::string hex_digest(const std::array<std::uint8_t, 32>& bytes) {
  std::ostringstream stream;
  stream << std::hex << std::setfill('0');
  for (const auto byte : bytes) {
    stream << std::setw(2) << static_cast<unsigned int>(byte);
  }
  return stream.str();
}

} // namespace

Result<std::string> canonicalize_json(const std::string_view document, const JsonParseLimits& limits) {
  auto parsed = parse_bounded_json(document, limits);
  if (!parsed.ok()) {
    return parsed.status();
  }
  auto canonical = canonicalize_value(parsed.value(), "$");
  if (!canonical.ok()) {
    return canonical.status();
  }
  try {
    return std::move(canonical).value().dump(-1, ' ', false, Json::error_handler_t::strict);
  } catch (const Json::exception& exception) {
    return ksj::base::Status::ValidationError("unable to serialize canonical JSON artifact: " +
                                              std::string(exception.what()));
  }
}

Result<ArtifactDigest> sha256_digest(const std::string_view canonical_document, const std::string_view field_name) {
  return ArtifactDigest::parse("sha256:" + hex_digest(sha256_bytes(canonical_document)), field_name);
}

Result<ArtifactDigest> domain_separated_sha256_digest(const std::string_view domain,
                                                      const std::string_view canonical_document,
                                                      const std::string_view field_name) {
  if (domain.empty()) {
    return ksj::base::Status::InvalidArgument("digest domain must not be empty");
  }
  std::string input;
  input.reserve(domain.size() + 1U + canonical_document.size());
  input.append(domain);
  input.push_back('\0');
  input.append(canonical_document);
  return sha256_digest(input, field_name);
}

Result<ArtifactDigest> canonical_json_digest(const std::string_view document, const std::string_view field_name) {
  auto canonical = canonicalize_json(document);
  if (!canonical.ok()) {
    return canonical.status();
  }
  return sha256_digest(canonical.value(), field_name);
}

} // namespace ksj::recon::graph
