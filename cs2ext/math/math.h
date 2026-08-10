#pragma once
#include <cmath>
extern int screenWidth;
extern int screenHeight;

struct view_matrix_t {
    float matrix[4][4];

    float* operator[](int index) {
        return matrix[index];
    }

    const float* operator[](int index) const {
        return matrix[index];
    }
};


struct Vector3 {
    Vector3(float x = 0.f, float y = 0.f, float z = 0.f) noexcept;
    Vector3 operator-(const Vector3& other) const noexcept;
    Vector3 operator+(const Vector3& other) const noexcept;
    Vector3 operator*(float factor) const noexcept;
    Vector3 operator/(float factor) const noexcept;
    Vector3 w2s(view_matrix_t matrix) const;

    void Normalize() noexcept {
        float length = sqrtf(x * x + y * y + z * z);
        if (length > 0.0f) {
            x /= length;
            y /= length;
            z /= length;
        }
    }

    float Dot(const Vector3& other) const noexcept {
        return x * other.x + y * other.y + z * other.z;
    }

    float x, y, z;
};

struct Vector2 {
    float x, y;
    Vector2(float x = 0.f, float y = 0.f) : x(x), y(y) {}
};