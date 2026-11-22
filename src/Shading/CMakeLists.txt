# See cmake/Freelancer.cmake. The source list is the link order, and with it the order of
# static initialisation.

fl_add_module(Shading DLL RETAIL
  SOURCES
    Shading.cpp
    Material.cpp
    MaterialLibrary.cpp
    MaterialRegex.cpp
    MaterialBatcher.cpp
    TextureLibrary.cpp
    MaterialAnimation.cpp
    Shading.rc
)

fl_link(Shading
  LIBS dacom.lib
  REFERENCES dacom)
