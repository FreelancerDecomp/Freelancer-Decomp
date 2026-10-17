# EquippedObj: client equipment FX

For every piece of equipment, the client game object of a ship or solar (`FClientGameobj`,
`src/freelancer/Client/client_gameobj.*`, retail `Client\Space\EquippedObj.cpp`) creates a small notifier
wrapper (`ClientEquip`) and puts it into the object's list (`+0xb8`, count at `+0xbc`). These wrappers react
to equipment events (firing, a hit, switching on), are updated every frame, and play sounds, lights and
particles. This document describes the structure; function status is in decomp-tracker.
Port files: `client_equip_fx.{h,cpp}` (factory, cleanup, rebuild), `client_gameobj.*`
(list initialisation), `cli_projectile_fx.*` (firing, activation, explosion), `cli_tractor_beam.cpp`.

## Structure

- The object stores a `CEqObj*` (`+0x10`) and accepts equipment only if
  `(ObjectClass & 0x103) == 0x103`. A list node is `{Next, Prev, Wrapper*}`.
- A wrapper is a descendant of `ClientEquip`: slot 0 is the notification (`INotify`), slot 1 the deleting
  destructor, slot 2 `bool Update(float)`. The object's update calls slot 2 of every wrapper; on a `false`
  answer it calls the destructor and unlinks the node. In the port the base is `FClientEquip : INotify`,
  the node is `FEquipFxListNode`, and the per-type tables are ordinary virtual overrides.
- **Rebuild** (`EquippedObjRebuildEquipFx`): clear the list → walk all of the ship's equipment →
  `EquippedObjCreateEquipFx(id)` for each piece → scan fuse ignitions (`Fuse::Load` of prototypes through
  `ClientFuseDB`) → a final call of the object's slot 170. It is called from the ship/solar `IObjRW`
  loaders and from `SpawnPlayerShip` (slot 92 of the game object) before the player observer is installed.

## Equipment type → wrapper

The factory chooses the class by the `CEquip` type mask (`EEquipType`):

| Type | Value | Port class | Size |
|---|---:|---|---:|
| Light | `0x1` | `FCliLightEquipFx` | `0x20` |
| AttachedFX | `0x2` | `FCliAttachedFxEquipFx` | `0x20` |
| MineDropper | `0x20` | `FCliMineDropperFx` | `0x40` |
| CmDropper | `0x40` | `FCliCmDropperFx` | `0x40` |
| Gun | `0x80` | `FCliGunFx` | `0x44` |
| Shield | `0x100` | `FCliShieldEquipFx` | `0x18` |
| ShieldGenerator | `0x200` | `FCliShieldGenEquipFx` | `0x10` |
| Thruster | `0x400` | `FCliThrusterEquipFx` | `0x20` |
| CloakingDevice | `0x1000` | `FCliCloakEquipFx` | `0x10` |
| Engine | `0x20000` | `FCliEngineEquipFx` | `0x60` |
| Tractor | `0x100000` | `FCliTractorEquipFx` | `0x38` |
| RepairDroid | `0x200000` | `FCliRepairDroidEquipFx` | `0x20` |
| InternalFX | `0x400000` | `FCliInternalFxEquipFx` | `0x14` |
| TradeLane | `0x800000` | `FCliTradeLaneEquipFx` | `0x34` |

The weapon family (`FCliLauncherFx` — 0x40, the common base of mines/countermeasures/guns) has its own
slots 3–5: `TryFire`, `FireBarrelAim`, `FireDirect`; their only external caller is the firing dispatcher
`CliShipFireWeapons`, which then plays the common "single" sounds once. The gun is `0x44` (at `+0x40` the
turret voice, which no destructor frees). The shield wrapper (`CliShield`, `0x18`) is the only one besides
weapons with slots of its own: recharge and collapse removal (the generator's `collapse`/`rebuilt` sounds),
collision, and munition hit; the object's hit slots send a hit on the shield subobject
(`SubObjectID::IsShieldEquipID`, type `0x100`) to it specifically. The tradelane wrapper holds two
`AnimDB::VChannel`s that are registered every frame and rotate the `TLR_Left`/`TLR_Right` joints by the
rotation angle.

## Lights

Equipment lights enter the common effects hierarchy (`FEffectBase`), go through the shared batch of screen
quads, and are drawn by their own table `CliEquipLight` (effect bucket `0x2e`); the port has no separate
light driver. The `Light` wrapper reacts to the events `0xa/0xb/0xc` and is activated by a separate call.

## Firing chain

`CliShipFireWeapons` (the dispatcher: walks the same `+0xb8` list) → `TryFire`/`FireDirect` of the socket →
`CliLauncherFireProjectiles` → the client projectile game object (`FClientMunitionGameobj`,
`FClientBeamGameobj`, `FClientProjectileGameobj`) → effect activation
(`CliBeamActivateEffects`) → `BeamEffectStart`. The client entry of firing:
`EquippedObjHandleEquipMsg` (operations `0x18` — fire, `0x19` — equipment activation) ←
`CliShipPreprocessEquipUpdate` ← `CliShipHandleEquipUpdate`. The server side is
`IServerImpl::FireWeapon`.

## Known gaps

- Some branches need code that the port does not have: the ammo UI and the network tail of firing
  (`DAT_0067ecd0`, `FUN_005532d0`), `FUN_004c4320` (the `ObjectShip` tail of the player's equipment
  removal), flags/`BaseWatcher` in slot 170.
- The rest of the status is in the tracker (search by `EquippedObj`, `epic:fuse`, `epic:sound`).
