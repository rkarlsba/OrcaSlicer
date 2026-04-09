/**
 * Displacement mapping for BumpMesh
 * 
 * Based on CNC Kitchen's stlTexturizer displacement algorithm.
 * Applies texture-based vertex displacement to meshes.
 * 
 * @copyright 2024 CNC Kitchen / OrcaSlicer Contributors
 * @license MIT
 */

const { computeUV, getCubicBlendWeights, MAPPING_MODES } = require('./mapping');

const QUANT = 1e4;

/**
 * Create position key for vertex deduplication
 */
function posKey(x, y, z) {
    return `${Math.round(x * QUANT)}_${Math.round(y * QUANT)}_${Math.round(z * QUANT)}`;
}

/**
 * Bilinear texture sampling
 * @param {Uint8Array} data - RGBA pixel data
 * @param {number} w - Image width
 * @param {number} h - Image height
 * @param {number} u - U coordinate [0, 1]
 * @param {number} v - V coordinate [0, 1]
 * @returns {number} Grayscale value [0, 1]
 */
function sampleBilinear(data, w, h, u, v) {
    // Ensure [0, 1)
    u = ((u % 1) + 1) % 1;
    v = ((v % 1) + 1) % 1;
    
    // Flip V (image row 0 is top, but v=0 should be bottom)
    v = 1 - v;
    
    const fx = u * (w - 1);
    const fy = v * (h - 1);
    const x0 = Math.floor(fx);
    const y0 = Math.floor(fy);
    const x1 = Math.min(x0 + 1, w - 1);
    const y1 = Math.min(y0 + 1, h - 1);
    const tx = fx - x0;
    const ty = fy - y0;
    
    // Sample red channel (assuming grayscale)
    const v00 = data[(y0 * w + x0) * 4] / 255;
    const v10 = data[(y0 * w + x1) * 4] / 255;
    const v01 = data[(y1 * w + x0) * 4] / 255;
    const v11 = data[(y1 * w + x1) * 4] / 255;
    
    return v00 * (1 - tx) * (1 - ty) +
           v10 * tx * (1 - ty) +
           v01 * (1 - tx) * ty +
           v11 * tx * ty;
}

/**
 * Apply displacement to mesh vertices
 * 
 * @param {Object} mesh - Input mesh { vertices, normals, indices }
 * @param {Object} texture - Texture data { pixels, width, height, channels }
 * @param {Object} settings - Displacement settings
 * @param {Object} bounds - Mesh bounds { min, max, center, size }
 * @param {Function} onProgress - Progress callback
 * @returns {Promise<Object>} Displaced mesh
 */
