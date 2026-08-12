#pragma once

/// Compressed sparse row matrix ownership, construction, validation, and traversal APIs.

#include "kspacejet/array/array.hpp"

#include <cstddef>
#include <limits>
#include <span>
#include <stdexcept>

namespace ksj::sparse {

enum class StorageFormat {
  coo,
  csr,
  csc,
};

enum class SparseOperation {
  none,
  transpose,
  conjugate_transpose,
};

enum class SparseTriangle {
  lower,
  upper,
};

enum class SparseDiagonal {
  non_unit,
  unit,
};

template <typename T> class CsrMatrix {
public:
  using value_type = T;

  CsrMatrix() = default;
  CsrMatrix(const CsrMatrix&) = delete;
  CsrMatrix& operator=(const CsrMatrix&) = delete;
  CsrMatrix(CsrMatrix&&) noexcept = default;
  CsrMatrix& operator=(CsrMatrix&&) noexcept = default;

  template <typename IndexT>
  CsrMatrix(std::size_t rows, std::size_t cols, ksj::array::VectorView<const IndexT> row_offsets,
            ksj::array::VectorView<const IndexT> column_indices, ksj::array::VectorView<const T> values)
      : rows_(rows), cols_(cols), row_offsets_(ksj::array::make_pooled_vector<int>(row_offsets.size())),
        row_starts_(ksj::array::make_pooled_vector<int>(rows)), row_ends_(ksj::array::make_pooled_vector<int>(rows)),
        column_indices_(ksj::array::make_pooled_vector<int>(column_indices.size())),
        values_(ksj::array::make_pooled_vector<T>(values.size())) {
    if (row_offsets.size() != rows_ + 1U) {
      throw std::invalid_argument("CSR row_offsets size must be rows + 1");
    }
    if (column_indices.size() != values.size()) {
      throw std::invalid_argument("CSR column index count must match value count");
    }
    if (row_offsets.empty() || row_offsets(0) != IndexT{} ||
        static_cast<std::size_t>(row_offsets(row_offsets.size() - 1U)) != values.size()) {
      throw std::invalid_argument("CSR row_offsets must span all non-zero values");
    }
    if (rows_ > static_cast<std::size_t>(std::numeric_limits<int>::max()) ||
        cols_ > static_cast<std::size_t>(std::numeric_limits<int>::max()) ||
        values.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
      throw std::length_error("CSR shape exceeds supported sparse index range");
    }

    for (std::size_t row = 0; row < row_offsets.size(); ++row) {
      if (row_offsets(row) < IndexT{} || static_cast<std::size_t>(row_offsets(row)) > values.size()) {
        throw std::invalid_argument("CSR row offset out of range");
      }
      if (row > 0 && row_offsets(row) < row_offsets(row - 1U)) {
        throw std::invalid_argument("CSR row offsets must be sorted");
      }
      row_offsets_(row) = static_cast<int>(row_offsets(row));
      if (row > 0) {
        row_ends_(row - 1U) = static_cast<int>(row_offsets(row));
      }
      if (row < rows_) {
        row_starts_(row) = static_cast<int>(row_offsets(row));
      }
    }

    for (std::size_t i = 0; i < values.size(); ++i) {
      if (column_indices(i) < IndexT{} || static_cast<std::size_t>(column_indices(i)) >= cols_) {
        throw std::invalid_argument("CSR column index out of range");
      }
      column_indices_(i) = static_cast<int>(column_indices(i));
      values_(i) = values(i);
    }
  }

  CsrMatrix(std::size_t rows, std::size_t cols, std::span<const std::size_t> row_offsets,
            std::span<const std::size_t> column_indices, std::span<const T> values)
      : CsrMatrix(rows, cols, ksj::array::VectorView<const std::size_t>(row_offsets.data(), row_offsets.size()),
                  ksj::array::VectorView<const std::size_t>(column_indices.data(), column_indices.size()),
                  ksj::array::VectorView<const T>(values.data(), values.size())) {}

  [[nodiscard]] std::size_t rows() const noexcept { return rows_; }
  [[nodiscard]] std::size_t cols() const noexcept { return cols_; }
  [[nodiscard]] std::size_t nonzeros() const noexcept { return values_.size(); }
  [[nodiscard]] const ksj::array::PooledVector<int>& row_offsets() const noexcept { return row_offsets_; }
  [[nodiscard]] const ksj::array::PooledVector<int>& row_starts() const noexcept { return row_starts_; }
  [[nodiscard]] const ksj::array::PooledVector<int>& row_ends() const noexcept { return row_ends_; }
  [[nodiscard]] const ksj::array::PooledVector<int>& column_indices() const noexcept { return column_indices_; }
  [[nodiscard]] const ksj::array::PooledVector<T>& values() const noexcept { return values_; }

private:
  std::size_t rows_{0};
  std::size_t cols_{0};
  ksj::array::PooledVector<int> row_offsets_{};
  ksj::array::PooledVector<int> row_starts_{};
  ksj::array::PooledVector<int> row_ends_{};
  ksj::array::PooledVector<int> column_indices_{};
  ksj::array::PooledVector<T> values_{};
};

} // namespace ksj::sparse
