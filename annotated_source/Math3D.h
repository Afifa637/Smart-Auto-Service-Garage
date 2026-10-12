#ifndef MATH3D_H
#define MATH3D_H

// ============================================================================
//  Math3D.h  -  A very small 3D math library written by hand.
// ============================================================================
//
//  WHY DOES THIS FILE EXIST?
//  -------------------------
//  Most OpenGL tutorials use a library called GLM for vectors and matrices.
//  Our project does NOT ship GLM, and adding a new library was not allowed,
//  so this header implements only the handful of operations we actually need:
//
//      Vec3   - a 3D vector  (positions, sizes, colours, directions)
//      Mat4   - a 4x4 matrix (model / view / projection transformations)
//      Mat3   - a 3x3 matrix (used only to transform normals correctly)
//
//  It is header-only: there is no Math3D.cpp, every function is `inline`.
//  Nothing here needs OpenGL, so this file can be read and understood on
//  its own.
//
//  IMPORTANT - MATRIX STORAGE ORDER
//  --------------------------------
//  OpenGL expects matrices in COLUMN-MAJOR order. That means the 16 floats
//  are stored column by column, not row by row:
//
//        m[0]  m[4]  m[8]   m[12]        row 0
//        m[1]  m[5]  m[9]   m[13]        row 1
//        m[2]  m[6]  m[10]  m[14]        row 2
//        m[3]  m[7]  m[11]  m[15]        row 3
//
//  The rule is:   m[ column * 4 + row ]
//
//  Because we already store them the way OpenGL wants, we can upload them
//  with glUniformMatrix4fv(..., GL_FALSE, ...)  (GL_FALSE = "do not transpose").
// ============================================================================

#include <cmath>

// Pi. We define it ourselves because M_PI is not guaranteed by the C++ standard
// on MSVC unless _USE_MATH_DEFINES is defined before <cmath>.
const float PI_F = 3.14159265358979323846f;

// Degrees are friendlier to type; the trig functions need radians.
inline float radians(float degrees) { return degrees * PI_F / 180.0f; }


// ----------------------------------------------------------------------------
//  Vec3 - three floats that we reuse for three different meanings:
//         a POSITION (x, y, z), a SIZE (width, height, depth),
//         or a COLOUR (red, green, blue).
// ----------------------------------------------------------------------------
struct Vec3
{
    float x, y, z;

    Vec3() : x(0.0f), y(0.0f), z(0.0f) {}
    explicit Vec3(float s) : x(s), y(s), z(s) {}
    Vec3(float X, float Y, float Z) : x(X), y(Y), z(Z) {}
};

inline Vec3 operator+(const Vec3& a, const Vec3& b) { return Vec3(a.x + b.x, a.y + b.y, a.z + b.z); }
inline Vec3 operator-(const Vec3& a, const Vec3& b) { return Vec3(a.x - b.x, a.y - b.y, a.z - b.z); }
inline Vec3 operator*(const Vec3& a, float s) { return Vec3(a.x * s, a.y * s, a.z * s); }
inline Vec3 operator*(float s, const Vec3& a) { return Vec3(a.x * s, a.y * s, a.z * s); }
inline Vec3 operator*(const Vec3& a, const Vec3& b) { return Vec3(a.x * b.x, a.y * b.y, a.z * b.z); }
inline Vec3 operator-(const Vec3& a) { return Vec3(-a.x, -a.y, -a.z); }

inline float dot(const Vec3& a, const Vec3& b)
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

inline Vec3 cross(const Vec3& a, const Vec3& b)
{
    return Vec3(a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x);
}

inline float length(const Vec3& a) { return std::sqrt(dot(a, a)); }

inline Vec3 normalize(const Vec3& a)
{
    float len = length(a);
    if (len < 1e-8f) return Vec3(0.0f, 0.0f, 0.0f);
    return a * (1.0f / len);
}


// ----------------------------------------------------------------------------
//  Mat3 - only used to send the "normal matrix" to the shader so that
//         lighting still looks right after non-uniform scaling.
// ----------------------------------------------------------------------------
struct Mat3
{
    float m[9];   // column-major:  m[col * 3 + row]
};


// ----------------------------------------------------------------------------
//  Mat4 - the 4x4 transformation matrix.
// ----------------------------------------------------------------------------
struct Mat4
{
    float m[16];  // column-major:  m[col * 4 + row]
};


// Identity matrix: "do nothing". Multiplying by it leaves a point unchanged.
inline Mat4 mat4Identity()
{
    Mat4 r;
    for (int i = 0; i < 16; ++i) r.m[i] = 0.0f;
    r.m[0] = r.m[5] = r.m[10] = r.m[15] = 1.0f;
    return r;
}


