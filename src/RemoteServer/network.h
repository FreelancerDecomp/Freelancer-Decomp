// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once
// network.h - DirectPlay 8 transport layer types shared by RemoteClient.dll and remoteserver.dll
// Both DLLs are built from the same codebase (E:\FL\Scratch\Source\Network\)
// with different #defines (FL_SERVER for RemoteClient.dll).

#include <vector>
#include <windows.h>

// Forward declarations for external DirectPlay and interface types
#include "dalib_net.h"

class IClient;
class IChatClient;
struct IServer;
class IChatServer;
// ============================================================================
// f) Protocol packet opcodes (all 161 unique opcodes / 162 packet classes)
//
// Channels:
//   Channel 1: Common (bidirectional) — 0x10000 | id  (21 opcodes)
//   Channel 2: Server -> Client       — 0x20000 | id  (81 opcodes)
//   Channel 3: Client -> Server       — 0x30000 | id  (60 opcodes)
// ============================================================================
enum EFlPacket : unsigned int
{
    // -----------------------------------------------------------------------
    // Common (bidirectional) 0x10000 | id (21 packets)
    // -----------------------------------------------------------------------
    CYCLOPSMESSAGE_COMMON_UPDATEOBJECT      = 0x10001,
    FLPACKET_COMMON_UPDATEOBJECT            = 0x10001,
    FLPACKET_COMMON_FIREWEAPON              = 0x10002,
    FLPACKET_COMMON_SETTARGET               = 0x10004,
    FLPACKET_COMMON_CHATMSG                 = 0x10005,
    FLPACKET_COMMON_ACTIVATEEQUIP           = 0x10008,
    FLPACKET_COMMON_ACTIVATECRUISE          = 0x1000e,
    FLPACKET_COMMON_GOTRADELANE             = 0x1000f,
    FLPACKET_COMMON_STOPTRADELANE           = 0x10010,
    FLPACKET_COMMON_SET_WEAPON_GROUP        = 0x10011,
    FLPACKET_COMMON_PLAYER_TRADE            = 0x10012,
    FLPACKET_COMMON_SET_VISITED_STATE       = 0x10013,
    FLPACKET_COMMON_JETTISONCARGO           = 0x10014,
    FLPACKET_COMMON_ACTIVATETHRUSTERS       = 0x10015,
    FLPACKET_COMMON_REQUEST_BEST_PATH       = 0x10016,
    FLPACKET_COMMON_REQUEST_GROUP_POSITIONS = 0x10017,
    FLPACKET_COMMON_REQUEST_PLAYER_STATS    = 0x10018,
    FLPACKET_COMMON_SET_MISSION_LOG         = 0x10019,
    FLPACKET_COMMON_REQUEST_RANK_LEVEL      = 0x1001a,
    FLPACKET_COMMON_POP_UP_DIALOG           = 0x1001b,
    FLPACKET_COMMON_SET_INTERFACE_STATE     = 0x1001c,
    FLPACKET_COMMON_TRACTOROBJECTS          = 0x1001d,

