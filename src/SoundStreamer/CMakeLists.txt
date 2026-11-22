# See cmake/Freelancer.cmake. The source list is the link order, and with it the order of
# static initialisation.

fl_add_module(SoundStreamer DLL RETAIL
  SOURCES
    SoundStreamer.cpp
    SoundStreamer.rc
)

fl_link(SoundStreamer
  LIBS dacom.lib ole32.lib dsound.lib winmm.lib msacm32.lib
  REFERENCES dacom
  # Win32: the DirectX 8.1 SDK's dsound.lib, ahead of the Windows SDK's.
  LIBDIRS $<$<BOOL:${FL_WIN32}>:${FL_DX8_DIR}/lib>)
