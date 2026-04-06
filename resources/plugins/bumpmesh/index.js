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
const path = require('path');
const fs = require('fs');

// Built-in textures (from CNC Kitchen stlTexturizer)
const BUILTIN_TEXTURES = [
    { id: 'basket', label: 'Basket Weave', file: 'basket.jpg' },
    { id: 'brick', label: 'Brick', file: 'brick.jpg' },
    { id: 'bubble', label: 'Bubble', file: 'bubble.jpg' },
    { id: 'carbonFiber', label: 'Carbon Fiber', file: 'carbonFiber.jpg' },
    { id: 'crystal', label: 'Crystal', file: 'crystal.jpg' },
    { id: 'dots', label: 'Dots', file: 'dots.jpg' },
    { id: 'grid', label: 'Grid', file: 'grid.png' },
    { id: 'gripSurface', label: 'Grip Surface', file: 'gripSurface.jpg' },
    { id: 'hexagon', label: 'Hexagon', file: 'hexagon.jpg' },
    { id: 'hexagons', label: 'Hexagons', file: 'hexagons.jpg' },
    { id: 'isogrid', label: 'Isogrid', file: 'isogrid.png' },
    { id: 'knitting', label: 'Knitting', file: 'knitting.jpg' },
    { id: 'knurling', label: 'Knurling', file: 'knurling.jpg' },
    { id: 'leather', label: 'Leather', file: 'leather.jpg' },
    { id: 'leather2', label: 'Leather 2', file: 'leather2.jpg' },
    { id: 'noise', label: 'Noise', file: 'noise.jpg' },
    { id: 'stripes', label: 'Stripes', file: 'stripes.png' },
    { id: 'stripes_02', label: 'Stripes 2', file: 'stripes_02.png' },
    { id: 'voronoi', label: 'Voronoi', file: 'voronoi.jpg' },
    { id: 'weave', label: 'Weave', file: 'weave.jpg' },
    { id: 'weave_02', label: 'Weave 2', file: 'weave_02.jpg' },
    { id: 'weave_03', label: 'Weave 3', file: 'weave_03.jpg' },
    { id: 'wood', label: 'Wood', file: 'wood.jpg' },
    { id: 'woodgrain_02', label: 'Woodgrain 2', file: 'woodgrain_02.jpg' },
    { id: 'woodgrain_03', label: 'Woodgrain 3', file: 'woodgrain_03.jpg' }
];

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
        
        // Register plugin menu
        await this._registerMenuItems(sdk);
    }
    
    /**
     * Register texture menu items
     */
    async _registerMenuItems(sdk) {
        try {
            // Register BumpMesh submenu
            await sdk.registerSubmenu({
                id: 'bumpmesh',
                label: 'BumpMesh Textures',
                position: 0
            });
            sdk.log('Registered BumpMesh submenu');
            
            // Register each built-in texture as a menu item
            for (let i = 0; i < BUILTIN_TEXTURES.length; i++) {
                const tex = BUILTIN_TEXTURES[i];
                await sdk.registerMenuItem({
                    id: `texture_${tex.id}`,
                    submenuId: 'bumpmesh',
                    label: tex.label,
                    callbackData: JSON.stringify({ action: 'applyTexture', textureId: tex.id }),
                    position: i
                });
            }
            sdk.log(`Registered ${BUILTIN_TEXTURES.length} texture menu items`);
            
            // Add separator and custom texture option
            await sdk.registerMenuItem({
                id: 'custom_texture',
                submenuId: 'bumpmesh',
                label: 'Open Custom Texture...',
                callbackData: JSON.stringify({ action: 'customTexture' }),
                position: BUILTIN_TEXTURES.length + 1
            });
            sdk.log('Registered custom texture menu item');
            
            // Register menu click handler
            sdk._setMenuClickHandler(this.onMenuClick.bind(this));
            
        } catch (e) {
            sdk.log('Error registering menu items: ' + e.message);
        }
    }
    
    /**
     * Handle menu item clicks
     */
    async onMenuClick(itemId, callbackData) {
        const sdk = this.sdk;
        sdk.log(`Menu clicked: ${itemId}, data: ${callbackData}`);
        
        try {
            const data = JSON.parse(callbackData);
            
            if (data.action === 'applyTexture') {
                // Find the texture info
                const tex = BUILTIN_TEXTURES.find(t => t.id === data.textureId);
                if (tex) {
                    // Load and apply the built-in texture
                    const texturePath = path.join(__dirname, 'textures', tex.file);
                    sdk.log(`Applying texture: ${texturePath}`);
                    await this.setTexture(texturePath);
                    sdk.showNotification(`Texture "${tex.label}" applied. Select a mesh and use Process operations.`);
                } else {
                    sdk.log(`Unknown texture id: ${data.textureId}`);
                }
            } else if (data.action === 'customTexture') {
                // Request file open dialog
                sdk.log('Requesting custom texture file...');
                // TODO: Need to add file dialog support to SDK
                sdk.showNotification('Custom texture upload coming soon!');
            }
        } catch (e) {
            sdk.log(`Error handling menu click: ${e.message}`);
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
