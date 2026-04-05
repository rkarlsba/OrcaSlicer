/**
 * Adaptive mesh subdivision for BumpMesh
 * 
 * Based on CNC Kitchen's stlTexturizer subdivision algorithm.
 * Subdivides mesh until all edges are below the target length.
 * 
 * @copyright 2024 CNC Kitchen / OrcaSlicer Contributors
 * @license MIT
 */

const SAFETY_CAP = 10_000_000; // Maximum triangles
const MAX_ITERATIONS = 12;
const QUANTISE = 1e4;

/**
 * Compute squared distance between two vertices
 */
function edgeLenSq(positions, a, b) {
    const dx = positions[a * 3] - positions[b * 3];
    const dy = positions[a * 3 + 1] - positions[b * 3 + 1];
    const dz = positions[a * 3 + 2] - positions[b * 3 + 2];
    return dx * dx + dy * dy + dz * dz;
}

/**
 * Get or create midpoint vertex
 */
function getMidpoint(positions, normals, cache, a, b) {
    const key = a < b ? `${a}:${b}` : `${b}:${a}`;
    if (cache.has(key)) return cache.get(key);
    
    const idx = positions.length / 3;
    
    // Midpoint position
    const mx = (positions[a * 3] + positions[b * 3]) / 2;
    const my = (positions[a * 3 + 1] + positions[b * 3 + 1]) / 2;
    const mz = (positions[a * 3 + 2] + positions[b * 3 + 2]) / 2;
    
    positions.push(mx, my, mz);
    
    // Midpoint normal (interpolate and normalize)
    if (normals) {
        const nx = normals[a * 3] + normals[b * 3];
        const ny = normals[a * 3 + 1] + normals[b * 3 + 1];
        const nz = normals[a * 3 + 2] + normals[b * 3 + 2];
        const len = Math.sqrt(nx * nx + ny * ny + nz * nz) || 1;
        normals.push(nx / len, ny / len, nz / len);
    }
    
    cache.set(key, idx);
    return idx;
}

/**
 * Convert non-indexed mesh to indexed
 */
function toIndexed(vertices, normals) {
    const n = vertices.length / 3;
    const outPositions = [];
    const outNormals = normals ? [] : null;
    const outIndices = [];
    const vertMap = new Map();
    
    for (let i = 0; i < n; i++) {
        const px = vertices[i * 3];
        const py = vertices[i * 3 + 1];
        const pz = vertices[i * 3 + 2];
        
        const key = `${Math.round(px * QUANTISE)}_${Math.round(py * QUANTISE)}_${Math.round(pz * QUANTISE)}`;
        
        let idx = vertMap.get(key);
        if (idx === undefined) {
            idx = outPositions.length / 3;
            outPositions.push(px, py, pz);
            if (outNormals && normals) {
                outNormals.push(normals[i * 3], normals[i * 3 + 1], normals[i * 3 + 2]);
            }
            vertMap.set(key, idx);
        }
        outIndices.push(idx);
    }
    
    return { positions: outPositions, normals: outNormals, indices: outIndices };
}

/**
 * Convert indexed mesh back to non-indexed
 */
function toNonIndexed(positions, normals, indices) {
    const triCount = indices.length / 3;
    const outVertices = new Float32Array(triCount * 9);
    const outNormals = new Float32Array(triCount * 9);
    
    for (let t = 0; t < triCount; t++) {
        for (let v = 0; v < 3; v++) {
            const vidx = indices[t * 3 + v];
            outVertices[t * 9 + v * 3] = positions[vidx * 3];
            outVertices[t * 9 + v * 3 + 1] = positions[vidx * 3 + 1];
            outVertices[t * 9 + v * 3 + 2] = positions[vidx * 3 + 2];
            
            if (normals) {
                outNormals[t * 9 + v * 3] = normals[vidx * 3];
                outNormals[t * 9 + v * 3 + 1] = normals[vidx * 3 + 1];
                outNormals[t * 9 + v * 3 + 2] = normals[vidx * 3 + 2];
            }
        }
    }
    
    return { vertices: outVertices, normals: outNormals };
}

/**
 * Single subdivision pass
 */
