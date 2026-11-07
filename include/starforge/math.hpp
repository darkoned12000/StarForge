#pragma once

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace starforge::math
{

// Small scalar/vector/matrix value types for the public API. These are
// deliberately independent of GLM so public consumers never see a
// third-party math type or gain access to backend implementation details.
template <class Scalar, std::size_t Count>
struct Vector
{
    std::array<Scalar, Count> data{};

    constexpr Vector() = default;
    constexpr explicit Vector(Scalar value)
    {
        data.fill(value);
    }

    template <class... Values>
        requires(sizeof...(Values) == Count)
    constexpr Vector(Values... values) : data{static_cast<Scalar>(values)...}
    {
    }

    constexpr Scalar& operator[](std::size_t index) noexcept { return data[index]; }
    constexpr const Scalar& operator[](std::size_t index) const noexcept { return data[index]; }

    constexpr const Scalar& x() const noexcept { return data[0]; }
    constexpr Scalar& x() noexcept { return data[0]; }

    constexpr const Scalar& y() const noexcept requires(Count >= 2) { return data[1]; }
    constexpr Scalar& y() noexcept requires(Count >= 2) { return data[1]; }

    constexpr const Scalar& z() const noexcept requires(Count >= 3) { return data[2]; }
    constexpr Scalar& z() noexcept requires(Count >= 3) { return data[2]; }

    constexpr const Scalar& w() const noexcept requires(Count >= 4) { return data[3]; }
    constexpr Scalar& w() noexcept requires(Count >= 4) { return data[3]; }

    constexpr bool operator==(const Vector&) const noexcept = default;
    constexpr bool operator!=(const Vector&) const noexcept = default;
};

template <class Scalar, std::size_t Count>
[[nodiscard]] constexpr Vector<Scalar, Count> operator-(const Vector<Scalar, Count>& value) noexcept
{
    Vector<Scalar, Count> result{};
    for (std::size_t i = 0; i < Count; ++i)
    {
        result.data[i] = -value.data[i];
    }
    return result;
}

template <class Scalar, std::size_t Count>
[[nodiscard]] constexpr Vector<Scalar, Count> operator+(
    const Vector<Scalar, Count>& lhs, const Vector<Scalar, Count>& rhs) noexcept
{
    Vector<Scalar, Count> result{};
    for (std::size_t i = 0; i < Count; ++i)
    {
        result.data[i] = lhs.data[i] + rhs.data[i];
    }
    return result;
}

template <class Scalar, std::size_t Count>
[[nodiscard]] constexpr Vector<Scalar, Count> operator-(
    const Vector<Scalar, Count>& lhs, const Vector<Scalar, Count>& rhs) noexcept
{
    Vector<Scalar, Count> result{};
    for (std::size_t i = 0; i < Count; ++i)
    {
        result.data[i] = lhs.data[i] - rhs.data[i];
    }
    return result;
}

template <class Scalar, std::size_t Count>
[[nodiscard]] constexpr Vector<Scalar, Count> operator*(const Vector<Scalar, Count>& value, Scalar scalar) noexcept
{
    Vector<Scalar, Count> result{};
    for (std::size_t i = 0; i < Count; ++i)
    {
        result.data[i] = value.data[i] * scalar;
    }
    return result;
}

template <class Scalar, std::size_t Count>
[[nodiscard]] constexpr Vector<Scalar, Count> operator*(Scalar scalar, const Vector<Scalar, Count>& value) noexcept
{
    return value * scalar;
}

// Component-wise (Hadamard) product, e.g. material tint modulation.
template <class Scalar, std::size_t Count>
[[nodiscard]] constexpr Vector<Scalar, Count> operator*(
    const Vector<Scalar, Count>& lhs, const Vector<Scalar, Count>& rhs) noexcept
{
    Vector<Scalar, Count> result{};
    for (std::size_t i = 0; i < Count; ++i)
    {
        result.data[i] = lhs.data[i] * rhs.data[i];
    }
    return result;
}

template <class Scalar, std::size_t Count>
[[nodiscard]] constexpr Vector<Scalar, Count> operator/(const Vector<Scalar, Count>& value, Scalar divisor) noexcept
{
    Vector<Scalar, Count> result{};
    for (std::size_t i = 0; i < Count; ++i)
    {
        result.data[i] = value.data[i] / divisor;
    }
    return result;
}

template <class Scalar, std::size_t Count>
constexpr Vector<Scalar, Count>& operator+=(Vector<Scalar, Count>& lhs, const Vector<Scalar, Count>& rhs) noexcept
{
    for (std::size_t i = 0; i < Count; ++i)
    {
        lhs.data[i] += rhs.data[i];
    }
    return lhs;
}

template <class Scalar, std::size_t Count>
constexpr Vector<Scalar, Count>& operator-=(Vector<Scalar, Count>& lhs, const Vector<Scalar, Count>& rhs) noexcept
{
    for (std::size_t i = 0; i < Count; ++i)
    {
        lhs.data[i] -= rhs.data[i];
    }
    return lhs;
}

template <class Scalar, std::size_t Count>
constexpr Vector<Scalar, Count>& operator*=(Vector<Scalar, Count>& lhs, Scalar scalar) noexcept
{
    for (std::size_t i = 0; i < Count; ++i)
    {
        lhs.data[i] *= scalar;
    }
    return lhs;
}

template <class Scalar, std::size_t Count>
constexpr Vector<Scalar, Count>& operator/=(Vector<Scalar, Count>& lhs, Scalar divisor) noexcept
{
    for (std::size_t i = 0; i < Count; ++i)
    {
        lhs.data[i] /= divisor;
    }
    return lhs;
}

using Vec2f = Vector<float, 2>;
using Vec2d = Vector<double, 2>;
using Vec3f = Vector<float, 3>;
using Vec3d = Vector<double, 3>;
using Vec4f = Vector<float, 4>;
using Vec4d = Vector<double, 4>;

template <class Scalar, std::size_t Count>
[[nodiscard]] constexpr Scalar dot(const Vector<Scalar, Count>& lhs, const Vector<Scalar, Count>& rhs) noexcept
{
    Scalar sum = Scalar{};
    for (std::size_t i = 0; i < Count; ++i)
    {
        sum += lhs.data[i] * rhs.data[i];
    }
    return sum;
}

template <class Scalar>
[[nodiscard]] constexpr Vector<Scalar, 3> cross(const Vector<Scalar, 3>& lhs, const Vector<Scalar, 3>& rhs) noexcept
{
    return Vector<Scalar, 3>{
        lhs.y() * rhs.z() - lhs.z() * rhs.y(),
        lhs.z() * rhs.x() - lhs.x() * rhs.z(),
        lhs.x() * rhs.y() - lhs.y() * rhs.x(),
    };
}

template <class Scalar, std::size_t Count>
[[nodiscard]] inline Scalar length(const Vector<Scalar, Count>& value) noexcept
{
    return std::sqrt(dot(value, value));
}

template <class Scalar, std::size_t Count>
[[nodiscard]] inline Vector<Scalar, Count> normalize(const Vector<Scalar, Count>& value) noexcept
{
    const Scalar len = length(value);
    return len > Scalar{} ? value / len : Vector<Scalar, Count>{};
}

template <class Scalar>
struct Matrix3
{
    // Column-major storage: columns[column][row].
    std::array<std::array<Scalar, 3>, 3> columns{};

    constexpr Matrix3() = default;
    constexpr explicit Matrix3(Scalar diagonal) noexcept
    {
        columns[0][0] = diagonal;
        columns[1][1] = diagonal;
        columns[2][2] = diagonal;
    }

    constexpr bool operator==(const Matrix3&) const noexcept = default;
    constexpr bool operator!=(const Matrix3&) const noexcept = default;
};

using Mat3f = Matrix3<float>;
using Mat3d = Matrix3<double>;

template <class Scalar>
[[nodiscard]] inline Matrix3<Scalar> operator*(const Matrix3<Scalar>& lhs, const Matrix3<Scalar>& rhs) noexcept
{
    Matrix3<Scalar> result{};
    for (std::size_t column = 0; column < 3; ++column)
    {
        for (std::size_t row = 0; row < 3; ++row)
        {
            Scalar sum = Scalar{};
            for (std::size_t k = 0; k < 3; ++k)
            {
                sum += lhs.columns[k][row] * rhs.columns[column][k];
            }
            result.columns[column][row] = sum;
        }
    }
    return result;
}

template <class Scalar>
[[nodiscard]] inline Vector<Scalar, 3> operator*(const Matrix3<Scalar>& matrix, const Vector<Scalar, 3>& vector) noexcept
{
    return {
        matrix.columns[0][0] * vector.x() + matrix.columns[1][0] * vector.y() + matrix.columns[2][0] * vector.z(),
        matrix.columns[0][1] * vector.x() + matrix.columns[1][1] * vector.y() + matrix.columns[2][1] * vector.z(),
        matrix.columns[0][2] * vector.x() + matrix.columns[1][2] * vector.y() + matrix.columns[2][2] * vector.z(),
    };
}

template <class Scalar>
struct Matrix4
{
    // Column-major storage: columns[column][row].
    std::array<std::array<Scalar, 4>, 4> columns{};

    constexpr Matrix4() = default;
    constexpr explicit Matrix4(Scalar diagonal) noexcept
    {
        columns[0][0] = diagonal;
        columns[1][1] = diagonal;
        columns[2][2] = diagonal;
        columns[3][3] = diagonal;
    }

    constexpr bool operator==(const Matrix4&) const noexcept = default;
    constexpr bool operator!=(const Matrix4&) const noexcept = default;
};

using Mat4f = Matrix4<float>;
using Mat4d = Matrix4<double>;

template <class Scalar>
[[nodiscard]] inline Matrix4<Scalar> operator*(const Matrix4<Scalar>& lhs, const Matrix4<Scalar>& rhs) noexcept
{
    Matrix4<Scalar> result{};
    for (std::size_t column = 0; column < 4; ++column)
    {
        for (std::size_t row = 0; row < 4; ++row)
        {
            Scalar sum = Scalar{};
            for (std::size_t k = 0; k < 4; ++k)
            {
                sum += lhs.columns[k][row] * rhs.columns[column][k];
            }
            result.columns[column][row] = sum;
        }
    }
    return result;
}

template <class Scalar>
[[nodiscard]] inline Vector<Scalar, 4> operator*(const Matrix4<Scalar>& matrix, const Vector<Scalar, 4>& vector) noexcept
{
    return {
        matrix.columns[0][0] * vector.x() + matrix.columns[1][0] * vector.y() +
            matrix.columns[2][0] * vector.z() + matrix.columns[3][0] * vector.w(),
        matrix.columns[0][1] * vector.x() + matrix.columns[1][1] * vector.y() +
            matrix.columns[2][1] * vector.z() + matrix.columns[3][1] * vector.w(),
        matrix.columns[0][2] * vector.x() + matrix.columns[1][2] * vector.y() +
            matrix.columns[2][2] * vector.z() + matrix.columns[3][2] * vector.w(),
        matrix.columns[0][3] * vector.x() + matrix.columns[1][3] * vector.y() +
            matrix.columns[2][3] * vector.z() + matrix.columns[3][3] * vector.w(),
    };
}

// General 4x4 inverse (adjugate/cofactor method, column-major storage).
// Returns the identity for singular matrices.
[[nodiscard]] inline Mat4f inverse(const Mat4f& matrix) noexcept
{
    const auto& m = matrix.columns;
    // clang-format off
    const float det =
        m[0][0] * (m[1][1] * (m[2][2] * m[3][3] - m[2][3] * m[3][2]) - m[1][2] * (m[2][1] * m[3][3] - m[2][3] * m[3][1]) + m[1][3] * (m[2][1] * m[3][2] - m[2][2] * m[3][1])) -
        m[0][1] * (m[1][0] * (m[2][2] * m[3][3] - m[2][3] * m[3][2]) - m[1][2] * (m[2][0] * m[3][3] - m[2][3] * m[3][0]) + m[1][3] * (m[2][0] * m[3][2] - m[2][2] * m[3][0])) +
        m[0][2] * (m[1][0] * (m[2][1] * m[3][3] - m[2][3] * m[3][1]) - m[1][1] * (m[2][0] * m[3][3] - m[2][3] * m[3][0]) + m[1][3] * (m[2][0] * m[3][1] - m[2][1] * m[3][0])) -
        m[0][3] * (m[1][0] * (m[2][1] * m[3][2] - m[2][2] * m[3][1]) - m[1][1] * (m[2][0] * m[3][2] - m[2][2] * m[3][0]) + m[1][2] * (m[2][0] * m[3][1] - m[2][1] * m[3][0]));
    // clang-format on
    if (det == 0.0f)
    {
        return Mat4f(1.0f);
    }
    const float invDet = 1.0f / det;
    Mat4f result;
    for (std::size_t column = 0; column < 4; ++column)
    {
        for (std::size_t row = 0; row < 4; ++row)
        {
            // Cofactor of (row, column), transposed for the adjugate.
            std::size_t k[3];
            std::size_t l[3];
            std::size_t ki = 0;
            std::size_t li = 0;
            for (std::size_t i = 0; i < 4; ++i)
            {
                if (i != row)
                {
                    k[ki++] = i;
                }
                if (i != column)
                {
                    l[li++] = i;
                }
            }
            const float cofactor = (m[k[0]][l[0]] * (m[k[1]][l[1]] * m[k[2]][l[2]] - m[k[1]][l[2]] * m[k[2]][l[1]]) -
                                    m[k[0]][l[1]] * (m[k[1]][l[0]] * m[k[2]][l[2]] - m[k[1]][l[2]] * m[k[2]][l[0]]) +
                                    m[k[0]][l[2]] * (m[k[1]][l[0]] * m[k[2]][l[1]] - m[k[1]][l[1]] * m[k[2]][l[0]]));
            const float sign = ((row + column) % 2 == 0) ? 1.0f : -1.0f;
            result.columns[column][row] = sign * cofactor * invDet;
        }
    }
    return result;
}

// Pure element-wise cast; no backend dependency, so this lives in the public
// header instead of crossing the world bridge.
[[nodiscard]] inline Mat4f toFloat32(const Mat4d& matrix) noexcept
{
    Mat4f result;
    for (std::size_t column = 0; column < 4; ++column)
    {
        for (std::size_t row = 0; row < 4; ++row)
        {
            result.columns[column][row] = static_cast<float>(matrix.columns[column][row]);
        }
    }
    return result;
}

// World-space basis conventions: right-handed, Y-up, camera looks down -Z.
inline constexpr Vec3f kRightX{1.0f, 0.0f, 0.0f};
inline constexpr Vec3f kUpY{0.0f, 1.0f, 0.0f};
inline constexpr Vec3f kForwardZ{0.0f, 0.0f, -1.0f};

// Right-handed, Y-up look-at view matrix (camera looks down -Z in view space).
[[nodiscard]] inline Mat4f lookAt(const Vec3f& eye, const Vec3f& center, const Vec3f& up) noexcept
{
    const Vec3f forward = normalize(center - eye);
    const Vec3f side = normalize(cross(forward, up));
    const Vec3f upv = cross(side, forward);
    Mat4f matrix(1.0f);
    matrix.columns[0] = {side.x(), upv.x(), -forward.x(), 0.0f};
    matrix.columns[1] = {side.y(), upv.y(), -forward.y(), 0.0f};
    matrix.columns[2] = {side.z(), upv.z(), -forward.z(), 0.0f};
    matrix.columns[3] = {-dot(side, eye), -dot(upv, eye), dot(forward, eye), 1.0f};
    return matrix;
}

namespace detail
{
[[nodiscard]] inline Mat4f perspectiveDepthZeroToOne(float fovYRadians, float aspect, float nearPlane, float farPlane) noexcept
{
    const float f = 1.0f / std::tan(fovYRadians * 0.5f);
    Mat4f matrix{};
    matrix.columns[0][0] = f / aspect;
    matrix.columns[1][1] = f;
    matrix.columns[2][2] = farPlane / (nearPlane - farPlane);
    matrix.columns[2][3] = -1.0f;
    matrix.columns[3][2] = (farPlane * nearPlane) / (nearPlane - farPlane);
    return matrix;
}
} // namespace detail

// Vulkan clip-space perspective projection (depth in [0, 1]).
[[nodiscard]] inline Mat4f perspectiveVulkan(float fovYRadians, float aspect, float nearPlane, float farPlane) noexcept
{
    return detail::perspectiveDepthZeroToOne(fovYRadians, aspect, nearPlane, farPlane);
}

// Vulkan reverse-Z perspective projection (near -> 1, far -> 0).
[[nodiscard]] inline Mat4f perspectiveVulkanReverseZ(float fovYRadians, float aspect, float nearPlane, float farPlane) noexcept
{
    return detail::perspectiveDepthZeroToOne(fovYRadians, aspect, farPlane, nearPlane);
}

} // namespace starforge::math