    // -----------------------------------------------------------------------
    // Server -> Client 0x20000 | id (81 packets)
    // -----------------------------------------------------------------------
    FLPACKET_SERVER_CONNECTRESPONSE              = 0x20001,
    FLPACKET_SERVER_LOGINRESPONSE                = 0x20002,
    FLPACKET_SERVER_CHARACTERINFO                = 0x20003,
    FLPACKET_SERVER_CREATESHIP                   = 0x20004,
    FLPACKET_SERVER_DAMAGEOBJECT                 = 0x20005,
    FLPACKET_SERVER_DESTROYOBJECT                = 0x20006,
    FLPACKET_SERVER_LAUNCH                       = 0x20007,
    FLPACKET_SERVER_CHARSELECTVERIFIED           = 0x20008,
    FLPACKET_SERVER_ACTIVATEOBJECT               = 0x2000a,
    FLPACKET_SERVER_LAND                         = 0x2000b,
    FLPACKET_SERVER_SETSTARTROOM                 = 0x2000d,
    FLPACKET_SERVER_GFCOMPLETENEWSBROADCASTLIST  = 0x2000e,
    FLPACKET_SERVER_GFCOMPLETECHARLIST           = 0x2000f,
    FLPACKET_SERVER_GFCOMPLETEMISSIONCOMPUTERLIST= 0x20010,
    FLPACKET_SERVER_GFCOMPLETESCRIPTBEHAVIORLIST = 0x20011,
    FLPACKET_SERVER_GFCOMPLETEAMBIENTSCRIPTLIST  = 0x20013,
    FLPACKET_SERVER_GFDESTROYNEWSBROADCAST       = 0x20014,
    FLPACKET_SERVER_GFDESTROYCHARACTER           = 0x20015,
    FLPACKET_SERVER_GFDESTROYMISSIONCOMPUTER     = 0x20016,
    FLPACKET_SERVER_GFDESTROYSCRIPTBEHAVIOR      = 0x20017,
    FLPACKET_SERVER_GFDESTROYAMBIENTSCRIPT       = 0x20019,
    FLPACKET_SERVER_GFSCRIPTBEHAVIOR             = 0x2001a,
    FLPACKET_SERVER_GFUPDATEMISSIONCOMPUTER      = 0x2001d,
    FLPACKET_SERVER_GFUPDATENEWSBROADCAST        = 0x2001e,
    FLPACKET_SERVER_GFUPDATEAMBIENTSCRIPT        = 0x2001f,
    FLPACKET_SERVER_GFMISSIONVENDORACCEPTANCE    = 0x20020,
    FLPACKET_SERVER_SYSTEM_SWITCH_OUT            = 0x20021,
    FLPACKET_SERVER_SYSTEM_SWITCH_IN             = 0x20022,
    FLPACKET_SERVER_SETSHIPARCH                  = 0x20023,
    FLPACKET_SERVER_SETEQUIPMENT                 = 0x20024,
    FLPACKET_SERVER_SETCARGO                     = 0x20025,
    FLPACKET_SERVER_GFUPDATECHAR                 = 0x20026,
    FLPACKET_SERVER_REQUESTCREATESHIPRESP        = 0x20027,
    FLPACKET_SERVER_CREATELOOT                   = 0x20028,
    FLPACKET_SERVER_SETREPUTATION                = 0x20029,
    FLPACKET_SERVER_ADJUSTATTITUDE               = 0x2002a,
    FLPACKET_SERVER_SETGROUPFEELINGS             = 0x2002b,
    FLPACKET_SERVER_CREATEMINE                   = 0x2002c,
    FLPACKET_SERVER_CREATECOUNTER                = 0x2002d,
    FLPACKET_SERVER_SETADDITEM                   = 0x2002e,
    FLPACKET_SERVER_SETREMOVEITEM                = 0x2002f,
    FLPACKET_SERVER_SETCASH                      = 0x20030,
    FLPACKET_SERVER_EXPLODEASTEROIDMINE          = 0x20031,
    FLPACKET_SERVER_REQUESTSPACESCRIPT           = 0x20032,
    FLPACKET_SERVER_SETMISSIONOBJECTIVESTATE     = 0x20033,
    FLPACKET_SERVER_REPLACEMISSIONOBJECTIVE      = 0x20034,
    FLPACKET_SERVER_SETMISSIONOBJECTIVES         = 0x20035,
    FLPACKET_SERVER_CREATEGUIDED                 = 0x20037,
    FLPACKET_SERVER_ITEMTRACTORED                = 0x20038,
    FLPACKET_SERVER_SCANNOTIFY                   = 0x20039,
    FLPACKET_SERVER_REPAIROBJECT                 = 0x2003c,
    FLPACKET_SERVER_REMOTEOBJECTCARGOUPDATE      = 0x2003d,
    FLPACKET_SERVER_SETNUMKILLS                  = 0x2003e,
    FLPACKET_SERVER_SETMISSIONSUCCESSES          = 0x2003f,
    FLPACKET_SERVER_SETMISSIONFAILURES           = 0x20040,
    FLPACKET_SERVER_BURNFUSE                     = 0x20041,
    FLPACKET_SERVER_CREATESOLAR                  = 0x20042,
    FLPACKET_SERVER_SET_STORY_CUE                = 0x20043,
    FLPACKET_SERVER_REQUEST_RETURNED             = 0x20044,
    FLPACKET_SERVER_SET_MISSION_MESSAGE          = 0x20045,
    FLPACKET_SERVER_MARKOBJ                      = 0x20046,
    FLPACKET_SERVER_CFGINTERFACENOTIFICATION     = 0x20047,
    FLPACKET_SERVER_SETCOLLISIONGROUPS           = 0x20048,
    FLPACKET_SERVER_SETHULLSTATUS                = 0x20049,
    FLPACKET_SERVER_SETGUIDEDTARGET              = 0x2004a,
    FLPACKET_SERVER_SET_CAMERA                   = 0x2004b,
    FLPACKET_SERVER_REVERT_CAMERA                = 0x2004c,
    FLPACKET_SERVER_LOADHINT                     = 0x2004d,
    FLPACKET_SERVER_SETDIRECTIVE                 = 0x2004e,
    FLPACKET_SERVER_SENDCOMM                     = 0x2004f,
    FLPACKET_SERVER_USE_ITEM                     = 0x20051,
    FLPACKET_SERVER_PLAYERLIST                   = 0x20052,
    FLPACKET_SERVER_FORMATION_UPDATE             = 0x20053,
    FLPACKET_SERVER_MISCOBJUPDATE                = 0x20054,
    FLPACKET_SERVER_OBJECTCARGOUPDATE            = 0x20055,
    FLPACKET_SERVER_SENDNNMESSAGE                = 0x20056,
    FLPACKET_SERVER_SET_MUSIC                    = 0x20057,
    FLPACKET_SERVER_CANCEL_MUSIC                 = 0x20058,
    FLPACKET_SERVER_PLAY_SOUND_EFFECT            = 0x20059,
    FLPACKET_SERVER_GFMISSIONVENDORWHYEMPTY      = 0x2005a,
    FLPACKET_SERVER_MISSIONSAVEA                 = 0x2005b,

