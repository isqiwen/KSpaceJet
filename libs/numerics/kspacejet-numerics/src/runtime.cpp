#include "kspacejet/numerics/runtime.hpp"

#include <array>
#include <mutex>

#include <ipp/ippcore_tl.h>

#include <mkl_service.h>

namespace ksj::numerics {
namespace {

std::once_flag g_numerics_runtime_init_once;

void initialize_ipp_backend_runtime() {
  // KSpaceJet schedules work at a higher level. Keep IPP single-threaded to avoid hidden nested parallelism inside
  // numerics kernels; callers that need concurrency should parallelize around numerics operations.
  (void)ippSetNumThreads_T(1);
}

void initialize_mkl_backend_runtime() {
  // MKL has a process-wide runtime. Disable dynamic thread selection and pin every compute domain used by numerics to
  // one internal thread, so reconstruction workers, tests, and benchmarks see deterministic outer-level scheduling.
  mkl_set_dynamic(0);
  mkl_set_num_threads(1);
  constexpr std::array<int, 5> kMklThreadDomains = {
    MKL_DOMAIN_ALL, MKL_DOMAIN_BLAS, MKL_DOMAIN_FFT, MKL_DOMAIN_VML, MKL_DOMAIN_PARDISO,
  };
  for (const int mkl_domain : kMklThreadDomains) {
    mkl_domain_set_num_threads(1, mkl_domain);
  }
}

void initialize_backend_runtimes() {
  // Keep all process-wide numerics backend policy in this one dispatch point. Future OpenCV, ITK, FFTW, or other
  // backend runtime setup should be added here as private helpers, while callers keep using
  // initialize_numerics_runtime.
  initialize_ipp_backend_runtime();
  initialize_mkl_backend_runtime();
}

} // namespace

void initialize_numerics_runtime() {
  std::call_once(g_numerics_runtime_init_once, initialize_backend_runtimes);
}

} // namespace ksj::numerics
