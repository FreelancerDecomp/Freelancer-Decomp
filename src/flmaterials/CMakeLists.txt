# See cmake/Freelancer.cmake. The source list is the link order, and with it the order of
# static initialisation.

fl_add_module(flmaterials DLL RETAIL
  SOURCES
    flmaterials.cpp
    MaterialImpl.cpp
    FLAppearance.cpp
    flmaterials.rc
)

fl_link(flmaterials
  LIBS dacom.lib
  REFERENCES dacom)