// Matrix multiplication.  C = A * B
//
// Read it right-to-left: the transformation in B happens FIRST, then A.
// So  translate * rotate * scale  means:  scale first, then rotate, then move.
inline Mat4 operator*(const Mat4& A, const Mat4& B)
{
    Mat4 C;
    for (int col = 0; col < 4; ++col)
    {
        for (int row = 0; row < 4; ++row)
        {
            float sum = 0.0f;
            for (int k = 0; k < 4; ++k)
                sum += A.m[k * 4 + row] * B.m[col * 4 + k];
            C.m[col * 4 + row] = sum;
        }
    }
    return C;
}


// TRANSLATION - slides an object to a new position without changing its shape.
//
//     | 1  0  0  tx |
//     | 0  1  0  ty |
//     | 0  0  1  tz |
//     | 0  0  0   1 |
inline Mat4 translate(float tx, float ty, float tz)
{
    Mat4 r = mat4Identity();
    r.m[12] = tx;   // row 0, column 3
    r.m[13] = ty;   // row 1, column 3
    r.m[14] = tz;   // row 2, column 3
    return r;
}
inline Mat4 translate(const Vec3& t) { return translate(t.x, t.y, t.z); }


// SCALING - stretches the object along each axis.
// This is the operation that turns our 1x1x1 cube into a long car body,
// a thin window, a tall wall, or a flat floor.
//
//     | sx  0  0  0 |
//     |  0 sy  0  0 |
//     |  0  0 sz  0 |
//     |  0  0  0  1 |
inline Mat4 scale(float sx, float sy, float sz)
{
    Mat4 r = mat4Identity();
    r.m[0] = sx;    // row 0, column 0
    r.m[5] = sy;    // row 1, column 1
    r.m[10] = sz;   // row 2, column 2
    return r;
}
inline Mat4 scale(const Vec3& s) { return scale(s.x, s.y, s.z); }


// ROTATION about the X axis (object tips forward / backward, like a windshield).
inline Mat4 rotateX(float degrees)
{
    float a = radians(degrees);
    float c = std::cos(a), s = std::sin(a);
    Mat4 r = mat4Identity();
    r.m[5] = c;    // row 1, column 1
    r.m[6] = s;    // row 2, column 1
    r.m[9] = -s;   // row 1, column 2
    r.m[10] = c;   // row 2, column 2
    return r;
}

// ROTATION about the Y axis (object spins like a ceiling fan / turntable).
inline Mat4 rotateY(float degrees)
{
    float a = radians(degrees);
    float c = std::cos(a), s = std::sin(a);
    Mat4 r = mat4Identity();
    r.m[0] = c;    // row 0, column 0
    r.m[2] = -s;   // row 2, column 0
    r.m[8] = s;    // row 0, column 2
    r.m[10] = c;   // row 2, column 2
    return r;
}

// ROTATION about the Z axis (object swings sideways - used for the robot joints).
inline Mat4 rotateZ(float degrees)
{
    float a = radians(degrees);
    float c = std::cos(a), s = std::sin(a);
    Mat4 r = mat4Identity();
    r.m[0] = c;    // row 0, column 0
    r.m[1] = s;    // row 1, column 0
    r.m[4] = -s;   // row 0, column 1
    r.m[5] = c;    // row 1, column 1
    return r;
}


// PERSPECTIVE PROJECTION
// Objects that are farther from the camera are drawn smaller. This is what
// makes the scene look genuinely 3D instead of flat.
//
//   fovYDegrees - vertical field of view (how "wide" the lens is)
//   aspect      - window width / window height
//   zNear/zFar  - the closest and farthest distances that get drawn
inline Mat4 perspective(float fovYDegrees, float aspect, float zNear, float zFar)
{
    float f = 1.0f / std::tan(radians(fovYDegrees) * 0.5f);

    Mat4 r;
    for (int i = 0; i < 16; ++i) r.m[i] = 0.0f;

    r.m[0] = f / aspect;
    r.m[5] = f;
    r.m[10] = (zFar + zNear) / (zNear - zFar);
    r.m[11] = -1.0f;
    r.m[14] = (2.0f * zFar * zNear) / (zNear - zFar);
    return r;
}




// ORTHOGRAPHIC PROJECTION - used by the directional-light shadow map.
inline Mat4 orthographic(float left, float right, float bottom, float top, float zNear, float zFar)
{
    Mat4 r = mat4Identity();
    r.m[0]  = 2.0f / (right - left);
    r.m[5]  = 2.0f / (top - bottom);
    r.m[10] = -2.0f / (zFar - zNear);
    r.m[12] = -(right + left) / (right - left);
    r.m[13] = -(top + bottom) / (top - bottom);
    r.m[14] = -(zFar + zNear) / (zFar - zNear);
    return r;
}

