#pragma once

/// Small fixed-size matrix utilities intended for stack-resident linear-algebra calculations.

#include <algorithm>
#include <array>
#include <concepts>
#include <cstddef>
#include <cmath>
#include <limits>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>

namespace ksj::linalg {

template <std::floating_point T> struct Vector2 {
  T x{};
  T y{};

  constexpr Vector2() noexcept = default;
  constexpr Vector2(T x_value, T y_value) noexcept : x(x_value), y(y_value) {}

  [[nodiscard]] static constexpr std::size_t size() noexcept { return 2U; }

  [[nodiscard]] constexpr T* data() noexcept { return &x; }
  [[nodiscard]] constexpr const T* data() const noexcept { return &x; }

  [[nodiscard]] constexpr T& operator[](const std::size_t index) noexcept { return index == 0U ? x : y; }

  [[nodiscard]] constexpr const T& operator[](const std::size_t index) const noexcept { return index == 0U ? x : y; }

  [[nodiscard]] constexpr T& at(const std::size_t index) {
    if (index >= size()) {
      throw std::out_of_range("Vector2 index is outside the vector");
    }
    return (*this)[index];
  }

  [[nodiscard]] constexpr const T& at(const std::size_t index) const {
    if (index >= size()) {
      throw std::out_of_range("Vector2 index is outside the vector");
    }
    return (*this)[index];
  }

  [[nodiscard]] constexpr T* begin() noexcept { return data(); }
  [[nodiscard]] constexpr const T* begin() const noexcept { return data(); }
  [[nodiscard]] constexpr const T* cbegin() const noexcept { return data(); }
  [[nodiscard]] constexpr T* end() noexcept { return data() + size(); }
  [[nodiscard]] constexpr const T* end() const noexcept { return data() + size(); }
  [[nodiscard]] constexpr const T* cend() const noexcept { return data() + size(); }

  [[nodiscard]] T length() const noexcept { return std::sqrt(x * x + y * y); }

  [[nodiscard]] Vector2 normalized() const noexcept {
    const auto value = length();
    return value == T{} ? Vector2{} : (*this / value);
  }

  [[nodiscard]] constexpr Vector2 operator+(const Vector2& rhs) const noexcept { return {x + rhs.x, y + rhs.y}; }

  [[nodiscard]] constexpr Vector2 operator-(const Vector2& rhs) const noexcept { return {x - rhs.x, y - rhs.y}; }

  [[nodiscard]] constexpr Vector2 operator*(T scalar) const noexcept { return {x * scalar, y * scalar}; }

  [[nodiscard]] constexpr Vector2 operator/(T scalar) const noexcept { return (*this) * (T{1} / scalar); }

  constexpr Vector2& operator+=(const Vector2& rhs) noexcept {
    x += rhs.x;
    y += rhs.y;
    return *this;
  }

  constexpr Vector2& operator-=(const Vector2& rhs) noexcept {
    x -= rhs.x;
    y -= rhs.y;
    return *this;
  }

  constexpr Vector2& operator*=(T scalar) noexcept {
    x *= scalar;
    y *= scalar;
    return *this;
  }

  constexpr Vector2& operator/=(T scalar) noexcept { return (*this) *= (T{1} / scalar); }

  [[nodiscard]] static constexpr T dot(const Vector2& lhs, const Vector2& rhs) noexcept {
    return lhs.x * rhs.x + lhs.y * rhs.y;
  }

  [[nodiscard]] static constexpr T cross(const Vector2& lhs, const Vector2& rhs) noexcept {
    return lhs.x * rhs.y - lhs.y * rhs.x;
  }

  [[nodiscard]] static T angle(const Vector2& lhs, const Vector2& rhs) noexcept {
    const auto projection = std::clamp(dot(lhs.normalized(), rhs.normalized()), T{-1}, T{1});
    return std::acos(projection);
  }

  [[nodiscard]] std::string to_string() const {
    std::stringstream ss;
    ss << '(' << x << ", " << y << ')';
    return ss.str();
  }
};

template <std::floating_point T> struct Vector3 {
  T x{};
  T y{};
  T z{};

  constexpr Vector3() noexcept = default;
  constexpr Vector3(T x_value, T y_value, T z_value) noexcept : x(x_value), y(y_value), z(z_value) {}

  [[nodiscard]] static constexpr std::size_t size() noexcept { return 3U; }

  [[nodiscard]] constexpr T* data() noexcept { return &x; }
  [[nodiscard]] constexpr const T* data() const noexcept { return &x; }

  [[nodiscard]] constexpr T& operator[](const std::size_t index) noexcept {
    return index == 0U ? x : (index == 1U ? y : z);
  }

  [[nodiscard]] constexpr const T& operator[](const std::size_t index) const noexcept {
    return index == 0U ? x : (index == 1U ? y : z);
  }

  [[nodiscard]] constexpr T& at(const std::size_t index) {
    if (index >= size()) {
      throw std::out_of_range("Vector3 index is outside the vector");
    }
    return (*this)[index];
  }

  [[nodiscard]] constexpr const T& at(const std::size_t index) const {
    if (index >= size()) {
      throw std::out_of_range("Vector3 index is outside the vector");
    }
    return (*this)[index];
  }

  [[nodiscard]] constexpr T* begin() noexcept { return data(); }
  [[nodiscard]] constexpr const T* begin() const noexcept { return data(); }
  [[nodiscard]] constexpr const T* cbegin() const noexcept { return data(); }
  [[nodiscard]] constexpr T* end() noexcept { return data() + size(); }
  [[nodiscard]] constexpr const T* end() const noexcept { return data() + size(); }
  [[nodiscard]] constexpr const T* cend() const noexcept { return data() + size(); }

  [[nodiscard]] T length() const noexcept { return std::sqrt(x * x + y * y + z * z); }

  [[nodiscard]] Vector3 normalized() const noexcept {
    const auto value = length();
    return value == T{} ? Vector3{} : (*this / value);
  }

  [[nodiscard]] constexpr Vector3 operator+(const Vector3& rhs) const noexcept {
    return {x + rhs.x, y + rhs.y, z + rhs.z};
  }

  [[nodiscard]] constexpr Vector3 operator-(const Vector3& rhs) const noexcept {
    return {x - rhs.x, y - rhs.y, z - rhs.z};
  }

  [[nodiscard]] constexpr Vector3 operator*(T scalar) const noexcept { return {x * scalar, y * scalar, z * scalar}; }

  [[nodiscard]] constexpr Vector3 operator/(T scalar) const noexcept { return (*this) * (T{1} / scalar); }

  constexpr Vector3& operator+=(const Vector3& rhs) noexcept {
    x += rhs.x;
    y += rhs.y;
    z += rhs.z;
    return *this;
  }

  constexpr Vector3& operator-=(const Vector3& rhs) noexcept {
    x -= rhs.x;
    y -= rhs.y;
    z -= rhs.z;
    return *this;
  }

  constexpr Vector3& operator*=(T scalar) noexcept {
    x *= scalar;
    y *= scalar;
    z *= scalar;
    return *this;
  }

  constexpr Vector3& operator/=(T scalar) noexcept { return (*this) *= (T{1} / scalar); }

  [[nodiscard]] static constexpr Vector3 cross(const Vector3& lhs, const Vector3& rhs) noexcept {
    return {
      lhs.y * rhs.z - lhs.z * rhs.y,
      lhs.z * rhs.x - lhs.x * rhs.z,
      lhs.x * rhs.y - lhs.y * rhs.x,
    };
  }

  [[nodiscard]] static constexpr T dot(const Vector3& lhs, const Vector3& rhs) noexcept {
    return lhs.x * rhs.x + lhs.y * rhs.y + lhs.z * rhs.z;
  }

  [[nodiscard]] static T angle(const Vector3& lhs, const Vector3& rhs) noexcept {
    const auto projection = std::clamp(dot(lhs.normalized(), rhs.normalized()), T{-1}, T{1});
    return std::acos(projection);
  }

  [[nodiscard]] std::string to_string() const {
    std::stringstream ss;
    ss << '(' << x << ", " << y << ", " << z << ')';
    return ss.str();
  }
};

template <std::floating_point T> struct Vector4 {
  T x{};
  T y{};
  T z{};
  T w{};

  constexpr Vector4() noexcept = default;
  constexpr Vector4(T x_value, T y_value, T z_value, T w_value) noexcept
      : x(x_value), y(y_value), z(z_value), w(w_value) {}

  [[nodiscard]] static constexpr std::size_t size() noexcept { return 4U; }

  [[nodiscard]] constexpr T* data() noexcept { return &x; }
  [[nodiscard]] constexpr const T* data() const noexcept { return &x; }

  [[nodiscard]] constexpr T& operator[](const std::size_t index) noexcept {
    return index == 0U ? x : (index == 1U ? y : (index == 2U ? z : w));
  }

  [[nodiscard]] constexpr const T& operator[](const std::size_t index) const noexcept {
    return index == 0U ? x : (index == 1U ? y : (index == 2U ? z : w));
  }

  [[nodiscard]] constexpr T& at(const std::size_t index) {
    if (index >= size()) {
      throw std::out_of_range("Vector4 index is outside the vector");
    }
    return (*this)[index];
  }

  [[nodiscard]] constexpr const T& at(const std::size_t index) const {
    if (index >= size()) {
      throw std::out_of_range("Vector4 index is outside the vector");
    }
    return (*this)[index];
  }

  [[nodiscard]] constexpr T* begin() noexcept { return data(); }
  [[nodiscard]] constexpr const T* begin() const noexcept { return data(); }
  [[nodiscard]] constexpr const T* cbegin() const noexcept { return data(); }
  [[nodiscard]] constexpr T* end() noexcept { return data() + size(); }
  [[nodiscard]] constexpr const T* end() const noexcept { return data() + size(); }
  [[nodiscard]] constexpr const T* cend() const noexcept { return data() + size(); }

  [[nodiscard]] T length() const noexcept { return std::sqrt(x * x + y * y + z * z + w * w); }

  [[nodiscard]] Vector4 normalized() const noexcept {
    const auto value = length();
    return value == T{} ? Vector4{} : (*this / value);
  }

  [[nodiscard]] constexpr Vector4 operator+(const Vector4& rhs) const noexcept {
    return {x + rhs.x, y + rhs.y, z + rhs.z, w + rhs.w};
  }

  [[nodiscard]] constexpr Vector4 operator-(const Vector4& rhs) const noexcept {
    return {x - rhs.x, y - rhs.y, z - rhs.z, w - rhs.w};
  }

  [[nodiscard]] constexpr Vector4 operator*(T scalar) const noexcept {
    return {x * scalar, y * scalar, z * scalar, w * scalar};
  }

  [[nodiscard]] constexpr Vector4 operator/(T scalar) const noexcept { return (*this) * (T{1} / scalar); }

  constexpr Vector4& operator+=(const Vector4& rhs) noexcept {
    x += rhs.x;
    y += rhs.y;
    z += rhs.z;
    w += rhs.w;
    return *this;
  }

  constexpr Vector4& operator-=(const Vector4& rhs) noexcept {
    x -= rhs.x;
    y -= rhs.y;
    z -= rhs.z;
    w -= rhs.w;
    return *this;
  }

  constexpr Vector4& operator*=(T scalar) noexcept {
    x *= scalar;
    y *= scalar;
    z *= scalar;
    w *= scalar;
    return *this;
  }

  constexpr Vector4& operator/=(T scalar) noexcept { return (*this) *= (T{1} / scalar); }

  [[nodiscard]] static constexpr T dot(const Vector4& lhs, const Vector4& rhs) noexcept {
    return lhs.x * rhs.x + lhs.y * rhs.y + lhs.z * rhs.z + lhs.w * rhs.w;
  }

  [[nodiscard]] static T angle(const Vector4& lhs, const Vector4& rhs) noexcept {
    const auto projection = std::clamp(dot(lhs.normalized(), rhs.normalized()), T{-1}, T{1});
    return std::acos(projection);
  }

  [[nodiscard]] std::string to_string() const {
    std::stringstream ss;
    ss << '(' << x << ", " << y << ", " << z << ", " << w << ')';
    return ss.str();
  }
};

template <std::floating_point T> struct Matrix3 {
  constexpr Matrix3() noexcept = default;

  constexpr Matrix3(T m00, T m01, T m02, T m10, T m11, T m12, T m20, T m21, T m22) noexcept
      : values_{m00, m01, m02, m10, m11, m12, m20, m21, m22} {}

  [[nodiscard]] static constexpr std::size_t rows() noexcept { return 3U; }
  [[nodiscard]] static constexpr std::size_t cols() noexcept { return 3U; }
  [[nodiscard]] static constexpr std::size_t size() noexcept { return 9U; }

  [[nodiscard]] constexpr T* data() noexcept { return values_.data(); }
  [[nodiscard]] constexpr const T* data() const noexcept { return values_.data(); }

  [[nodiscard]] constexpr T& operator[](const std::size_t index) noexcept { return values_[index]; }
  [[nodiscard]] constexpr const T& operator[](const std::size_t index) const noexcept { return values_[index]; }

  [[nodiscard]] constexpr T& at(const std::size_t index) {
    if (index >= size()) {
      throw std::out_of_range("Matrix3 index is outside the matrix");
    }
    return values_[index];
  }

  [[nodiscard]] constexpr const T& at(const std::size_t index) const {
    if (index >= size()) {
      throw std::out_of_range("Matrix3 index is outside the matrix");
    }
    return values_[index];
  }

  [[nodiscard]] constexpr T& operator()(const std::size_t row, const std::size_t col) {
    if (row >= rows() || col >= cols()) {
      throw std::out_of_range("Matrix3 row or column is outside the matrix");
    }
    return values_[row * cols() + col];
  }

  [[nodiscard]] constexpr const T& operator()(const std::size_t row, const std::size_t col) const {
    if (row >= rows() || col >= cols()) {
      throw std::out_of_range("Matrix3 row or column is outside the matrix");
    }
    return values_[row * cols() + col];
  }

  [[nodiscard]] constexpr T* begin() noexcept { return data(); }
  [[nodiscard]] constexpr const T* begin() const noexcept { return data(); }
  [[nodiscard]] constexpr const T* cbegin() const noexcept { return data(); }
  [[nodiscard]] constexpr T* end() noexcept { return data() + size(); }
  [[nodiscard]] constexpr const T* end() const noexcept { return data() + size(); }
  [[nodiscard]] constexpr const T* cend() const noexcept { return data() + size(); }

private:
  std::array<T, 9> values_{};
};

template <std::floating_point T> struct Matrix2x4 {
  constexpr Matrix2x4() noexcept = default;

  constexpr Matrix2x4(T m00, T m01, T m02, T m03, T m10, T m11, T m12, T m13) noexcept
      : values_{m00, m01, m02, m03, m10, m11, m12, m13} {}

  [[nodiscard]] static constexpr std::size_t rows() noexcept { return 2U; }
  [[nodiscard]] static constexpr std::size_t cols() noexcept { return 4U; }
  [[nodiscard]] static constexpr std::size_t size() noexcept { return 8U; }

  [[nodiscard]] constexpr T* data() noexcept { return values_.data(); }
  [[nodiscard]] constexpr const T* data() const noexcept { return values_.data(); }

  [[nodiscard]] constexpr T& operator[](const std::size_t index) noexcept { return values_[index]; }
  [[nodiscard]] constexpr const T& operator[](const std::size_t index) const noexcept { return values_[index]; }

  [[nodiscard]] constexpr T& at(const std::size_t index) {
    if (index >= size()) {
      throw std::out_of_range("Matrix2x4 index is outside the matrix");
    }
    return values_[index];
  }

  [[nodiscard]] constexpr const T& at(const std::size_t index) const {
    if (index >= size()) {
      throw std::out_of_range("Matrix2x4 index is outside the matrix");
    }
    return values_[index];
  }

  [[nodiscard]] constexpr T& operator()(const std::size_t row, const std::size_t col) {
    if (row >= rows() || col >= cols()) {
      throw std::out_of_range("Matrix2x4 row or column is outside the matrix");
    }
    return values_[row * cols() + col];
  }

  [[nodiscard]] constexpr const T& operator()(const std::size_t row, const std::size_t col) const {
    if (row >= rows() || col >= cols()) {
      throw std::out_of_range("Matrix2x4 row or column is outside the matrix");
    }
    return values_[row * cols() + col];
  }

  [[nodiscard]] constexpr T* begin() noexcept { return data(); }
  [[nodiscard]] constexpr const T* begin() const noexcept { return data(); }
  [[nodiscard]] constexpr const T* cbegin() const noexcept { return data(); }
  [[nodiscard]] constexpr T* end() noexcept { return data() + size(); }
  [[nodiscard]] constexpr const T* end() const noexcept { return data() + size(); }
  [[nodiscard]] constexpr const T* cend() const noexcept { return data() + size(); }

private:
  std::array<T, 8> values_{};
};

template <std::floating_point T> struct Matrix4x2 {
  constexpr Matrix4x2() noexcept = default;

  constexpr Matrix4x2(T m00, T m01, T m10, T m11, T m20, T m21, T m30, T m31) noexcept
      : values_{m00, m01, m10, m11, m20, m21, m30, m31} {}

  [[nodiscard]] static constexpr std::size_t rows() noexcept { return 4U; }
  [[nodiscard]] static constexpr std::size_t cols() noexcept { return 2U; }
  [[nodiscard]] static constexpr std::size_t size() noexcept { return 8U; }

  [[nodiscard]] constexpr T* data() noexcept { return values_.data(); }
  [[nodiscard]] constexpr const T* data() const noexcept { return values_.data(); }

  [[nodiscard]] constexpr T& operator[](const std::size_t index) noexcept { return values_[index]; }
  [[nodiscard]] constexpr const T& operator[](const std::size_t index) const noexcept { return values_[index]; }

  [[nodiscard]] constexpr T& at(const std::size_t index) {
    if (index >= size()) {
      throw std::out_of_range("Matrix4x2 index is outside the matrix");
    }
    return values_[index];
  }

  [[nodiscard]] constexpr const T& at(const std::size_t index) const {
    if (index >= size()) {
      throw std::out_of_range("Matrix4x2 index is outside the matrix");
    }
    return values_[index];
  }

  [[nodiscard]] constexpr T& operator()(const std::size_t row, const std::size_t col) {
    if (row >= rows() || col >= cols()) {
      throw std::out_of_range("Matrix4x2 row or column is outside the matrix");
    }
    return values_[row * cols() + col];
  }

  [[nodiscard]] constexpr const T& operator()(const std::size_t row, const std::size_t col) const {
    if (row >= rows() || col >= cols()) {
      throw std::out_of_range("Matrix4x2 row or column is outside the matrix");
    }
    return values_[row * cols() + col];
  }

  [[nodiscard]] constexpr T* begin() noexcept { return data(); }
  [[nodiscard]] constexpr const T* begin() const noexcept { return data(); }
  [[nodiscard]] constexpr const T* cbegin() const noexcept { return data(); }
  [[nodiscard]] constexpr T* end() noexcept { return data() + size(); }
  [[nodiscard]] constexpr const T* end() const noexcept { return data() + size(); }
  [[nodiscard]] constexpr const T* cend() const noexcept { return data() + size(); }

private:
  std::array<T, 8> values_{};
};

using Vector3f = Vector3<float>;
using Vector3d = Vector3<double>;

using Matrix3f = Matrix3<float>;
using Matrix3d = Matrix3<double>;
using Matrix2x4f = Matrix2x4<float>;
using Matrix2x4d = Matrix2x4<double>;
using Matrix4x2f = Matrix4x2<float>;
using Matrix4x2d = Matrix4x2<double>;
using Vector2f = Vector2<float>;
using Vector2d = Vector2<double>;
using Vector4f = Vector4<float>;
using Vector4d = Vector4<double>;

inline constexpr float matrix3f_singularity_epsilon = 1.0e-6F;
inline constexpr double matrix3d_singularity_epsilon = 1.0e-12;

template <std::floating_point T>
inline constexpr T matrix3_singularity_epsilon_v = static_cast<T>(matrix3d_singularity_epsilon);

template <> inline constexpr float matrix3_singularity_epsilon_v<float> = matrix3f_singularity_epsilon;

template <> inline constexpr double matrix3_singularity_epsilon_v<double> = matrix3d_singularity_epsilon;

template <std::floating_point T = double> [[nodiscard]] constexpr Matrix3<T> identity_matrix3() noexcept {
  return {
    T{1}, T{}, T{}, T{}, T{1}, T{}, T{}, T{}, T{1},
  };
}

template <std::floating_point T> [[nodiscard]] inline Matrix3<T> rotation_x(const T radians) noexcept {
  const T cosine = std::cos(radians);
  const T sine = std::sin(radians);
  return {
    T{1}, T{}, T{}, T{}, cosine, sine, T{}, -sine, cosine,
  };
}

template <std::floating_point T> [[nodiscard]] inline Matrix3<T> rotation_y(const T radians) noexcept {
  const T cosine = std::cos(radians);
  const T sine = std::sin(radians);
  return {
    cosine, T{}, -sine, T{}, T{1}, T{}, sine, T{}, cosine,
  };
}

template <std::floating_point T> [[nodiscard]] inline Matrix3<T> rotation_z(const T radians) noexcept {
  const T cosine = std::cos(radians);
  const T sine = std::sin(radians);
  return {
    cosine, sine, T{}, -sine, cosine, T{}, T{}, T{}, T{1},
  };
}

template <std::floating_point T>
[[nodiscard]] constexpr Matrix3<T> multiply(const Matrix3<T>& lhs, const Matrix3<T>& rhs) noexcept {
  return {
    lhs[0] * rhs[0] + lhs[1] * rhs[3] + lhs[2] * rhs[6], lhs[0] * rhs[1] + lhs[1] * rhs[4] + lhs[2] * rhs[7],
    lhs[0] * rhs[2] + lhs[1] * rhs[5] + lhs[2] * rhs[8], lhs[3] * rhs[0] + lhs[4] * rhs[3] + lhs[5] * rhs[6],
    lhs[3] * rhs[1] + lhs[4] * rhs[4] + lhs[5] * rhs[7], lhs[3] * rhs[2] + lhs[4] * rhs[5] + lhs[5] * rhs[8],
    lhs[6] * rhs[0] + lhs[7] * rhs[3] + lhs[8] * rhs[6], lhs[6] * rhs[1] + lhs[7] * rhs[4] + lhs[8] * rhs[7],
    lhs[6] * rhs[2] + lhs[7] * rhs[5] + lhs[8] * rhs[8],
  };
}

template <std::floating_point T>
[[nodiscard]] constexpr Vector3<T> matrix_times_point(const Matrix3<T>& matrix, const Vector3<T> point) noexcept {
  return {
    matrix[0] * point.x + matrix[1] * point.y + matrix[2] * point.z,
    matrix[3] * point.x + matrix[4] * point.y + matrix[5] * point.z,
    matrix[6] * point.x + matrix[7] * point.y + matrix[8] * point.z,
  };
}

template <std::floating_point T>
[[nodiscard]] constexpr Vector3<T> point_times_matrix(const Vector3<T> point, const Matrix3<T>& matrix) noexcept {
  return {
    point.x * matrix[0] + point.y * matrix[3] + point.z * matrix[6],
    point.x * matrix[1] + point.y * matrix[4] + point.z * matrix[7],
    point.x * matrix[2] + point.y * matrix[5] + point.z * matrix[8],
  };
}

template <std::floating_point T> [[nodiscard]] constexpr T determinant(const Matrix3<T>& matrix) noexcept {
  return matrix[0] * (matrix[4] * matrix[8] - matrix[5] * matrix[7]) -
         matrix[1] * (matrix[3] * matrix[8] - matrix[5] * matrix[6]) +
         matrix[2] * (matrix[3] * matrix[7] - matrix[4] * matrix[6]);
}

template <std::floating_point T>
[[nodiscard]] inline std::optional<Matrix3<T>>
inverse(const Matrix3<T>& matrix, const T singular_epsilon = matrix3_singularity_epsilon_v<T>) noexcept {
  const T det = determinant(matrix);
  if (std::fabs(det) <= std::fabs(singular_epsilon)) {
    return std::nullopt;
  }

  const T scale = T{1} / det;
  return Matrix3<T>{
    (matrix[4] * matrix[8] - matrix[5] * matrix[7]) * scale, (matrix[2] * matrix[7] - matrix[1] * matrix[8]) * scale,
    (matrix[1] * matrix[5] - matrix[2] * matrix[4]) * scale, (matrix[5] * matrix[6] - matrix[3] * matrix[8]) * scale,
    (matrix[0] * matrix[8] - matrix[2] * matrix[6]) * scale, (matrix[2] * matrix[3] - matrix[0] * matrix[5]) * scale,
    (matrix[3] * matrix[7] - matrix[4] * matrix[6]) * scale, (matrix[1] * matrix[6] - matrix[0] * matrix[7]) * scale,
    (matrix[0] * matrix[4] - matrix[1] * matrix[3]) * scale,
  };
}

template <std::floating_point T>
[[nodiscard]] inline std::optional<Matrix2x4<T>>
pseudo_inverse_4x2(const Matrix4x2<T>& matrix,
                   const T singular_epsilon = std::numeric_limits<T>::epsilon() * T{64}) noexcept {
  const auto a00 = matrix[0];
  const auto a01 = matrix[1];
  const auto a10 = matrix[2];
  const auto a11 = matrix[3];
  const auto a20 = matrix[4];
  const auto a21 = matrix[5];
  const auto a30 = matrix[6];
  const auto a31 = matrix[7];

  const auto ata00 = a00 * a00 + a10 * a10 + a20 * a20 + a30 * a30;
  const auto ata01 = a00 * a01 + a10 * a11 + a20 * a21 + a30 * a31;
  const auto ata11 = a01 * a01 + a11 * a11 + a21 * a21 + a31 * a31;

  const auto determinant = ata00 * ata11 - ata01 * ata01;
  if (std::fabs(determinant) <= std::fabs(singular_epsilon)) {
    return std::nullopt;
  }

  const auto scale = T{1} / determinant;
  const auto inv00 = ata11 * scale;
  const auto inv01 = -ata01 * scale;
  const auto inv10 = -ata01 * scale;
  const auto inv11 = ata00 * scale;

  Matrix2x4<T> output{};
  output[0] = inv00 * a00 + inv01 * a01;
  output[1] = inv00 * a10 + inv01 * a11;
  output[2] = inv00 * a20 + inv01 * a21;
  output[3] = inv00 * a30 + inv01 * a31;
  output[4] = inv10 * a00 + inv11 * a01;
  output[5] = inv10 * a10 + inv11 * a11;
  output[6] = inv10 * a20 + inv11 * a21;
  output[7] = inv10 * a30 + inv11 * a31;
  return output;
}

template <std::floating_point T>
[[nodiscard]] constexpr Vector2<T> multiply(const Matrix2x4<T>& matrix, const Vector4<T>& vector) noexcept {
  return Vector2<T>{
    matrix[0] * vector[0] + matrix[1] * vector[1] + matrix[2] * vector[2] + matrix[3] * vector[3],
    matrix[4] * vector[0] + matrix[5] * vector[1] + matrix[6] * vector[2] + matrix[7] * vector[3],
  };
}

template <std::floating_point T>
[[nodiscard]] inline std::optional<Vector2<T>>
least_squares_4x2(const Matrix4x2<T>& matrix, const Vector4<T>& rhs,
                  const T singular_epsilon = std::numeric_limits<T>::epsilon() * T{64}) noexcept {
  const auto pseudo_inverse = pseudo_inverse_4x2(matrix, singular_epsilon);
  if (!pseudo_inverse.has_value()) {
    return std::nullopt;
  }

  return multiply(*pseudo_inverse, rhs);
}

template <std::floating_point T>
[[nodiscard]] inline Vector2<T> multiply(const Matrix2x4<T>& matrix, const T x0, const T x1, const T x2,
                                         const T x3) noexcept {
  return Vector2<T>{
    matrix[0] * x0 + matrix[1] * x1 + matrix[2] * x2 + matrix[3] * x3,
    matrix[4] * x0 + matrix[5] * x1 + matrix[6] * x2 + matrix[7] * x3,
  };
}

} // namespace ksj::linalg