function subdividePass(positions, normals, indices, maxSq, safetyCap) {
    const midCache = new Map();
    const edgeKey = (a, b) => a < b ? `${a}:${b}` : `${b}:${a}`;
    
    // Step 1: Identify edges that need splitting
    const splitEdges = new Set();
    for (let t = 0; t < indices.length; t += 3) {
        const a = indices[t], b = indices[t + 1], c = indices[t + 2];
        if (edgeLenSq(positions, a, b) > maxSq) splitEdges.add(edgeKey(a, b));
        if (edgeLenSq(positions, b, c) > maxSq) splitEdges.add(edgeKey(b, c));
        if (edgeLenSq(positions, c, a) > maxSq) splitEdges.add(edgeKey(c, a));
    }
    
    if (splitEdges.size === 0) {
        return { newIndices: indices, changed: false };
    }
    
    // Step 2: Rebuild triangles
    const nextIndices = [];
    
    for (let t = 0; t < indices.length; t += 3) {
        if (nextIndices.length / 3 >= safetyCap) {
            // Safety cap reached, copy remaining triangles
            for (let r = t; r < indices.length; r++) {
                nextIndices.push(indices[r]);
            }
            break;
        }
        
        const a = indices[t], b = indices[t + 1], c = indices[t + 2];
        const sAB = splitEdges.has(edgeKey(a, b));
        const sBC = splitEdges.has(edgeKey(b, c));
        const sCA = splitEdges.has(edgeKey(c, a));
        const n = (sAB ? 1 : 0) + (sBC ? 1 : 0) + (sCA ? 1 : 0);
        
        if (n === 0) {
            // No split needed
            nextIndices.push(a, b, c);
            
        } else if (n === 3) {
            // Split all edges (1->4 triangles)
            const mAB = getMidpoint(positions, normals, midCache, a, b);
            const mBC = getMidpoint(positions, normals, midCache, b, c);
            const mCA = getMidpoint(positions, normals, midCache, c, a);
            nextIndices.push(
                a, mAB, mCA,
                mAB, b, mBC,
                mCA, mBC, c,
                mAB, mBC, mCA
            );
            
        } else if (n === 1) {
            // Split one edge (1->2 triangles)
            if (sAB) {
                const m = getMidpoint(positions, normals, midCache, a, b);
                nextIndices.push(a, m, c, m, b, c);
            } else if (sBC) {
                const m = getMidpoint(positions, normals, midCache, b, c);
                nextIndices.push(a, b, m, a, m, c);
            } else {
                const m = getMidpoint(positions, normals, midCache, c, a);
                nextIndices.push(a, b, m, m, b, c);
            }
            
        } else {
            // Split two edges (1->3 triangles)
            if (!sAB) {
                const mBC = getMidpoint(positions, normals, midCache, b, c);
                const mCA = getMidpoint(positions, normals, midCache, c, a);
                nextIndices.push(a, b, mBC, a, mBC, mCA, c, mCA, mBC);
            } else if (!sBC) {
                const mAB = getMidpoint(positions, normals, midCache, a, b);
                const mCA = getMidpoint(positions, normals, midCache, c, a);
                nextIndices.push(a, mAB, mCA, mAB, b, c, mAB, c, mCA);
            } else {
                const mAB = getMidpoint(positions, normals, midCache, a, b);
                const mBC = getMidpoint(positions, normals, midCache, b, c);
                nextIndices.push(b, mBC, mAB, a, mAB, mBC, a, mBC, c);
            }
        }
    }
    
    return { newIndices: nextIndices, changed: true };
}

/**
 * Compute vertex normals from face normals (area weighted)
 */
