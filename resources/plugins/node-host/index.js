/**
 * OrcaSlicer Plugin Host - Node.js Runtime
 * 
 * This script runs as a child process of OrcaSlicer and handles:
 * - Loading JavaScript/TypeScript plugins
 * - IPC communication with OrcaSlicer via stdio
 * - Providing the plugin SDK to loaded plugins
 * 
 * @copyright 2024 OrcaSlicer Contributors
 * @license AGPL-3.0
 */

const { EventEmitter } = require('events');
const path = require('path');
const fs = require('fs');

//==============================================================================
// IPC Protocol - Length-prefixed JSON messages
//==============================================================================

class IPCChannel extends EventEmitter {
    constructor(input, output) {
        super();
        this.input = input;
        this.output = output;
        this.buffer = Buffer.alloc(0);
        this.nextId = 1;
        this.pendingRequests = new Map();
        
        this.input.on('data', (chunk) => this._onData(chunk));
        this.input.on('end', () => this.emit('close'));
        this.input.on('error', (err) => this.emit('error', err));
    }
    
    _onData(chunk) {
        this.buffer = Buffer.concat([this.buffer, chunk]);
        
        while (this.buffer.length >= 4) {
            const msgLen = this.buffer.readUInt32LE(0);
            if (this.buffer.length < 4 + msgLen) break;
            
            const msgData = this.buffer.slice(4, 4 + msgLen).toString('utf8');
            this.buffer = this.buffer.slice(4 + msgLen);
            
            try {
                const msg = JSON.parse(msgData);
                this._handleMessage(msg);
            } catch (e) {
                console.error('IPC: Failed to parse message:', e);
            }
        }
    }
    
    _handleMessage(msg) {
        // Check if this is a response to a pending request
        if (msg.id && this.pendingRequests.has(msg.id)) {
            const { resolve, reject } = this.pendingRequests.get(msg.id);
            this.pendingRequests.delete(msg.id);
            
            if (msg.error) {
                reject(new Error(msg.error));
            } else {
                resolve(msg.result);
            }
            return;
        }
        
        // Otherwise it's a request or notification from the host
        this.emit('message', msg);
    }
    
    send(msg) {
        const json = JSON.stringify(msg);
        const buffer = Buffer.alloc(4 + Buffer.byteLength(json));
        buffer.writeUInt32LE(Buffer.byteLength(json), 0);
        buffer.write(json, 4, 'utf8');
        this.output.write(buffer);
    }
    
    async call(method, params = {}) {
        const id = this.nextId++;
        
        return new Promise((resolve, reject) => {
            this.pendingRequests.set(id, { resolve, reject });
            this.send({ type: 'request', id, method, params });
            
            // Timeout after 30 seconds
            setTimeout(() => {
                if (this.pendingRequests.has(id)) {
                    this.pendingRequests.delete(id);
                    reject(new Error(`Request ${method} timed out`));
                }
            }, 30000);
        });
    }
    
    notify(method, params = {}) {
        this.send({ type: 'notification', method, params });
    }
    
    response(id, result) {
        this.send({ type: 'response', id, result });
    }
    
    error(id, code, message) {
        this.send({ type: 'response', id, error: message, errorCode: code });
    }
}

//==============================================================================
// Plugin SDK - API provided to plugins
//==============================================================================

class PluginSDK {
    constructor(pluginId, ipc) {
        this.pluginId = pluginId;
        this.ipc = ipc;
    }
    
    //--------------------------------------------------------------------------
    // Mesh Operations
    //--------------------------------------------------------------------------
    
    /**
     * Get mesh data for selected objects
     * @returns {Promise<MeshView[]>}
     */
    async getSelectedMeshes() {
        return this.ipc.call('getSelectedMeshes', { pluginId: this.pluginId });
    }
    
    /**
     * Get mesh data for a specific object/volume
     * @param {number} objectIdx 
     * @param {number} volumeIdx 
     * @returns {Promise<MeshView|null>}
     */
    async getMesh(objectIdx, volumeIdx) {
        return this.ipc.call('getMesh', { 
            pluginId: this.pluginId, 
            objectIdx, 
            volumeIdx 
        });
    }
    
    /**
     * Replace mesh data for an object/volume
     * @param {number} objectIdx 
     * @param {number} volumeIdx 
     * @param {MeshData} meshData 
     * @returns {Promise<boolean>}
     */
    async setMesh(objectIdx, volumeIdx, meshData) {
        return this.ipc.call('setMesh', {
            pluginId: this.pluginId,
            objectIdx,
            volumeIdx,
            vertices: Array.from(meshData.vertices),
            normals: meshData.normals ? Array.from(meshData.normals) : null,
            indices: Array.from(meshData.indices)
        });
    }
    
