// SPDX-License-Identifier: Apache-2.0
//
// A technical, bounded Cartesian Provider ABI v1 fixture.  It deliberately
// owns no runtime data plane: host-provided input, output grants, and scratch
// are the only callback-local memory capabilities used by process().

#include "kspacejet/provider/v1/provider.h"

#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <new>
#include <string_view>
#include <system_error>
#include <utility>

struct ksj_provider_operator;

struct ksj_execution_context {
  ksj_provider_operator* owner{nullptr};
};

struct ksj_key_state {
  ksj_provider_operator* owner{nullptr};
};

struct ksj_provider_operator {
  std::uint32_t rows{0U};
  std::uint32_t cols{0U};
  ksj_execution_context context{};
  ksj_key_state key_state{};
  bool context_active{false};
  bool key_state_active{false};
};

namespace {

constexpr char kProviderId[] = "org.kspacejet.minimal.cartesian";
constexpr char kOperatorId[] = "cartesian_ifft2_single_coil";
constexpr char kFrameTypeId[] = "ksj.kspace-frame";
constexpr char kImageTypeId[] = "ksj.image-frame";
constexpr char kDimensionChannel[] = "channel";
constexpr char kDimensionKy[] = "ky";
constexpr char kDimensionKx[] = "kx";

// These are detached identities for the ABI descriptors declared in the
// adjacent OperatorContract.  They are part of the frozen Provider boundary:
// accepting only a convenient subset of a descriptor would permit a host to
// relabel an incompatible payload as a frame or image.
constexpr char kFrameAbiDescriptorDigestHex[] = "cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc";
constexpr char kFramePayloadSchemaDigestHex[] = "7318daba9d4e9992d33ded54fcf8bd2db1ad9c501ca1bb4f30f351fcace94e9b";
constexpr char kFrameMetadataSchemaDigestHex[] = "2eb80e75da97288c839ca2c1d2c81e480f93c71739dd182a071e7b3145c72994";
constexpr char kImageAbiDescriptorDigestHex[] = "bc161b76c25315236dd5d01fc766635200c1033b7b795bb629d625746f843cbe";
constexpr char kImagePayloadSchemaDigestHex[] = "42fb021252293d7b2d5ba913d75a89d4c868e72c6a6c559dde6243d6b0c780fb";
constexpr char kImageMetadataSchemaDigestHex[] = "3f9bbd8144c338693445519780fb102144091b34c1bdf0d76ca529e7f453516b";

// These descriptor identities are intentionally fixed literals rather than
// test-pattern bytes.  A production bundle manifest must bind them to a
// signed/content-addressed bundle before strict-profile admission.
constexpr char kOperatorInterfaceDigestHex[] = "61d6559102d222b56dbe57309fd688ad75a25d7b5f6c83502798b91efb647735";
constexpr char kOperatorContractDigestHex[] = "c42136027e84e0e476a879ef8e765d7c59fba1a72112384be3ee33b767f1da1f";
constexpr char kProviderBundleDigestHex[] = "6fc3fb5999f676e000ece47b99e8048d8e9098d5f2ad05f86b9c81b18e75957f";

constexpr char kErrorBadAbi[] = "Minimal Cartesian Provider received incompatible ABI storage";
constexpr char kErrorInvalidArgument[] = "Minimal Cartesian Provider received an invalid lifecycle argument";
constexpr char kErrorUnsupportedOperator[] = "Minimal Cartesian Provider does not expose the requested operator";
constexpr char kErrorContractDigest[] = "Required contract digest does not match the minimal Cartesian operator";
constexpr char kErrorUnsupportedConfig[] =
  "Minimal Cartesian Provider requires canonical {\"cols\":N,\"rows\":N} power-of-two dimensions";
constexpr char kErrorLease[] = "Minimal Cartesian Provider requires one frame, one output grant, and host scratch";
constexpr char kErrorFrame[] = "Minimal Cartesian Provider requires one exact single-coil ksj.kspace-frame payload";
constexpr char kErrorOutput[] = "Minimal Cartesian Provider requires one exact ksj.image-frame output grant";
constexpr char kErrorScratch[] = "Minimal Cartesian Provider scratch grant is too small or incompatible";
constexpr char kErrorInternal[] = "Minimal Cartesian Provider trapped an unexpected internal exception";

constexpr std::uint32_t kMaximumDimension = 512U;
constexpr std::uint64_t kMaximumPixels =
  static_cast<std::uint64_t>(kMaximumDimension) * static_cast<std::uint64_t>(kMaximumDimension);
constexpr std::uint64_t kMaximumOutputBytes = kMaximumPixels * sizeof(float);
constexpr std::uint64_t kMaximumScratchBytes = kMaximumPixels * 2U * sizeof(float);
constexpr std::uint32_t kRequiredAlignment = 64U;

constexpr std::uint32_t kInputPort = 0U;
constexpr std::uint32_t kOutputPort = 0U;
constexpr float kTwoPi = 6.28318530717958647692F;
constexpr std::uint64_t kFrameLayoutFlags = KSJ_TYPE_LAYOUT_CHANNEL_MAJOR_CONTIGUOUS | KSJ_TYPE_STRIDES_CANONICAL;
constexpr std::uint64_t kImageLayoutFlags = KSJ_TYPE_LAYOUT_ROW_MAJOR_CONTIGUOUS | KSJ_TYPE_STRIDES_CANONICAL;

struct ComplexFloat {
  float real{0.0F};
  float imaginary{0.0F};
};

static_assert(sizeof(ComplexFloat) == 2U * sizeof(float));

[[nodiscard]] ksj_provider_abi_header make_header(const std::uint32_t struct_size,
                                                  const std::uint64_t capability_bits = 0U) noexcept {
  return ksj_provider_abi_header_make(struct_size, capability_bits);
}

[[nodiscard]] bool has_compatible_header(const ksj_provider_abi_header* header,
                                         const std::size_t required_size) noexcept {
  return header != nullptr && header->struct_size >= required_size && header->abi_major == KSJ_PROVIDER_ABI_MAJOR &&
         header->abi_minor <= KSJ_PROVIDER_ABI_MINOR && header->reserved[0] == 0U && header->reserved[1] == 0U;
}

template <typename T> [[nodiscard]] bool has_full_compatible_header(const T* value) noexcept {
  return value != nullptr && has_compatible_header(&value->abi, sizeof(T));
}

[[nodiscard]] bool is_power_of_two(const std::uint32_t value) noexcept {
  return value != 0U && (value & (value - 1U)) == 0U;
}

[[nodiscard]] bool is_aligned(const void* pointer, const std::uint32_t alignment) noexcept {
  if (pointer == nullptr || alignment == 0U || !is_power_of_two(alignment)) {
    return false;
  }
  return (reinterpret_cast<std::uintptr_t>(pointer) & (static_cast<std::uintptr_t>(alignment) - 1U)) == 0U;
}

[[nodiscard]] bool has_usable_host_memory(const void* pointer, const std::uint32_t declared_alignment,
                                          const std::uint32_t required_alignment) noexcept {
  return pointer != nullptr && declared_alignment >= required_alignment && is_power_of_two(declared_alignment) &&
         is_aligned(pointer, declared_alignment);
}

[[nodiscard]] ksj_utf8_view make_utf8_view(const char* data, const std::uint64_t size) noexcept {
  ksj_utf8_view view{};
  view.abi = make_header(sizeof(view));
  view.data = data;
  view.size = size;
  return view;
}

[[nodiscard]] constexpr std::uint8_t hex_nibble(const char value) noexcept {
  if (value >= '0' && value <= '9') {
    return static_cast<std::uint8_t>(value - '0');
  }
  if (value >= 'a' && value <= 'f') {
    return static_cast<std::uint8_t>(value - 'a' + 10);
  }
  return 0U;
}

template <std::size_t N> [[nodiscard]] ksj_digest256 make_digest_from_hex(const char (&hex)[N]) noexcept {
  static_assert(N == KSJ_PROVIDER_DIGEST256_SIZE * 2U + 1U, "A SHA-256 literal must contain 64 hexadecimal bytes.");
  ksj_digest256 digest{};
  digest.abi = make_header(sizeof(digest));
  for (std::uint32_t index = 0U; index < KSJ_PROVIDER_DIGEST256_SIZE; ++index) {
    const std::size_t offset = static_cast<std::size_t>(index) * 2U;
    digest.bytes[index] = static_cast<std::uint8_t>((hex_nibble(hex[offset]) << 4U) | hex_nibble(hex[offset + 1U]));
  }
  return digest;
}

void set_error(ksj_error_view* out_error, const ksj_status status, const char* message,
               const std::uint64_t message_size) noexcept {
  if (!has_full_compatible_header(out_error)) {
    return;
  }
  *out_error = {};
  out_error->abi = make_header(sizeof(*out_error));
  out_error->status = status;
  out_error->category = 0U;
  out_error->provider_error_code = 0U;
  out_error->message = make_utf8_view(message, message_size);
}

template <std::size_t N>
[[nodiscard]] ksj_status reject(ksj_error_view* out_error, const ksj_status status, const char (&message)[N]) noexcept {
  set_error(out_error, status, message, static_cast<std::uint64_t>(N - 1U));
  return status;
}

[[nodiscard]] bool text_equals(const ksj_utf8_view& view, const char* expected,
                               const std::size_t expected_size) noexcept {
  return has_full_compatible_header(&view) && view.data != nullptr && view.size == expected_size &&
         std::memcmp(view.data, expected, expected_size) == 0;
}

[[nodiscard]] bool valid_digest(const ksj_digest256& digest) noexcept {
  return has_full_compatible_header(&digest);
}

[[nodiscard]] bool valid_borrowed_bytes(const ksj_byte_view& bytes) noexcept {
  return has_full_compatible_header(&bytes) && (bytes.size == 0U || bytes.data != nullptr);
}

[[nodiscard]] bool valid_type_header(const ksj_type_descriptor_view& type) noexcept {
  return has_full_compatible_header(&type) && valid_digest(type.payload_schema_digest) &&
         valid_digest(type.descriptor_digest) && valid_digest(type.metadata_schema_digest) && type.rank <= 8U &&
         type.minimum_alignment != 0U && is_power_of_two(type.minimum_alignment) && type.reserved0 == 0U &&
         (type.rank == 0U || type.dimension_names != nullptr);
}

[[nodiscard]] bool matches_digest(const ksj_digest256& lhs, const ksj_digest256& rhs) noexcept {
  return std::memcmp(lhs.bytes, rhs.bytes, KSJ_PROVIDER_DIGEST256_SIZE) == 0;
}

[[nodiscard]] bool has_exact_dimensions(const ksj_type_descriptor_view& type, const char* const* names,
                                        const std::uint32_t count) noexcept {
  if (!valid_type_header(type) || type.rank != count) {
    return false;
  }
  for (std::uint32_t index = 0U; index < count; ++index) {
    const auto expected_size = std::strlen(names[index]);
    if (!text_equals(type.dimension_names[index], names[index], expected_size)) {
      return false;
    }
  }
  return true;
}

[[nodiscard]] bool has_zero_canonical_strides(const ksj_type_descriptor_view& type) noexcept {
  for (const auto stride : type.stride_bytes) {
    if (stride != 0U) {
      return false;
    }
  }
  return true;
}

[[nodiscard]] bool has_frame_type(const ksj_type_descriptor_view& type) noexcept {
  constexpr const char* kDimensions[] = {kDimensionChannel, kDimensionKy, kDimensionKx};
  return valid_type_header(type) && text_equals(type.type_id, kFrameTypeId, sizeof(kFrameTypeId) - 1U) &&
         type.revision == 1U && type.payload_kind == KSJ_PAYLOAD_KIND_BUFFER_HANDLE &&
         matches_digest(type.descriptor_digest, make_digest_from_hex(kFrameAbiDescriptorDigestHex)) &&
         matches_digest(type.payload_schema_digest, make_digest_from_hex(kFramePayloadSchemaDigestHex)) &&
         matches_digest(type.metadata_schema_digest, make_digest_from_hex(kFrameMetadataSchemaDigestHex)) &&
         type.element_type == KSJ_ELEMENT_TYPE_COMPLEX_INT16 && type.layout_flags == kFrameLayoutFlags &&
         has_zero_canonical_strides(type) && type.allowed_memory_domains == KSJ_PROVIDER_MEMORY_HOST_PAGEABLE &&
         type.minimum_alignment == kRequiredAlignment &&
         type.mutability == KSJ_PAYLOAD_MUTABILITY_IMMUTABLE_AFTER_PUBLISH &&
         has_exact_dimensions(type, kDimensions, 3U);
}

[[nodiscard]] bool has_image_type(const ksj_type_descriptor_view& type) noexcept {
  constexpr const char* kDimensions[] = {kDimensionKy, kDimensionKx};
  return valid_type_header(type) && text_equals(type.type_id, kImageTypeId, sizeof(kImageTypeId) - 1U) &&
         type.revision == 1U && type.payload_kind == KSJ_PAYLOAD_KIND_BUFFER_HANDLE &&
         matches_digest(type.descriptor_digest, make_digest_from_hex(kImageAbiDescriptorDigestHex)) &&
         matches_digest(type.payload_schema_digest, make_digest_from_hex(kImagePayloadSchemaDigestHex)) &&
         matches_digest(type.metadata_schema_digest, make_digest_from_hex(kImageMetadataSchemaDigestHex)) &&
         type.element_type == KSJ_ELEMENT_TYPE_FLOAT32 && type.layout_flags == kImageLayoutFlags &&
         has_zero_canonical_strides(type) && type.allowed_memory_domains == KSJ_PROVIDER_MEMORY_HOST_PAGEABLE &&
         type.minimum_alignment == kRequiredAlignment &&
         type.mutability == KSJ_PAYLOAD_MUTABILITY_IMMUTABLE_AFTER_PUBLISH &&
         has_exact_dimensions(type, kDimensions, 2U);
}

[[nodiscard]] bool parse_positive_dimension(const char* first, const char* last, std::uint32_t& output) noexcept {
  if (first == nullptr || first == last || *first == '0') {
    return false;
  }
  std::uint32_t value = 0U;
  const auto parsed = std::from_chars(first, last, value);
  if (parsed.ec != std::errc{} || parsed.ptr != last || value < 2U || value > kMaximumDimension ||
      !is_power_of_two(value)) {
    return false;
  }
  output = value;
  return true;
}

[[nodiscard]] bool parse_canonical_config(const ksj_byte_view& config, std::uint32_t& rows,
                                          std::uint32_t& cols) noexcept {
  constexpr std::string_view kPrefix{"{\"cols\":"};
  constexpr std::string_view kMiddle{",\"rows\":"};
  constexpr std::string_view kSuffix{"}"};
  if (!has_full_compatible_header(&config) || config.data == nullptr || config.size == 0U ||
      config.size > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
    return false;
  }
  const auto encoded = std::string_view{static_cast<const char*>(config.data), static_cast<std::size_t>(config.size)};
  if (!encoded.starts_with(kPrefix) || !encoded.ends_with(kSuffix)) {
    return false;
  }
  const auto middle = encoded.find(kMiddle, kPrefix.size());
  if (middle == std::string_view::npos) {
    return false;
  }
  const char* const first = encoded.data();
  const char* const cols_first = first + kPrefix.size();
  const char* const cols_last = first + middle;
  const char* const rows_first = cols_last + kMiddle.size();
  const char* const rows_last = first + encoded.size() - kSuffix.size();
  return parse_positive_dimension(cols_first, cols_last, cols) && parse_positive_dimension(rows_first, rows_last, rows);
}

[[nodiscard]] bool is_operator(const ksj_provider_operator* operator_handle) noexcept {
  return operator_handle != nullptr && operator_handle->rows != 0U && operator_handle->cols != 0U;
}

[[nodiscard]] bool is_context(const ksj_provider_operator* operator_handle,
                              const ksj_execution_context* context) noexcept {
  return is_operator(operator_handle) && context == &operator_handle->context && context->owner == operator_handle &&
         operator_handle->context_active;
}

[[nodiscard]] bool is_key_state(const ksj_provider_operator* operator_handle, const ksj_key_state* key_state) noexcept {
  return is_operator(operator_handle) && key_state == &operator_handle->key_state &&
         key_state->owner == operator_handle && operator_handle->key_state_active;
}

[[nodiscard]] bool has_usable_callbacks(const ksj_firing_lease_callbacks_v1* callbacks) noexcept {
  constexpr std::uint64_t kRequiredCapabilities =
    KSJ_LEASE_CAP_INPUT_BATCHES | KSJ_LEASE_CAP_OUTPUT_GRANTS | KSJ_LEASE_CAP_SCRATCH;
  return has_full_compatible_header(callbacks) &&
         (callbacks->abi.capability_bits & kRequiredCapabilities) == kRequiredCapabilities &&
         callbacks->get_info != nullptr && callbacks->get_input_batch != nullptr && callbacks->get_scratch != nullptr &&
         callbacks->acquire_output_grant != nullptr && callbacks->output_grants != nullptr &&
         has_full_compatible_header(callbacks->output_grants) &&
         callbacks->output_grants->map_mutable_payload != nullptr && callbacks->output_grants->seal != nullptr &&
         callbacks->output_grants->release != nullptr;
}

void write_done(ksj_process_result* out_result, const std::uint32_t sealed_output_count,
                const std::uint64_t consumed_input_item_count, const std::uint64_t terminal_epoch) noexcept {
  *out_result = {};
  out_result->abi = make_header(sizeof(*out_result));
  out_result->outcome = KSJ_PROVIDER_PROCESS_DONE;
  out_result->sealed_output_count = sealed_output_count;
  out_result->consumed_input_item_count = consumed_input_item_count;
  out_result->terminal_epoch = terminal_epoch;
  out_result->async_token = nullptr;
}

[[nodiscard]] ComplexFloat add(const ComplexFloat lhs, const ComplexFloat rhs) noexcept {
  return {.real = lhs.real + rhs.real, .imaginary = lhs.imaginary + rhs.imaginary};
}

[[nodiscard]] ComplexFloat subtract(const ComplexFloat lhs, const ComplexFloat rhs) noexcept {
  return {.real = lhs.real - rhs.real, .imaginary = lhs.imaginary - rhs.imaginary};
}

[[nodiscard]] ComplexFloat multiply(const ComplexFloat lhs, const ComplexFloat rhs) noexcept {
  return {.real = lhs.real * rhs.real - lhs.imaginary * rhs.imaginary,
          .imaginary = lhs.real * rhs.imaginary + lhs.imaginary * rhs.real};
}

void inverse_fft_line(ComplexFloat* data, const std::uint32_t length, const std::uint32_t stride) noexcept {
  std::uint32_t reversed = 0U;
  for (std::uint32_t index = 1U; index < length; ++index) {
    std::uint32_t bit = length >> 1U;
    while ((reversed & bit) != 0U) {
      reversed ^= bit;
      bit >>= 1U;
    }
    reversed ^= bit;
    if (index < reversed) {
      std::swap(data[static_cast<std::size_t>(index) * stride], data[static_cast<std::size_t>(reversed) * stride]);
    }
  }

  for (std::uint32_t block = 2U;; block <<= 1U) {
    const float angle = kTwoPi / static_cast<float>(block);
    const ComplexFloat block_root{.real = std::cos(angle), .imaginary = std::sin(angle)};
    const std::uint32_t half = block >> 1U;
    for (std::uint32_t start = 0U; start < length; start += block) {
      ComplexFloat root{.real = 1.0F, .imaginary = 0.0F};
      for (std::uint32_t offset = 0U; offset < half; ++offset) {
        auto& lhs = data[static_cast<std::size_t>(start + offset) * stride];
        auto& rhs = data[static_cast<std::size_t>(start + offset + half) * stride];
        const ComplexFloat right = multiply(rhs, root);
        const ComplexFloat left = lhs;
        lhs = add(left, right);
        rhs = subtract(left, right);
        root = multiply(root, block_root);
      }
    }
    if (block == length) {
      break;
    }
  }
}

void inverse_fft2(ComplexFloat* data, const std::uint32_t rows, const std::uint32_t cols) noexcept {
  for (std::uint32_t row = 0U; row < rows; ++row) {
    inverse_fft_line(data + static_cast<std::size_t>(row) * cols, cols, 1U);
  }
  for (std::uint32_t col = 0U; col < cols; ++col) {
    inverse_fft_line(data + col, rows, cols);
  }
  const float normalization = 1.0F / static_cast<float>(static_cast<std::uint64_t>(rows) * cols);
  const std::size_t pixel_count = static_cast<std::size_t>(rows) * cols;
  for (std::size_t index = 0U; index < pixel_count; ++index) {
    data[index].real *= normalization;
    data[index].imaginary *= normalization;
  }
}

struct OutputGrantGuard {
  const ksj_output_grant_callbacks_v1* callbacks{nullptr};
  ksj_output_grant* grant{nullptr};
  bool settled{false};

