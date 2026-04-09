# OrcaSlicer Plugin Development Guide

## Overview

OrcaSlicer supports a modular plugin system that allows extending functionality through external code. Plugins can be written in:

- **JavaScript/TypeScript** - Using Node.js runtime (recommended for most use cases)
- **Python** - Using embedded Python interpreter (future)
- **C++** - Native plugins as shared libraries (for performance-critical operations)

## Plugin Capabilities

Plugins can declare one or more capabilities:

| Capability | Description |
|------------|-------------|
| `meshModification` | Modify mesh geometry (vertices, faces) |
| `meshImport` | Import meshes from custom file formats |
| `meshExport` | Export meshes to custom file formats |
| `gcodePostProcess` | Post-process generated G-code |
| `uiExtension` | Add UI elements (menus, dialogs) |
| `textureMaterial` | Apply textures or materials to meshes |
| `analysis` | Analyze models or slices |

## Directory Structure

```
OrcaSlicer/
├── resources/
│   └── plugins/
│       ├── node-host/          # Node.js runtime (installed with OrcaSlicer)
│       └── bumpmesh/           # Example plugin
│           ├── package.json    # Plugin manifest
│           ├── index.js        # Main entry point
│           └── ...
└── [user plugins directory]/
    └── my-plugin/
        ├── package.json
        └── index.js
```

## JavaScript/TypeScript Plugins

### Plugin Manifest (package.json)

```json
{
  "name": "@orcaslicer/my-plugin",
  "version": "1.0.0",
  "description": "My awesome plugin",
  "author": "Your Name",
  "license": "MIT",
  "main": "index.js",
  "orcaPlugin": {
    "displayName": "My Plugin",
    "capabilities": ["meshModification"],
    "icon": "icon.png",
    "configSchema": {
      "type": "object",
      "properties": {
        "mySetting": {
          "type": "number",
          "default": 1.0,
          "title": "My Setting"
        }
      }
    }
  }
}
```

### Plugin Implementation

```javascript
/**
 * OrcaSlicer Plugin Example
 */
class MyPlugin {
    constructor(sdk) {
        this.sdk = sdk;
    }
    
    /**
     * Called when plugin is loaded
     */
    async onLoad(sdk) {
        sdk.log('My plugin loaded!');
        return true;
    }
    
    /**
     * Called when plugin is unloaded
     */
    async onUnload() {
        this.sdk.log('My plugin unloaded!');
    }
    
    /**
     * Process a mesh
     * @param {Object} params - { objectIdx, volumeIdx, operation, params }
     * @param {PluginSDK} sdk - Plugin SDK instance
     */
    async processMesh({ objectIdx, volumeIdx, operation, params }, sdk) {
        // Get the mesh data
        const mesh = await sdk.getMesh(objectIdx, volumeIdx);
        if (!mesh) {
            return { success: false, message: 'Could not get mesh' };
        }
        
        // Get bounds
        const bounds = await sdk.getMeshBounds(objectIdx, volumeIdx);
        
        // Modify vertices
        const vertices = Float32Array.from(mesh.vertices);
        for (let i = 0; i < vertices.length; i += 3) {
            // Example: scale by 1.1
            vertices[i] *= 1.1;
            vertices[i + 1] *= 1.1;
            vertices[i + 2] *= 1.1;
        }
        
        // Update progress
        sdk.updateProgress(50, 'Processing...');
        
        // Set the modified mesh
        const success = await sdk.setMesh(objectIdx, volumeIdx, {
            vertices,
            indices: mesh.indices
        });
        
        sdk.updateProgress(100, 'Done!');
        
        return { success };
    }
}

module.exports = MyPlugin;
```

## Plugin SDK Reference

### Mesh Operations

#### `sdk.getSelectedMeshes()`
Returns array of mesh views for currently selected objects.