    /**
     * Add a new object with the given mesh
     * @param {MeshData} meshData 
     * @param {string} name 
     * @returns {Promise<number>} Object index
     */
    async addObject(meshData, name) {
        return this.ipc.call('addObject', {
            pluginId: this.pluginId,
            vertices: Array.from(meshData.vertices),
            normals: meshData.normals ? Array.from(meshData.normals) : null,
            indices: Array.from(meshData.indices),
            name
        });
    }
    
    /**
     * Get mesh bounding box
     * @param {number} objectIdx 
     * @param {number} volumeIdx 
     * @returns {Promise<{min: Vec3, max: Vec3, center: Vec3, size: Vec3}>}
     */
    async getMeshBounds(objectIdx, volumeIdx) {
        return this.ipc.call('getMeshBounds', {
            pluginId: this.pluginId,
            objectIdx,
            volumeIdx
        });
    }
    
    //--------------------------------------------------------------------------
    // Image Operations
    //--------------------------------------------------------------------------
    
    /**
     * Load an image from file
     * @param {string} filePath 
     * @returns {Promise<ImageData|null>}
     */
    async loadImage(filePath) {
        return this.ipc.call('loadImage', { 
            pluginId: this.pluginId, 
            path: filePath 
        });
    }
    
    /**
     * Save an image to file
     * @param {ImageData} imageData 
     * @param {string} filePath 
     * @returns {Promise<boolean>}
     */
    async saveImage(imageData, filePath) {
        return this.ipc.call('saveImage', {
            pluginId: this.pluginId,
            pixels: Array.from(imageData.pixels),
            width: imageData.width,
            height: imageData.height,
            channels: imageData.channels,
            path: filePath
        });
    }
    
    //--------------------------------------------------------------------------
    // Configuration
    //--------------------------------------------------------------------------
    
    /**
     * Get a configuration value
     * @param {string} key 
     * @returns {Promise<string>}
     */
    async getConfig(key) {
        return this.ipc.call('getConfig', { pluginId: this.pluginId, key });
    }
    
    /**
     * Set a configuration value
     * @param {string} key 
     * @param {string} value 
     */
    async setConfig(key, value) {
        return this.ipc.call('setConfig', { pluginId: this.pluginId, key, value });
    }
    
    /**
     * Get a printer/print/filament setting
     * @param {string} key 
     * @returns {Promise<string>}
     */
    async getSetting(key) {
        return this.ipc.call('getSetting', { pluginId: this.pluginId, key });
    }
    
    //--------------------------------------------------------------------------
    // Progress
    //--------------------------------------------------------------------------
    
    /**
     * Update progress indicator
     * @param {number} percent 0-100
     * @param {string} message Optional message
     */
    updateProgress(percent, message = '') {
        this.ipc.notify('progress', {
            pluginId: this.pluginId,
            percent,
            message
        });
    }
    
    /**
     * Log a message
     * @param {string} message 
     */
    log(message) {
        this.ipc.notify('log', { pluginId: this.pluginId, message });
    }
    
    /**
     * Report an error
     * @param {string} message 
     */
    error(message) {
        this.ipc.notify('error', { pluginId: this.pluginId, message });
    }
    
    //--------------------------------------------------------------------------
    // UI Operations
    //--------------------------------------------------------------------------
    
    /**
     * Show a message dialog
     * @param {string} title 
     * @param {string} message 
     */
    async showMessage(title, message) {
        return this.ipc.call('showMessage', {
            pluginId: this.pluginId,
            title,
            message
        });
    }
    
    /**
     * Show a brief notification (toast/status bar)
     * @param {string} message - Message to display
     * @param {number} [duration] - Display time in milliseconds (default 3000)
     */
    showNotification(message, duration = 3000) {
        this.ipc.notify('showNotification', {
            pluginId: this.pluginId,
            message,
            duration
        });
    }
    
    /**
     * Show a confirmation dialog
     * @param {string} title 
     * @param {string} message 
     * @returns {Promise<boolean>}
     */
    async showConfirm(title, message) {
        return this.ipc.call('showConfirm', {
            pluginId: this.pluginId,
            title,
            message
        });
    }
    
    /**
     * Open a file dialog
     * @param {string} title 
     * @param {string} filter File filter (e.g., "Image files|*.png;*.jpg")
     * @returns {Promise<string|null>}
     */
    async openFileDialog(title, filter) {
        return this.ipc.call('openFileDialog', {
            pluginId: this.pluginId,
            title,
            filter
        });
    }
    
