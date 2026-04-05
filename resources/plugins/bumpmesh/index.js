/**
 * BumpMesh Plugin for OrcaSlicer
 * 
 * Applies texture displacement to 3D meshes.
 * Based on CNC Kitchen's stlTexturizer / BumpMesh.
 * 
 * @copyright 2024 CNC Kitchen / OrcaSlicer Contributors
 * @license MIT
 */

const { subdivide } = require('./subdivision');
const { applyDisplacement } = require('./displacement');
const { computeUV, MAPPING_MODES } = require('./mapping');

// Default settings
const DEFAULT_SETTINGS = {
    mappingMode: 'triplanar',
    scaleU: 1.0,
    scaleV: 1.0,
    offsetU: 0.0,
    offsetV: 0.0,
    rotation: 0,
    amplitude: 1.0,
    symmetricDisplacement: true,
    maxEdgeLength: 0.5,
    targetTriangles: 250000,
    topAngleLimit: 0,
    bottomAngleLimit: 0,
    mappingBlend: 0.5,
    seamBandWidth: 0.35
};

/**
 * BumpMesh Plugin Class
 */
class BumpMeshPlugin {
    constructor(sdk) {
        this.sdk = sdk;
        this.settings = { ...DEFAULT_SETTINGS };
        this.texturePath = null;
        this.textureData = null;
    }
    
    /**
     * Called when plugin is loaded
     */
    async onLoad(sdk) {
        sdk.log('BumpMesh plugin loaded');
        
        // Load saved settings
        try {
            const savedSettings = await sdk.getConfig('settings');
            if (savedSettings) {
                this.settings = { ...DEFAULT_SETTINGS, ...JSON.parse(savedSettings) };
            }
        } catch (e) {
            sdk.log('Could not load saved settings: ' + e.message);
        }
    }
    
    /**
     * Called when plugin is unloaded
     */
    async onUnload() {
        this.sdk.log('BumpMesh plugin unloaded');
    }
    
    /**
     * Process mesh - main entry point for mesh modification
     * @param {Object} params 
     * @param {number} params.objectIdx
     * @param {number} params.volumeIdx
     * @param {string} params.operation
     * @param {Object} params.params - Operation-specific parameters
     */
    async processMesh({ objectIdx, volumeIdx, operation, params }, sdk) {
        sdk = sdk || this.sdk;
        
        switch (operation) {
            case 'applyTexture':
                return this.applyTexture(objectIdx, volumeIdx, params, sdk);
            case 'setTexture':
                return this.setTexture(params, sdk);
            case 'setSettings':
                return this.setSettings(params, sdk);
            case 'getSettings':
                return this.getSettings();
            default:
                throw new Error(`Unknown operation: ${operation}`);
        }
    }
    
    /**
     * Set the texture to use for displacement
     */
    async setTexture({ path }, sdk) {
        sdk = sdk || this.sdk;
        
        if (!path) {
            // Open file dialog
            path = await sdk.openFileDialog(
                'Select Texture Image',
                'Image files|*.png;*.jpg;*.jpeg;*.bmp'
            );
            if (!path) return { success: false, message: 'No file selected' };
        }
        
        try {
            this.textureData = await sdk.loadImage(path);
            this.texturePath = path;
            sdk.log(`Loaded texture: ${path} (${this.textureData.width}x${this.textureData.height})`);
            return { success: true, path };
        } catch (e) {
            sdk.error(`Failed to load texture: ${e.message}`);
            return { success: false, message: e.message };
        }
    }
    
    /**
     * Update settings
     */
    async setSettings(newSettings, sdk) {
        sdk = sdk || this.sdk;
        
        this.settings = { ...this.settings, ...newSettings };
        
        // Save settings
        await sdk.setConfig('settings', JSON.stringify(this.settings));
        
        return { success: true, settings: this.settings };
    }
    
    /**
     * Get current settings
     */
    getSettings() {
        return { ...this.settings };
    }
    
    /**
     * Apply texture displacement to a mesh
     */
    async applyTexture(objectIdx, volumeIdx, params, sdk) {
        sdk = sdk || this.sdk;
        
        // Merge params with current settings
        const settings = { ...this.settings, ...params };
        
        // Check if we have a texture
        if (!this.textureData) {
            // Try to set texture first
            const result = await this.setTexture({ path: params?.texturePath }, sdk);
            if (!result.success) {
                return { success: false, message: 'No texture loaded' };
            }
        }
        
        // Get the mesh
        sdk.updateProgress(5, 'Getting mesh data...');
        const meshView = await sdk.getMesh(objectIdx, volumeIdx);
        if (!meshView) {
            return { success: false, message: 'Could not get mesh data' };
        }
        
        sdk.log(`Input mesh: ${meshView.triangleCount} triangles`);
        
        // Get bounds
        const bounds = await sdk.getMeshBounds(objectIdx, volumeIdx);
        
        // Convert to working format
        let mesh = {
            vertices: Float32Array.from(meshView.vertices),
            normals: meshView.normals ? Float32Array.from(meshView.normals) : null,
            indices: Int32Array.from(meshView.indices)
        };
        
        // Step 1: Subdivide mesh
        sdk.updateProgress(10, 'Subdividing mesh...');
        mesh = await subdivide(mesh, settings.maxEdgeLength, (progress, triCount, maxEdge) => {
            const pct = 10 + Math.floor(progress * 40);
            sdk.updateProgress(pct, `Subdividing... ${triCount} triangles, max edge: ${maxEdge.toFixed(2)}mm`);
        });
        
        sdk.log(`After subdivision: ${mesh.indices.length / 3} triangles`);
        
        // Step 2: Apply displacement
        sdk.updateProgress(50, 'Applying displacement...');
        mesh = await applyDisplacement(
            mesh,
            this.textureData,
            settings,
            bounds,
            (progress) => {
                const pct = 50 + Math.floor(progress * 40);
                sdk.updateProgress(pct, 'Applying displacement...');
            }
        );
        
        // Step 3: Decimate if over target
        if (mesh.indices.length / 3 > settings.targetTriangles) {
            sdk.updateProgress(90, 'Decimating mesh...');
            // TODO: Implement QEM decimation
            // For now, skip decimation
        }
        
        // Step 4: Set the modified mesh back
        sdk.updateProgress(95, 'Updating mesh...');
        const success = await sdk.setMesh(objectIdx, volumeIdx, {
            vertices: mesh.vertices,
            normals: mesh.normals,
            indices: mesh.indices
        });
        
        sdk.updateProgress(100, 'Done!');
        
        const finalTriCount = mesh.indices.length / 3;
        sdk.log(`Final mesh: ${finalTriCount} triangles`);
        
        return {
            success,
            triangleCount: finalTriCount,
            message: success ? `Applied texture with ${finalTriCount} triangles` : 'Failed to update mesh'
        };
    }
}

module.exports = BumpMeshPlugin;
