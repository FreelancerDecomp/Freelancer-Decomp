# See cmake/Freelancer.cmake. The source list is the link order, and with it the order of
# static initialisation.

# Win32 only.
if(NOT FL_WIN32)
  return()
endif()

# Retail FLServer.exe is an MFC42 doc/view application linked against the ANSI MFC42.DLL (imports
# by ordinal): dynamic MFC, multibyte character set.

fl_add_module(FLServer EXE RETAIL EXPORTS MFC DEF FLServer.def
  SOURCES
    FLServer.cpp
    MainFrm.cpp
    FLServerDoc.cpp
    FLServerView.cpp
    CntrItem.cpp
    ShutdownDlg.cpp
    ServerControl.cpp
    RichEditText.cpp
    Motd.cpp
    AreYouSureDlg.cpp
    ErrorDlg.cpp
    FLListBox.cpp
    AccountsDlg.cpp
    OperationCompleteDlg.cpp
    ServerNewsDlg.cpp
    SetPasswordDlg.cpp
    EditAccountDlg.cpp
    ChoosePlayerDlg.cpp
    SelectStarSystemDlg.cpp
    ServerConfigDlg.cpp
    CreateAccountDlg.cpp
    ServerInfoDlg.cpp
    ChatRdl.cpp
    ChatAccountDlg.cpp
    ChatSystemDlg.cpp
    ResourceStrings.cpp
    StarSystemList.cpp
    UniverseChatDlg.cpp
    FLSummaryForm.cpp
    FLServerCmdLine.cpp
    CrashReport.cpp
    # The host shared by FLServer and FLDedicated (docs/dedicated_server.md).
    ../ServerHost/server_host.cpp
    FLServer.rc
)

# Every translation unit includes StdAfx.h first (MFC must see windows.h first); no precompiled
# header, so a file another lane adds cannot break the PCH chain.
# Retail FLServer.exe's 71-name export table: forwarders to Server/DALib/Common plus @57
# ServerLogf (.text:0040e330).
fl_includes(FLServer .)
fl_defines(FLServer _WINDOWS)
fl_rc(FLServer INCLUDES res DEFINES _AFXDLL)
fl_link(FLServer
  LIBS dacom.lib DALib.lib Common.lib Server.lib RemoteClient.lib imm32.lib winmm.lib
  REFERENCES dacom DALib Common Server RemoteClient
  LIBDIRS ${PROJECT_SOURCE_DIR}/bin)
fl_order_after(FLServer serverresources)
