#pragma once

#include <algorithm>

#include <omp.h>

#ifndef _OPENMP
#error "kspacejet/threading/openmp_settings_scope.hpp requires an OpenMP-enabled compilation target."
#endif

namespace ksj::threading {

class OpenMpSettingsScope {
public:
  OpenMpSettingsScope() noexcept
      : dynamic_(omp_get_dynamic()), max_active_levels_(omp_get_max_active_levels()),
        max_threads_(omp_get_max_threads()) {}

  ~OpenMpSettingsScope() {
    omp_set_dynamic(dynamic_);
    omp_set_max_active_levels(max_active_levels_);
    omp_set_num_threads(max_threads_);
  }

  OpenMpSettingsScope(const OpenMpSettingsScope&) = delete;
  OpenMpSettingsScope& operator=(const OpenMpSettingsScope&) = delete;

  void set_dynamic(const bool enabled) const noexcept { omp_set_dynamic(enabled ? 1 : 0); }

  void set_max_active_levels(const int levels) const noexcept { omp_set_max_active_levels(std::max(1, levels)); }

  void set_num_threads(const int count) const noexcept { omp_set_num_threads(std::max(1, count)); }

private:
  int dynamic_;
  int max_active_levels_;
  int max_threads_;
};

} // namespace ksj::threading
