# See cmake/Freelancer.cmake. The source list is the link order, and with it the order of
# static initialisation.

# Win32 only.
if(NOT FL_WIN32)
  return()
endif()

fl_add_module(OfferBribeResources RESOURCE_DLL RETAIL
  SOURCES
    OfferBribeResources.rc
)
