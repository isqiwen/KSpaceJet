#include <kspacejet/provider/provider.h>
#include <kspacejet/provider/detail/provider_support.hpp>
#include <kspacejet/provider/type_registry.h>

int ksj_provider_sdk_cxx_header_check() {
  ksj_provider_api api{};
  const ksj_type_descriptor_view image_frame = ksj_type_registry_image_frame();
  const ksj_type_descriptor_view acquisition = ksj_type_registry_ismrmrd_acquisition();
  const ksj_type_descriptor_view image = ksj_type_registry_ismrmrd_image();
  std::uint64_t product = 0U;
  const ksj_utf8_view text = ksj::provider::detail::make_utf8_view("provider-sdk");
  const ksj_digest256 bundle_digest = ksj::provider::detail::make_bundle_digest_from_hex(
    "0000000000000000000000000000000000000000000000000000000000000000");
  api.abi = ksj_provider_abi_header_make(sizeof(api), 0U);
  return api.abi.reserved0 == 0U && ksj::provider::detail::has_full_compatible_header(&api) &&
             ksj::provider::detail::text_equals(text, "provider-sdk") &&
             ksj::provider::detail::has_full_compatible_header(&bundle_digest) &&
             ksj::provider::detail::has_valid_type_descriptor(image_frame) &&
             ksj::provider::detail::checked_multiply(2U, 3U, product) && product == 6U &&
             ksj_type_registry_matches_image_frame(&image_frame) &&
             ksj_type_registry_matches_ismrmrd_acquisition(&acquisition) &&
             ksj_type_registry_matches_ismrmrd_image(&image)
           ? 0
           : 1;
}
