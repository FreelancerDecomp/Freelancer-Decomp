# See cmake/Freelancer.cmake. The source list is the link order, and with it the order of
# static initialisation.

# Win32 only.
if(NOT FL_WIN32)
  return()
endif()

fl_add_module(MiscTextInfo2 RESOURCE_DLL RETAIL
  SOURCES
    MiscTextInfo2.rc
)
