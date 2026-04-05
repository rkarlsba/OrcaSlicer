/**
 * UV Mapping modes for BumpMesh
 * 
 * Based on CNC Kitchen's stlTexturizer mapping algorithm.
 * Provides multiple projection modes for texture mapping.
 * 
 * @copyright 2024 CNC Kitchen / OrcaSlicer Contributors
 * @license MIT
 */

const TWO_PI = Math.PI * 2;

const MAPPING_MODES = {
    planarXY: 0,
    planarXZ: 1,
    planarYZ: 2,
    cylindrical: 3,
    spherical: 4,
    triplanar: 5,
    cubic: 6
};

/**
 * Get dominant axis for cubic mapping
 */
function getDominantCubicAxis(normal) {
    const ax = Math.abs(normal.x);
    const ay = Math.abs(normal.y);
    const az = Math.abs(normal.z);
    
    if (ax >= ay && ax >= az) return 'x';
    if (ay >= az) return 'y';
    return 'z';
}

/**
 * Get cubic blend weights for smooth transitions
 */
function getCubicBlendWeights(normal, blend, seamBandWidth = 0.35) {
    const axis = getDominantCubicAxis(normal);
    const ax = Math.abs(normal.x);
    const ay = Math.abs(normal.y);
    const az = Math.abs(normal.z);
    
    const primary = axis === 'x' ? ax : axis === 'y' ? ay : az;
    const secondary = axis === 'x' ? Math.max(ay, az) : axis === 'y' ? Math.max(ax, az) : Math.max(ax, ay);
    
    if (blend <= 0.001) {
        return {
            x: axis === 'x' ? 1 : 0,
            y: axis === 'y' ? 1 : 0,
            z: axis === 'z' ? 1 : 0
        };
    }
    
    const oneHot = {
        x: axis === 'x' ? 1 : 0,
        y: axis === 'y' ? 1 : 0,
        z: axis === 'z' ? 1 : 0
    };
    
    const seamWidth = Math.max(seamBandWidth, 0.0001);
    const seamMixRaw = 1 - Math.min(1, Math.max(0, (primary - secondary) / seamWidth));
    const seamMix = blend * seamMixRaw * seamMixRaw * (3 - 2 * seamMixRaw);
    
    if (seamMix <= 0.001) return oneHot;
    
    const power = 1 + (1 - seamMix) * 11;
    const sx = Math.pow(ax, power);
    const sy = Math.pow(ay, power);
    const sz = Math.pow(az, power);
    const smoothSum = sx + sy + sz + 1e-6;
    
    const smooth = {
        x: sx / smoothSum,
        y: sy / smoothSum,
        z: sz / smoothSum
    };
    
    const mx = oneHot.x * (1 - seamMix) + smooth.x * seamMix;
    const my = oneHot.y * (1 - seamMix) + smooth.y * seamMix;
    const mz = oneHot.z * (1 - seamMix) + smooth.z * seamMix;
    const sum = mx + my + mz;
    
    return { x: mx / sum, y: my / sum, z: mz / sum };
}

/**
 * Apply UV transform (scale, offset, rotation)
 */
function applyTransform(u, v, scaleU, scaleV, offsetU, offsetV, rotRad) {
    let uu = u / scaleU + offsetU;
    let vv = v / scaleV + offsetV;
    
    if (rotRad !== 0) {
        const c = Math.cos(rotRad);
        const s = Math.sin(rotRad);
        uu -= 0.5;
        vv -= 0.5;
        const ru = c * uu - s * vv;
        const rv = s * uu + c * vv;
        uu = ru + 0.5;
        vv = rv + 0.5;
    }
    
    // Fractional part (always positive)
    return {
        u: ((uu % 1) + 1) % 1,
        v: ((vv % 1) + 1) % 1
    };
}

/**
 * Compute UV coordinates for a vertex
 * 
 * @param {Object} pos - Vertex position { x, y, z }
 * @param {Object} normal - Vertex normal { x, y, z }
 * @param {string} mode - Mapping mode name
 * @param {Object} settings - UV settings
 * @param {Object} bounds - Mesh bounds { min, max, center, size }
 * @returns {Object} UV result
 */
