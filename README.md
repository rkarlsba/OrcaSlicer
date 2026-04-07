<!-- {{{
vim:ts=4:sw=4:sts=4:et:si:ai:fdm=marker:tw=110
}}} -->

# [Roy Sigurd Karlsbakk](mailto:roy@karlsbakk.net)'s <i>highly onofficial</i> fork of OrcaSlicer

If you want the official stuff, see
 - [OrcaSlicer's main website](https://github.com/OrcaSlicer/OrcaSlicer)
 - [OrcaSlicer on Github](https://github.com/OrcaSlicer/OrcaSlicer)

## Disclaimer 

This is work in progress, so don't blame me for whatever goes wrong. Really, it's Claude's fault, all of it!

## Branches

As of now, this only has a single in addition to **main**:

### plugin_api

The plugin API is an extension to OrcaSlicer to allow for third party plugins. So far, only a single
node-based plugin is implemented, so I don't know how well this actually works. This is mainly Claude's work
(blame him for everything!), but it seems to work, albeit with rather a few bugs.

#### Plugins
Under [plugins](resources/plugins/), there are currently two:

    - [node-host](resources/plugins/node-host/)
        This is just the node host, meaning the one handling plugins written in JavaScript or TypeScript.

    - [bumpmesh](resources/plugins/bumpmesh/)
        This is [CNCKitchen's BumpMesh](https://bumpmesh.com/), its source downloaded, slightly modified and and used as an OrcaSlicer plugin.

## Downloads

For now, there's only the source, but I may upload more stuff after a while. See the original orcaslicer
github for more info on how to install stuff.

[roy](mailto:roy@karlsbakk.net)
