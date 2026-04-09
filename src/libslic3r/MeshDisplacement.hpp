// OrcaSlicer Mesh Displacement (BumpMesh)
// Applies texture-based vertex displacement to triangle meshes.
// Inspired by stlTexturizer / BumpMesh by CNC Kitchen.
// Copyright (C) 2024 OrcaSlicer Contributors
// License: AGPL-3.0

#pragma once

#include "TriangleMesh.hpp"
#include "BoundingBox.hpp"

#include <string>
#include <vector>
#include <cstdint>
#include <functional>

namespace Slic3r {

// Grayscale texture image for displacement sampling
struct DisplacementTexture {
    std::vector<uint8_t> pixels; // 1 byte per pixel, grayscale
    int width  = 0;
    int height = 0;

    bool valid() const { return !pixels.empty() && width > 0 && height > 0; }
};

// Parameters controlling displacement
struct DisplacementSettings {
    float amplitude       = 0.5f;  // Max displacement in mm
    float scale_u         = 1.0f;  // Texture tiling scale U
    float scale_v         = 1.0f;  // Texture tiling scale V
    float offset_u        = 0.0f;
    float offset_v        = 0.0f;
    float rotation        = 0.0f;  // Degrees
    int   projection_mode = 0;     // 0=Triplanar, 1=Cubic, 2=Cylindrical, 3=Spherical, 4=XY, 5=XZ, 6=YZ
    bool  symmetric       = false; // true: grey 0.5 = no displacement; false: grey 0 = no displacement
    float max_edge_length = 0.0f;  // Target max edge length for subdivision (0 = auto)
    float top_angle_limit    = 0.0f;  // Mask angle for top-facing faces (degrees)
    float bottom_angle_limit = 0.0f;  // Mask angle for bottom-facing faces (degrees)
};

// Load a texture image from a file path, convert to grayscale
// Uses wxImage internally — must be called from GUI thread or after wxImage handlers are initialized
DisplacementTexture load_displacement_texture(const std::string& filepath);

// Apply displacement to a mesh.
// The mesh is first subdivided so edges are ≤ max_edge_length,
// then vertices are displaced along their smoothed normals by
// the texture value × amplitude.
// Returns a new mesh with displacement baked in.
// progress_fn is called with fraction [0,1] — can be null.
indexed_triangle_set apply_displacement(
    const indexed_triangle_set& input,
    const DisplacementTexture&  texture,
    const DisplacementSettings& settings,
    std::function<void(float)>  progress_fn = nullptr);

} // namespace Slic3r