    /**
     * Save file dialog
     * @param {string} title 
     * @param {string} filter 
     * @param {string} defaultName 
     * @returns {Promise<string|null>}
     */
    async saveFileDialog(title, filter, defaultName) {
        return this.ipc.call('saveFileDialog', {
            pluginId: this.pluginId,
            title,
            filter,
            defaultName
        });
    }
    
    //--------------------------------------------------------------------------
    // Utility
    //--------------------------------------------------------------------------
    
    /**
     * Get plugin's data directory
     * @returns {Promise<string>}
     */
    async getDataDir() {
        return this.ipc.call('getPluginDataDir', { pluginId: this.pluginId });
    }
    
    /**
     * Get OrcaSlicer resources directory
     * @returns {Promise<string>}
     */
    async getResourcesDir() {
        return this.ipc.call('getResourcesDir', { pluginId: this.pluginId });
    }
    
    //--------------------------------------------------------------------------
    // Menu Operations - Register menu items under the Plugins menu
    //--------------------------------------------------------------------------
    
    /**
     * Register a submenu under the Plugins menu
     * @param {Object} submenu
     * @param {string} submenu.id - Unique submenu ID within this plugin
     * @param {string} submenu.label - Display label
     * @param {string} [submenu.iconPath] - Optional icon path
     * @param {number} [submenu.order] - Sort order (default 0)
     * @returns {Promise<boolean>}
     */
    async registerSubmenu(submenu) {
        return this.ipc.call('registerSubmenu', {
            pluginId: this.pluginId,
            submenu: {
                id: submenu.id,
                label: submenu.label,
                iconPath: submenu.iconPath || '',
                order: submenu.order || 0
            }
        });
    }
    
    /**
     * Unregister a submenu (and all its items)
     * @param {string} submenuId
     * @returns {Promise<boolean>}
     */
    async unregisterSubmenu(submenuId) {
        return this.ipc.call('unregisterSubmenu', {
            pluginId: this.pluginId,
            submenuId
        });
    }
    
    /**
     * Register a menu item
     * @param {Object} item
     * @param {string} item.id - Unique item ID within this plugin
     * @param {string} item.label - Display label
     * @param {string} [item.tooltip] - Optional tooltip
     * @param {string} [item.iconPath] - Optional icon path
     * @param {string} [item.submenuId] - If set, place in this submenu
     * @param {string} [item.shortcut] - Keyboard shortcut (e.g., "Ctrl+Shift+T")
     * @param {boolean} [item.checkable] - Whether item is checkable
     * @param {boolean} [item.checked] - Initial checked state
     * @param {boolean} [item.disabled] - Initial disabled state
     * @param {boolean} [item.separator] - If true, item is a separator
     * @param {number} [item.order] - Sort order (default 0)
     * @param {*} [item.callbackData] - Extra data passed back on click
     * @returns {Promise<boolean>}
     */
    async registerMenuItem(item) {
        let flags = 0;
        if (item.checkable) flags |= 1;    // Checkable
        if (item.checked) flags |= 2;       // Checked  
        if (item.disabled) flags |= 4;      // Disabled
        if (item.separator) flags |= 8;     // Separator
        
        return this.ipc.call('registerMenuItem', {
            pluginId: this.pluginId,
            item: {
                id: item.id,
                label: item.label || '',
                tooltip: item.tooltip || '',
                iconPath: item.iconPath || '',
                submenuId: item.submenuId || '',
                shortcut: item.shortcut || '',
                flags: flags,
                order: item.order || 0,
                callbackData: JSON.stringify(item.callbackData || null)
            }
        });
    }
    
    /**
     * Unregister a menu item
     * @param {string} itemId
     * @returns {Promise<boolean>}
     */
    async unregisterMenuItem(itemId) {
        return this.ipc.call('unregisterMenuItem', {
            pluginId: this.pluginId,
            itemId
        });
    }
    
    /**
     * Update a menu item's properties
     * @param {string} itemId
     * @param {Object} item - Updated properties (same as registerMenuItem)
     * @returns {Promise<boolean>}
     */
    async updateMenuItem(itemId, item) {
        let flags = 0;
        if (item.checkable) flags |= 1;
        if (item.checked) flags |= 2;
        if (item.disabled) flags |= 4;
        if (item.separator) flags |= 8;
        
        return this.ipc.call('updateMenuItem', {
            pluginId: this.pluginId,
            itemId,
            item: {
                id: itemId,
                label: item.label || '',
                tooltip: item.tooltip || '',
                iconPath: item.iconPath || '',
                submenuId: item.submenuId || '',
                shortcut: item.shortcut || '',
                flags: flags,
                order: item.order || 0,
                callbackData: JSON.stringify(item.callbackData || null)
            }
        });
    }
    
