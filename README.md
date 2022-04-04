# OBS Main View Source

## Introduction

This is a simple plugin for OBS Studio that provides a source to duplicate the main view.

This plugin is compatible with Source Record filter and Dedicated NDI filter.

## Properties

No properties are provided.

### Cache the main view

Cache texture of the main view at the first rendering and reuse the cached texture for the later rendering.
If enabled, the previous frame will be displayed if the source is nested. Also scene items overflowing the texture will be cropped.
Enable this setting if one of these conditions applies.
- You want to hide overflowing scene items outside the bounding box of the main view.
- You want to put the source on the main view to enjoy fractals.

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
