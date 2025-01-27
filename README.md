# Main View Source plugin for OBS Studio

## Introduction

This is a simple plugin for OBS Studio that provides a source to duplicate the main view.

This plugin is compatible with Source Record filter and Dedicated NDI filter.

## Properties

### Cache the main view

Cache texture of the main view at the first rendering and reuse the cached texture for the later rendering.
If enabled, the previous frame will be displayed if the source is nested. Also scene items overflowing the texture will be cropped.
Enable this setting if one of these conditions applies.
- You want to hide overflowing scene items outside the bounding box of the main view.
- You want to put the source on the main view to enjoy fractals.

### Render before output/display rendering

*Deprecated*

Render the texture before OBS start to render output and each display.
Recommended to enable if
- You want to have the source in a nested scene.

If enabled, since the order of rendering sources changes, something might be affected.

If no issues are reported without disabling this property, this property might be dropped in the future and fixed to enabled. If you need this property, please file an issue not to deprecate.

## Build and install
### Linux
Use cmake to build on Linux. After checkout, run these commands.
```
sed -i 's;${CMAKE_INSTALL_FULL_LIBDIR};/usr/lib;' CMakeLists.txt
mkdir build && cd build
cmake -DCMAKE_INSTALL_PREFIX=/usr ..
make
sudo make install
```

### macOS
Use cmake to build on Linux. After checkout, run these commands.
```
mkdir build && cd build
cmake ..
make
```

## See also
- [obs-source-record](https://github.com/exeldro/obs-source-record)
- [obs-ndi](https://github.com/Palakis/obs-ndi)
