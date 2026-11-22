# See cmake/Freelancer.cmake. The source list is the link order, and with it the order of
# static initialisation.

fl_add_module(Server DLL RETAIL
  SOURCES
    IServerImpl.cpp
    server_pub.cpp
    server_classes.cpp
    server_playerdb.cpp
    server_playergroup.cpp
    server_account.cpp
    server_asteroid.cpp
    server_starsystem.cpp
    server_objectlist.cpp
    server_playerview.cpp
    server_space.cpp
    server_chat.cpp
    server_gameobject.cpp
    server_observer.cpp
    server_createinfo.cpp
    server_savegame.cpp
    server_savefile.cpp
    server_fuse.cpp
    server_loot.cpp
    server_mine.cpp
    server_guided.cpp
    server_countermeasure.cpp
    server_basedb.cpp
)

fl_defines(Server _WINDOWS _USRDLL SERVER_EXPORTS)
fl_link(Server
  LIBS kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib Common.lib
  REFERENCES dacom DALib Common)
