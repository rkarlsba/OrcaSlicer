// OrcaSlicer Mesh Displacement (BumpMesh)
// Applies texture-based vertex displacement to triangle meshes.
// Inspired by stlTexturizer / BumpMesh by CNC Kitchen.
// Copyright (C) 2024 OrcaSlicer Contributors
// License: AGPL-3.0

#include "MeshDisplacement.hpp"
#include "TriangleMesh.hpp"

#include <cmath>
#include <algorithm>
#include <unordered_map>
#include <unordered_set>
#include <cassert>

#include <boost/log/trivial.hpp>

namespace Slic3r {

// ============================================================================
// Texture loading — stub in libslic3r. GUI layer provides the real implementation.
// ============================================================================

DisplacementTexture load_displacement_texture(const std::string& filepath)
{
    // This stub should never be called directly — the GUI code in
    // BumpMeshDialog.cpp loads the texture via wxImage and fills
    // the DisplacementTexture struct manually.
    BOOST_LOG_TRIVIAL(warning) << "MeshDisplacement: load_displacement_texture called from non-GUI context";
    return {};
}

// ============================================================================
// Bilinear texture sampler
// ============================================================================

static float sample_bilinear(const DisplacementTexture& tex, float u, float v)
{
    // Tile UVs to [0, 1)
    u = u - std::floor(u);
    v = v - std::floor(v);
    // Flip V: image row 0 is top, but V=0 should be bottom
    v = 1.0f - v;

    float fx = u * (tex.width - 1);
    float fy = v * (tex.height - 1);
    int   x0 = (int)fx;
    int   y0 = (int)fy;
    int   x1 = std::min(x0 + 1, tex.width - 1);
    int   y1 = std::min(y0 + 1, tex.height - 1);
    float tx = fx - x0;
    float ty = fy - y0;

    float v00 = tex.pixels[y0 * tex.width + x0] / 255.0f;
    float v10 = tex.pixels[y0 * tex.width + x1] / 255.0f;
    float v01 = tex.pixels[y1 * tex.width + x0] / 255.0f;
    float v11 = tex.pixels[y1 * tex.width + x1] / 255.0f;

    return v00 * (1 - tx) * (1 - ty)
         + v10 * tx * (1 - ty)
         + v01 * (1 - tx) * ty
         + v11 * tx * ty;
}

// ============================================================================
// UV computation — matches stlTexturizer mapping modes
// ============================================================================

// UV result: either a single UV pair or a weighted blend (triplanar)
struct UVSample { float u, v, w; };
struct UVResult {
    bool              triplanar = false;
    float             u = 0, v = 0;
    UVSample          samples[3];
    int               n_samples = 0;
};

static inline float fract(float x) { return x - std::floor(x); }

struct TransformResult { float u, v; };

static TransformResult apply_uv_transform(float u, float v,
                                           float scale_u, float scale_v,
                                           float offset_u, float offset_v,
                                           float cos_r, float sin_r)
{
    float uu = u / scale_u + offset_u;
    float vv = v / scale_v + offset_v;
    if (cos_r != 1.0f || sin_r != 0.0f) {
        uu -= 0.5f; vv -= 0.5f;
        float ru = cos_r * uu - sin_r * vv;
        float rv = sin_r * uu + cos_r * vv;
        uu = ru + 0.5f; vv = rv + 0.5f;
    }
    return { fract(uu), fract(vv) };
}

static UVResult compute_uv(const Vec3f& pos, const Vec3f& normal,
                            int mode, const DisplacementSettings& settings,
                            const Vec3f& bb_min, const Vec3f& bb_size)
{
    float md = std::max({bb_size.x(), bb_size.y(), bb_size.z(), 1e-6f});
    float rot_rad = settings.rotation * (float)M_PI / 180.0f;
    float cos_r = std::cos(rot_rad);
    float sin_r = std::sin(rot_rad);

    UVResult result;

    switch (mode) {
    case 4: { // Planar XY
        auto tr = apply_uv_transform(
            (pos.x() - bb_min.x()) / md,
            (pos.y() - bb_min.y()) / md,
            settings.scale_u, settings.scale_v,
            settings.offset_u, settings.offset_v, cos_r, sin_r);
        result.u = tr.u; result.v = tr.v;
        return result;
    }
    case 5: { // Planar XZ
        auto tr = apply_uv_transform(
            (pos.x() - bb_min.x()) / md,
            (pos.z() - bb_min.z()) / md,
            settings.scale_u, settings.scale_v,
            settings.offset_u, settings.offset_v, cos_r, sin_r);
        result.u = tr.u; result.v = tr.v;
        return result;
    }
    case 6: { // Planar YZ
        auto tr = apply_uv_transform(
            (pos.y() - bb_min.y()) / md,
            (pos.z() - bb_min.z()) / md,
            settings.scale_u, settings.scale_v,
            settings.offset_u, settings.offset_v, cos_r, sin_r);
        result.u = tr.u; result.v = tr.v;
        return result;
    }
    case 3: { // Spherical
        float rx = pos.x() - (bb_min.x() + bb_size.x() * 0.5f);
        float ry = pos.y() - (bb_min.y() + bb_size.y() * 0.5f);
        float rz = pos.z() - (bb_min.z() + bb_size.z() * 0.5f);
        float r  = std::sqrt(rx*rx + ry*ry + rz*rz);
        float phi   = std::acos(std::clamp(rz / std::max(r, 1e-6f), -1.0f, 1.0f));
        float theta = std::atan2(ry, rx);
        float u_raw = theta / (2.0f * (float)M_PI) + 0.5f;
        float v_raw = phi / (float)M_PI;
        auto tr = apply_uv_transform(u_raw, v_raw,
            settings.scale_u, settings.scale_v,
            settings.offset_u, settings.offset_v, cos_r, sin_r);
        result.u = tr.u; result.v = tr.v;
        return result;
    }
    case 2: { // Cylindrical
        float cx = bb_min.x() + bb_size.x() * 0.5f;
        float cy = bb_min.y() + bb_size.y() * 0.5f;
        float r  = std::max(bb_size.x(), bb_size.y()) * 0.5f;
        float C  = 2.0f * (float)M_PI * std::max(r, 1e-6f);
        float rx = pos.x() - cx;
        float ry = pos.y() - cy;
        float theta = std::atan2(ry, rx);
        float u_raw = theta / (2.0f * (float)M_PI) + 0.5f;
        float v_raw = (pos.z() - bb_min.z()) / C;
        auto tr = apply_uv_transform(u_raw, v_raw,
            settings.scale_u, settings.scale_v,
            settings.offset_u, settings.offset_v, cos_r, sin_r);
        result.u = tr.u; result.v = tr.v;
        return result;
    }
    case 1: { // Cubic (Box) — blend 3 planar projections by dominant face normal
        float ax = std::abs(normal.x());
        float ay = std::abs(normal.y());
        float az = std::abs(normal.z());
        // Sharpened power blending
        float bx = ax * ax * ax * ax;
        float by = ay * ay * ay * ay;
        float bz = az * az * az * az;
        float sum = bx + by + bz + 1e-6f;

        float yz_u = (pos.y() - bb_min.y()) / md;
        if (normal.x() < 0) yz_u = -yz_u;
        float xz_u = (pos.x() - bb_min.x()) / md;
        if (normal.y() > 0) xz_u = -xz_u;
        float xy_u = (pos.x() - bb_min.x()) / md;
        if (normal.z() < 0) xy_u = -xy_u;

        auto tr_yz = apply_uv_transform(yz_u, (pos.z() - bb_min.z()) / md,
            settings.scale_u, settings.scale_v, settings.offset_u, settings.offset_v, cos_r, sin_r);
        auto tr_xz = apply_uv_transform(xz_u, (pos.z() - bb_min.z()) / md,
            settings.scale_u, settings.scale_v, settings.offset_u, settings.offset_v, cos_r, sin_r);
        auto tr_xy = apply_uv_transform(xy_u, (pos.y() - bb_min.y()) / md,
            settings.scale_u, settings.scale_v, settings.offset_u, settings.offset_v, cos_r, sin_r);

        result.triplanar = true;
        result.n_samples = 3;
        result.samples[0] = { tr_xy.u, tr_xy.v, bz / sum };
        result.samples[1] = { tr_xz.u, tr_xz.v, by / sum };
        result.samples[2] = { tr_yz.u, tr_yz.v, bx / sum };
        return result;
    }
    case 0:  // Triplanar (default)
    default: {
        float ax = std::abs(normal.x());
        float ay = std::abs(normal.y());
        float az = std::abs(normal.z());
        float ax2 = ax * ax, ay2 = ay * ay, az2 = az * az;
        float bx = ax2 * ax2;
        float by = ay2 * ay2;
        float bz = az2 * az2;
        float sum = bx + by + bz + 1e-6f;

        float yz_u = (pos.y() - bb_min.y()) / md;
        if (normal.x() < 0) yz_u = -yz_u;
        float xz_u = (pos.x() - bb_min.x()) / md;
        if (normal.y() > 0) xz_u = -xz_u;
        float xy_u = (pos.x() - bb_min.x()) / md;
        if (normal.z() < 0) xy_u = -xy_u;

        auto tr_yz = apply_uv_transform(yz_u, (pos.z() - bb_min.z()) / md,
            settings.scale_u, settings.scale_v, settings.offset_u, settings.offset_v, cos_r, sin_r);
        auto tr_xz = apply_uv_transform(xz_u, (pos.z() - bb_min.z()) / md,
            settings.scale_u, settings.scale_v, settings.offset_u, settings.offset_v, cos_r, sin_r);
        auto tr_xy = apply_uv_transform(xy_u, (pos.y() - bb_min.y()) / md,
            settings.scale_u, settings.scale_v, settings.offset_u, settings.offset_v, cos_r, sin_r);

        result.triplanar = true;
        result.n_samples = 3;
        result.samples[0] = { tr_xy.u, tr_xy.v, bz / sum };
        result.samples[1] = { tr_xz.u, tr_xz.v, by / sum };
        result.samples[2] = { tr_yz.u, tr_yz.v, bx / sum };
        return result;
    }
    }
}

// ============================================================================
// Edge-key hash for subdivision midpoint cache
// ============================================================================

struct EdgeKey {
    int v1, v2;
    EdgeKey(int a, int b) : v1(std::min(a, b)), v2(std::max(a, b)) {}
    bool operator==(const EdgeKey& o) const { return v1 == o.v1 && v2 == o.v2; }
};

struct EdgeKeyHash {
    size_t operator()(const EdgeKey& k) const {
        // Use a large prime to minimize collisions
        return size_t(k.v1) * 2654435761u + size_t(k.v2);
    }
};

// ============================================================================
// Adaptive edge-based subdivision
// ============================================================================

// Subdivide mesh so no edge exceeds max_edge_length.
// Uses iterative edge splitting (1→2, 1→3, 1→4 triangle splits).
static indexed_triangle_set subdivide_mesh(const indexed_triangle_set& input,
                                            float max_edge_length,
                                            int   max_iterations = 10,
                                            int   safety_cap     = 5000000)
{
    if (max_edge_length <= 0.0f || input.indices.empty())
        return input;

    float max_sq = max_edge_length * max_edge_length;

    // Work with growing vertex/index arrays
    std::vector<Vec3f>   vertices = input.vertices;
    std::vector<Vec3i32> indices  = input.indices;

    for (int iter = 0; iter < max_iterations; ++iter) {
        if ((int)indices.size() >= safety_cap)
            break;

        // Midpoint cache: edge → new vertex index
        std::unordered_map<EdgeKey, int, EdgeKeyHash> mid_cache;

        auto get_midpoint = [&](int a, int b) -> int {
            EdgeKey key(a, b);
            auto it = mid_cache.find(key);
            if (it != mid_cache.end())
                return it->second;
            int idx = (int)vertices.size();
            vertices.push_back((vertices[a] + vertices[b]) * 0.5f);
            mid_cache[key] = idx;
            return idx;
        };

        auto edge_len_sq = [&](int a, int b) -> float {
            Vec3f d = vertices[a] - vertices[b];
            return d.squaredNorm();
        };

        // Step 1: Find all edges that need splitting
        // Use EdgeKey (stable min/max pair) — NOT dependent on vertices.size()
        std::unordered_set<EdgeKey, EdgeKeyHash> split_edges;

        for (const auto& f : indices) {
            if (edge_len_sq(f[0], f[1]) > max_sq) split_edges.insert(EdgeKey(f[0], f[1]));
            if (edge_len_sq(f[1], f[2]) > max_sq) split_edges.insert(EdgeKey(f[1], f[2]));
            if (edge_len_sq(f[2], f[0]) > max_sq) split_edges.insert(EdgeKey(f[2], f[0]));
        }

        if (split_edges.empty())
            break; // All edges are short enough

        // Step 2: Rebuild index list
        std::vector<Vec3i32> new_indices;
        new_indices.reserve(indices.size() * 2);

        for (const auto& f : indices) {
            if ((int)new_indices.size() >= safety_cap) {
                // Carry remaining
                break;
            }

            int a = f[0], b = f[1], c = f[2];
            bool sAB = split_edges.count(EdgeKey(a, b)) > 0;
            bool sBC = split_edges.count(EdgeKey(b, c)) > 0;
            bool sCA = split_edges.count(EdgeKey(c, a)) > 0;
            int n = (sAB ? 1 : 0) + (sBC ? 1 : 0) + (sCA ? 1 : 0);

            if (n == 0) {
                new_indices.push_back(f);
            } else if (n == 3) {
                // 1→4 subdivision
                int mAB = get_midpoint(a, b);
                int mBC = get_midpoint(b, c);
                int mCA = get_midpoint(c, a);
                new_indices.push_back({a,   mAB, mCA});
                new_indices.push_back({mAB, b,   mBC});
                new_indices.push_back({mCA, mBC, c});
                new_indices.push_back({mAB, mBC, mCA});
            } else if (n == 1) {
                // 1→2 subdivision — bisect the one marked edge
                if (sAB) {
                    int m = get_midpoint(a, b);
                    new_indices.push_back({a, m, c});
                    new_indices.push_back({m, b, c});
                } else if (sBC) {
                    int m = get_midpoint(b, c);
                    new_indices.push_back({a, b, m});
                    new_indices.push_back({a, m, c});
                } else { // sCA
                    int m = get_midpoint(c, a);
                    new_indices.push_back({a, b, m});
                    new_indices.push_back({m, b, c});
                }
            } else { // n == 2
                // 1→3 subdivision — fan from the untouched-edge vertex
                if (!sAB) {
                    int mBC = get_midpoint(b, c);
                    int mCA = get_midpoint(c, a);
                    new_indices.push_back({a,   b,   mBC});
                    new_indices.push_back({a,   mBC, mCA});
                    new_indices.push_back({c,   mCA, mBC});
                } else if (!sBC) {
                    int mAB = get_midpoint(a, b);
                    int mCA = get_midpoint(c, a);
                    new_indices.push_back({a,   mAB, mCA});
                    new_indices.push_back({mAB, b,   c});
                    new_indices.push_back({mAB, c,   mCA});
                } else { // !sCA
                    int mAB = get_midpoint(a, b);
                    int mBC = get_midpoint(b, c);
                    new_indices.push_back({b,   mBC, mAB});
                    new_indices.push_back({a,   mAB, mBC});
                    new_indices.push_back({a,   mBC, c});
                }
            }
        }

        indices = std::move(new_indices);

        BOOST_LOG_TRIVIAL(debug) << "MeshDisplacement: subdivision iter " << iter
                                 << ", faces=" << indices.size()
                                 << ", vertices=" << vertices.size();
    }

    indexed_triangle_set result;
    result.vertices = std::move(vertices);
    result.indices  = std::move(indices);
    return result;
}

// ============================================================================
// Compute area-weighted smooth vertex normals
// ============================================================================

static std::vector<Vec3f> compute_smooth_normals(const indexed_triangle_set& its)
{
    std::vector<Vec3f> normals(its.vertices.size(), Vec3f::Zero());

    for (const auto& f : its.indices) {
        const Vec3f& a = its.vertices[f[0]];
        const Vec3f& b = its.vertices[f[1]];
        const Vec3f& c = its.vertices[f[2]];
        // Cross product = 2× face area × face normal direction (area-weighted)
        Vec3f fn = (b - a).cross(c - a);
        normals[f[0]] += fn;
        normals[f[1]] += fn;
        normals[f[2]] += fn;
    }

    for (auto& n : normals) {
        float len = n.norm();
        if (len > 1e-10f)
            n /= len;
        else
            n = Vec3f(0, 0, 1); // fallback
    }

    return normals;
}

// ============================================================================
// Main displacement function
// ============================================================================

indexed_triangle_set apply_displacement(
    const indexed_triangle_set& input,
    const DisplacementTexture&  texture,
    const DisplacementSettings& settings,
    std::function<void(float)>  progress_fn)
{
    if (input.indices.empty() || input.vertices.empty() || !texture.valid()) {
        BOOST_LOG_TRIVIAL(warning) << "MeshDisplacement: invalid input, returning unchanged mesh";
        return input;
    }

    // Compute bounding box
    Vec3f bb_min = input.vertices[0];
    Vec3f bb_max = input.vertices[0];
    for (const auto& v : input.vertices) {
        bb_min = bb_min.cwiseMin(v);
        bb_max = bb_max.cwiseMax(v);
    }
    Vec3f bb_size = bb_max - bb_min;
    float max_dim = std::max({bb_size.x(), bb_size.y(), bb_size.z(), 1e-6f});

    // Determine max edge length for subdivision
    // Auto: aim for ~1/50 of the max dimension, clamped to reasonable range
    float max_edge = settings.max_edge_length;
    if (max_edge <= 0.0f) {
        max_edge = max_dim / 50.0f;
        max_edge = std::clamp(max_edge, 0.1f, 5.0f);
    }

    BOOST_LOG_TRIVIAL(info) << "MeshDisplacement: input faces=" << input.indices.size()
                            << " vertices=" << input.vertices.size()
                            << " bbox=[" << bb_size.x() << "x" << bb_size.y() << "x" << bb_size.z()
                            << "] max_edge=" << max_edge
                            << " amplitude=" << settings.amplitude;

    if (progress_fn) progress_fn(0.05f);

    // Step 1: Subdivide
    indexed_triangle_set subdiv = subdivide_mesh(input, max_edge);

    BOOST_LOG_TRIVIAL(info) << "MeshDisplacement: after subdivision faces=" << subdiv.indices.size()
                            << " vertices=" << subdiv.vertices.size();

    if (progress_fn) progress_fn(0.4f);

    // Step 2: Compute smooth vertex normals
    std::vector<Vec3f> smooth_normals = compute_smooth_normals(subdiv);

    if (progress_fn) progress_fn(0.5f);

    // Map dialog projection mode indices to our internal modes:
    // Dialog: 0=Triplanar, 1=Cubic, 2=Cylindrical, 3=Spherical, 4=Planar XY, 5=Planar XZ, 6=Planar YZ
    int proj_mode = settings.projection_mode;

    // Step 3: Displace each vertex
    for (int i = 0; i < (int)subdiv.vertices.size(); ++i) {
        const Vec3f& pos     = subdiv.vertices[i];
        const Vec3f& normal  = smooth_normals[i];

        // Angle masking: check if this vertex's normal is mostly horizontal top/bottom
        if (settings.top_angle_limit > 0.0f || settings.bottom_angle_limit > 0.0f) {
            float nz_abs = std::abs(normal.z());
            float angle_from_z = std::acos(std::clamp(nz_abs, 0.0f, 1.0f)) * 180.0f / (float)M_PI;
            bool masked = false;
            if (normal.z() > 0 && settings.top_angle_limit > 0.0f &&
                angle_from_z <= settings.top_angle_limit)
                masked = true;
            if (normal.z() < 0 && settings.bottom_angle_limit > 0.0f &&
                angle_from_z <= settings.bottom_angle_limit)
                masked = true;
            if (masked) continue; // Skip this vertex — no displacement
        }

        // Compute UV
        UVResult uv = compute_uv(pos, normal, proj_mode, settings, bb_min, bb_size);

        // Sample texture
        float grey;
        if (uv.triplanar) {
            grey = 0.0f;
            for (int s = 0; s < uv.n_samples; ++s)
                grey += sample_bilinear(texture, uv.samples[s].u, uv.samples[s].v) * uv.samples[s].w;
        } else {
            grey = sample_bilinear(texture, uv.u, uv.v);
        }

        // Compute displacement amount
        float disp;
        if (settings.symmetric)
            disp = (grey - 0.5f) * 2.0f * settings.amplitude;
        else
            disp = grey * settings.amplitude;

        // Displace along smooth normal
        subdiv.vertices[i] += normal * disp;

        if (progress_fn && (i % 10000 == 0))
            progress_fn(0.5f + 0.45f * float(i) / float(subdiv.vertices.size()));
    }

    if (progress_fn) progress_fn(0.98f);

    BOOST_LOG_TRIVIAL(info) << "MeshDisplacement: displacement complete, final faces=" << subdiv.indices.size();

    if (progress_fn) progress_fn(1.0f);

    return subdiv;
}

} // namespace Slic3r
