# See cmake/Freelancer.cmake. The source list is the link order, and with it the order of
# static initialisation.

# TexConvert.lib: port-only replacement for D3DX 8.1's D3DXLoadSurfaceFromMemory, used by RPGPU
# (docs/re/texconvert.md). No Direct3D or platform headers.

fl_add_module(TexConvert LIB PORT PORTABLE
  SOURCES
    TexConvert.cpp
)
