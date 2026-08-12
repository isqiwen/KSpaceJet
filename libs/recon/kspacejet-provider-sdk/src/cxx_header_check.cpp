#include <kspacejet/provider/v1/provider.h>

static_assert(KSJ_PROVIDER_ABI_MAJOR == 1U);

int ksj_provider_sdk_cxx_header_check() {
  ksj_provider_api_v1 api{};
  api.abi = ksj_provider_abi_header_make(sizeof(api), 0U);
  return api.abi.abi_minor == KSJ_PROVIDER_ABI_MINOR ? 0 : 1;
}