    /**
     * Get all menu items registered by this plugin
     * @returns {Promise<Object[]>}
     */
    async getRegisteredMenuItems() {
        return this.ipc.call('getRegisteredMenuItems', {
            pluginId: this.pluginId
        });
    }
    
    /**
     * Set callback for when a menu item is clicked
     * This is automatically called by the plugin host
     * @param {function(string, *)} callback - (itemId, callbackData) => void
     * @internal
     */
    _setMenuClickHandler(callback) {
        this._menuClickHandler = callback;
    }
    
    /**
     * Called by the plugin host when a menu item is clicked
     * @internal
     */
    _handleMenuClick(itemId, callbackData) {
        if (this._menuClickHandler) {
            let data = null;
            try {
                data = callbackData ? JSON.parse(callbackData) : null;
            } catch (e) {
                data = callbackData;
            }
            this._menuClickHandler(itemId, data);
        }
    }
}

//==============================================================================
// Mesh Utilities
//==============================================================================

/**
 * Mesh data structure
 * @typedef {Object} MeshData
 * @property {Float32Array} vertices - Flat array [x0,y0,z0, x1,y1,z1, ...]
 * @property {Float32Array} normals - Per-vertex normals (optional)
 * @property {Int32Array} indices - Triangle indices [i0,i1,i2, ...]
 */

/**
 * Create MeshData from arrays
 * @param {number[]} vertices 
 * @param {number[]} indices 
 * @param {number[]?} normals 
 * @returns {MeshData}
 */
function createMeshData(vertices, indices, normals = null) {
    return {
        vertices: Float32Array.from(vertices),
        indices: Int32Array.from(indices),
        normals: normals ? Float32Array.from(normals) : null
    };
}

/**
 * Compute normals for a mesh
 * @param {MeshData} mesh 
 * @returns {Float32Array}
 */
function computeNormals(mesh) {
    const vertexCount = mesh.vertices.length / 3;
    const normals = new Float32Array(mesh.vertices.length);
    
    // Accumulate face normals to vertices
    for (let i = 0; i < mesh.indices.length; i += 3) {
        const i0 = mesh.indices[i] * 3;
        const i1 = mesh.indices[i + 1] * 3;
        const i2 = mesh.indices[i + 2] * 3;
        
        // Get vertices
        const v0 = [mesh.vertices[i0], mesh.vertices[i0 + 1], mesh.vertices[i0 + 2]];
        const v1 = [mesh.vertices[i1], mesh.vertices[i1 + 1], mesh.vertices[i1 + 2]];
        const v2 = [mesh.vertices[i2], mesh.vertices[i2 + 1], mesh.vertices[i2 + 2]];
        
        // Compute face normal
        const e1 = [v1[0] - v0[0], v1[1] - v0[1], v1[2] - v0[2]];
        const e2 = [v2[0] - v0[0], v2[1] - v0[1], v2[2] - v0[2]];
        const fn = [
            e1[1] * e2[2] - e1[2] * e2[1],
            e1[2] * e2[0] - e1[0] * e2[2],
            e1[0] * e2[1] - e1[1] * e2[0]
        ];
        
        // Add to vertex normals
        for (const idx of [i0, i1, i2]) {
            normals[idx] += fn[0];
            normals[idx + 1] += fn[1];
            normals[idx + 2] += fn[2];
        }
    }
    
    // Normalize
    for (let i = 0; i < normals.length; i += 3) {
        const len = Math.sqrt(
            normals[i] * normals[i] + 
            normals[i + 1] * normals[i + 1] + 
            normals[i + 2] * normals[i + 2]
        ) || 1;
        normals[i] /= len;
        normals[i + 1] /= len;
        normals[i + 2] /= len;
    }
    
    return normals;
}

//==============================================================================
// Plugin Manager
//==============================================================================

class PluginManager {
    constructor(ipc) {
        this.ipc = ipc;
        this.plugins = new Map(); // pluginId -> { module, instance, sdk }
    }
    