// Transform helpers used for dynamic lights and debug geometry.
inline Vec3 transformPoint(const Mat4& m, const Vec3& p)
{
    return Vec3(
        m.m[0] * p.x + m.m[4] * p.y + m.m[8]  * p.z + m.m[12],
        m.m[1] * p.x + m.m[5] * p.y + m.m[9]  * p.z + m.m[13],
        m.m[2] * p.x + m.m[6] * p.y + m.m[10] * p.z + m.m[14]);
}

inline Vec3 transformDirection(const Mat4& m, const Vec3& d)
{
    return Vec3(
        m.m[0] * d.x + m.m[4] * d.y + m.m[8]  * d.z,
        m.m[1] * d.x + m.m[5] * d.y + m.m[9]  * d.z,
        m.m[2] * d.x + m.m[6] * d.y + m.m[10] * d.z);
}

// VIEW MATRIX ("look at")
// The view matrix moves the whole world so that the camera ends up at the
// origin looking down the -Z axis. Moving the camera right is the same as
// moving the world left - that is all this matrix does.
inline Mat4 lookAt(const Vec3& eye, const Vec3& center, const Vec3& up)
{
    Vec3 f = normalize(center - eye);   // forward
    Vec3 s = normalize(cross(f, up));   // right ("side")
    Vec3 u = cross(s, f);               // true up

    Mat4 r = mat4Identity();
    r.m[0] = s.x;  r.m[4] = s.y;  r.m[8] = s.z;  r.m[12] = -dot(s, eye);
    r.m[1] = u.x;  r.m[5] = u.y;  r.m[9] = u.z;  r.m[13] = -dot(u, eye);
    r.m[2] = -f.x; r.m[6] = -f.y; r.m[10] = -f.z; r.m[14] = dot(f, eye);
    r.m[3] = 0.0f; r.m[7] = 0.0f; r.m[11] = 0.0f; r.m[15] = 1.0f;
    return r;
}


// NORMAL MATRIX
// ------------------------------------------------------------------
// A surface normal is a direction, not a position. If we scale a cube
// non-uniformly (for example 4.2 long but only 0.5 tall, like the car body)
// and then transform the normal with the same matrix, the normal comes out
// pointing in the wrong direction and the shading looks wrong.
//
// The correct matrix for normals is the inverse-transpose of the upper-left
// 3x3 part of the model matrix. That is exactly what this function builds.
// ------------------------------------------------------------------
inline Mat3 normalMatrix(const Mat4& model)
{
    // Pull out the upper-left 3x3 block (remember: m[col * 4 + row]).
    float a00 = model.m[0], a01 = model.m[4], a02 = model.m[8];   // row 0
    float a10 = model.m[1], a11 = model.m[5], a12 = model.m[9];   // row 1
    float a20 = model.m[2], a21 = model.m[6], a22 = model.m[10];  // row 2

    float det = a00 * (a11 * a22 - a12 * a21)
        - a01 * (a10 * a22 - a12 * a20)
        + a02 * (a10 * a21 - a11 * a20);

    Mat3 r;

    if (std::fabs(det) < 1e-10f)
    {
        // Degenerate matrix (a zero scale somewhere). Fall back to identity
        // so the program never divides by zero.
        r.m[0] = 1.0f; r.m[1] = 0.0f; r.m[2] = 0.0f;
        r.m[3] = 0.0f; r.m[4] = 1.0f; r.m[5] = 0.0f;
        r.m[6] = 0.0f; r.m[7] = 0.0f; r.m[8] = 1.0f;
        return r;
    }

    float invDet = 1.0f / det;

    // B = inverse of the 3x3 block, written row by row.
    float b00 = (a11 * a22 - a12 * a21) * invDet;
    float b01 = -(a01 * a22 - a02 * a21) * invDet;
    float b02 = (a01 * a12 - a02 * a11) * invDet;
    float b10 = -(a10 * a22 - a12 * a20) * invDet;
    float b11 = (a00 * a22 - a02 * a20) * invDet;
    float b12 = -(a00 * a12 - a02 * a10) * invDet;
    float b20 = (a10 * a21 - a11 * a20) * invDet;
    float b21 = -(a00 * a21 - a01 * a20) * invDet;
    float b22 = (a00 * a11 - a01 * a10) * invDet;

    // We now need the TRANSPOSE of B stored in COLUMN-major order.
    // Transposing and switching row-major -> column-major cancel each other
    // out, so we can simply store B row by row.
    r.m[0] = b00; r.m[1] = b01; r.m[2] = b02;
    r.m[3] = b10; r.m[4] = b11; r.m[5] = b12;
    r.m[6] = b20; r.m[7] = b21; r.m[8] = b22;
    return r;
}

#endif // MATH3D_H