function computeUV(pos, normal, mode, settings, bounds) {
    const { min, size, center } = bounds;
    const scaleU = settings.scaleU || 1;
    const scaleV = settings.scaleV || 1;
    const offsetU = settings.offsetU || 0;
    const offsetV = settings.offsetV || 0;
    const rotRad = (settings.rotation || 0) * Math.PI / 180;
    const maxDim = Math.max(size.x, size.y, size.z, 1e-6);
    
    let u = 0, v = 0;
    const modeIdx = typeof mode === 'string' ? MAPPING_MODES[mode] : mode;
    
    switch (modeIdx) {
        case MAPPING_MODES.planarXY: {
            u = (pos.x - min.x) / maxDim;
            v = (pos.y - min.y) / maxDim;
            break;
        }
        
        case MAPPING_MODES.planarXZ: {
            u = (pos.x - min.x) / maxDim;
            v = (pos.z - min.z) / maxDim;
            break;
        }
        
        case MAPPING_MODES.planarYZ: {
            u = (pos.y - min.y) / maxDim;
            v = (pos.z - min.z) / maxDim;
            break;
        }
        
        case MAPPING_MODES.cylindrical: {
            const rx = pos.x - center.x;
            const ry = pos.y - center.y;
            const theta = Math.atan2(ry, rx);
            u = (theta / TWO_PI) + 0.5;
            v = (pos.z - min.z) / maxDim;
            break;
        }
        
        case MAPPING_MODES.spherical: {
            const rx = pos.x - center.x;
            const ry = pos.y - center.y;
            const rz = pos.z - center.z;
            const r = Math.sqrt(rx * rx + ry * ry + rz * rz) || 1e-6;
            const phi = Math.acos(Math.max(-1, Math.min(1, rz / r)));
            const theta = Math.atan2(ry, rx);
            u = (theta / TWO_PI) + 0.5;
            v = phi / Math.PI;
            break;
        }
        
        case MAPPING_MODES.cubic: {
            const weights = getCubicBlendWeights(normal, settings.mappingBlend || 0, settings.seamBandWidth || 0.35);
            
            let yzU = (pos.y - min.y) / maxDim;
            if (normal.x < 0) yzU = -yzU;
            let xzU = (pos.x - min.x) / maxDim;
            if (normal.y > 0) xzU = -xzU;
            let xyU = (pos.x - min.x) / maxDim;
            if (normal.z < 0) xyU = -xyU;
            
            const tYZ = applyTransform(yzU, (pos.z - min.z) / maxDim, scaleU, scaleV, offsetU, offsetV, rotRad);
            const tXZ = applyTransform(xzU, (pos.z - min.z) / maxDim, scaleU, scaleV, offsetU, offsetV, rotRad);
            const tXY = applyTransform(xyU, (pos.y - min.y) / maxDim, scaleU, scaleV, offsetU, offsetV, rotRad);
            
            if (weights.x > 0.999) return { triplanar: false, ...tYZ };
            if (weights.y > 0.999) return { triplanar: false, ...tXZ };
            if (weights.z > 0.999) return { triplanar: false, ...tXY };
            
            return {
                triplanar: true,
                samples: [
                    { u: tXY.u, v: tXY.v, w: weights.z },
                    { u: tXZ.u, v: tXZ.v, w: weights.y },
                    { u: tYZ.u, v: tYZ.v, w: weights.x }
                ]
            };
        }
        
        case MAPPING_MODES.triplanar:
        default: {
            const ax = Math.abs(normal.x);
            const ay = Math.abs(normal.y);
            const az = Math.abs(normal.z);
            const pw = 4.0;
            const bx = Math.pow(ax, pw);
            const by = Math.pow(ay, pw);
            const bz = Math.pow(az, pw);
            const sum = bx + by + bz + 1e-6;
            const wx = bx / sum;
            const wy = by / sum;
            const wz = bz / sum;
            
            let yzU = (pos.y - min.y) / maxDim;
            if (normal.x < 0) yzU = -yzU;
            let xzU = (pos.x - min.x) / maxDim;
            if (normal.y > 0) xzU = -xzU;
            let xyU = (pos.x - min.x) / maxDim;
            if (normal.z < 0) xyU = -xyU;
            
            const uvXY = applyTransform(xyU, (pos.y - min.y) / maxDim, scaleU, scaleV, offsetU, offsetV, rotRad);
            const uvXZ = applyTransform(xzU, (pos.z - min.z) / maxDim, scaleU, scaleV, offsetU, offsetV, rotRad);
            const uvYZ = applyTransform(yzU, (pos.z - min.z) / maxDim, scaleU, scaleV, offsetU, offsetV, rotRad);
            
            return {
                triplanar: true,
                samples: [
                    { ...uvXY, w: wz },
                    { ...uvXZ, w: wy },
                    { ...uvYZ, w: wx }
                ]
            };
        }
    }
    
    return applyTransform(u, v, scaleU, scaleV, offsetU, offsetV, rotRad);
}

module.exports = {
    MAPPING_MODES,
    computeUV,
    getDominantCubicAxis,
    getCubicBlendWeights,
    applyTransform
};