    async loadPlugin(pluginId, pluginPath, manifest) {
        console.error(`Loading plugin: ${pluginId} from ${pluginPath}`);
        
        try {
            // Resolve the entry point
            const entryPoint = path.resolve(pluginPath, manifest.entryPoint || 'index.js');
            
            if (!fs.existsSync(entryPoint)) {
                throw new Error(`Entry point not found: ${entryPoint}`);
            }
            
            // Create SDK for this plugin
            const sdk = new PluginSDK(pluginId, this.ipc);
            
            // Load the module
            // Clear require cache to allow reloading
            delete require.cache[require.resolve(entryPoint)];
            const pluginModule = require(entryPoint);
            
            // Create plugin instance
            const instance = typeof pluginModule === 'function' 
                ? new pluginModule(sdk)
                : pluginModule;
            
            // Call onLoad if available
            if (typeof instance.onLoad === 'function') {
                await instance.onLoad(sdk);
            }
            
            this.plugins.set(pluginId, { module: pluginModule, instance, sdk });
            
            return true;
            
        } catch (error) {
            console.error(`Failed to load plugin ${pluginId}:`, error);
            throw error;
        }
    }
    
    async unloadPlugin(pluginId) {
        const plugin = this.plugins.get(pluginId);
        if (!plugin) return;
        
        try {
            if (typeof plugin.instance.onUnload === 'function') {
                await plugin.instance.onUnload();
            }
        } catch (error) {
            console.error(`Error unloading plugin ${pluginId}:`, error);
        }
        
        this.plugins.delete(pluginId);
    }
    
    async callPlugin(pluginId, method, params) {
        const plugin = this.plugins.get(pluginId);
        if (!plugin) {
            throw new Error(`Plugin not loaded: ${pluginId}`);
        }
        
        const fn = plugin.instance[method];
        if (typeof fn !== 'function') {
            throw new Error(`Plugin ${pluginId} does not have method ${method}`);
        }
        
        return fn.call(plugin.instance, params, plugin.sdk);
    }
    
    getPlugin(pluginId) {
        return this.plugins.get(pluginId);
    }
}

//==============================================================================
// Main Entry Point
//==============================================================================

async function main() {
    console.error('OrcaSlicer Node.js Plugin Runtime starting...');
    
    // Create IPC channel over stdin/stdout
    const ipc = new IPCChannel(process.stdin, process.stdout);
    
    // Create plugin manager
    const manager = new PluginManager(ipc);
    
    // Handle messages from OrcaSlicer
    ipc.on('message', async (msg) => {
        try {
            switch (msg.method) {
                case 'loadPlugin': {
                    const { pluginId, pluginPath, manifest } = msg.params;
                    await manager.loadPlugin(pluginId, pluginPath, manifest);
                    ipc.response(msg.id, { success: true });
                    break;
                }
                
                case 'unloadPlugin': {
                    const { pluginId } = msg.params;
                    await manager.unloadPlugin(pluginId);
                    ipc.response(msg.id, { success: true });
                    break;
                }
                
                case 'callPlugin': {
                    const { pluginId, method, params } = msg.params;
                    const result = await manager.callPlugin(pluginId, method, params);
                    ipc.response(msg.id, result);
                    break;
                }
                
                case 'processMesh': {
                    const { pluginId, objectIdx, volumeIdx, operation, params } = msg.params;
                    const result = await manager.callPlugin(pluginId, 'processMesh', {
                        objectIdx,
                        volumeIdx,
                        operation,
                        params
                    });
                    ipc.response(msg.id, result);
                    break;
                }
                
                case 'processGcode': {
                    const { pluginId, gcode } = msg.params;
                    const result = await manager.callPlugin(pluginId, 'processGcode', { gcode });
                    ipc.response(msg.id, result);
                    break;
                }
                
                case 'menuClick': {
                    const { pluginId, itemId, callbackData, isChecked } = msg.params;
                    await manager.callPlugin(pluginId, 'onMenuClick', { 
                        itemId, 
                        callbackData,
                        isChecked: isChecked || false
                    });
                    ipc.response(msg.id, { success: true });
                    break;
                }
                
                case 'ping': {
                    ipc.response(msg.id, { pong: true });
                    break;
                }
                
                case 'shutdown': {
                    console.error('Shutdown requested');
                    ipc.response(msg.id, { success: true });
                    process.exit(0);
                    break;
                }
                
                default:
                    ipc.error(msg.id, -32601, `Unknown method: ${msg.method}`);
            }
        } catch (error) {
            console.error('Error handling message:', error);
            ipc.error(msg.id, -32603, error.message);
        }
    });
    
    ipc.on('close', () => {
        console.error('IPC channel closed');
        process.exit(0);
    });
    
    ipc.on('error', (error) => {
        console.error('IPC error:', error);
    });
    
    // Notify host that we're ready
    ipc.notify('ready', { version: process.version });
    
    console.error('Plugin runtime ready');
}

// Export utilities for plugins
module.exports = {
    createMeshData,
    computeNormals,
    PluginSDK
};

// Run if this is the main module
if (require.main === module) {
    main().catch(error => {
        console.error('Fatal error:', error);
        process.exit(1);
    });
}
