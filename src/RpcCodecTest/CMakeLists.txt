# See cmake/Freelancer.cmake. The source list is the link order, and with it the order of
# static initialisation.

# RpcCodecTest.exe: PORT(tools), the network packet codec test (no retail counterpart). It
# compiles RemoteClient.dll's codec sources with RemoteClient's defines and runs every packet
# type's Encode/Decode on fixed inputs; see docs/testing.md.

fl_add_module(RpcCodecTest EXE TOOL CONSOLE
  SOURCES
    RpcCodecTest.cpp
    ../RemoteClient/rpc_client_packets.cpp
    ../RemoteCommon/remote_packet_codecs.cpp
    ../RemoteCommon/remote_server_channel_codecs.cpp
)

# RemoteClient's defines, so the codecs compile as they do in RemoteClient.dll.
fl_defines(RpcCodecTest FL_SERVER REMOTECLIENT_EXPORTS _CONSOLE)
fl_link(RpcCodecTest
  LIBS dacom.lib Common.lib Server.lib rpclocal.lib DALib.lib
  REFERENCES Server dacom Common DALib rpclocal)