  ~OutputGrantGuard() {
    if (callbacks == nullptr || grant == nullptr || settled || callbacks->release == nullptr) {
      return;
    }
    ksj_error_view error{};
    error.abi = make_header(sizeof(error));
    error.message.abi = make_header(sizeof(error.message));
    (void)callbacks->release(callbacks->host_context, grant, &error);
  }
};

[[nodiscard]] ksj_operator_descriptor make_operator_descriptor() noexcept;
[[nodiscard]] ksj_provider_descriptor
make_provider_descriptor(const ksj_operator_descriptor* operator_descriptor) noexcept;

struct ProviderMetadata {
  ProviderMetadata() noexcept
      : operator_descriptor(make_operator_descriptor()),
        provider_descriptor(make_provider_descriptor(&operator_descriptor)) {}

  ksj_operator_descriptor operator_descriptor{};
  ksj_provider_descriptor provider_descriptor{};
};

[[nodiscard]] ksj_operator_descriptor make_operator_descriptor() noexcept {
  ksj_operator_descriptor descriptor{};
  descriptor.abi =
    make_header(sizeof(descriptor), KSJ_OPERATOR_CAP_CANCEL_NO_ALLOCATION | KSJ_OPERATOR_CAP_CANCEL_NO_THROW);
  descriptor.operator_id = make_utf8_view(kOperatorId, sizeof(kOperatorId) - 1U);
  descriptor.interface_revision = 1U;
  descriptor.max_in_flight = 1U;
  descriptor.interface_digest = make_digest_from_hex(kOperatorInterfaceDigestHex);
  descriptor.contract_digest = make_digest_from_hex(kOperatorContractDigestHex);
  descriptor.thread_safety = KSJ_PROVIDER_SERIAL_INSTANCE;
  descriptor.max_private_threads = 0U;
  descriptor.max_input_items_per_firing = 1U;
  descriptor.max_output_items_per_firing = 1U;
  descriptor.max_output_bytes_per_firing = kMaximumOutputBytes;
  descriptor.max_scratch_bytes_per_firing = kMaximumScratchBytes;
  descriptor.max_retained_input_bytes = 0U;
  descriptor.max_async_tail_bytes = 0U;
  return descriptor;
}

[[nodiscard]] ksj_provider_descriptor
make_provider_descriptor(const ksj_operator_descriptor* operator_descriptor) noexcept {
  ksj_provider_descriptor descriptor{};
  descriptor.abi =
    make_header(sizeof(descriptor), KSJ_PROVIDER_CAP_SYNC_PROCESS | KSJ_PROVIDER_CAP_NO_PRIVATE_THREADS |
                                      KSJ_PROVIDER_CAP_NO_DIRECT_FILE_IO | KSJ_PROVIDER_CAP_NO_DIRECT_NETWORK_IO);
  descriptor.provider_id = make_utf8_view(kProviderId, sizeof(kProviderId) - 1U);
  descriptor.version.abi = make_header(sizeof(descriptor.version));
  descriptor.version.major = 1U;
  descriptor.version.minor = 0U;
  descriptor.version.patch = 0U;
  descriptor.version.prerelease = 0U;
  descriptor.provider_abi_major = KSJ_PROVIDER_ABI_MAJOR;
  descriptor.provider_abi_minor = KSJ_PROVIDER_ABI_MINOR;
  descriptor.bundle_digest = make_digest_from_hex(kProviderBundleDigestHex);
  descriptor.operator_count = 1U;
  descriptor.reserved0 = 0U;
  descriptor.operators = operator_descriptor;
  return descriptor;
}

[[nodiscard]] const ProviderMetadata& provider_metadata() noexcept {
  static const ProviderMetadata metadata{};
  return metadata;
}

ksj_status KSJ_PROVIDER_CALL operator_create(const ksj_operator_create_request* request,
                                             ksj_provider_operator** out_operator, ksj_error_view* out_error) noexcept {
  try {
    if (!has_full_compatible_header(request) || out_operator == nullptr ||
        !has_full_compatible_header(&request->required_contract_digest)) {
      return reject(out_error, KSJ_STATUS_INVALID_ARGUMENT, kErrorInvalidArgument);
    }
    *out_operator = nullptr;
    if (!text_equals(request->operator_id, kOperatorId, sizeof(kOperatorId) - 1U)) {
      return reject(out_error, KSJ_STATUS_UNSUPPORTED, kErrorUnsupportedOperator);
    }
    if (!matches_digest(request->required_contract_digest, provider_metadata().operator_descriptor.contract_digest)) {
      return reject(out_error, KSJ_STATUS_FAILED_PRECONDITION, kErrorContractDigest);
    }
    if (request->host_services != nullptr && !has_full_compatible_header(request->host_services)) {
      return reject(out_error, KSJ_STATUS_BAD_ABI, kErrorBadAbi);
    }
    std::uint32_t rows = 0U;
    std::uint32_t cols = 0U;
    if (!parse_canonical_config(request->canonical_config, rows, cols)) {
      return reject(out_error, KSJ_STATUS_INVALID_ARGUMENT, kErrorUnsupportedConfig);
    }
    auto* const operator_handle = new (std::nothrow) ksj_provider_operator{.rows = rows, .cols = cols};
    if (operator_handle == nullptr) {
      return reject(out_error, KSJ_STATUS_RESOURCE_EXHAUSTED, kErrorInternal);
    }
    *out_operator = operator_handle;
    return KSJ_STATUS_OK;
  } catch (...) {
    return reject(out_error, KSJ_STATUS_INTERNAL_ERROR, kErrorInternal);
  }
}

ksj_status KSJ_PROVIDER_CALL execution_context_create(ksj_provider_operator* operator_handle,
                                                      const ksj_execution_context_descriptor* descriptor,
                                                      ksj_execution_context** out_context,
                                                      ksj_error_view* out_error) noexcept {
  if (!is_operator(operator_handle) || !has_full_compatible_header(descriptor) || out_context == nullptr) {
    return reject(out_error, KSJ_STATUS_INVALID_ARGUMENT, kErrorInvalidArgument);
  }
  if (descriptor->host_services != nullptr && !has_full_compatible_header(descriptor->host_services)) {
    return reject(out_error, KSJ_STATUS_BAD_ABI, kErrorBadAbi);
  }
  if (operator_handle->context_active) {
    return reject(out_error, KSJ_STATUS_FAILED_PRECONDITION, kErrorInvalidArgument);
  }
  operator_handle->context.owner = operator_handle;
  operator_handle->context_active = true;
  *out_context = &operator_handle->context;
  return KSJ_STATUS_OK;
}

ksj_status KSJ_PROVIDER_CALL key_state_init(ksj_provider_operator* operator_handle, ksj_execution_context* context,
                                            const ksj_key_state_descriptor* descriptor, ksj_key_state** out_key_state,
                                            ksj_error_view* out_error) noexcept {
  if (!is_context(operator_handle, context) || !has_full_compatible_header(descriptor) || out_key_state == nullptr ||
      !valid_borrowed_bytes(descriptor->semantic_key)) {
    return reject(out_error, KSJ_STATUS_INVALID_ARGUMENT, kErrorInvalidArgument);
  }
  if (operator_handle->key_state_active) {
    return reject(out_error, KSJ_STATUS_FAILED_PRECONDITION, kErrorInvalidArgument);
  }
  operator_handle->key_state.owner = operator_handle;
  operator_handle->key_state_active = true;
  *out_key_state = &operator_handle->key_state;
  return KSJ_STATUS_OK;
}

ksj_status KSJ_PROVIDER_CALL operator_on_start(ksj_provider_operator* operator_handle, ksj_execution_context* context,
                                               ksj_key_state* key_state, const ksj_scan_start_descriptor* descriptor,
                                               ksj_error_view* out_error) noexcept {
  if (!is_context(operator_handle, context) || !is_key_state(operator_handle, key_state) ||
      !has_full_compatible_header(descriptor)) {
    return reject(out_error, KSJ_STATUS_INVALID_ARGUMENT, kErrorInvalidArgument);
  }
  return KSJ_STATUS_OK;
}

ksj_status KSJ_PROVIDER_CALL operator_process_batch(ksj_provider_operator* operator_handle,
                                                    ksj_execution_context* context, ksj_key_state* key_state,
                                                    ksj_firing_lease* lease,
                                                    const ksj_firing_lease_callbacks_v1* callbacks,
                                                    ksj_process_result* out_result,
                                                    ksj_error_view* out_error) noexcept {
  try {
    if (!is_context(operator_handle, context) || !is_key_state(operator_handle, key_state) || lease == nullptr ||
        !has_full_compatible_header(out_result)) {
      return reject(out_error, KSJ_STATUS_INVALID_ARGUMENT, kErrorInvalidArgument);
    }
    if (!has_usable_callbacks(callbacks)) {
      return reject(out_error, KSJ_STATUS_BAD_ABI, kErrorBadAbi);
    }

    const std::uint64_t pixel_count = static_cast<std::uint64_t>(operator_handle->rows) * operator_handle->cols;
    const std::uint64_t input_bytes = pixel_count * 4U;
    const std::uint64_t output_bytes = pixel_count * sizeof(float);
    const std::uint64_t scratch_bytes = pixel_count * sizeof(ComplexFloat);

    ksj_firing_lease_info info{};
    info.abi = make_header(sizeof(info));
    const auto info_status = callbacks->get_info(callbacks->host_context, lease, &info, out_error);
    if (info_status != KSJ_STATUS_OK || !has_full_compatible_header(&info) || info.input_batch_count != 1U ||
        info.output_grant_count != 1U || info.reserved_output_bytes < output_bytes ||
        info.reserved_scratch_bytes < scratch_bytes) {
      return info_status == KSJ_STATUS_OK ? reject(out_error, KSJ_STATUS_CONTRACT_VIOLATION, kErrorLease) : info_status;
    }

    ksj_input_batch_view batch{};
    batch.abi = make_header(sizeof(batch));
    const auto batch_status = callbacks->get_input_batch(callbacks->host_context, lease, 0U, &batch, out_error);
    if (batch_status != KSJ_STATUS_OK) {
      return batch_status;
    }
    if (!has_full_compatible_header(&batch) || batch.input_port != kInputPort || batch.item_count != 1U ||
        batch.items == nullptr || !has_full_compatible_header(&batch.items[0U])) {
      return reject(out_error, KSJ_STATUS_CONTRACT_VIOLATION, kErrorFrame);
    }
    const auto& input = batch.items[0U];
    if (!has_full_compatible_header(&input.payload) || input.payload.data == nullptr ||
        input.payload.byte_count != input_bytes || input.payload.memory_domain != KSJ_PROVIDER_MEMORY_HOST_PAGEABLE ||
        !has_usable_host_memory(input.payload.data, input.payload.alignment, kRequiredAlignment) ||
        !valid_borrowed_bytes(input.metadata) || !has_frame_type(input.payload.type)) {
      return reject(out_error, KSJ_STATUS_CONTRACT_VIOLATION, kErrorFrame);
    }

    ksj_output_grant* grant = nullptr;
    const auto acquire_status =
      callbacks->acquire_output_grant(callbacks->host_context, lease, kOutputPort, &grant, out_error);
    if (acquire_status != KSJ_STATUS_OK || grant == nullptr) {
      return acquire_status == KSJ_STATUS_OK ? reject(out_error, KSJ_STATUS_CONTRACT_VIOLATION, kErrorOutput)
                                             : acquire_status;
    }
    OutputGrantGuard grant_guard{.callbacks = callbacks->output_grants, .grant = grant};

    ksj_mutable_payload_view output{};
    output.abi = make_header(sizeof(output));
    const auto map_status =
      callbacks->output_grants->map_mutable_payload(callbacks->output_grants->host_context, grant, &output, out_error);
    if (map_status != KSJ_STATUS_OK) {
      return map_status;
    }
    if (!has_full_compatible_header(&output) || output.data == nullptr || output.capacity_bytes < output_bytes ||
        output.committed_bytes != 0U || output.memory_domain != KSJ_PROVIDER_MEMORY_HOST_PAGEABLE ||
        output.alignment < kRequiredAlignment ||
        !has_usable_host_memory(output.data, output.alignment, kRequiredAlignment) || !has_image_type(output.type)) {
      return reject(out_error, KSJ_STATUS_CONTRACT_VIOLATION, kErrorOutput);
    }

    ksj_scratch_view scratch{};
    scratch.abi = make_header(sizeof(scratch));
    const auto scratch_status = callbacks->get_scratch(callbacks->host_context, lease, &scratch, out_error);
    if (scratch_status != KSJ_STATUS_OK) {
      return scratch_status;
    }
    if (!has_full_compatible_header(&scratch) || scratch.data == nullptr || scratch.byte_count < scratch_bytes ||
        scratch.memory_domain != KSJ_PROVIDER_MEMORY_HOST_PAGEABLE ||
        !has_usable_host_memory(scratch.data, scratch.alignment, alignof(ComplexFloat))) {
      return reject(out_error, KSJ_STATUS_CONTRACT_VIOLATION, kErrorScratch);
    }

    auto* const workspace = reinterpret_cast<ComplexFloat*>(scratch.data);
    const auto* const source = static_cast<const std::byte*>(input.payload.data);
    for (std::uint64_t index = 0U; index < pixel_count; ++index) {
      std::int16_t real = 0;
      std::int16_t imaginary = 0;
      const auto offset = static_cast<std::size_t>(index * 4U);
      std::memcpy(&real, source + offset, sizeof(real));
      std::memcpy(&imaginary, source + offset + sizeof(real), sizeof(imaginary));
      workspace[index] = {.real = static_cast<float>(real), .imaginary = static_cast<float>(imaginary)};
    }
    inverse_fft2(workspace, operator_handle->rows, operator_handle->cols);

    auto* const destination = static_cast<std::byte*>(output.data);
    for (std::uint64_t index = 0U; index < pixel_count; ++index) {
      const auto& value = workspace[index];
      const float magnitude = std::sqrt(value.real * value.real + value.imaginary * value.imaginary);
      std::memcpy(destination + static_cast<std::size_t>(index * sizeof(float)), &magnitude, sizeof(magnitude));
    }

    ksj_output_seal_descriptor seal{};
    seal.abi = make_header(sizeof(seal));
    seal.output_port = kOutputPort;
    seal.produced_item_count = 1U;
    seal.produced_byte_count = output_bytes;
    seal.semantic_key_hash = input.semantic_key_hash;
    seal.order_key = input.order_key;
    seal.type = output.type;
    seal.metadata.abi = make_header(sizeof(seal.metadata));
    seal.metadata.data = nullptr;
    seal.metadata.size = 0U;
    const auto seal_status =
      callbacks->output_grants->seal(callbacks->output_grants->host_context, grant, &seal, out_error);
    if (seal_status != KSJ_STATUS_OK) {
      return seal_status;
    }
    grant_guard.settled = true;
    write_done(out_result, 1U, 1U, info.terminal_epoch);
    return KSJ_STATUS_OK;
  } catch (...) {
    return reject(out_error, KSJ_STATUS_INTERNAL_ERROR, kErrorInternal);
  }
}

ksj_status KSJ_PROVIDER_CALL operator_on_scan_end(ksj_provider_operator* operator_handle,
                                                  ksj_execution_context* context, ksj_key_state* key_state,
                                                  const ksj_scan_end_descriptor* descriptor,
                                                  ksj_firing_lease* terminal_lease,
                                                  const ksj_firing_lease_callbacks_v1* callbacks,
                                                  ksj_process_result* out_result, ksj_error_view* out_error) noexcept {
  if (!is_context(operator_handle, context) || !is_key_state(operator_handle, key_state) ||
      !has_full_compatible_header(descriptor) || descriptor->kind != KSJ_PROVIDER_SCAN_END_NORMAL ||
      descriptor->reserved0 != 0U || terminal_lease == nullptr || !has_full_compatible_header(out_result) ||
      !has_full_compatible_header(callbacks)) {
    return reject(out_error, KSJ_STATUS_INVALID_ARGUMENT, kErrorInvalidArgument);
  }
  write_done(out_result, 0U, 0U, descriptor->terminal_epoch);
  return KSJ_STATUS_OK;
}

ksj_status KSJ_PROVIDER_CALL operator_on_cancel(ksj_provider_operator* operator_handle, ksj_execution_context* context,
                                                ksj_key_state* key_state, const ksj_cancel_context* descriptor,
                                                ksj_error_view* out_error) noexcept {
  if (!is_context(operator_handle, context) || !is_key_state(operator_handle, key_state) ||
      !has_full_compatible_header(descriptor) || descriptor->reserved0 != 0U ||
      (descriptor->kind != KSJ_PROVIDER_SCAN_END_CANCELLED && descriptor->kind != KSJ_PROVIDER_SCAN_END_FAILED)) {
    return reject(out_error, KSJ_STATUS_INVALID_ARGUMENT, kErrorInvalidArgument);
  }
  return KSJ_STATUS_OK;
}

void KSJ_PROVIDER_CALL key_state_reset(ksj_provider_operator* operator_handle, ksj_execution_context* context,
                                       ksj_key_state* key_state) noexcept {
  if (!is_context(operator_handle, context) || !is_key_state(operator_handle, key_state)) {
    return;
  }
  operator_handle->key_state.owner = nullptr;
  operator_handle->key_state_active = false;
}

void KSJ_PROVIDER_CALL execution_context_destroy(ksj_provider_operator* operator_handle,
                                                 ksj_execution_context* context) noexcept {
  if (!is_context(operator_handle, context)) {
    return;
  }
  operator_handle->context.owner = nullptr;
  operator_handle->context_active = false;
}

void KSJ_PROVIDER_CALL operator_destroy(ksj_provider_operator* operator_handle) noexcept {
  delete operator_handle;
}

} // namespace

