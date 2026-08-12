#include <kspacejet/provider/v1/provider.h>

int ksj_provider_sdk_c_header_check(void) {
  ksj_provider_query_request request = {0};
  request.abi = ksj_provider_abi_header_make((uint32_t)sizeof(request), UINT64_C(0));
  return request.abi.abi_major == KSJ_PROVIDER_ABI_MAJOR ? 0 : 1;
}