```javascript
const meshes = await sdk.getSelectedMeshes();
for (const mesh of meshes) {
    console.log(`Triangles: ${mesh.triangleCount}`);
}
```

#### `sdk.getMesh(objectIdx, volumeIdx)`
Get mesh data for a specific object/volume.

```javascript
const mesh = await sdk.getMesh(0, 0);
// mesh.vertices: Float32Array [x0, y0, z0, x1, y1, z1, ...]
// mesh.normals: Float32Array (per-vertex normals)
// mesh.indices: Int32Array [i0, i1, i2, ...]
// mesh.vertexCount: number
// mesh.triangleCount: number
```

#### `sdk.setMesh(objectIdx, volumeIdx, meshData)`
Replace mesh data for an object/volume.

```javascript
const success = await sdk.setMesh(0, 0, {
    vertices: new Float32Array([...]),
    indices: new Int32Array([...]),
    normals: new Float32Array([...])  // optional
});
```

#### `sdk.addObject(meshData, name)`
Add a new object to the scene.

```javascript
const objectIdx = await sdk.addObject({
    vertices: new Float32Array([...]),
    indices: new Int32Array([...])
}, "My New Object");
```

#### `sdk.getMeshBounds(objectIdx, volumeIdx)`
Get bounding box information.

```javascript
const bounds = await sdk.getMeshBounds(0, 0);
// bounds.min: { x, y, z }
// bounds.max: { x, y, z }
// bounds.center: { x, y, z }
// bounds.size: { x, y, z }
```

### Image Operations

#### `sdk.loadImage(path)`
Load an image file.

```javascript
const image = await sdk.loadImage('/path/to/texture.png');
// image.pixels: Uint8Array (RGBA)
// image.width: number
// image.height: number
// image.channels: number
```

#### `sdk.saveImage(imageData, path)`
Save an image to file.

```javascript
await sdk.saveImage({
    pixels: new Uint8Array([...]),
    width: 256,
    height: 256,
    channels: 4
}, '/path/to/output.png');
```

### Configuration

#### `sdk.getConfig(key)`
Get plugin configuration value.

```javascript
const value = await sdk.getConfig('mySetting');
```

#### `sdk.setConfig(key, value)`
Set plugin configuration value.

```javascript
await sdk.setConfig('mySetting', 'myValue');
```

#### `sdk.getSetting(key)`
Get print/printer/filament setting.

```javascript
const layerHeight = await sdk.getSetting('layer_height');
```

### Progress & Logging

#### `sdk.updateProgress(percent, message)`
Update progress indicator.

```javascript
sdk.updateProgress(50, 'Processing mesh...');
```

#### `sdk.log(message)`
Log a message.

```javascript
sdk.log('Processing started');
```

#### `sdk.error(message)`
Report an error.

```javascript
sdk.error('Failed to process mesh');
```

### UI Operations

#### `sdk.showMessage(title, message)`
Show a message dialog.

```javascript
await sdk.showMessage('Info', 'Operation completed!');
```

#### `sdk.showConfirm(title, message)`
Show a confirmation dialog.

```javascript
const confirmed = await sdk.showConfirm('Confirm', 'Are you sure?');
if (confirmed) {
    // User clicked Yes
}
```

#### `sdk.openFileDialog(title, filter)`
Open a file selection dialog.

```javascript
const path = await sdk.openFileDialog(
    'Select Image',
    'Image files|*.png;*.jpg;*.jpeg'
);
```

#### `sdk.saveFileDialog(title, filter, defaultName)`
Open a save file dialog.

```javascript
const path = await sdk.saveFileDialog(
    'Save Output',
    'STL files|*.stl',
    'output.stl'
);
```

### Utility

#### `sdk.getDataDir()`
Get plugin's data directory.

```javascript
const dataDir = await sdk.getDataDir();
// Store plugin-specific files here
```

#### `sdk.getResourcesDir()`
Get OrcaSlicer's resources directory.

