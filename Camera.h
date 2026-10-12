#ifndef CAMERA_H
#define CAMERA_H

#include "Math3D.h"

struct Camera
{
    Vec3  position;
    float yaw;          // degrees. -90 looks down -Z, 0 looks down +X
    float pitch;        // degrees. clamped to [-89, +89]
    float fov;          // vertical field of view in degrees
    float moveSpeed;    // world units per second
    float mouseSens;    // degrees per pixel

    Camera()
        : position(0.0f, 0.0f, 0.0f)
        , yaw(-90.0f)
        , pitch(0.0f)
        , fov(60.0f)
        , moveSpeed(7.0f)
        , mouseSens(0.10f)
    {
    }

    // The direction the camera is looking, rebuilt from yaw and pitch.
    Vec3 front() const
    {
        float cy = std::cos(radians(yaw));
        float sy = std::sin(radians(yaw));
        float cp = std::cos(radians(pitch));
        float sp = std::sin(radians(pitch));
        return normalize(Vec3(cy * cp, sp, sy * cp));
    }

    Vec3 right() const
    {
        return normalize(cross(front(), Vec3(0.0f, 1.0f, 0.0f)));
    }

    Mat4 viewMatrix() const
    {
        return lookAt(position, position + front(), Vec3(0.0f, 1.0f, 0.0f));
    }

    // ---- Input helpers -----------------------------------------------------

    void moveForward(float dt) { position = position + front() * (moveSpeed * dt); }
    void moveBackward(float dt) { position = position - front() * (moveSpeed * dt); }
    void moveRight(float dt) { position = position + right() * (moveSpeed * dt); }
    void moveLeft(float dt) { position = position - right() * (moveSpeed * dt); }
    void moveUp(float dt) { position.y += moveSpeed * dt; }
    void moveDown(float dt) { position.y -= moveSpeed * dt; }

    void addMouseDelta(float dx, float dy)
    {
        yaw += dx * mouseSens;
        pitch += dy * mouseSens;

        // Stop the view from flipping over at the poles.
        if (pitch > 89.0f)  pitch = 89.0f;
        if (pitch < -89.0f) pitch = -89.0f;
    }

    void addZoom(float scrollY)
    {
        fov -= scrollY * 2.0f;
        if (fov < 20.0f) fov = 20.0f;
        if (fov > 90.0f) fov = 90.0f;
    }
};

#endif // CAMERA_H