function computeNormals(positions, indices) {
    const vertexCount = positions.length / 3;
    const normals = new Array(positions.length).fill(0);
    
    // Accumulate face normals
    for (let t = 0; t < indices.length; t += 3) {
        const i0 = indices[t], i1 = indices[t + 1], i2 = indices[t + 2];
        
        const ax = positions[i0 * 3], ay = positions[i0 * 3 + 1], az = positions[i0 * 3 + 2];
        const bx = positions[i1 * 3], by = positions[i1 * 3 + 1], bz = positions[i1 * 3 + 2];
        const cx = positions[i2 * 3], cy = positions[i2 * 3 + 1], cz = positions[i2 * 3 + 2];
        
        const e1x = bx - ax, e1y = by - ay, e1z = bz - az;
        const e2x = cx - ax, e2y = cy - ay, e2z = cz - az;
        
        // Cross product (not normalized = area weighted)
        const nx = e1y * e2z - e1z * e2y;
        const ny = e1z * e2x - e1x * e2z;
        const nz = e1x * e2y - e1y * e2x;
        
        for (const idx of [i0, i1, i2]) {
            normals[idx * 3] += nx;
            normals[idx * 3 + 1] += ny;
            normals[idx * 3 + 2] += nz;
        }
    }
    
    // Normalize
    for (let i = 0; i < vertexCount; i++) {
        const nx = normals[i * 3];
        const ny = normals[i * 3 + 1];
        const nz = normals[i * 3 + 2];
        const len = Math.sqrt(nx * nx + ny * ny + nz * nz) || 1;
        normals[i * 3] = nx / len;
        normals[i * 3 + 1] = ny / len;
        normals[i * 3 + 2] = nz / len;
    }
    
    return normals;
}

/**
 * Main subdivision function
 * 
 * @param {Object} mesh - Input mesh { vertices, normals, indices }
 * @param {number} maxEdgeLength - Maximum allowed edge length
 * @param {Function} onProgress - Progress callback (progress, triCount, longestEdge)
 * @returns {Promise<Object>} Subdivided mesh
 */
async function subdivide(mesh, maxEdgeLength, onProgress) {
    // Convert to indexed if non-indexed
    let positions, normals, indices;
    
    if (!mesh.indices || mesh.indices.length === 0) {
        // Non-indexed mesh - convert
        const indexed = toIndexed(
            Array.from(mesh.vertices),
            mesh.normals ? Array.from(mesh.normals) : null
        );
        positions = indexed.positions;
        normals = indexed.normals;
        indices = indexed.indices;
    } else {
        positions = Array.from(mesh.vertices);
        normals = mesh.normals ? Array.from(mesh.normals) : null;
        indices = Array.from(mesh.indices);
    }
    
    // Compute normals if not provided
    if (!normals || normals.length === 0) {
        normals = computeNormals(positions, indices);
    }
    
    const maxSq = maxEdgeLength * maxEdgeLength;
    let safetyCapHit = false;
    
    for (let iter = 0; iter < MAX_ITERATIONS; iter++) {
        const triCount = indices.length / 3;
        
        if (triCount >= SAFETY_CAP) {
            safetyCapHit = true;
            break;
        }
        
        // Find longest edge for progress reporting
        let maxEdgeLenSq = 0;
        for (let t = 0; t < indices.length; t += 3) {
            const a = indices[t], b = indices[t + 1], c = indices[t + 2];
            maxEdgeLenSq = Math.max(maxEdgeLenSq,
                edgeLenSq(positions, a, b),
                edgeLenSq(positions, b, c),
                edgeLenSq(positions, c, a)
            );
        }
        const longestEdge = Math.sqrt(maxEdgeLenSq);
        
        // Report progress
        if (onProgress) {
            onProgress(iter / MAX_ITERATIONS, triCount, longestEdge);
        }
        
        // Perform subdivision pass
        const result = subdividePass(positions, normals, indices, maxSq, SAFETY_CAP);
        indices = result.newIndices;
        
        // Check if we're done
        if (!result.changed || indices.length / 3 >= SAFETY_CAP) {
            if (indices.length / 3 >= SAFETY_CAP) safetyCapHit = true;
            break;
        }
        
        // Yield to event loop
        await new Promise(r => setImmediate(r));
    }
    
    // Recalculate normals after subdivision
    normals = computeNormals(positions, indices);
    
    // Convert back to non-indexed for displacement
    const result = toNonIndexed(positions, normals, indices);
    
    return {
        vertices: result.vertices,
        normals: result.normals,
        indices: new Int32Array(indices),
        safetyCapHit
    };
}

module.exports = { subdivide };