```javascript
const resourcesDir = await sdk.getResourcesDir();
```

## Native C++ Plugins

For performance-critical plugins, you can write native C++ code:

```cpp
// my_plugin.cpp

#include "libslic3r/Plugin/PluginAPI.hpp"
#include "libslic3r/Plugin/NativePluginRuntime.hpp"

using namespace Slic3r::Plugin;

class MyNativePlugin : public NativePluginBase {
public:
    MyNativePlugin(PluginManifest manifest) 
        : NativePluginBase(std::move(manifest)) {}
    
    bool on_load(PluginContext* ctx) override {
        NativePluginBase::on_load(ctx);
        // Initialize plugin
        return true;
    }
    
    void on_unload() override {
        // Cleanup
        NativePluginBase::on_unload();
    }
    
    bool process_mesh(PluginContext* ctx, 
                      int object_idx, 
                      int volume_idx,
                      const std::string& operation) override {
        auto mesh = ctx->get_mesh(object_idx, volume_idx);
        if (!mesh) return false;
        
        // Modify mesh...
        MeshData new_mesh;
        // ...
        
        return ctx->set_mesh(object_idx, volume_idx, new_mesh);
    }
};

PluginManifest create_manifest() {
    return make_manifest(
        "com.example.mynativeplugin",
        "My Native Plugin",
        "1.0.0",
        PluginCapability::MeshModification,
        "A native C++ plugin",
        "Your Name"
    );
}

ORCA_DECLARE_PLUGIN(MyNativePlugin, create_manifest)
```

Compile as a shared library:

```bash
# Linux
g++ -shared -fPIC -o libmyplugin.so my_plugin.cpp -I/path/to/orcaslicer/src

# macOS
clang++ -shared -fPIC -o libmyplugin.dylib my_plugin.cpp -I/path/to/orcaslicer/src

# Windows
cl /LD my_plugin.cpp /I path\to\orcaslicer\src
```

## Installation

### User Plugins

1. Create a folder in the user plugins directory:
   - **Windows**: `%APPDATA%\OrcaSlicer\plugins\`
   - **macOS**: `~/Library/Application Support/OrcaSlicer/plugins/`
   - **Linux**: `~/.config/OrcaSlicer/plugins/`

2. Copy your plugin folder there.

3. Restart OrcaSlicer or use the Plugin Manager to reload.

### Plugin Manager

OrcaSlicer includes a Plugin Manager (Preferences > Plugins) where you can:

- Enable/disable plugins
- Configure plugin settings
- Install plugins from ZIP files
- Update plugins
- View plugin information and errors

## Best Practices

1. **Version your plugins** - Use semantic versioning and update version on changes.

2. **Handle errors gracefully** - Always wrap operations in try/catch and report errors via `sdk.error()`.

3. **Report progress** - For long operations, use `sdk.updateProgress()` frequently.

4. **Test cancellation** - Check if operation should be cancelled and abort gracefully.

5. **Minimize memory usage** - Large meshes can consume significant memory. Process in chunks if possible.

6. **Document your plugin** - Include a README with usage instructions.

7. **Follow the API** - Don't try to access OrcaSlicer internals directly.

## Troubleshooting

### Plugin Not Loading

1. Check the console/logs for error messages
2. Verify `package.json` is valid JSON
3. Ensure `orcaPlugin` section exists with valid capabilities
4. Check that the main entry point exists

### Mesh Operations Failing

1. Verify vertex/index arrays are the correct type (Float32Array, Int32Array)
2. Check that indices are within valid range
3. Ensure vertex count is divisible by 3

### Performance Issues

1. Use TypedArrays instead of regular arrays
2. Process large meshes in chunks
3. Consider writing performance-critical code as native plugin

## Support

- Report issues: https://github.com/OrcaSlicerr/OrcaSlicer/issues
- Plugin development questions: https://github.com/OrcaSlicer/OrcaSlicer/discussions
