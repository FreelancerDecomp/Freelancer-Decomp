// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

#include "common_ini.h" // For Vector

// Inline operator overloads for Freelancer's Vector class 
// These are used heavily in IVP's original Havok physics math (impact solver, exact simplex, etc)
// but Freelancer's Vector struct does not have them by default.

inline Vector operator+(const Vector& a, const Vector& b)
{
    Vector res;
    res.x = a.x + b.x;
    res.y = a.y + b.y;
    res.z = a.z + b.z;
    return res;
}

inline Vector operator-(const Vector& a, const Vector& b)
{
    Vector res;
    res.x = a.x - b.x;
    res.y = a.y - b.y;
    res.z = a.z - b.z;
    return res;
}

inline Vector operator*(const Vector& a, float scalar)
{
    Vector res;
    res.x = a.x * scalar;
    res.y = a.y * scalar;
    res.z = a.z * scalar;
    return res;
}

inline Vector operator*(const Vector& a, const Vector& b) // Component-wise multiplication
{
    Vector res;
    res.x = a.x * b.x;
    res.y = a.y * b.y;
    res.z = a.z * b.z;
    return res;
}

// Cross product
inline Vector operator^(const Vector& a, const Vector& b)
{
    Vector res;
    res.x = a.y * b.z - a.z * b.y;
    res.y = a.z * b.x - a.x * b.z;
    res.z = a.x * b.y - a.y * b.x;
    return res;
}

// Dot product
inline float operator|(const Vector& a, const Vector& b)
{
    return (a.x * b.x + a.y * b.y + a.z * b.z);
}

inline Vector& operator+=(Vector& a, const Vector& b)
{
    a.x += b.x;
    a.y += b.y;
    a.z += b.z;
    return a;
}

inline Vector& operator-=(Vector& a, const Vector& b)
{
    a.x -= b.x;
    a.y -= b.y;
    a.z -= b.z;
    return a;
}

inline Vector& operator*=(Vector& a, float scalar)
{
    a.x *= scalar;
    a.y *= scalar;
    a.z *= scalar;
    return a;
}