KSJ_PROVIDER_ENTRY ksj_status KSJ_PROVIDER_CALL ksj_provider_query(const ksj_provider_query_request* request,
                                                                   ksj_provider_descriptor* out_descriptor,
                                                                   ksj_provider_api_v1* out_api,
                                                                   ksj_error_view* out_error) {
  try {
    if (!has_full_compatible_header(request) || !has_full_compatible_header(out_descriptor) ||
        !has_full_compatible_header(out_api)) {
      return reject(out_error, KSJ_STATUS_BAD_ABI, kErrorBadAbi);
    }
    if (request->minimum_abi_minor > KSJ_PROVIDER_ABI_MINOR || request->maximum_abi_minor < KSJ_PROVIDER_ABI_MINOR ||
        !has_full_compatible_header(&request->host_build_id) ||
        (request->host_build_id.size != 0U && request->host_build_id.data == nullptr)) {
      return reject(out_error, KSJ_STATUS_BAD_ABI, kErrorBadAbi);
    }
    const auto& metadata = provider_metadata();
    *out_descriptor = metadata.provider_descriptor;
    *out_api = {};
    out_api->abi = make_header(sizeof(*out_api));
    out_api->operator_create = &operator_create;
    out_api->execution_context_create = &execution_context_create;
    out_api->key_state_init = &key_state_init;
    out_api->operator_on_start = &operator_on_start;
    out_api->operator_process_batch = &operator_process_batch;
    out_api->operator_on_scan_end = &operator_on_scan_end;
    out_api->operator_on_cancel = &operator_on_cancel;
    out_api->key_state_reset = &key_state_reset;
    out_api->execution_context_destroy = &execution_context_destroy;
    out_api->operator_destroy = &operator_destroy;
    return KSJ_STATUS_OK;
  } catch (...) {
    return reject(out_error, KSJ_STATUS_INTERNAL_ERROR, kErrorInternal);
  }
}