async function applyDisplacement(mesh, texture, settings, bounds, onProgress) {
    const vertices = mesh.vertices;
    const normals = mesh.normals;
    const count = vertices.length / 3;
    
    const newPos = new Float32Array(count * 3);
    const newNrm = new Float32Array(count * 3);
    
    const { width: imgWidth, height: imgHeight, pixels: imageData } = texture;
    
    // Texture aspect correction
    const tmax = Math.max(imgWidth, imgHeight, 1);
    const aspectU = tmax / Math.max(imgWidth, 1);
    const aspectV = tmax / Math.max(imgHeight, 1);
    
    const settingsWithAspect = {
        ...settings,
        textureAspectU: aspectU,
        textureAspectV: aspectV
    };
    
    // Convert bounds to expected format
    const boundsObj = {
        min: { x: bounds.min.x, y: bounds.min.y, z: bounds.min.z },
        max: { x: bounds.max.x, y: bounds.max.y, z: bounds.max.z },
        center: { x: bounds.center.x, y: bounds.center.y, z: bounds.center.z },
        size: { x: bounds.size.x, y: bounds.size.y, z: bounds.size.z }
    };
    
    // Pass 1: Compute smooth normals per unique position
    const smoothNrmMap = new Map();
    const triCount = count / 3;
    
    for (let t = 0; t < triCount; t++) {
        const base = t * 9; // 3 vertices * 3 components
        
        // Get triangle vertices
        const v0 = { x: vertices[base], y: vertices[base + 1], z: vertices[base + 2] };
        const v1 = { x: vertices[base + 3], y: vertices[base + 4], z: vertices[base + 5] };
        const v2 = { x: vertices[base + 6], y: vertices[base + 7], z: vertices[base + 8] };
        
        // Compute face normal (cross product)
        const e1 = { x: v1.x - v0.x, y: v1.y - v0.y, z: v1.z - v0.z };
        const e2 = { x: v2.x - v0.x, y: v2.y - v0.y, z: v2.z - v0.z };
        const faceNrm = {
            x: e1.y * e2.z - e1.z * e2.y,
            y: e1.z * e2.x - e1.x * e2.z,
            z: e1.x * e2.y - e1.y * e2.x
        };
        const faceArea = Math.sqrt(faceNrm.x ** 2 + faceNrm.y ** 2 + faceNrm.z ** 2);
        
        // Accumulate to each vertex
        for (let vi = 0; vi < 3; vi++) {
            const idx = base + vi * 3;
            const pos = { x: vertices[idx], y: vertices[idx + 1], z: vertices[idx + 2] };
            const nrm = { x: normals[idx], y: normals[idx + 1], z: normals[idx + 2] };
            
            const k = posKey(pos.x, pos.y, pos.z);
            const existing = smoothNrmMap.get(k);
            
            if (existing) {
                existing.x += nrm.x * faceArea;
                existing.y += nrm.y * faceArea;
                existing.z += nrm.z * faceArea;
            } else {
                smoothNrmMap.set(k, {
                    x: nrm.x * faceArea,
                    y: nrm.y * faceArea,
                    z: nrm.z * faceArea
                });
            }
        }
    }
    
    // Normalize accumulated normals
    smoothNrmMap.forEach((n) => {
        const len = Math.sqrt(n.x ** 2 + n.y ** 2 + n.z ** 2) || 1;
        n.x /= len;
        n.y /= len;
        n.z /= len;
    });
    
    // Pass 2: Sample displacement texture once per unique position
    const dispCache = new Map();
    const modeIdx = typeof settings.mappingMode === 'string' 
        ? MAPPING_MODES[settings.mappingMode] 
        : settings.mappingMode;
    
    for (let i = 0; i < count; i++) {
        const idx = i * 3;
        const pos = { x: vertices[idx], y: vertices[idx + 1], z: vertices[idx + 2] };
        const k = posKey(pos.x, pos.y, pos.z);
        
        if (dispCache.has(k)) continue;
        
        const sn = smoothNrmMap.get(k);
        
        // Compute UV and sample texture
        const uvResult = computeUV(pos, sn, modeIdx, settingsWithAspect, boundsObj);
        
        let grey;
        if (uvResult.triplanar) {
            grey = 0;
            for (const s of uvResult.samples) {
                grey += sampleBilinear(imageData, imgWidth, imgHeight, s.u, s.v) * s.w;
            }
        } else {
            grey = sampleBilinear(imageData, imgWidth, imgHeight, uvResult.u, uvResult.v);
        }
        
        dispCache.set(k, grey);
    }
    
    // Pass 3: Displace every vertex
    const amplitude = settings.amplitude || 1.0;
    const symmetric = settings.symmetricDisplacement !== false;
    
    for (let i = 0; i < count; i++) {
        if (onProgress && i % 5000 === 0) {
            onProgress(i / count);
        }
        
        const idx = i * 3;
        const pos = { x: vertices[idx], y: vertices[idx + 1], z: vertices[idx + 2] };
        const k = posKey(pos.x, pos.y, pos.z);
        
        const sn = smoothNrmMap.get(k);
        const grey = dispCache.get(k);
        
        // Apply symmetric or one-sided displacement
        const centeredGrey = symmetric ? (grey - 0.5) * 2 : grey;
        const disp = centeredGrey * amplitude;
        
        newPos[idx] = pos.x + sn.x * disp;
        newPos[idx + 1] = pos.y + sn.y * disp;
        newPos[idx + 2] = pos.z + sn.z * disp;
        
        // Keep original normal for now (will be recomputed)
        newNrm[idx] = normals[idx];
        newNrm[idx + 1] = normals[idx + 1];
        newNrm[idx + 2] = normals[idx + 2];
    }
    
    // Pass 4: Recompute face normals from displaced positions
    for (let t = 0; t < triCount; t++) {
        const base = t * 9;
        
        const ax = newPos[base], ay = newPos[base + 1], az = newPos[base + 2];
        const bx = newPos[base + 3], by = newPos[base + 4], bz = newPos[base + 5];
        const cx = newPos[base + 6], cy = newPos[base + 7], cz = newPos[base + 8];
        
        const e1 = { x: bx - ax, y: by - ay, z: bz - az };
        const e2 = { x: cx - ax, y: cy - ay, z: cz - az };
        
        let nx = e1.y * e2.z - e1.z * e2.y;
        let ny = e1.z * e2.x - e1.x * e2.z;
        let nz = e1.x * e2.y - e1.y * e2.x;
        const len = Math.sqrt(nx * nx + ny * ny + nz * nz) || 1;
        nx /= len;
        ny /= len;
        nz /= len;
        
        for (let v = 0; v < 3; v++) {
            const vidx = base + v * 3;
            newNrm[vidx] = nx;
            newNrm[vidx + 1] = ny;
            newNrm[vidx + 2] = nz;
        }
    }
    
    return {
        vertices: newPos,
        normals: newNrm,
        indices: mesh.indices
    };
}

module.exports = { applyDisplacement };