    // -----------------------------------------------------------------------
    // Client -> Server 0x30000 | id (60 packets)
    // -----------------------------------------------------------------------
    FLPACKET_CLIENT_LOGIN                        = 0x30001,
    FLPACKET_CLIENT_MUNCOLLISION                 = 0x30003,
    FLPACKET_CLIENT_REQUESTLAUNCH                = 0x30004,
    FLPACKET_CLIENT_REQUESTCHARINFO              = 0x30005,
    FLPACKET_CLIENT_SELECTCHARACTER              = 0x30006,
    FLPACKET_CLIENT_ENTERBASE                    = 0x30007,
    FLPACKET_CLIENT_REQUESTBASEINFO              = 0x30008,
    FLPACKET_CLIENT_REQUESTLOCATIONINFO          = 0x30009,
    FLPACKET_CLIENT_GFREQUESTSHIPINFO            = 0x3000a,
    FLPACKET_CLIENT_SYSTEM_SWITCH_OUT_COMPLETE   = 0x3000b,
    FLPACKET_CLIENT_OBJCOLLISION                 = 0x3000c,
    FLPACKET_CLIENT_EXITBASE                     = 0x3000d,
    FLPACKET_CLIENT_ENTERLOCATION                = 0x3000e,
    FLPACKET_CLIENT_EXITLOCATION                 = 0x3000f,
    FLPACKET_CLIENT_REQUESTCREATESHIP            = 0x30010,
    FLPACKET_CLIENT_GFGOODSELL                   = 0x30011,
    FLPACKET_CLIENT_GFGOODBUY                    = 0x30012,
    FLPACKET_CLIENT_GFSELECTOBJECT               = 0x30013,
    FLPACKET_CLIENT_MISSIONRESPONSE              = 0x30014,
    FLPACKET_CLIENT_REQSHIPARCH                  = 0x30015,
    FLPACKET_CLIENT_REQEQUIPMENT                 = 0x30016,
    FLPACKET_CLIENT_REQCARGO                     = 0x30017,
    FLPACKET_CLIENT_REQADDITEM                   = 0x30018,
    FLPACKET_CLIENT_REQREMOVEITEM                = 0x30019,
    FLPACKET_CLIENT_REQMODIFYITEM                = 0x3001a,
    FLPACKET_CLIENT_REQSETCASH                   = 0x3001b,
    FLPACKET_CLIENT_REQCHANGECASH                = 0x3001c,
    FLPACKET_CLIENT_SAVEGAME                     = 0x3001e,
    FLPACKET_CLIENT_MINEASTEROID                 = 0x30020,
    FLPACKET_CLIENT_DBGCREATESHIP                = 0x30022,
    FLPACKET_CLIENT_DBGLOADSYSTEM                = 0x30023,
    FLPACKET_CLIENT_DOCK                         = 0x30024,
    FLPACKET_CLIENT_DBGDESTROYOBJECT             = 0x30025,
    FLPACKET_CLIENT_TRADERESPONSE                = 0x30027,
    FLPACKET_CLIENT_CARGOSCAN                    = 0x3002b,
    FLPACKET_CLIENT_DBGCONSOLE                   = 0x3002d,
    FLPACKET_CLIENT_DBGFREESYSTEM                = 0x3002e,
    FLPACKET_CLIENT_SETMANEUVER                  = 0x3002f,
    FLPACKET_CLIENT_DBGRELOCATE_SHIP             = 0x30030,
    FLPACKET_CLIENT_REQUEST_EVENT                = 0x30031,
    FLPACKET_CLIENT_REQUEST_CANCEL               = 0x30032,
    FLPACKET_CLIENT_INTERFACEITEMUSED            = 0x30035,
    FLPACKET_CLIENT_REQCOLLISIONGROUPS           = 0x30036,
    FLPACKET_CLIENT_COMMCOMPLETE                 = 0x30037,
    FLPACKET_CLIENT_REQUESTNEWCHARINFO           = 0x30038,
    FLPACKET_CLIENT_CREATENEWCHAR                = 0x30039,
    FLPACKET_CLIENT_DESTROYCHAR                  = 0x3003a,
    FLPACKET_CLIENT_REQHULLSTATUS                = 0x3003b,
    FLPACKET_CLIENT_GFGOODVAPORIZED              = 0x3003c,
    FLPACKET_CLIENT_BADLANDSOBJCOLLISION         = 0x3003d,
    FLPACKET_CLIENT_LAUNCHCOMPLETE               = 0x3003e,
    FLPACKET_CLIENT_HAIL                         = 0x3003f,
    FLPACKET_CLIENT_REQUEST_USE_ITEM             = 0x30040,
    FLPACKET_CLIENT_ABORT_MISSION                = 0x30041,
    FLPACKET_CLIENT_SKIP_AUTOSAVE                = 0x30042,
    FLPACKET_CLIENT_JUMPINCOMPLETE               = 0x30043,
    FLPACKET_CLIENT_REQINVINCIBILITY             = 0x30044,
    FLPACKET_CLIENT_MISSIONSAVEB                 = 0x30045,
    FLPACKET_CLIENT_REQDIFFICULTYSCALE           = 0x30046,
    FLPACKET_CLIENT_RTCDONE                      = 0x30047,
};

// ============================================================================
// g) RemoteClient.dll server-side exports; declarations live in one public header.
// ============================================================================
#include "remoteclient.h"

// ============================================================================
// h) remoteserver.dll (client-side DirectPlay 8 transport) C exports
//
// Undecorated C exports returning the client's RPC connection to the remote server.
// ============================================================================
extern "C" {
    IServer* GetServerInterface();
    IChatServer* GetChatServerInterface();
}
