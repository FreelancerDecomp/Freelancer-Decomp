# See cmake/Freelancer.cmake. The source list is the link order, and with it the order of
# static initialisation.

fl_add_module(Deformable2 DLL RETAIL
  SOURCES
    Deformable2.cpp
    EyeMaterial.cpp
    Deformable2.rc
)

fl_link(Deformable2
  LIBS dacom.lib
  REFERENCES dacom)
