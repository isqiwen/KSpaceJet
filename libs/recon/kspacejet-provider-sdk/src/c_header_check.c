#include <kspacejet/provider/provider.h>
#include <kspacejet/provider/type_registry.h>

int ksj_provider_sdk_c_header_check(void) {
  ksj_provider_query_request request = {0};
  const ksj_type_descriptor_view kspace_frame = ksj_type_registry_kspace_frame();
  const ksj_type_descriptor_view control_message = ksj_type_registry_control_message();
  const ksj_type_descriptor_view waveform = ksj_type_registry_ismrmrd_waveform();
  request.abi = ksj_provider_abi_header_make((uint32_t)sizeof(request), UINT64_C(0));
  return request.abi.reserved0 == UINT32_C(0) && ksj_type_registry_matches_kspace_frame(&kspace_frame) &&
             ksj_type_registry_matches_control_message(&control_message) &&
             ksj_type_registry_matches_ismrmrd_waveform(&waveform)
           ? 0
           : 1;
}
