# See cmake/Freelancer.cmake. The source list is the link order, and with it the order of
# static initialisation.

fl_add_module(SoundManager DLL RETAIL
  SOURCES
    SoundManager.cpp
    SoundManager.rc
)

fl_link(SoundManager
  LIBS dacom.lib ole32.lib dsound.lib msacm32.lib winmm.lib
  REFERENCES dacom
  # Win32: the DirectX 8.1 SDK's dsound.lib, ahead of the Windows SDK's.
  LIBDIRS $<$<BOOL:${FL_WIN32}>:${FL_DX8_DIR}/lib>)
