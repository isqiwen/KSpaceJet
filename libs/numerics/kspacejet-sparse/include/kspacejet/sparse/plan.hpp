#pragma once

/// Reusable execution plans for repeated operations on an immutable CSR matrix.

#include "kspacejet/sparse/csr_matrix.hpp"
#include "kspacejet/sparse/detail/intel/intel_sparse_operations.hpp"

#include <stdexcept>
#include <utility>

namespace ksj::sparse {

namespace detail {
template <typename T> struct CsrPlanAccess;
}

/// Reuses backend analysis and native matrix handles across repeated CSR operations.
///
/// The referenced matrix storage must outlive the plan and must not be moved while the plan is in use.
/// Calls that share one plan must be externally serialized.
template <typename T> class CsrPlan {
public:
  explicit CsrPlan(const CsrMatrix<T>& matrix) : matrix_(&matrix) {
    (void)detail::intel::make_handle(matrix, intel_handle_);
  }

  CsrPlan(const CsrPlan&) = delete;
  CsrPlan& operator=(const CsrPlan&) = delete;

  CsrPlan(CsrPlan&& other) noexcept
      : matrix_(std::exchange(other.matrix_, nullptr)), intel_handle_(std::move(other.intel_handle_)) {}

  CsrPlan& operator=(CsrPlan&& other) noexcept {
    if (this != &other) {
      matrix_ = std::exchange(other.matrix_, nullptr);
      intel_handle_ = std::move(other.intel_handle_);
    }
    return *this;
  }

  [[nodiscard]] const CsrMatrix<T>& matrix() const {
    if (matrix_ == nullptr) {
      throw std::logic_error("cannot use a moved-from CSR plan");
    }
    return *matrix_;
  }

private:
  friend struct detail::CsrPlanAccess<T>;

  const CsrMatrix<T>* matrix_{nullptr};
  detail::intel::SparseHandle intel_handle_{};
};

namespace detail {

template <typename T> struct CsrPlanAccess {
  [[nodiscard]] static const intel::SparseHandle& intel_handle(const CsrPlan<T>& plan) noexcept {
    return plan.intel_handle_;
  }
};

} // namespace detail

template <typename T> [[nodiscard]] CsrPlan<T> make_csr_plan(const CsrMatrix<T>& matrix) {
  return CsrPlan<T>(matrix);
}

} // namespace ksj::sparse
