# Индекс типов с виртуальными методами

Сгенерировано `tools/abi.py build` — руками не править.
`public` = объявлен в `src/include/` (виден всем проектам через `Directory.Build.props`); `private` = виден только своему модулю.
«Своих» — объявленные в самом типе; «всего» — с унаследованными слотами (т.е. `всего*4` = размер vtable в байтах).

| Тип | Своих | Всего | Где объявлен | Видимость | База |
|-----|------:|------:|--------------|-----------|------|
| `Act` | 3 | 3 | `src/Content/MissionAct.h:32` | private (Content) | — |
| `Action` | 2 | 2 | `src/include/common_async.h:23` | **public** | — |
| `AnimationArch` | 1 | 3 | `src/freelancer/Client/engine_light.h:299` | private (freelancer) | `public OwnerList<AnimFrame>` |
| `ArchEnumerator` | 1 | 1 | `src/include/common_archetype.h:309` | **public** | — |
| `Armor` | 4 | 17 | `src/include/common_archetype.h:981` | **public** | `public Equipment` |
| `Asteroid` | 2 | 8 | `src/include/common_archetype.h:1550` | **public** | `public Root` |
| `AttachedEquipment` | 4 | 17 | `src/include/common_archetype.h:866` | **public** | `public Equipment` |
| `AttachedFXEquip` | 1 | 14 | `src/include/common_archetype.h:1251` | **public** | `public Equipment` |
| `AttribStackNode` | 9 | 41 | `src/include/common_text.h:800` | **public** | `public RDLNode` |
| `BaseFollowerBehavior` | 2 | 29 | `src/include/common_behavior.h:876` | **public** | `public Behavior` |
| `BaseHint` | 4 | 4 | `src/include/common_goods.h:767` | **public** | — |
| `BaseIdea` | 6 | 6 | `src/include/common_idea.h:181` | **public** | — |
| `BaseOp` | 1 | 1 | `src/include/common_pub.h:695` | **public** | — |
| `BaseRichTextWin` | 2 | 47 | `src/freelancer/Client/rich_text_win.h:83` | private (freelancer) | `public WinControl` |
| `Behavior` | 27 | 27 | `src/include/common_behavior.h:538` | **public** | — |
| `BinaryRDLReader` | 3 | 3 | `src/include/common_text.h:1170` | **public** | — |
| `BinaryRDLWriter` | 2 | 2 | `src/include/common_text.h:1190` | **public** | — |
| `BroadcastRecord` | 3 | 3 | `src/include/server_basedb.h:123` | **public** | — |
| `Bullet` | 1 | 24 | `src/include/server_gameobject.h:1576` | **public** | `public Munition` |
| `ButtonMover` | 2 | 2 | `src/freelancer/Client/button_mover.h:48` | private (freelancer) | — |
| `ButtonScaler` | 2 | 2 | `src/freelancer/Client/button_mover.h:82` | private (freelancer) | — |
| `CAccount` | 1 | 1 | `src/include/server_playerdb.h:69` | **public** | — |
| `CAccountsDlg` | 2 | 2 | `src/FLServer/AccountsDlg.h:11` | private (FLServer) | `public CDialog` |
| `CAreYouSureDlg` | 1 | 1 | `src/FLServer/AreYouSureDlg.h:6` | private (FLServer) | `public CDialog` |
| `CargoEnumerator` | 1 | 1 | `src/include/common_server.h:139` | **public** | — |
| `CargoPod` | 2 | 19 | `src/include/common_archetype.h:1237` | **public** | `public AttachedEquipment` |
| `CAsteroid` | 4 | 65 | `src/include/common_cobject.h:971` | **public** | `public CSimple` |
| `CAttachedEquip` | 15 | 51 | `src/include/common_cequip.h:441` | **public** | `public CExternalEquip` |
| `CAttachmentObj` | 6 | 6 | `src/include/common_cequip.h:377` | **public** | — |
| `CBase` | 1 | 1 | `src/include/common_goods.h:820` | **public** | — |
| `CBeam` | 5 | 72 | `src/include/common_cobject.h:762` | **public** | `public CMunition` |
| `CChatAccountDlg` | 5 | 5 | `src/FLServer/ChatAccountDlg.h:12` | private (FLServer) | `public CDialog` |
| `CChatSystemDlg` | 5 | 5 | `src/FLServer/ChatSystemDlg.h:10` | private (FLServer) | `public CDialog` |
| `CChoosePlayerDlg` | 4 | 4 | `src/FLServer/ChoosePlayerDlg.h:12` | private (FLServer) | `public CDialog` |
| `CClientImpl` | 127 | 253 | `src/RemoteClient/ClientImpl.h:5` | private (RemoteClient) | `public IClient, public IChatClient` |
| `CCounterMeasure` | 4 | 70 | `src/include/common_cobject.h:837` | **public** | `public CProjectile` |
| `CCreateAccountDlg` | 2 | 2 | `src/FLServer/CreateAccountDlg.h:13` | private (FLServer) | `public CDialog` |
| `CDeadReckonedVector` | 1 | 1 | `src/include/common_cobject.h:1142` | **public** | — |
| `CDPClient` | 1 | 4 | `src/include/dalib_net.h:154` | **public** | `public IDPMsgHandler` |
| `CDPClientProxy` | 1 | 4 | `src/include/dalib_net.h:230` | **public** | `public IDPMsgHandler` |
| `CDPMessage` | 1 | 1 | `src/include/dalib_net.h:97` | **public** | — |
| `CDPMsgList` | 1 | 1 | `src/include/dalib_net.h:129` | **public** | — |
| `CDPServer` | 2 | 2 | `src/include/dalib_net.h:265` | **public** | — |
| `CDynamicAsteroid` | 3 | 51 | `src/include/common_cobject.h:1056` | **public** | `public CObject` |
| `CEArmor` | 1 | 25 | `src/include/common_cequip.h:1591` | **public** | `public CInternalEquip` |
| `CEAttachedFX` | 2 | 38 | `src/include/common_cequip.h:1714` | **public** | `public CExternalEquip` |
| `CECargo` | 8 | 32 | `src/include/common_cequip.h:517` | **public** | `public CInternalEquip` |
| `CECargoPod` | 3 | 54 | `src/include/common_cequip.h:1607` | **public** | `public CAttachedEquip` |
| `CECloakingDevice` | 3 | 54 | `src/include/common_cequip.h:2304` | **public** | `public CAttachedEquip` |
| `CECounterMeasureDropper` | 2 | 71 | `src/include/common_cequip.h:1665` | **public** | `public CELauncher` |
| `CEditAccountDlg` | 3 | 3 | `src/FLServer/EditAccountDlg.h:12` | private (FLServer) | `public CDialog` |
| `CEEngine` | 9 | 33 | `src/include/common_cequip.h:733` | **public** | `public CInternalEquip, public CPhysControllerEquip` |
| `CEGun` | 11 | 80 | `src/include/common_cequip.h:942` | **public** | `public CELauncher` |
| `CEInternalFX` | 2 | 26 | `src/include/common_cequip.h:1740` | **public** | `public CInternalEquip` |
| `CELauncher` | 18 | 69 | `src/include/common_cequip.h:865` | **public** | `public CAttachedEquip` |
| `CELightEquip` | 2 | 38 | `src/include/common_cequip.h:1692` | **public** | `public CExternalEquip` |
| `CEMineDropper` | 2 | 71 | `src/include/common_cequip.h:1641` | **public** | `public CELauncher` |
| `CEPower` | 2 | 26 | `src/include/common_cequip.h:663` | **public** | `public CInternalEquip` |
| `CEqObj` | 37 | 98 | `src/include/common_cobject.h:1514` | **public** | `public CSimple` |
| `CEqObjCreateInfo` | 1 | 1 | `src/include/server_createinfo.h:110` | **public** | — |
| `CEquip` | 22 | 22 | `src/include/common_cequip.h:193` | **public** | — |
| `CEquipmentObj` | 1 | 49 | `src/include/common_cobject.h:444` | **public** | `public CObject` |
| `CERepairDroid` | 3 | 27 | `src/include/common_cequip.h:2275` | **public** | `public CInternalEquip` |
| `CErrorDlg` | 1 | 1 | `src/FLServer/ErrorDlg.h:6` | private (FLServer) | `public CDialog` |
| `CEScanner` | 3 | 27 | `src/include/common_cequip.h:1540` | **public** | `public CInternalEquip, public ScanList::Filter` |
| `CEShield` | 13 | 64 | `src/include/common_cequip.h:1173` | **public** | `public CAttachedEquip` |
| `CEShieldGenerator` | 4 | 55 | `src/include/common_cequip.h:1083` | **public** | `public CAttachedEquip` |
| `CEShieldIntruderChecker` | 2 | 4 | `src/include/common_cequip.h:1156` | **public** | `public PhySys::CObjectCB` |
| `CEThruster` | 6 | 57 | `src/include/common_cequip.h:1306` | **public** | `public CAttachedEquip, public CPhysControllerEquip` |
| `CETractor` | 2 | 26 | `src/include/common_cequip.h:1381` | **public** | `public CInternalEquip` |
| `CETradeLaneEquip` | 1 | 25 | `src/include/common_cequip.h:2247` | **public** | `public CInternalEquip` |
| `CExternalEquip` | 14 | 36 | `src/include/common_cequip.h:301` | **public** | `public CEquip` |
| `CFLListBox` | 13 | 23 | `src/FLServer/FLListBox.h:13` | private (FLServer) | `public IFLListBox, public CListBox` |
| `CFLServerApp` | 4 | 4 | `src/FLServer/FLServer.h:16` | private (FLServer) | `public CWinApp` |
| `CFLServerCntrItem` | 1 | 1 | `src/FLServer/CntrItem.h:8` | private (FLServer) | `public CRichEditCntrItem` |
| `CFLServerCommandLineInfo` | 1 | 1 | `src/FLServer/FLServerCmdLine.h:7` | private (FLServer) | `public CCommandLineInfo` |
| `CFLServerDoc` | 7 | 7 | `src/FLServer/FLServerDoc.h:9` | private (FLServer) | `public CRichEditDoc` |
| `CFLServerView` | 4 | 4 | `src/FLServer/FLServerView.h:11` | private (FLServer) | `public CRichEditView` |
| `CFLSummaryForm` | 4 | 4 | `src/FLServer/FLSummaryForm.h:15` | private (FLServer) | `public CFormView` |
| `CGuided` | 4 | 71 | `src/include/common_cobject.h:699` | **public** | `public CMunition` |
| `CGunWrapper` | 2 | 3 | `src/include/dalib_net.h:427` | **public** | `public IGunQueueReader` |
| `CImeUiFont_Base` | 5 | 5 | `src/include/imeui.h:160` | **public** | — |
| `CInputBox` | 11 | 11 | `src/include/imeui.h:68` | **public** | — |
| `CInternalEquip` | 2 | 24 | `src/include/common_cequip.h:280` | **public** | `public CEquip` |
| `ClientIgniteFuseAction` | 1 | 1 | `src/freelancer/Client/client_fuse.h:215` | private (freelancer) | `public ClientFuseAction` |
| `ClientImpulseAction` | 1 | 1 | `src/freelancer/Client/client_fuse.h:190` | private (freelancer) | `public ClientFuseAction` |
| `ClipNode` | 9 | 41 | `src/include/common_text.h:979` | **public** | `public RDLNode` |
| `CloakingDevice` | 3 | 20 | `src/include/common_archetype.h:1303` | **public** | `public AttachedEquipment` |
| `CLoginMgr` | 2 | 3 | `src/freelancer/Client/login_mgr.h:114` | private (freelancer) | `public IGunQueueReader, public CDPClient` |
| `CLoot` | 6 | 67 | `src/include/common_cobject.h:869` | **public** | `public CSimple` |
| `CMainFrame` | 3 | 3 | `src/FLServer/MainFrm.h:10` | private (FLServer) | `public CFrameWnd` |
| `CMine` | 4 | 70 | `src/include/common_cobject.h:806` | **public** | `public CProjectile` |
| `CMunition` | 1 | 67 | `src/include/common_cobject.h:682` | **public** | `public CProjectile` |
| `Cnd` | 7 | 7 | `src/Content/MissionCnd.h:33` | private (Content) | — |
| `CNonPhysAttachment` | 6 | 12 | `src/include/common_cequip.h:388` | **public** | `public CAttachmentObj` |
| `CNudgeEngine` | 3 | 17 | `src/include/common_cequip.h:2172` | **public** | `public CPhysControllerEquip` |
| `CObject` | 15 | 48 | `src/include/common_cobject.h:159` | **public** | `public EngineObject` |
| `CObjectCB` | 2 | 2 | `src/include/common_physys.h:213` | **public** | — |
| `Commodity` | 6 | 19 | `src/include/common_archetype.h:1327` | **public** | `public Equipment` |
| `CommReferrable` | 3 | 3 | `src/include/common_net.h:64` | **public** | — |
| `Controller` | 1 | 9 | `src/include/common_physys.h:347` | **public** | `public IVP_Controller_Dependent` |
| `COperationCompleteDlg` | 1 | 1 | `src/FLServer/OperationCompleteDlg.h:5` | private (FLServer) | `public CDialog` |
| `CostumeHint` | 1 | 5 | `src/include/common_goods.h:791` | **public** | `public BaseHint` |
| `CounterMeasure` | 4 | 21 | `src/include/common_archetype.h:1526` | **public** | `public Projectile` |
| `CounterMeasureDropper` | 4 | 24 | `src/include/common_archetype.h:1156` | **public** | `public Launcher` |
| `CPhysAttachment` | 6 | 12 | `src/include/common_cequip.h:420` | **public** | `public CAttachmentObj` |
| `CPhysControllerEquip` | 5 | 14 | `src/include/common_cequip.h:692` | **public** | `public PhySys::Controller` |
| `CPlayerAccount` | 1 | 1 | `src/include/common_net.h:568` | **public** | — |
| `CPlayerGroup` | 1 | 1 | `src/include/common_server.h:602` | **public** | — |
| `CPlayerTradeOffer` | 1 | 1 | `src/include/common_tradeoffer.h:32` | **public** | — |
| `CProjectile` | 5 | 66 | `src/include/common_cobject.h:611` | **public** | `public CSimple` |
| `CRemotePhysicsController` | 4 | 21 | `src/include/common_server.h:1251` | **public** | `public IVP_Controller_Golem, public CRemotePhysicsSimulation` |
| `CRemotePhysicsSimulation` | 2 | 2 | `src/include/common_server.h:961` | **public** | — |
| `CSelectStarSystemDlg` | 4 | 4 | `src/FLServer/SelectStarSystemDlg.h:13` | private (FLServer) | `public CDialog` |
| `CServerConfigDlg` | 3 | 3 | `src/FLServer/ServerConfigDlg.h:13` | private (FLServer) | `public CDialog` |
| `CServerInfoDlg` | 1 | 1 | `src/FLServer/ServerInfoDlg.h:14` | private (FLServer) | `public CDialog` |
| `CServerNewsDlg` | 3 | 3 | `src/FLServer/ServerNewsDlg.h:6` | private (FLServer) | `public CDialog` |
| `CSetPasswordDlg` | 2 | 2 | `src/FLServer/SetPasswordDlg.h:8` | private (FLServer) | `public CDialog` |
| `CShip` | 10 | 108 | `src/include/common_cobject.h:1971` | **public** | `public CEqObj, public PhySys::Controller` |
| `CShipCreateInfo` | 1 | 2 | `src/include/server_createinfo.h:319` | **public** | `CEqObjCreateInfo` |
| `CShutdownDlg` | 2 | 2 | `src/FLServer/ShutdownDlg.h:7` | private (FLServer) | `public CDialog` |
| `CSimple` | 13 | 61 | `src/include/common_cobject.h:459` | **public** | `public CObject` |
| `CSolar` | 11 | 109 | `src/include/common_cobject.h:2329` | **public** | `public CEqObj` |
| `CSteering` | 3 | 17 | `src/include/common_cequip.h:2130` | **public** | `public CPhysControllerEquip` |
| `CStrafeEngine` | 3 | 17 | `src/include/common_cequip.h:2212` | **public** | `public CPhysControllerEquip` |
| `CTradeEquipList` | 1 | 1 | `src/freelancer/Client/trade_equip_list.h:93` | private (freelancer) | — |
| `CTradeEquipListLine` | 1 | 1 | `src/freelancer/Client/trade_equip_list.h:58` | private (freelancer) | — |
| `CUniverseChatDlg` | 4 | 4 | `src/FLServer/UniverseChatDlg.h:10` | private (FLServer) | `public CDialog` |
| `DamageGroupAction` | 1 | 1 | `src/include/server_fuse.h:165` | **public** | `public ServerFuseAction` |
| `DamageHardpointAttachmentAction` | 1 | 1 | `src/include/server_fuse.h:208` | **public** | `public ServerFuseAction` |
| `DamageRootAction` | 1 | 1 | `src/include/server_fuse.h:125` | **public** | `public ServerFuseAction` |
| `DestroyGroupAction` | 1 | 1 | `src/include/server_fuse.h:145` | **public** | `public ServerFuseAction` |
| `DestroyHardpointAttachmentAction` | 1 | 1 | `src/include/server_fuse.h:187` | **public** | `public ServerFuseAction` |
| `DeviceTRC` | 12 | 25 | `src/include/common_text.h:476` | **public** | `public TextRenderContext` |
| `DirectiveCallback` | 1 | 1 | `src/include/common_pub.h:1363` | **public** | — |
| `DumpCargoAction` | 1 | 1 | `src/include/server_fuse.h:296` | **public** | `public ServerFuseAction` |
| `DynamicAsteroid` | 2 | 8 | `src/include/common_archetype.h:1578` | **public** | `public Root` |
| `Engine` | 4 | 17 | `src/include/common_archetype.h:945` | **public** | `public Equipment` |
| `EngineObject` | 19 | 33 | `src/include/common_cobject.h:81` | **public** | `public IEngineInstance` |
| `Enumerator` | 1 | 1 | `src/include/common_server.h:161` | **public** | — |
| `EqObj` | 4 | 10 | `src/include/common_archetype.h:598` | **public** | `public Root` |
| `EqObj` | 39 | 67 | `src/include/server_gameobject.h:638` | **public** | `public GameObject` |
| `Equipment` | 7 | 13 | `src/include/common_archetype.h:814` | **public** | `public Root` |
| `EventHandler` | 1 | 1 | `src/freelancer/Client/event_handler.h:15` | private (freelancer) | — |
| `ExclusionZoneMgr` | 3 | 3 | `src/include/common_net.h:734` | **public** | — |
| `FAnimScript` | 3 | 3 | `src/EngBase/EngBase.h:438` | private (EngBase) | — |
| `FApproach2Base` | 11 | 11 | `src/movie/MovieObject.h:20` | private (movie) | — |
| `FCargoScanRoute` | 5 | 11 | `src/Content/GuidesInternal.h:234` | private (Content) | `public FGuideRoute` |
| `FCliDynamicAsteroidGameobj` | 1 | 28 | `src/freelancer/Client/cli_asteroid_dynamic.h:19` | private (freelancer) | `public FClientGameobj` |
| `FCliEffectFactory` | 3 | 3 | `src/freelancer/Client/particle_effect.h:233` | private (freelancer) | — |
| `FClientEquip` | 2 | 3 | `src/freelancer/Client/client_equip_fx.h:15` | private (freelancer) | `INotify` |
| `FClientEquippedObj` | 4 | 32 | `src/freelancer/Client/client_gameobj.h:427` | private (freelancer) | `public FClientSimpleGameobj, public INotify` |
| `FClientGameobj` | 26 | 27 | `src/freelancer/Client/client_gameobj.h:169` | private (freelancer) | `public IObjInspectImpl` |
| `FClientProjectileGameobj` | 1 | 29 | `src/freelancer/Client/client_gameobj.h:824` | private (freelancer) | `public FClientSimpleGameobj` |
| `FClientShipGameobj` | 1 | 33 | `src/freelancer/Client/client_gameobj.h:598` | private (freelancer) | `public FClientEquippedObj` |
| `FClientSimpleGameobj` | 1 | 28 | `src/freelancer/Client/client_gameobj.h:401` | private (freelancer) | `public FClientGameobj` |
| `FCliLauncherFx` | 3 | 6 | `src/freelancer/Client/cli_projectile_fx.h:60` | private (freelancer) | `FClientEquip` |
| `FCliMedAsteroidGameobj` | 1 | 28 | `src/freelancer/Client/cli_asteroid_med.h:23` | private (freelancer) | `public FClientGameobj` |
| `FContentGuide` | 12 | 15 | `src/Content/Guides.h:237` | private (Content) | `public FContentRefObject` |
| `FContentMessageRouter` | 2 | 2 | `src/Content/ContentMessageRouter.h:35` | private (Content) | `public FContentMessageRouterBase` |
| `FContentNpcShip` | 2 | 7 | `src/Content/NpcShip.h:97` | private (Content) | `public FContentShipBase, public pub::AI::ContentCallback` |
| `FContentRefObject` | 3 | 3 | `src/Content/ContentRefObject.h:11` | private (Content) | — |
| `FContentShipBase` | 3 | 5 | `src/Content/BehaviorCmds.h:64` | private (Content) | `public FContentSpaceObj, public pub::AI::DirectiveCallback` |
| `FContentSpaceObj` | 2 | 2 | `src/Content/NpcBase.h:107` | private (Content) | — |
| `FDeformMesh` | 7 | 7 | `src/Deformable2/Deformable2.h:151` | private (Deformable2) | — |
| `FDestroyMission` | 1 | 46 | `src/Content/RandomMissionObject.h:142` | private (Content) | `public FRandomMissionObject` |
| `FEffectBase` | 9 | 9 | `src/freelancer/Client/effect_base.h:15` | private (freelancer) | — |
| `FEngineLight` | 3 | 3 | `src/freelancer/Client/engine_light.h:40` | private (freelancer) | — |
| `FEyeMaterialImpl` | 1 | 33 | `src/Deformable2/EyeMaterial.h:119` | private (Deformable2) | `public IMaterial` |
| `FFxEmitterBase` | 1 | 73 | `src/alchemy/alchemy_emitters.h:123` | private (alchemy) | `public FGenericFxObjectBase` |
| `FFxNodePropertySet` | 3 | 27 | `src/alchemy/alchemy_types.h:307` | private (alchemy) | `public IFxNodePropertySet` |
| `FGameCamera` | 12 | 12 | `src/freelancer/Client/space_camera_game.h:75` | private (freelancer) | `FSpaceCamera` |
| `FGameLight` | 1 | 4 | `src/freelancer/Client/engine_light.h:347` | private (freelancer) | `FEngineLight` |
| `FGateTunnel` | 5 | 5 | `src/freelancer/Client/gate_tunnel.h:102` | private (freelancer) | — |
| `FGcsCloneableLine` | 1 | 6 | `src/Content/Gcs.h:157` | private (Content) | `public FGcsLine` |
| `FGcsConversation` | 5 | 5 | `src/Content/Gcs.h:821` | private (Content) | — |
| `FGcsConversationManager` | 1 | 1 | `src/Content/Gcs.h:1147` | private (Content) | `public FGcsConversationManagerInstance` |
| `FGcsEvent` | 1 | 1 | `src/Content/Gcs.h:1007` | private (Content) | — |
| `FGcsHandler` | 3 | 3 | `src/Content/Gcs.h:1191` | private (Content) | — |
| `FGcsHandlerSet` | 1 | 1 | `src/Content/Gcs.h:1277` | private (Content) | — |
| `FGcsLine` | 5 | 5 | `src/Content/Gcs.h:101` | private (Content) | — |
| `FGenericFxObjectBase` | 4 | 72 | `src/alchemy/alchemy_genericfx.h:126` | private (alchemy) | `public IGenericFx` |
| `FGuideRoute` | 6 | 6 | `src/Content/GuidesInternal.h:103` | private (Content) | — |
| `FHotspot` | 1 | 1 | `src/freelancer/Client/gf_hotspot.h:25` | private (freelancer) | — |
| `FMeshHolder` | 1 | 1 | `src/freelancer/Client/client_mesh_load.h:53` | private (freelancer) | — |
| `FMetaBehavior` | 1 | 1 | `src/Content/Guides.h:160` | private (Content) | `public FGuideDescBase` |
| `FMissionNag` | 3 | 3 | `src/Content/MissionAct.h:525` | private (Content) | — |
| `FMissionNagDist` | 1 | 4 | `src/Content/MissionAct.h:557` | private (Content) | `public FMissionNag` |
| `FMissionObject` | 27 | 29 | `src/Content/MissionObject.h:665` | private (Content) | `public IDLL` |
| `FMissionObjective` | 7 | 7 | `src/Content/MissionObject.h:312` | private (Content) | — |
| `FMissionPlayer` | 3 | 3 | `src/Content/MissionObject.h:594` | private (Content) | — |
| `FMissionScript` | 2 | 31 | `src/Content/MissionScript.h:108` | private (Content) | `FMissionObject` |
| `FMissionShip` | 1 | 1 | `src/Content/MissionObject.h:71` | private (Content) | — |
| `FMixerNode` | 4 | 4 | `src/EngBase/EngBase.h:631` | private (EngBase) | — |
| `FObjListDirective` | 4 | 7 | `src/Content/ObjListDirective.h:33` | private (Content) | `public FContentRefObject` |
| `FontStringElement` | 10 | 10 | `src/freelancer/Client/font_string.h:45` | private (freelancer) | — |
| `FPacketBase` | 3 | 4 | `src/include/rpc_packet.h:60` | **public** | `public FRpcRefObject` |
| `FParticleEffect` | 1 | 10 | `src/freelancer/Client/particle_effect.h:19` | private (freelancer) | `FEffectBase` |
| `FPatrolGuide` | 1 | 20 | `src/Content/GuidesInternal.h:298` | private (Content) | `public FTargetGuide` |
| `FPatrolRoute` | 2 | 8 | `src/Content/GuidesInternal.h:197` | private (Content) | `public FGuideRoute` |
| `FRandEncParams` | 3 | 6 | `src/Content/RandomEncounters.h:70` | private (Content) | `public FContentRefObject` |
| `FRandEncPopulator` | 3 | 6 | `src/Content/RandomEncounters.h:211` | private (Content) | `public FContentRefObject` |
| `FRandomMissionGenerator` | 4 | 4 | `src/Content/RandomMissionGen.h:473` | private (Content) | `private FRmGeneratorRegistration` |
| `FRandomMissionLoader` | 5 | 8 | `src/Content/RandomMission.h:136` | private (Content) | `public FContentRefObject` |
| `FRandomMissionObject` | 16 | 45 | `src/Content/RandomMissionObject.h:37` | private (Content) | `public FMissionObject, public StateMachineGroup` |
| `FRmConversation` | 5 | 12 | `src/Content/RandomMissionOffer.h:175` | private (Content) | `public FRmConversationBase` |
| `FRmConversationBase` | 2 | 7 | `src/Content/RandomMissionOffer.h:157` | private (Content) | `public FGcsConversation` |
| `FRmDestroySolarsFilter` | 1 | 7 | `src/Content/RandomMissionGen.h:351` | private (Content) | `FRmPathFilter` |
| `FRmDifficultyWindow` | 5 | 5 | `src/Content/RandomMissionGen.h:82` | private (Content) | — |
| `FRmDifficultyWindowList` | 1 | 1 | `src/Content/RandomMissionGen.h:102` | private (Content) | — |
| `FRmMissionDataBase` | 2 | 2 | `src/Content/RandomMission.h:31` | private (Content) | `public pub::Controller::CreateParms, public FContentRefObject` |
| `FRmObjective` | 6 | 10 | `src/Content/RandomMissionRuntime.h:275` | private (Content) | `public FRmParamText` |
| `FRmObjectiveMap` | 1 | 1 | `src/Content/RandomMissionOffer.h:105` | private (Content) | — |
| `FRmObjectiveText` | 1 | 1 | `src/Content/RandomMissionGen.h:146` | private (Content) | — |
| `FRmObjectiveType2` | 1 | 11 | `src/Content/RandomMissionRuntime.h:312` | private (Content) | `public FRmObjective` |
| `FRmObjectiveType3` | 2 | 13 | `src/Content/RandomMissionRuntime.h:350` | private (Content) | `public FRmObjectiveType2` |
| `FRmObjectiveType4` | 1 | 12 | `src/Content/RandomMissionRuntime.h:393` | private (Content) | `public FRmObjectiveType2` |
| `FRmOfferComponent` | 3 | 4 | `src/Content/RandomMissionOffer.h:203` | private (Content) | `public MessageHandler` |
| `FRmOfferText` | 3 | 3 | `src/Content/RandomMissionGen.h:126` | private (Content) | — |
| `FRmParamText` | 4 | 4 | `src/Content/RandomMissionRuntime.h:249` | private (Content) | — |
| `FRmPathFilter` | 6 | 6 | `src/Content/RandomMissionGen.h:332` | private (Content) | — |
| `FRmRecordList` | 4 | 4 | `src/Content/RandomMissionMachine.h:181` | private (Content) | — |
| `FRmRuntimeMachine` | 21 | 21 | `src/Content/RandomMissionMachine.h:354` | private (Content) | `public FRmMachineGroup, public IRmTextParamSource` |
| `FRmShipCountTrigger` | 1 | 8 | `src/Content/RandomMissionMachine.h:285` | private (Content) | `public FRmTrigger` |
| `FRmShipHealthTrigger` | 1 | 8 | `src/Content/RandomMissionMachine.h:305` | private (Content) | `public FRmTrigger` |
| `FRmSpawnRecord` | 1 | 1 | `src/Content/RandomMissionMachine.h:120` | private (Content) | — |
| `FRmTreePath` | 2 | 2 | `src/Content/RandomMissionGen.h:251` | private (Content) | — |
| `FRmTrigger` | 7 | 7 | `src/Content/RandomMissionRuntime.h:100` | private (Content) | `public StateMachineMessageHandler` |
| `FRmVignetteMachine` | 1 | 22 | `src/Content/RandomMissionMachine.h:477` | private (Content) | `public FRmRuntimeMachine` |
| `FRmVignetteNode` | 1 | 1 | `src/Content/RandomMissionGen.h:205` | private (Content) | — |
| `FRmVignetteParams` | 2 | 2 | `src/Content/RandomMissionGen.h:183` | private (Content) | — |
| `FRoomCamera` | 3 | 23 | `src/freelancer/Client/room_camera.h:21` | private (freelancer) | `public ICamera` |
| `FRoomObject` | 13 | 13 | `src/freelancer/Client/room_control.h:34` | private (freelancer) | — |
| `FRpcRefObject` | 1 | 1 | `src/include/rpc_packet.h:29` | **public** | — |
| `FStarSphere` | 7 | 7 | `src/freelancer/Client/star_sphere.h:55` | private (freelancer) | — |
| `FTargetGuide` | 4 | 19 | `src/Content/GuidesInternal.h:40` | private (Content) | `public FContentGuide` |
| `FThorn_Scene` | 2 | 2 | `src/freelancer/Client/gf_thorn_scene.h:22` | private (freelancer) | `FThornComponent` |
| `FThornEntity` | 5 | 5 | `src/Thorn/ThornScene.h:130` | private (Thorn) | — |
| `FThornEvent` | 6 | 6 | `src/Thorn/ThornScene.h:302` | private (Thorn) | — |
| `FTradeGuide` | 2 | 21 | `src/Content/GuidesInternal.h:159` | private (Content) | `public FTargetGuide` |
| `FTypedChannel` | 15 | 15 | `src/EngBase/EngBase.h:468` | private (EngBase) | — |
| `Fuse` | 2 | 2 | `src/include/common_fuses.h:120` | **public** | — |
| `FuseAction` | 12 | 12 | `src/include/common_fuses.h:72` | **public** | — |
| `GameObject` | 27 | 28 | `src/include/server_gameobject.h:249` | **public** | `public IObjInspectImpl` |
| `Guided` | 2 | 25 | `src/include/server_gameobject.h:1516` | **public** | `public Munition, public PhySys::Controller, public PhySys::CObjectCB` |
| `Gun` | 4 | 24 | `src/include/common_archetype.h:1086` | **public** | `public Launcher` |
| `HardpointSummary` | 1 | 1 | `src/include/common_goods.h:370` | **public** | — |
| `HUDItem` | 11 | 11 | `src/freelancer/Client/hud_element_list.h:51` | private (freelancer) | — |
| `HUDItem_Factory` | 2 | 2 | `src/freelancer/Client/hud_element_list.h:109` | private (freelancer) | — |
| `HudTargetBase` | 6 | 17 | `src/freelancer/Client/hud_target.h:259` | private (freelancer) | `public HUDItem, public WinControl, public ButtonMover, public EventHandler` |
| `I3DMathEngine` | 24 | 27 | `src/include/x86math.h:51` | **public** | `IDAComponent` |
| `IAction` | 3 | 3 | `src/include/common_cequip.h:1796` | **public** | — |
| `IAggregateComponent` | 1 | 4 | `src/include/dacom.h:30` | **public** | `IDAComponent` |
| `IALBatchedMaterial` | 6 | 9 | `src/alchemy/alchemy_material.h:75` | private (alchemy) | `IDAComponent` |
| `IAlchemy` | 28 | 41 | `src/include/alchemy.h:83` | **public** | `IFxNodeLibrary` |
| `IALMaterial` | 18 | 21 | `src/alchemy/alchemy_material.h:108` | private (alchemy) | `IDAComponent` |
| `IAnimatedTransform` | 10 | 13 | `src/alchemy/alchemy_types.h:1336` | private (alchemy) | `IDAComponent` |
| `IAnimation2` | 36 | 39 | `src/include/engine/engbase.h:191` | **public** | `IDAComponent` |
| `IBase` | 27 | 27 | `src/include/common_universe.h:193` | **public** | — |
| `IBatchedMaterial` | 6 | 9 | `src/include/render/IBatchedMaterial.h:13` | **public** | `IDAComponent` |
| `IBehaviorManager` | 1 | 1 | `src/include/common_iface.h:1148` | **public** | — |
| `IBigImage2` | 9 | 12 | `src/include/ximage.h:46` | **public** | `IDAComponent` |
| `IBigImageSource` | 3 | 6 | `src/include/ximage.h:14` | **public** | `IDAComponent` |
| `ICamera` | 17 | 20 | `src/include/common_misc.h:503` | **public** | `IDAComponent` |
| `ICameraControl` | 10 | 13 | `src/include/thorn_controls.h:64` | **public** | `IDAComponent` |
| `IChannel` | 16 | 19 | `src/include/engine/engbase.h:277` | **public** | `IDAComponent` |
| `IChannelArchetype` | 8 | 11 | `src/include/engine/engbase.h:339` | **public** | `IDAComponent` |
| `IChannelOwner` | 1 | 1 | `src/include/engine/engbase.h:253` | **public** | — |
| `IChannelSink` | 3 | 3 | `src/include/engine/engbase.h:267` | **public** | — |
| `IChatClient` | 1 | 1 | `src/include/rpc_iclient.h:624` | **public** | — |
| `IChatServer` | 1 | 1 | `src/include/server_iserver.h:352` | **public** | — |
| `IClient` | 126 | 126 | `src/include/rpc_iclient.h:169` | **public** | — |
| `ICliObj` | 14 | 14 | `src/include/common_archetype.h:317` | **public** | — |
| `ICliObjFactory` | 1 | 1 | `src/include/common_cliobj.h:17` | **public** | — |
| `ICOManager` | 8 | 11 | `src/include/dacom.h:57` | **public** | `IDAComponent` |
| `IComponentFactory` | 1 | 4 | `src/include/dacom.h:73` | **public** | `IDAComponent` |
| `ICompoundControl` | 10 | 13 | `src/include/thorn_controls.h:101` | **public** | `IDAComponent` |
| `ICRSpline` | 13 | 16 | `src/Thorn/Thorn.h:152` | private (Thorn) | `IDAComponent` |
| `ID3DLightControl` | 6 | 9 | `src/include/thorn_controls.h:193` | **public** | `IDAComponent` |
| `IDAComponent` | 3 | 3 | `src/include/dacom.h:22` | **public** | — |
| `IDatabase` | 16 | 16 | `src/include/common_newchar.h:218` | **public** | — |
| `IDataBuffer` | 1 | 4 | `src/include/readfile.h:94` | **public** | `IDAComponent` |
| `IDataViewOwner` | 2 | 2 | `src/freelancer/Client/wincontrol_base.h:46` | private (freelancer) | — |
| `IDeformable` | 36 | 39 | `src/include/engine/engbase.h:393` | **public** | `IDAComponent` |
| `IDLL` | 2 | 2 | `src/include/content.h:8` | **public** | — |
| `IDPMsgHandler` | 3 | 3 | `src/include/dalib_net.h:71` | **public** | — |
| `IEngine` | 55 | 58 | `src/include/engine/engbase.h:46` | **public** | `IDAComponent` |
| `IEngineComponent` | 10 | 13 | `src/include/engine/engbase.h:138` | **public** | `IDAComponent` |
| `IEngineInstance` | 14 | 14 | `src/include/common_cobject.h:57` | **public** | — |
| `IEnvironment` | 10 | 15 | `src/freelancer/Client/client_environment.h:59` | private (freelancer) | `public IInputEvent` |
| `IFileSystem` | 38 | 42 | `src/include/readfile.h:38` | **public** | `IComponentFactory` |
| `IFLAppearance` | 11 | 14 | `src/include/render/IFLAppearance.h:7` | **public** | `IDAComponent` |
| `IFLListBox` | 10 | 10 | `src/FLServer/IFLListBox.h:9` | private (FLServer) | — |
| `IFogControl` | 11 | 14 | `src/include/thorn_controls.h:214` | **public** | `IDAComponent` |
| `IFont` | 11 | 11 | `src/include/font.h:17` | **public** | — |
| `IFxAnimatedSingle` | 16 | 19 | `src/alchemy/alchemy_types.h:1231` | private (alchemy) | `IDAComponent` |
| `IFxEffect` | 16 | 19 | `src/include/alchemy.h:176` | **public** | `IDAComponent` |
| `IFxEffectArchetype` | 18 | 21 | `src/include/alchemy.h:34` | **public** | `IDAComponent` |
| `IFxEffectLibrary` | 12 | 15 | `src/include/alchemy.h:154` | **public** | `IDAComponent` |
| `IFxMesh` | 6 | 9 | `src/include/alchemy.h:118` | **public** | `IDAComponent` |
| `IFxMeshFactory` | 1 | 4 | `src/include/alchemy.h:134` | **public** | `IDAComponent` |
| `IFxNodeLibrary` | 10 | 13 | `src/include/alchemy.h:66` | **public** | `IDAComponent` |
| `IFxNodePropertySet` | 21 | 24 | `src/alchemy/alchemy_types.h:221` | private (alchemy) | `IDAComponent` |
| `IFxRuntime` | 8 | 11 | `src/include/alchemy.h:140` | **public** | `IDAComponent` |
| `IGammaControl` | 5 | 8 | `src/include/render/IRenderPipeline.h:396` | **public** | `IDAComponent` |
| `IGenericFx` | 65 | 68 | `src/alchemy/alchemy_types.h:617` | private (alchemy) | `IDAComponent` |
| `IGeoTransformControl` | 4 | 7 | `src/include/thorn_controls.h:46` | **public** | `IDAComponent` |
| `IgniteFuseAction` | 1 | 1 | `src/include/server_fuse.h:89` | **public** | `public ServerFuseAction` |
| `IGraph` | 10 | 10 | `src/freelancer/Client/graph_library.h:61` | private (freelancer) | — |
| `IGunBrowser` | 3 | 3 | `src/include/dalib_net.h:416` | **public** | `public IUnknown` |
| `IGunConnection` | 6 | 6 | `src/include/dalib_net.h:366` | **public** | `public IUnknown` |
| `IGunHost` | 7 | 7 | `src/include/dalib_net.h:393` | **public** | `public IUnknown` |
| `IGunQueueReader` | 1 | 1 | `src/include/dalib_net.h:361` | **public** | — |
| `IHardpoint` | 5 | 8 | `src/include/engine/engbase.h:320` | **public** | `IDAComponent` |
| `IIKControl` | 3 | 6 | `src/include/thorn_controls.h:139` | **public** | `IDAComponent` |
| `IImaData` | 2 | 5 | `src/include/render/shading.h:260` | **public** | `public IDAComponent` |
| `IImage` | 3 | 3 | `src/freelancer/Client/image.h:14` | private (freelancer) | — |
| `IImaInstance` | 4 | 7 | `src/include/render/shading.h:267` | **public** | `public IDAComponent` |
| `IInputEvent` | 5 | 5 | `src/freelancer/Client/client_environment.h:40` | private (freelancer) | — |
| `IInstanceTransform` | 14 | 14 | `src/include/engine/engbase.h:25` | **public** | — |
| `IIVPAnchorEvents` | 1 | 1 | `src/include/common_ivp_real_object.h:707` | **public** | — |
| `ILightControl` | 17 | 20 | `src/include/thorn_controls.h:154` | **public** | `IDAComponent` |
| `ILoadingScreen` | 2 | 2 | `src/include/common_ini.h:331` | **public** | — |
| `ILua` | 1 | 4 | `src/include/luaprofile.h:24` | **public** | `IDAComponent` |
| `ImageNode` | 10 | 42 | `src/include/common_text.h:840` | **public** | `public RDLNode` |
| `IMatAnimLibrary` | 4 | 7 | `src/include/render/shading.h:392` | **public** | `IDAComponent` |
| `IMaterial` | 29 | 32 | `src/include/render/IMaterial.h:11` | **public** | `IDAComponent` |
| `IMaterialBatcher2` | 17 | 20 | `src/include/render/shading.h:350` | **public** | `IDAComponent` |
| `IMaterialBatcherEvents` | 4 | 7 | `src/include/render/shading.h:341` | **public** | `IDAComponent` |
| `IMaterialLibrary` | 24 | 27 | `src/include/render/shading.h:283` | **public** | `IDAComponent` |
| `IMatHookUp` | 1 | 4 | `src/include/render/rendcomp.h:79` | **public** | `IDAComponent` |
| `IMEFocus` | 12 | 12 | `src/include/imeui.h:204` | **public** | — |
| `IMEInputBox` | 9 | 20 | `src/include/imeui.h:125` | **public** | `public CInputBox` |
| `IMesh` | 27 | 30 | `src/include/render/mesh_iface.h:11` | **public** | `IDAComponent` |
| `IMessageRouter` | 4 | 4 | `src/include/common_net.h:405` | **public** | — |
| `IMotionPath` | 5 | 8 | `src/include/thorn_iface.h:6` | **public** | `IDAComponent` |
| `ImpulseAction` | 1 | 1 | `src/include/server_fuse.h:229` | **public** | `public ServerFuseAction` |
| `INebula` | 23 | 23 | `src/freelancer/Client/cli_nebula.h:76` | private (freelancer) | — |
| `INestedSingleRamp` | 15 | 18 | `src/alchemy/alchemy_types.h:1158` | private (alchemy) | `IDAComponent` |
| `INotify` | 1 | 1 | `src/include/common_cequip.h:150` | **public** | — |
| `InternalFXEquip` | 3 | 16 | `src/include/common_archetype.h:1266` | **public** | `public Equipment` |
| `IObjAffect` | 31 | 31 | `src/include/common_iface.h:578` | **public** | — |
| `IObjDB` | 4 | 4 | `src/include/common_cobjdb.h:26` | **public** | — |
| `IObjInspect` | 88 | 88 | `src/include/common_iface.h:236` | **public** | — |
| `IObjInspectImpl` | 1 | 1 | `src/include/common_iface.h:828` | **public** | `public IObjRW, public CBase` |
| `IParamCurve` | 16 | 19 | `src/include/thorn_iface.h:175` | **public** | `IDAComponent` |
| `IPersistString` | 3 | 6 | `src/include/thorn_iface.h:103` | **public** | `IDAComponent` |
| `IPlanetRing` | 13 | 14 | `src/freelancer/Client/texture_panels.h:35` | private (freelancer) | `public Universe::IZoneImmersion` |
| `IProfileParser` | 6 | 9 | `src/include/luaprofile.h:33` | **public** | `IDAComponent` |
| `IPSysControl` | 5 | 8 | `src/include/thorn_controls.h:251` | **public** | `IDAComponent` |
| `IRampColor` | 18 | 21 | `src/alchemy/alchemy_types.h:1126` | private (alchemy) | `IDAComponent` |
| `IRampColorRamp` | 16 | 19 | `src/alchemy/alchemy_types.h:1199` | private (alchemy) | `IDAComponent` |
| `IRampSingle` | 14 | 17 | `src/alchemy/alchemy_types.h:1105` | private (alchemy) | `IDAComponent` |
| `IRegExp` | 5 | 8 | `src/include/dacom.h:79` | **public** | `IDAComponent` |
| `IRenderableMesh` | 4 | 7 | `src/include/render/rendcomp.h:84` | **public** | `IDAComponent` |
| `IRenderComponent` | 15 | 18 | `src/include/render/rendcomp.h:55` | **public** | `IDAComponent` |
| `IRenderer2` | 20 | 23 | `src/include/render/rendcomp.h:25` | **public** | `IDAComponent` |
| `IRenderPipeline` | 74 | 77 | `src/include/render/IRenderPipeline.h:32` | **public** | `IDAComponent` |
| `IRenderPipelineEx` | 17 | 20 | `src/include/render/IRenderPipelineEx.h:137` | **public** | `IDAComponent` |
| `IReverbControl` | 2 | 5 | `src/include/thorn_controls.h:234` | **public** | `IDAComponent` |
| `IRmTextParamSource` | 1 | 1 | `src/Content/RandomMissionOffer.h:39` | private (Content) | — |
| `IRootData` | 1 | 4 | `src/include/engine/engbase.h:301` | **public** | `IDAComponent` |
| `IRPDraw` | 1 | 4 | `src/include/render/IRenderPipeline.h:316` | **public** | `IDAComponent` |
| `IRPDrawEx` | 2 | 5 | `src/include/render/IRenderPipelineEx.h:199` | **public** | `IDAComponent` |
| `IRPIndexBuffer` | 9 | 12 | `src/include/render/IRenderPipeline.h:325` | **public** | `IDAComponent` |
| `IRPTexture` | 5 | 8 | `src/include/render/IRenderPipeline.h:425` | **public** | `IDAComponent` |
| `IRPVertexBuffer` | 10 | 13 | `src/include/render/IRenderPipeline.h:364` | **public** | `IDAComponent` |
| `ISave` | 7 | 7 | `src/include/server_savefile.h:91` | **public** | — |
| `IScriptControl` | 4 | 7 | `src/include/thorn_controls.h:89` | **public** | `IDAComponent` |
| `IScriptEngine` | 42 | 45 | `src/include/thorn_iface.h:538` | **public** | `IDAComponent` |
| `ISearchPath` | 2 | 6 | `src/include/readfile.h:99` | **public** | `IComponentFactory` |
| `IServer` | 92 | 92 | `src/include/server_iserver.h:45` | **public** | — |
| `IServerImpl` | 1 | 93 | `src/include/rpc_iserver.h:49` | **public** | `public IServer, public IChatServer` |
| `IServerImpl` | 1 | 93 | `src/include/server_iserverimpl.h:44` | **public** | `public IServer, public IChatServer` |
| `ISound` | 15 | 15 | `src/freelancer/Client/sound_archetype.h:22` | private (freelancer) | — |
| `ISoundArchetype` | 10 | 13 | `src/include/soundmanager.h:25` | **public** | `IDAComponent` |
| `ISoundControl` | 8 | 11 | `src/include/thorn_controls.h:262` | **public** | `IDAComponent` |
| `ISoundListenerPose` | 6 | 9 | `src/include/soundmanager.h:196` | **public** | `IDAComponent` |
| `ISoundManager` | 53 | 56 | `src/include/soundmanager.h:207` | **public** | `IDAComponent` |
| `ISoundManagerParserBind` | 1 | 4 | `src/SoundManager/SoundManagerInternal.h:63` | private (SoundManager) | `IDAComponent` |
| `ISoundManagerStatus` | 1 | 4 | `src/include/soundmanager.h:20` | **public** | `IDAComponent` |
| `ISoundSource` | 40 | 43 | `src/include/soundmanager.h:68` | **public** | `IDAComponent` |
| `ISpatialPartition` | 9 | 9 | `src/include/common_spatial.h:47` | **public** | — |
| `ISphereMesh` | 12 | 15 | `src/include/render/rendcomp.h:109` | **public** | `IDAComponent` |
| `IStoryMissionEvents` | 1 | 1 | `src/include/content.h:21` | **public** | — |
| `IStreamer2` | 12 | 15 | `src/include/soundstreamer.h:20` | **public** | `IDAComponent` |
| `ISubtitleControl` | 13 | 16 | `src/include/thorn_controls.h:284` | **public** | `IDAComponent` |
| `ISystem` | 22 | 22 | `src/include/common_universe.h:130` | **public** | — |
| `ISystemContainer` | 4 | 8 | `src/include/system.h:6` | **public** | `IAggregateComponent` |
| `ITextureLibrary2` | 43 | 46 | `src/include/render/shading.h:148` | **public** | `IDAComponent` |
| `IThornComContext` | 3 | 6 | `src/include/thorn_iface.h:501` | **public** | `IDAComponent` |
| `IThornLoadParser` | 6 | 6 | `src/include/thorn_iface.h:512` | **public** | — |
| `IThornSaveParser` | 4 | 4 | `src/include/thorn_iface.h:527` | **public** | — |
| `ITriMeshAnim` | 1 | 4 | `src/include/render/rendcomp.h:125` | **public** | `IDAComponent` |
| `IUIPage` | 4 | 4 | `src/freelancer/Client/ui_page.h:39` | private (freelancer) | — |
| `IUserEventControl` | 13 | 16 | `src/include/thorn_controls.h:304` | **public** | `IDAComponent` |
| `IVertexBufferManager` | 6 | 9 | `src/include/render/IRenderPipeline.h:297` | **public** | `IDAComponent` |
| `IVMeshLibrary` | 2 | 5 | `src/include/render/rendcomp.h:131` | **public** | `IDAComponent` |
| `IVMeshPart` | 4 | 7 | `src/include/render/rendcomp.h:97` | **public** | `IDAComponent` |
| `IVMeshWire` | 3 | 6 | `src/include/render/mesh_iface.h:45` | **public** | `IDAComponent` |
| `IVoice` | 8 | 8 | `src/freelancer/Client/sound_archetype.h:69` | private (freelancer) | — |
| `IVP_Anomaly_Limits` | 2 | 2 | `src/IVP/ivp_physics/ivp_anomaly_manager.h:79` | private (Common) | — |
| `IVP_Anomaly_Manager` | 7 | 7 | `src/IVP/ivp_physics/ivp_anomaly_manager.h:130` | private (Common) | — |
| `IVP_Collision` | 4 | 5 | `src/IVP/ivp_collision/ivp_collision.h:67` | private (Common) | `public IVP_Time_Event` |
| `IVP_Collision_Delegator` | 3 | 3 | `src/IVP/ivp_collision/ivp_collision.h:38` | private (Common) | — |
| `IVP_Collision_Delegator_Root` | 3 | 6 | `src/IVP/ivp_collision/ivp_collision.h:108` | private (Common) | `public IVP_Collision_Delegator` |
| `IVP_Collision_Filter` | 3 | 3 | `src/IVP/ivp_physics/ivp_listener.h:144` | private (Common) | — |
| `IVP_Collision_Filter_Coll_Group_Ident` | 3 | 6 | `src/IVP/ivp_physics/ivp_listener.h:156` | private (Common) | `public IVP_Collision_Filter` |
| `IVP_Controller` | 7 | 7 | `src/include/common_ivp_controller.h:128` | **public** | — |
| `IVP_Controller_Dependent` | 1 | 8 | `src/include/common_ivp_controller.h:256` | **public** | `public IVP_Controller` |
| `IVP_Controller_Golem` | 4 | 17 | `src/include/common_ivp_controller.h:408` | **public** | `public IVP_Controller_Motion` |
| `IVP_Controller_Independent` | 2 | 9 | `src/include/common_ivp_controller.h:280` | **public** | `public IVP_Controller` |
| `IVP_Controller_Motion` | 4 | 13 | `src/include/common_ivp_controller.h:337` | **public** | `public IVP_Controller_Independent` |
| `IVP_Event_Manager` | 2 | 2 | `src/IVP/ivp_physics/ivp_environment.h:119` | private (Common) | — |
| `IVP_Friction_Sys_Energy` | 2 | 11 | `src/IVP/ivp_physics/ivp_mindist.h:1638` | private (Common) | `public IVP_Controller_Independent` |
| `IVP_Friction_Sys_Static` | 4 | 13 | `src/IVP/ivp_physics/ivp_mindist.h:1603` | private (Common) | `public IVP_Controller_Independent` |
| `IVP_Ledge_Mindist` | 4 | 4 | `src/IVP/ivp_physics/ivp_mindist.h:1989` | private (Common) | `public IVP_Mindist_Base` |
| `IVP_Ledge_Sweep_Evaluator` | 1 | 1 | `src/IVP/ivp_collision/ivp_ledge_sweep_solver.h:484` | private (Common) | — |
| `IVP_Listener_Collision` | 1 | 1 | `src/IVP/ivp_physics/ivp_listener.h:73` | private (Common) | — |
| `IVP_Listener_Hull` | 4 | 4 | `src/IVP/ivp_physics/ivp_listener.h:106` | private (Common) | — |
| `IVP_Listener_Object` | 5 | 5 | `src/IVP/ivp_physics/ivp_listener.h:53` | private (Common) | — |
| `IVP_Listener_Phantom` | 3 | 3 | `src/IVP/ivp_physics/ivp_phantom.h:77` | private (Common) | — |
| `IVP_Listener_PSI` | 2 | 2 | `src/IVP/ivp_physics/ivp_listener.h:133` | private (Common) | — |
| `IVP_Listener_Set_Active` | 3 | 3 | `src/IVP/ivp_utility/ivp_u_set_active.h:52` | private (Common) | — |
| `IVP_ListManager` | 5 | 13 | `src/IVP/ivp_physics/ivp_mindist.h:1666` | private (Common) | `public IVP_Controller_Dependent` |
| `IVP_Material` | 6 | 6 | `src/IVP/ivp_physics/ivp_material.h:45` | private (Common) | — |
| `IVP_Material_Manager` | 5 | 5 | `src/IVP/ivp_physics/ivp_material.h:95` | private (Common) | — |
| `IVP_Material_Simple` | 5 | 11 | `src/IVP/ivp_physics/ivp_material.h:66` | private (Common) | `public IVP_Material` |
| `IVP_Message_Router` | 2 | 2 | `src/IVP/ivp_utility/ivp_message_router.h:19` | private (Common) | — |
| `IVP_Object` | 1 | 1 | `src/include/common_ivp_real_object.h:590` | **public** | — |
| `IVP_OV_Element` | 1 | 5 | `src/IVP/ivp_physics/ivp_ov_element.h:161` | private (Common) | `public IVP_Listener_Hull` |
| `IVP_PerformanceCounter` | 6 | 6 | `src/IVP/ivp_physics/ivp_listener.h:175` | private (Common) | — |
| `IVP_Range_Manager` | 3 | 3 | `src/IVP/ivp_physics/ivp_environment.h:318` | private (Common) | — |
| `IVP_Ray_Hit_Listener` | 2 | 2 | `src/IVP/ivp_collision/ivp_ray_solver.h:42` | private (Common) | — |
| `IVP_Ray_Solver_Os` | 1 | 1 | `src/IVP/ivp_collision/ivp_ray_solver.h:140` | private (Common) | — |
| `IVP_Real_Object` | 2 | 2 | `src/include/common_ivp_real_object.h:736` | **public** | `public IVP_Real_Object_Fast_Static` |
| `IVP_SurfaceManager` | 11 | 11 | `src/IVP/ivp_physics/ivp_surface_manager.h:123` | private (Common) | — |
| `IVP_Synapse_OO` | 1 | 5 | `src/IVP/ivp_collision/ivp_oo_watcher.h:42` | private (Common) | `public IVP_Listener_Hull` |
| `IVP_Synapse_Real` | 1 | 5 | `src/IVP/ivp_physics/ivp_mindist.h:51` | private (Common) | `public IVP_Listener_Hull` |
| `IVP_Time_Event` | 1 | 1 | `src/IVP/ivp_physics/ivp_time_event.h:25` | private (Common) | — |
| `IVP_Triangle` | 1 | 1 | `src/IVP/ivp_compact_builder/ivp_object_polygon_tetra.h:72` | private (Common) | — |
| `IVP_U_Active_Float` | 2 | 3 | `src/IVP/ivp_physics/ivp_active_value.h:98` | private (Common) | `public IVP_U_Active_Value` |
| `IVP_U_Active_Float_Delayed` | 1 | 1 | `src/IVP/ivp_physics/ivp_active_value.h:72` | private (Common) | — |
| `IVP_U_Active_Float_Listener` | 1 | 1 | `src/IVP/ivp_physics/ivp_active_value.h:56` | private (Common) | — |
| `IVP_U_Active_Int` | 1 | 2 | `src/IVP/ivp_physics/ivp_active_value.h:125` | private (Common) | `public IVP_U_Active_Value` |
| `IVP_U_Active_Int_Delayed` | 1 | 1 | `src/IVP/ivp_physics/ivp_active_value.h:78` | private (Common) | — |
| `IVP_U_Active_Int_Listener` | 1 | 1 | `src/IVP/ivp_physics/ivp_active_value.h:62` | private (Common) | — |
| `IVP_U_Active_Terminal_Double` | 3 | 6 | `src/IVP/ivp_physics/ivp_active_value.h:147` | private (Common) | `public IVP_U_Active_Float, public IVP_U_Active_Float_Delayed` |
| `IVP_U_Active_Terminal_Int` | 3 | 5 | `src/IVP/ivp_physics/ivp_active_value.h:166` | private (Common) | `public IVP_U_Active_Int, public IVP_U_Active_Int_Delayed` |
| `IVP_U_Active_Value` | 1 | 1 | `src/IVP/ivp_physics/ivp_active_value.h:84` | private (Common) | — |
| `IVP_U_Active_Value_Hash` | 2 | 4 | `src/IVP/ivp_physics/ivp_active_value.h:186` | private (Common) | `public IVP_VHash` |
| `IVP_U_Active_Value_Manager` | 15 | 15 | `src/IVP/ivp_physics/ivp_active_value.h:206` | private (Common) | — |
| `IVP_Universe_Manager` | 4 | 4 | `src/IVP/ivp_physics/ivp_cluster_manager.h:34` | private (Common) | — |
| `IVP_VHash` | 2 | 2 | `src/IVP/ivp_utility/ivp_vhash.h:27` | private (Common) | — |
| `IZone` | 36 | 37 | `src/include/common_universe.h:299` | **public** | `IZoneImmersion` |
| `IZoneImmersion` | 1 | 1 | `src/include/common_universe.h:287` | **public** | — |
| `JumpInAction` | 4 | 7 | `src/include/common_cequip.h:2093` | **public** | `public IAction, public PhySys::Controller` |
| `JustifyNode` | 9 | 41 | `src/include/common_text.h:943` | **public** | `public RDLNode` |
| `LaneAction` | 4 | 13 | `src/include/common_cequip.h:1864` | **public** | `public PhySys::Controller, public IAction` |
| `LaunchAction` | 5 | 8 | `src/include/common_cequip.h:1995` | **public** | `public IAction, public PhySys::Controller, public pub::AI::DirectiveCallback` |
| `Launcher` | 3 | 20 | `src/include/common_archetype.h:895` | **public** | `public AttachedEquipment` |
| `Light` | 2 | 15 | `src/include/common_archetype.h:1196` | **public** | `public Equipment` |
| `Loot` | 1 | 29 | `src/include/server_gameobject.h:1380` | **public** | `public GameObject` |
| `LootCrate` | 1 | 14 | `src/include/common_archetype.h:852` | **public** | `public Equipment` |
| `MakeInvincibleAction` | 1 | 1 | `src/include/server_fuse.h:277` | **public** | `public ServerFuseAction` |
| `MD5Hash` | 1 | 1 | `src/include/md5hash.h:10` | **public** | — |
| `MedAsteroid` | 1 | 29 | `src/include/server_gameobject.h:1665` | **public** | `public GameObject` |
| `MedMine` | 2 | 30 | `src/include/server_gameobject.h:1695` | **public** | `public GameObject, public PhySys::CObjectCB` |
| `MessageHandler` | 1 | 1 | `src/include/common_net.h:424` | **public** | — |
| `MetaList` | 1 | 1 | `src/include/common_metalist.h:31` | **public** | — |
| `Mine` | 4 | 21 | `src/include/common_archetype.h:1502` | **public** | `public Projectile` |
| `Mine` | 1 | 18 | `src/include/server_gameobject.h:1445` | **public** | `public Projectile, public PhySys::Controller, public PhySys::CObjectCB` |
| `MineDropper` | 2 | 22 | `src/include/common_archetype.h:1142` | **public** | `public Launcher` |
| `Munition` | 6 | 23 | `src/include/common_archetype.h:1444` | **public** | `public Projectile` |
| `NavBar` | 2 | 47 | `src/freelancer/Client/nav_bar.h:61` | private (freelancer) | `public WinControl, public ButtonMover, public ButtonScaler` |
| `NnButtonControl` | 1 | 46 | `src/freelancer/Client/nn_button_control.h:55` | private (freelancer) | `public WinControl` |
| `NnInventory` | 2 | 47 | `src/freelancer/Client/nn_inventory.h:110` | private (freelancer) | `public WinControl` |
| `NnTextControl` | 1 | 46 | `src/freelancer/Client/nn_text_control.h:21` | private (freelancer) | `public WinControl` |
| `NullNode` | 9 | 41 | `src/include/common_text.h:659` | **public** | `public RDLNode` |
| `NumberStringChild` | 3 | 13 | `src/freelancer/Client/font_string.h:138` | private (freelancer) | `public FontStringElement` |
| `Observer` | 1 | 1 | `src/include/server_sysdb.h:308` | **public** | — |
| `OwnerList` | 1 | 2 | `src/include/common_metalist.h:295` | **public** | `public MetaList<T>` |
| `ParagraphNode` | 9 | 41 | `src/include/common_text.h:921` | **public** | `public RDLNode` |
| `PhysicalSystem` | 7 | 12 | `src/Common/common_physys_internal.h:36` | private (Common) | `public IVP_Listener_Object, public IVP_Listener_Collision, public IVP_Collision_Filter_Coll_Group_Ident` |
| `PositionNode` | 9 | 41 | `src/include/common_text.h:874` | **public** | `public RDLNode` |
| `PotionHelper` | 2 | 2 | `src/include/common_systemmanager.h:992` | **public** | — |
| `Power` | 4 | 17 | `src/include/common_archetype.h:922` | **public** | `public Equipment` |
| `ProgressCB` | 1 | 1 | `src/include/common_dataconfig.h:21` | **public** | — |
| `Projectile` | 4 | 17 | `src/include/common_archetype.h:1415` | **public** | `public Equipment` |
| `RDLNode` | 32 | 32 | `src/include/common_text.h:578` | **public** | — |
| `RDLRefNode` | 9 | 41 | `src/include/common_text.h:1049` | **public** | `public RDLNode` |
| `RepairDroid` | 4 | 17 | `src/include/common_archetype.h:1284` | **public** | `public Equipment` |
| `RepairKit` | 4 | 17 | `src/include/common_archetype.h:1357` | **public** | `public Equipment` |
| `RepGroupCB` | 1 | 1 | `src/include/common_reputation.h:289` | **public** | — |
| `RichTextEditWin` | 5 | 53 | `src/freelancer/Client/rich_text_win.h:298` | private (freelancer) | `public RichTextWin, public EventHandler` |
| `RichTextWin` | 1 | 48 | `src/freelancer/Client/rich_text_win.h:220` | private (freelancer) | `public BaseRichTextWin` |
| `RoomButton` | 3 | 3 | `src/freelancer/Client/room_control_hotspot.h:30` | private (freelancer) | — |
| `Root` | 6 | 6 | `src/include/common_archetype.h:343` | **public** | — |
| `ScanList` | 1 | 1 | `src/include/common_cequip.h:1504` | **public** | — |
| `Scanner` | 4 | 17 | `src/include/common_archetype.h:1176` | **public** | `public Equipment` |
| `ServerEquip` | 2 | 3 | `src/include/server_gameobject.h:897` | **public** | `public INotify` |
| `ServerFilter` | 1 | 1 | `src/freelancer/Client/server_filter.h:16` | private (freelancer) | — |
| `Shield` | 1 | 18 | `src/include/common_archetype.h:1001` | **public** | `public AttachedEquipment` |
| `ShieldBattery` | 4 | 17 | `src/include/common_archetype.h:1378` | **public** | `public Equipment` |
| `ShieldGenerator` | 5 | 22 | `src/include/common_archetype.h:1014` | **public** | `public AttachedEquipment` |
| `Ship` | 4 | 71 | `src/include/common_archetype.h:755` | **public** | `public EqObj, public CommReferrable` |
| `Ship` | 1 | 68 | `src/include/server_gameobject.h:1060` | **public** | `public EqObj` |
| `Solar` | 4 | 71 | `src/include/common_archetype.h:715` | **public** | `public EqObj` |
| `Solar` | 2 | 69 | `src/include/server_gameobject.h:1336` | **public** | `public EqObj` |
| `SphereIntruderChecker` | 2 | 4 | `src/include/common_physys.h:622` | **public** | `public PhySys::CObjectCB` |
| `SrvGun` | 2 | 8 | `src/include/server_gameobject.h:1010` | **public** | `public SrvLauncher` |
| `SrvLauncher` | 3 | 6 | `src/include/server_gameobject.h:950` | **public** | `public ServerEquip` |
| `StartCameraParticlesAction` | 1 | 1 | `src/freelancer/Client/client_fuse.h:112` | private (freelancer) | `public ClientFuseAction` |
| `StartEffectAction` | 1 | 1 | `src/freelancer/Client/client_fuse.h:150` | private (freelancer) | `public ClientFuseAction` |
| `StartExplosionAction` | 1 | 1 | `src/freelancer/Client/client_fuse.h:81` | private (freelancer) | `public ClientFuseAction` |
| `StateMachine` | 1 | 1 | `src/include/common_net.h:452` | **public** | — |
| `StyleNode` | 9 | 41 | `src/include/common_text.h:1015` | **public** | `public RDLNode` |
| `SysObjEnumerator` | 1 | 1 | `src/include/common_server.h:381` | **public** | — |
| `TextNode` | 9 | 41 | `src/include/common_text.h:725` | **public** | `public RDLNode` |
| `TextPtrNode` | 9 | 41 | `src/include/common_text.h:764` | **public** | `public RDLNode` |
| `TextRenderContext` | 13 | 13 | `src/include/common_text.h:315` | **public** | — |
| `TextWin` | 1 | 29 | `src/freelancer/Client/text_win.h:33` | private (freelancer) | `public Win3D` |
| `Thruster` | 4 | 21 | `src/include/common_archetype.h:1065` | **public** | `public AttachedEquipment` |
| `Tractor` | 4 | 17 | `src/include/common_archetype.h:1216` | **public** | `public Equipment` |
| `TradeLaneEquip` | 1 | 14 | `src/include/common_archetype.h:1401` | **public** | `public Equipment` |
| `TRANode` | 9 | 41 | `src/include/common_text.h:681` | **public** | `public RDLNode` |
| `TumbleAction` | 1 | 1 | `src/include/server_fuse.h:252` | **public** | `public ServerFuseAction` |
| `UiFastTableRefCounter` | 1 | 1 | `src/freelancer/Client/ui_fast_table.h:65` | private (freelancer) | — |
| `VChannel` | 2 | 2 | `src/include/common_animdb.h:19` | **public** | — |
| `Win3D` | 28 | 28 | `src/freelancer/Client/win3d.h:47` | private (freelancer) | `public LinkTree<Win3D>` |
| `WinControl` | 17 | 45 | `src/freelancer/Client/wincontrol_base.h:52` | private (freelancer) | `public Win3D, public IDataViewOwner` |
| `WinFactory` | 1 | 1 | `src/freelancer/Client/win_factory.h:24` | private (freelancer) | — |
| `WireButtonControl` | 1 | 1 | `src/freelancer/Client/wire_controls.h:121` | private (freelancer) | `public WireStaticControl` |
| `XMLReader` | 3 | 3 | `src/include/common_text.h:1212` | **public** | — |
| `XMLWriter` | 2 | 2 | `src/include/common_text.h:1242` | **public** | — |
| `ZoneEnumerator` | 1 | 1 | `src/include/common_server.h:388` | **public** | — |

