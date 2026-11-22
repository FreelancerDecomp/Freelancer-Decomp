# See cmake/Freelancer.cmake. The source list is the link order, and with it the order of
# static initialisation.

fl_add_module(system DLL RETAIL
  SOURCES
    system.cpp
    system.rc
)

fl_link(system
  LIBS dacom.lib
  REFERENCES dacom
  LIBDIRS ${PROJECT_SOURCE_DIR}/bin)
