# Client configuration: key map, preferences, performance options, startup, fonts, cursors, cameras

This page documents the INI files that the Freelancer client (`freelancer.exe`) reads for input, the options screens, display and performance settings, start-up and login, fonts, mouse cursors and cameras. The files that end users normally edit (`UserKeyMap.ini`, `PerfOptions.ini`, the client part of `Freelancer.ini`) are described first and most carefully; window-control, HUD and lookup-table files are in [client-ui-config-windows.md](client-ui-config-windows.md), sound and miscellaneous tables in [client-audio-misc.md](client-audio-misc.md).

## File location / how it is found

All files are read through `INI_Reader` (grammar and getter semantics in [parsers.md](parsers.md)). Facts that apply to every table below:

* Section names and key names are compared **case-insensitively** (DACOM `stricmp`), except where a row says otherwise (the `PerfOptions.ini` option names are an exact `strcmp`).
* There is no default mechanism in the reader: the "Default" column is the value the destination variable holds before the file is read (static initialiser or constructor). A key that is absent keeps that value.
* "Type" names the getter the code uses: *string* = `get_value_string`, *int* = `get_value_int` (`atoi`-style), *number* = `get_value_float` (accepts `0x..`, decimal, fractions) and often converted to an integer afterwards, *bool* = `get_value_bool` (`true`/`1`/`false`/`0`; anything else logs an error and reads as false). "Flag" means the code tests `get_value_float(0) != 0`, so only numeric `0` (or an empty value) is false; the words `true`/`false` are **not** understood by flag keys.
* Where the code does not check a value, the Meaning column says *not validated*.
* Text files: a line whose first non-blank character is `;` is a comment, a trailing `; text` is cut off (so a value can never contain `;`), `[` at the start of a line begins a section, blank lines are skipped. The files shipped under `DATA\` are binary `BINI` files; the reader accepts the same content as text, so a text file with the same sections and keys works identically.
* Paths: the **Data path** is the `data path` key of `[Freelancer]` in `Freelancer.ini` (default `..\Data`, see [section 4](#4-freelancerini-client-keys)); "relative to Data" means joined to it with `GetFileSysPath` ([common_misc.cpp:935](../../src/Common/common_misc.cpp#L935)). The **user directory** is `<My Documents>\My Games\Freelancer`, created on demand by `GetUserDataPath` ([common_misc.cpp:1000](../../src/Common/common_misc.cpp#L1000)).

| File | Location | Read by | Section |
|---|---|---|---|
| `Freelancer.ini` | current directory | [startup_config.cpp](../../src/freelancer/Client/startup_config.cpp), [login_mgr.cpp](../../src/freelancer/Client/login_mgr.cpp), [dataconfig_progress.cpp](../../src/freelancer/Client/dataconfig_progress.cpp) | 4 |
| `interface\KeyMap.ini` | relative to Data | `LoadKeyMapIni` | 1 |
| `UserKeyMap.ini` | user directory | `LoadUserKeyMapIni` / `SaveUserKeyMapIni` | 1 |
| `../Data/interface/keylist.ini`, `../Data/interface/optlist.ini` | relative to the **current directory** | `NnPreferences` | 2 |
| `PerfOptions.ini` | user directory | `LoadPerfOptions` / `SavePerfOptions` | 3 |
| files named by `[Data]` `fonts`, `rich_fonts` | relative to Data | `LoadFonts`, `LoadRichFontsFromIni` | 5 |
| `mouse.ini` | relative to Data | `MouseInitCursors` | 6 |
| `cameras.ini`, per-ship cockpit `.ini` | relative to Data | `SpaceAppLoadCamerasIni`, four camera loaders | 7 |
| `[Data] intro` file | relative to Data | `LoadIntroConfigFromIni` | 8 |

---

## 1. Key map

The key map is two files with the same format: the base map `interface\KeyMap.ini` (shipped, defines every command and its default keys) and the user map `UserKeyMap.ini` (written by the in-game Key page, replaces the key bindings of the base map). Sources: [menu_keymap.cpp](../../src/freelancer/Client/menu_keymap.cpp), constants in [menu_keymap_config.h](../../src/freelancer/Client/menu_keymap_config.h), name table [menu_keymap_nicknames.inc](../../src/freelancer/Client/menu_keymap_nicknames.inc).

Loading order ([menu_keymap.cpp:530](../../src/freelancer/Client/menu_keymap.cpp#L530)): on the first request for an accelerator group the base map is loaded with `bFatal = true` (a missing base map is a fatal error, the only reader in the client that passes `bFatal = true`), then the user map is loaded on top of it. Group names requested by the code: `IDR_ALWAYS_PRESENT`, `IDR_COCKPIT`, `IDR_SPACE` (string literals in `main.cpp`, `space_camera_game.cpp`, `space_state.cpp`).

### 1.1 Header line

The **first section header** of both files must be exactly `[KeyMap=1.1]` (the version is part of the section name).

* Base map: a missing or different first header logs `KeyMap file (...) is not version 1.1` and loading **continues** ([menu_keymap.cpp:251](../../src/freelancer/Client/menu_keymap.cpp#L251)).
* User map: without the exact first header the file is ignored (load returns 0, nothing changes) ([menu_keymap.cpp:314](../../src/freelancer/Client/menu_keymap.cpp#L314)).

### 1.2 `[KeyCmd]` (repeated section)

One command with its key bindings. Every `[KeyCmd]` section of the base map is appended; sections are never merged.

| Key | Type | Default | Meaning / allowed values | Source |
|---|---|---|---|---|
| `nickname` | string | none | Command name, one of the 204 names in the [nickname table](#13-nickname-table) (case-insensitive, first match wins). A name that is not in the table resolves to `USER_NONE` and the **whole section is dropped**. A section without `nickname` is dropped as well. | [menu_keymap.cpp:149](../../src/freelancer/Client/menu_keymap.cpp#L149) |
| `ids_name` | number → int | `0` | String-resource id of the command's label on the Key page. `0` = no label (the row then shows an empty caption). Not validated. | [menu_keymap.cpp:156](../../src/freelancer/Client/menu_keymap.cpp#L156) |
| `ids_info` | number → int | `0` | String-resource id of the help text. Not validated. | [menu_keymap.cpp:161](../../src/freelancer/Client/menu_keymap.cpp#L161) |
| `state` | comma list of words | `keydown` | Which key events trigger the command. Words (case-insensitive): `keydown`, `keyup`, `repeat`, `shift_ignore`. They are OR-ed together; unknown words add nothing; an empty value or a result of zero means `keydown`. `repeat` is required for auto-repeat key-downs to fire; `shift_ignore` makes the command match whatever modifier keys are held. | [menu_keymap.cpp:165](../../src/freelancer/Client/menu_keymap.cpp#L165), [menu_keymap.cpp:209](../../src/freelancer/Client/menu_keymap.cpp#L209) |
| `key` | **repeated**; see key syntax below | none | Adds one binding to the command, in file order. Bindings that fail to parse are skipped silently. A command can hold any number of bindings from a file; the in-game Key page shows the first two (primary and alternate) and refuses to add a third. | [menu_keymap.cpp:192](../../src/freelancer/Client/menu_keymap.cpp#L192) |

Commands with the same nickname may appear several times; lookups by nickname use the **first** one.

#### Key value syntax (`key = <code>[, <modifier>]`)

Parsed by `ParseKeyValue` ([menu_keymap.cpp:87](../../src/freelancer/Client/menu_keymap.cpp#L87)); the same parser is used for the user map.

| Form of the first parameter | Result |
|---|---|
| `"c` (a double quote followed by one character) | virtual-key code = character code of the second character, **not** converted to upper case (so `"a` is code 97, not `A`) |
| starts with `0` (`0x41`, `041`, `0`) | parsed as **hexadecimal** with `sscanf("%X")`; a parse failure drops the binding |
| anything else | decimal integer (`MsvcrtAtoi`); a value outside `-8 ... 255` only logs `invalid KeyMap key value` — the binding is still stored |

Codes `0 ... 255` are Windows virtual-key codes. Negative codes are mouse events: `-1` ... `-8` = mouse button index 0 ... 7, `-9` = mouse-wheel notch ([input_manager.cpp:224](../../src/freelancer/Client/input_manager.cpp#L224), [menu_keymap.cpp:757](../../src/freelancer/Client/menu_keymap.cpp#L757)).

Second parameter (optional): a modifier word. It is matched by **prefix**, case-insensitively: `shift`, `control`, `alt`. Each recognised word *replaces* the previous modifier (only one modifier per binding survives; the last wins). Any other word (for example `ctrl`) makes the whole binding invalid and it is dropped. Internal modifier masks: Shift = 1, Control = 4, Alt = 0x10; a binding fires only when the live modifier state equals its mask exactly (unless the command has `shift_ignore`).

### 1.3 Nickname table

`[KeyCmd] nickname` values (index = internal id; `USER_NONE` = 0 means "unknown"):

```
USER_NONE, USER_KEY, USER_CHAR, USER_BUTTON, USER_WHEEL, USER_MOVE, USER_BUTTON0, USER_BUTTON1, USER_BUTTON2,
USER_BUTTON3, USER_BUTTON4, USER_BUTTON5, USER_BUTTON6, USER_BUTTON7, USER_EVENTS, USER_CANCEL,
USER_STACKABLE_CANCEL, USER_ENTER, USER_NO, USER_YES, USER_PAUSE, USER_RESET, USER_SAVE_GAME,
USER_FULLSCREEN, USER_USE_ITEM, USER_ACTIVATE_ITEM, USER_NEXT_ITEM, USER_PREV_ITEM, USER_SET_TARGET,
USER_NEXT_TARGET, USER_PREV_TARGET, USER_NEXT_WEAPON_SUBTARGET, USER_PREV_WEAPON_SUBTARGET,
USER_NEXT_SUBTARGET, USER_PREV_SUBTARGET, USER_CLEAR_TARGET, USER_NEXT_ENEMY, USER_PREV_ENEMY,
USER_CLOSEST_ENEMY, USER_ZOOM_IN, USER_ZOOM_OUT, USER_ZOOM_TOGGLE, USER_X_ROTATE, USER_X_UNROTATE,
USER_Y_ROTATE, USER_Y_UNROTATE, USER_Z_ROTATE, USER_Z_UNROTATE, USER_VIEW_RESET, USER_COCKPIT_CAMERA_MODE,
USER_LOOK_ROTATE_CAMERA_LEFT, USER_LOOK_ROTATE_CAMERA_RIGHT, USER_LOOK_ROTATE_CAMERA_UP,
USER_LOOK_ROTATE_CAMERA_DOWN, USER_LOOK_ROTATE_CAMERA_RESET, USER_CHASE_CAMERA_MODE,
USER_REARVIEW_CAMERA_MODE, USER_REARVIEW_CAMERA_MODE_OFF, USER_AFTERBURN, USER_AFTERBURN_OFF,
USER_AFTERBURN_ON, USER_TOGGLE_AUTO_AVOIDANCE, USER_TOGGLE_AUTO_LEVEL, USER_TOGGLE_LEVEL_CAMERA, USER_WARP,
USER_ENGINE_TOGGLE, USER_SET_THROTTLE, USER_INC_THROTTLE, USER_DEC_THROTTLE, USER_CRUISE, USER_THROTTLE_0,
USER_THROTTLE_10, USER_THROTTLE_20, USER_THROTTLE_25, USER_THROTTLE_30, USER_THROTTLE_40, USER_THROTTLE_50,
USER_THROTTLE_60, USER_THROTTLE_70, USER_THROTTLE_75, USER_THROTTLE_80, USER_THROTTLE_90, USER_THROTTLE_100,
USER_RADAR, USER_CONTACT_LIST, USER_RADAR_ZOOM, USER_SWITCH_TO_PLAYER_SHIP, USER_SWITCH_TO_WEAPON_LIST,
USER_SWITCH_TO_TARGET, USER_FORMATION_LIST, USER_RADAR_ZOOM_IN, USER_RADAR_ZOOM_OUT, USER_RADIO, USER_CHAT,
USER_STATUS, USER_TARGET, USER_STATUS_MODE, USER_TOGGLE_MAINFRAME, USER_NN, USER_NAV_MAP, USER_NAVMAP,
USER_PLAYER_STATS, USER_INVENTORY, USER_STORY_STAR, USER_CHAT_WINDOW, USER_GROUP_WINDOW, USER_HELP,
USER_INVENTORY_CLOSE, NN_TOGGLE_OPEN, USER_MINIMIZE_HUD, USER_DISPLAY_LAST_OBJECTIVE, USER_COLLECT_LOOT,
USER_ACTIONS, USER_MANEUVER, USER_SET_MANEUVER_DIRECTION, USER_MANEUVER_WINDOW, USER_MANEUVER_TRAIL,
USER_MANEUVER_AFTERBURNER, USER_MANEUVER_EVADE, USER_MANEUVER_ENGINEKILL, USER_MANEUVER_BRAKE_REVERSE,
USER_MANEUVER_DOCK, USER_MANEUVER_GOTO, USER_MANEUVER_FACE, USER_MANEUVER_CRUISE, USER_MANEUVER_TRADE_LANE,
USER_MANEUVER_DRASTIC_EVADE, USER_MANEUVER_FORMATION, USER_MANEUVER_STRAFE, USER_MANEUVER_TRAIL_CLOSER,
USER_MANEUVER_TRAIL_FARTHER, USER_MANEUVER_CORKSCREW_EVADE, USER_MANEUVER_SLIDE_EVADE,
USER_MANEUVER_SLIDE_EVADE_LEFT, USER_MANEUVER_SLIDE_EVADE_RIGHT, USER_MANEUVER_SLIDE_EVADE_UP,
USER_MANEUVER_SLIDE_EVADE_DOWN, USER_ACTIVATE_MANEUVER, USER_SCAN_CARGO, USER_TRACTOR_BEAM, USER_JAMMER,
USER_STEALTH, USER_CLOAK, USER_REPAIR_HEALTH, USER_REPAIR_SHIELD, USER_WEAPON_GROUP1, USER_WEAPON_GROUP2,
USER_WEAPON_GROUP3, USER_WEAPON_GROUP4, USER_WEAPON_GROUP5, USER_WEAPON_GROUP6, USER_TOGGLE_WEAPON1,
USER_TOGGLE_WEAPON2, USER_TOGGLE_WEAPON3, USER_TOGGLE_WEAPON4, USER_TOGGLE_WEAPON5, USER_TOGGLE_WEAPON6,
USER_TOGGLE_WEAPON7, USER_TOGGLE_WEAPON8, USER_TOGGLE_WEAPON9, USER_TOGGLE_WEAPON10, USER_FIRE_WEAPON1,
USER_FIRE_WEAPON2, USER_FIRE_WEAPON3, USER_FIRE_WEAPON4, USER_FIRE_WEAPON5, USER_FIRE_WEAPON6,
USER_FIRE_WEAPON7, USER_FIRE_WEAPON8, USER_FIRE_WEAPON9, USER_FIRE_WEAPON10, USER_FIRE_WEAPON_GROUP1,
USER_FIRE_WEAPON_GROUP2, USER_FIRE_WEAPON_GROUP3, USER_FIRE_WEAPON_GROUP4, USER_FIRE_WEAPON_GROUP5,
USER_FIRE_WEAPON_GROUP6, USER_ASSIGN_WEAPON_GROUP1, USER_ASSIGN_WEAPON_GROUP2, USER_ASSIGN_WEAPON_GROUP3,
USER_ASSIGN_WEAPON_GROUP4, USER_ASSIGN_WEAPON_GROUP5, USER_ASSIGN_WEAPON_GROUP6, USER_REMAPPABLE_LEFT,
USER_REMAPPABLE_RIGHT, USER_REMAPPABLE_UP, USER_REMAPPABLE_DOWN, USER_FIRE_FORWARD, USER_LAUNCH_MISSILES,
USER_LAUNCH_MINES, USER_LAUNCH_COUNTERMEASURES, USER_AUTO_TURRET, USER_LAUNCH_TORPEDOS,
USER_LAUNCH_CRUISE_DISRUPTORS, USER_TURN, USER_NEXT_OBJECT, USER_PREV_OBJECT, USER_EXIT_GAME,
USER_MANEUVER_FREE_FLIGHT, USER_FIRE_WEAPONS, USER_TURN_SHIP, USER_GROUP_INVITE, USER_TRADE_REQUEST,
USER_SCREEN_SHOT
```

### 1.4 `[KeyMap]` (repeated section, base map only)

Defines an *accelerator group*: a named set of commands that is switched on and off as a unit (see the group names in the loading paragraph). The user map has no `[KeyMap]` sections (any that appear are ignored).

| Key | Type | Default | Meaning / allowed values | Source |
|---|---|---|---|---|
| `nickname` | string (whole value) | empty | Group name, looked up case-insensitively by the code. The first group with a matching name is used. | [menu_keymap.cpp:283](../../src/freelancer/Client/menu_keymap.cpp#L283) |
| `key` | **repeated**, string | none | **A `[KeyCmd]` nickname, not a key code.** Adds that command (the first `[KeyCmd]` with this nickname) to the group; unknown names are ignored. A group registered later is consulted before older ones. | [menu_keymap.cpp:288](../../src/freelancer/Client/menu_keymap.cpp#L288) |

### 1.5 `UserKeyMap.ini` (user directory)

Written by `SaveUserKeyMapIni` ([menu_keymap.cpp:352](../../src/freelancer/Client/menu_keymap.cpp#L352)) when the player confirms the Key page; file name is `<user directory>/UserKeyMap.ini` ([menu_keymap.cpp:404](../../src/freelancer/Client/menu_keymap.cpp#L404)). Layout produced by the game:

```ini
; Freelancer - User Key Commands

[KeyMap=1.1]

[KeyCmd]
nickname = USER_FULLSCREEN
key = 13,ALT

[KeyCmd]
nickname = USER_PAUSE
key = 19
```

Only commands that still have at least one binding are written, and `key` is always written as a decimal code with an optional `,SHIFT`, `,CONTROL` or `,ALT` suffix. When the game reads the file back (`LoadUserKeyMapIni`, [menu_keymap.cpp:303](../../src/freelancer/Client/menu_keymap.cpp#L303)):

| Key | Type | Meaning | Source |
|---|---|---|---|
| `[KeyCmd]` / `nickname` | string | Selects the existing command with this nickname (first match). If the name is unknown, no command is selected and the `key` lines of that section are ignored. A later `nickname` in the same section re-selects. | [menu_keymap.cpp:333](../../src/freelancer/Client/menu_keymap.cpp#L333) |
| `[KeyCmd]` / `key` (repeated) | see key syntax | Appended to the selected command. `ids_name`, `ids_info` and `state` are **not read** from the user map. | [menu_keymap.cpp:337](../../src/freelancer/Client/menu_keymap.cpp#L337) |

Important behaviour: as soon as the first header is the correct `[KeyMap=1.1]`, **every command's bindings from the base map are erased** before the user sections are applied. A hand-written user file therefore replaces the whole key map: any command that is not listed has no key afterwards. A file that has the header but no `[KeyCmd]` section unbinds everything. A missing file (or a wrong first header) changes nothing. On the Key page, *Defaults* reloads the base map as if it were a user map, and *Cancel* reloads the user file, falling back to the base map when the user file contains no `[KeyCmd]` section ([nn_preferences.cpp:698](../../src/freelancer/Client/nn_preferences.cpp#L698)).

Keys the in-game Key page refuses to bind (virtual-key codes `0x10` Shift, `0x11` Ctrl, `0x12` Alt, `0x1B` Esc, `0x70` F1, `0x5B`/`0x5C` Windows keys, and `-1`, mouse button index 0) are only filtered by the UI; a hand-edited file is not checked ([nn_preferences.cpp:66](../../src/freelancer/Client/nn_preferences.cpp#L66)).

---

## 2. Options screen definition: `keylist.ini` and `optlist.ini`

Both files describe the rows of the in-game Options screen (class `NN_Preferences`). Both paths are hard-coded as `../Data/interface/keylist.ini` and `../Data/interface/optlist.ini`, relative to the **current directory** — they do not follow `data path` ([nn_preferences.cpp:68](../../src/freelancer/Client/nn_preferences.cpp#L68), [nn_preferences.cpp:2044](../../src/freelancer/Client/nn_preferences.cpp#L2044)). A file that cannot be opened logs `Could not open` and leaves the page empty.

### 2.1 `keylist.ini`

Read by `NnPreferences::LoadKeyList` ([nn_preferences.cpp:1577](../../src/freelancer/Client/nn_preferences.cpp#L1577)): the three row lists are cleared, then the file is read.

| Section | Key | Type | Default | Meaning / allowed values | Source |
|---|---|---|---|---|---|
| `[Group]` | `group_num` | int | `-1` | Group slot the following rows belong to. The code assumes `0 ... 2`; not validated. Group `1` gets a fixed caption row first. | [nn_preferences.cpp:1598](../../src/freelancer/Client/nn_preferences.cpp#L1598) |
| `[Group]` | `name` | int | none | String-resource id of the group's tab caption, stored in the slot given by `group_num` (so `group_num` must come first). | [nn_preferences.cpp:1600](../../src/freelancer/Client/nn_preferences.cpp#L1600) |
| `[Key]` (repeated, one row each) | `id` | string | `-1` | A `[KeyCmd]` nickname from the key map, or the text `-1` for an empty spacer row. A name not found in the key map leaves the row empty. The row is added to the group set by the most recent `[Group]`. | [nn_preferences.cpp:1623](../../src/freelancer/Client/nn_preferences.cpp#L1623) |
| `[Key]` | `name` | int | none | Read and discarded (the label comes from the key map's `ids_name`). | [nn_preferences.cpp:1637](../../src/freelancer/Client/nn_preferences.cpp#L1637) |

Rows are only added when the group number is `0 ... 3`. The row of the command with internal id `0x14` (`USER_PAUSE`) is shown non-editable when it has a label (see `AddKeyRow`, [nn_preferences.cpp:1648](../../src/freelancer/Client/nn_preferences.cpp#L1648)).

### 2.2 `optlist.ini`

Read by `NnPreferences::LoadOptList` ([nn_preferences.cpp:1886](../../src/freelancer/Client/nn_preferences.cpp#L1886)); also read once in "seed mode" on a first run to compute default performance settings (`SeedGraphicsDefaults`, [nn_preferences.cpp:2102](../../src/freelancer/Client/nn_preferences.cpp#L2102)). Three sections, each a list of `option` lines:

| Section | Page |
|---|---|
| `[Interface]` | Interface page |
| `[Video]` | Video page |
| `[Audio]` | Audio page |

Each section is padded with blank rows to 15 rows when it has 14 or fewer `option` lines; a page shows 14 rows. Keys other than `option` are ignored.

`option` (repeated, one row each) takes **positional** parameters ([nn_preferences.cpp:1822](../../src/freelancer/Client/nn_preferences.cpp#L1822)):

| Param | Type | Default | Meaning / allowed values |
|---|---|---|---|
| 0 | int | `-1` | String-resource id of the row caption. It doubles as the option's identity: it is matched against the *IDS* column of the [performance options table](#32-performance-option-keys) to find the setting the row controls. Two ids are special: `0x536` (screen resolution selector) and `0x767` (graphics preset slider). |
| 1 | int | `-1` | String-resource id of the rollover help text. |
| 2 | int | `-1` | Default value in percent (`0 ... 100`; `-1` = none). When seeding, the matching performance option gets the value `Default * 0.01`. Not validated. |
| 3 | bool | `0` | `1` = slider, `0` = on/off toggle. |
| 4 | bool | `0` | `1` = the row is greyed out while the options screen is opened in flight ("not changeable in game"). |
| 5 | int | `-1` | Preset range lower bound (only used for rows driven by the graphics preset, id `0x767`). |
| 6 | int | `-1` | Preset range upper bound. |

A line with a **single** parameter is a section header row (caption only). Parameters 5 and 6 are read only when the line has more than five parameters. Preset behaviour ([nn_preferences.cpp:2192](../../src/freelancer/Client/nn_preferences.cpp#L2192)): for a row with a preset range, a slider is set to `0` when the preset value is `<= min`, to `100` when `>= max`, and linearly in between; a toggle is switched on when `min <= value <= max`.

---

## 3. `PerfOptions.ini` (user directory)

Stores the player's graphics, sound and gameplay-comfort settings. Read once at engine start (`LoadPerfOptions`, [perf_options.cpp:494](../../src/freelancer/Client/perf_options.cpp#L494), called from [engine_init.cpp:748](../../src/freelancer/Client/engine_init.cpp#L748)) and rewritten by `SavePerfOptions` ([perf_options.cpp:607](../../src/freelancer/Client/perf_options.cpp#L607)) at start-up and whenever the Options screen is left or confirmed. Path: `<user directory>\PerfOptions.ini`.

The file is trusted only as a whole. It is rejected — all options revert to defaults and the first-run seeding (`optlist.ini` defaults plus the hardware probe) runs again — when:

* `[PerfVersion] version` is missing or `< 5.0`;
* a recognised `[PerfOptions]` value is negative;
* `[PerfCount] count` does not equal the number of **recognised** keys found in `[PerfOptions]`;
* the file cannot be opened.

Section order matters: `[PerfVersion]` must come **before** `[PerfOptions]` (otherwise the version is still unset, the `[PerfOptions]` section stops the scan and the file is rejected), and `[PerfCount]` anywhere before the end of the file. Example (synthetic):

```ini
[PerfVersion]
version=5
SkipMachineWarnings=FALSE

[PerfCount]
count=36

[PerfOptions]
mouse_speed = 0.28
audio_music = 0.80
; ... all 36 options ...

[Display]
size= 1024, 768
color_bpp= 32
depth_bpp= 24
```

### 3.1 Sections

| Section | Key | Type | Default | Meaning / allowed values | Source |
|---|---|---|---|---|---|
| `[PerfVersion]` | `version` | number | unset (= rejected) | File format version; accepted when `>= 5`. The game writes `5`. | [perf_options.cpp:525](../../src/freelancer/Client/perf_options.cpp#L525) |
| `[PerfVersion]` | `SkipMachineWarnings` | bool | `false` | `true`/`1`/`false`/`0`. `true` suppresses the low-machine-specification warning dialog at start-up. The game writes `TRUE`/`FALSE`. | [perf_options.cpp:527](../../src/freelancer/Client/perf_options.cpp#L527) |
| `[PerfCount]` | `count` | number → int | `0` | Must equal the number of recognised option keys in `[PerfOptions]` (the game writes 36). | [perf_options.cpp:540](../../src/freelancer/Client/perf_options.cpp#L540) |
| `[PerfOptions]` | one key per option (below) | number | `0` until applied | One floating-point value per option, normally `0.0 ... 1.0`. **Option names are matched case-sensitively** and exactly. Unknown names are ignored and not counted. A negative value rejects the file. Repeated keys: each recognised occurrence counts towards `count`; the last value wins. Upper bounds are not validated. | [perf_options.cpp:558](../../src/freelancer/Client/perf_options.cpp#L558) |
| `[Display]` | `size` | int, int | `0, 0` (auto) | Window/screen width and height in pixels, two comma-separated parameters. | [perf_options.cpp:581](../../src/freelancer/Client/perf_options.cpp#L581) |
| `[Display]` | `color_bpp` | int | `-1` | Colour depth in bits (the game writes `16`/`32`); `-1` = automatic. Not validated. | [perf_options.cpp:586](../../src/freelancer/Client/perf_options.cpp#L586) |
| `[Display]` | `depth_bpp` | int | `-1` | Depth-buffer bits; `-1` = automatic. Not validated. | [perf_options.cpp:588](../../src/freelancer/Client/perf_options.cpp#L588) |

The defaults of `[Display]` are the values `InitializeEngine` sets before loading ([engine_init.cpp:731](../../src/freelancer/Client/engine_init.cpp#L731)). Command-line display switches are applied after the file ([startup_config.cpp:195](../../src/freelancer/Client/startup_config.cpp#L195)).

### 3.2 Performance option keys

The `IDS` column is the string-resource id that `optlist.ini` rows use to refer to the option (`0` = no Options-screen row). "Applied by" describes what the value does as implemented; the registry is [perf_options.cpp:298](../../src/freelancer/Client/perf_options.cpp#L298). Written back only for options that have been applied at least once; written with two decimals (`name = 0.50`).

| Key | IDS | Applied by / meaning |
|---|---|---|
| `mouse_speed` | 0x5dd | Clamped to `0 ... 1`, mouse speed factor `v * 1.75 + 0.25` ([input_manager.cpp:1209](../../src/freelancer/Client/input_manager.cpp#L1209)) |
| `gamma_ramp` | 0x534 | Values `>= 0`: render gamma = `v + 1.0`; negative values do nothing ([engine_init.cpp:496](../../src/freelancer/Client/engine_init.cpp#L496)) |
| `DitherControl` | 0x67f | Dithering on when `v >= 0.3` ([engine_init.cpp:507](../../src/freelancer/Client/engine_init.cpp#L507)) |
| `MipFilterControlCallback` | – | Sets the mip-filter flag to 1 whatever the value ([engine_init.cpp:516](../../src/freelancer/Client/engine_init.cpp#L516)) |
| `TurnSensitivity` | 0x614 | Passed to the player ship's behaviour manager (`set_turn_sensitivity`) ([perf_options.cpp:169](../../src/freelancer/Client/perf_options.cpp#L169)) |
| `generic_effect_perf` | 0x5cb | Stored as `1.0 - v`; effect types whose `priority` is `>=` this level are drawn (see `[EffectType]` in [client-audio-misc.md](client-audio-misc.md#4-effect-types-and-lod-effecttype-effectlod)) |
| `Nebula` | 0x57d | Global nebula detail (`NebulaApplyGlobalDetail`, [cli_nebula.cpp](../../src/freelancer/Client/cli_nebula.cpp)) |
| `SunEffect` | 0x5ca | `min(1, v * 1.33333)` scales the detail of every star's sun body ([perf_options.cpp:127](../../src/freelancer/Client/perf_options.cpp#L127)) |
| `Asteroids` | 0x57c | Global asteroid-field detail (`CliAsteroidApplyGlobalDetail`, [cli_asteroid.cpp](../../src/freelancer/Client/cli_asteroid.cpp)) |
| `space_dust_detail` | 0x57a | Stored unchanged (default `1.0`) as the space-dust detail |
| `star_sphere` | 0x57b | Star-sphere detail (`StarSphereApplyDetail`, [star_sphere.cpp](../../src/freelancer/Client/star_sphere.cpp)) |
| `AtmosphereMaterialConfig` | 0x5c9 | Material-library global record 1 is enabled when `v > 0.999` |
| `HUDAnimConfig` | 0x65e | Material-library global record 4 is enabled when `v > 0.999` |
| `level_of_detail` | 0x5d5 | `v < 0.75` selects the "low" mode; the LOD scale is `v * 2/3 + 0.5` (low) or `(v - 0.75) * 4 + 1` ([perf_options.cpp:252](../../src/freelancer/Client/perf_options.cpp#L252)) |
| `use_environment_maps` | 0x657 | Kept only when `v > 0` **and** the video device reports environment-map ability; otherwise forced to `0` |
| `high_detail_textures` | 0x658 | Thresholds on `v`: `> 0.6` (and enough video memory) high-detail textures, `> 0.5` pipeline state 4, `> 0.4` / `> 0.3` material-library records 0 / 2 ([perf_options.cpp:201](../../src/freelancer/Client/perf_options.cpp#L201)) |
| `sound_max_dist_scale` | 0x77a | Stored (sound distance scale slot) |
| `sound_level_of_detail` | 0x77b | Sound LOD level `floor(2 * (1 - v))` |
| `USE_3D_SOUNDS` | 0x77e | Stored (3D-sound slot) |
| `audio_music` | 0x539 | Music volume (`SetClientAudioMusicVolume`) |
| `audio_ambient` | 0x584 | Ambient volume |
| `audio_sfx` | 0x538 | Effects volume |
| `audio_interface` | 0x583 | Interface-sound volume |
| `audio_voice` | 0x581 | Voice volume |
| `rtc_performance` | 0x656 | Passed to `RtcSlider::set_slider` (real-time-cutscene performance) |
| `general_graphics` | – | Stored as an integer (`ftol`); follows the graphics preset slider |
| `advanced_graphics` | – | Flag: `v != 0` |
| `default_graphics` | – | Stored as an integer (`ftol`); machine-probe result (see below) |
| `skip_transition_scripts` | 0x5d7 | Latches skipping of transition script sequences |
| `enable_tooltips` | 0x578 | `v > 0.5` (default on) |
| `enable_rollover` | 0x579 | `v > 0.5` (default on) |
| `cockpit` | 0x5da | `v > 0.5` shows the cockpit view |
| `USE_DYNAMIC_COMM_HEAD` | 0x631 | Stored (animated comm-head slot) |
| `DIFFICULTY_SCALE` | 0x57f | Stored (difficulty slot) |
| `autohide_maneuver_bar` | 0x613 | `v > 0.5` |
| `max_submitted_sounds` | 0x687 | Voice budget `int(v * 100 + 0.5)` (default 32) |

First-run seeding: `default_graphics` is set from `ProbeMachineGraphicsLevel` ([perf_options.cpp:353](../../src/freelancer/Client/perf_options.cpp#L353)), a score in `{0.25, 0.5, 0.6, 0.75, 1.0}` derived from hardware transform-and-light support, video memory and CPU speed; the other options then follow from the `optlist.ini` percentages ([nn_preferences.cpp:2102](../../src/freelancer/Client/nn_preferences.cpp#L2102)).

---

## 4. `Freelancer.ini` client keys

`Freelancer.ini` is opened from the **current directory** by name; there is no command-line override of the path ([startup_config.cpp:44](../../src/freelancer/Client/startup_config.cpp#L44)). The same file also carries server, resource and data sections documented in [freelancer-ini.md](freelancer-ini.md); this section lists only what the client's start-up code reads. The client reads the file at least three times, in this order: `LoadStartupConfig`, `CLoginMgr::LoadConfig` (both in `WinMain`), then `LoadClientIniData` during engine initialisation.

### 4.1 Start-up (`LoadStartupConfig`, [startup_config.cpp:36](../../src/freelancer/Client/startup_config.cpp#L36))

| Section | Key | Type | Default | Meaning / allowed values | Source |
|---|---|---|---|---|---|
| `[Freelancer]` | `data path` | string | `..\Data` | Root of all game data. Truncated to 259 characters. | [startup_config.cpp:79](../../src/freelancer/Client/startup_config.cpp#L79) |
| `[Startup]` | `movie_file` | string, **repeated, at most 3** | none | Start-up movies, played in file order. A fourth and later `movie_file` is ignored; the counter runs across all `[Startup]` sections. | [startup_config.cpp:104](../../src/freelancer/Client/startup_config.cpp#L104) |
| `[Data]` | `gate_tunnels` | string | empty | File with the jump-tunnel layer definitions (`[gate_tunnel]`, see [client-world-fx.md](client-world-fx.md)). | [startup_config.cpp:128](../../src/freelancer/Client/startup_config.cpp#L128) |
| `[Data]` | `jump_effect` | string | empty | File with the jump gate / jump ship effects (see [client-world-fx.md](client-world-fx.md)). | [startup_config.cpp:142](../../src/freelancer/Client/startup_config.cpp#L142) |

Command-line tokens `+Q`/`-Q` (quick start) and `+X`/`-X` (floating-point exception trap) are read in the same function; they are not INI keys.

### 4.2 Login and list server (`CLoginMgr::LoadConfig`, [login_mgr.cpp:1512](../../src/freelancer/Client/login_mgr.cpp#L1512))

| Section | Key | Type | Default | Meaning / allowed values | Source |
|---|---|---|---|---|---|
| `[Freelancer]` | `local_server` | string | `Server.dll` | Module name of the in-process single-player server. Truncated to 31 characters. Not validated; a bad name fails when the module is loaded. | [login_mgr.cpp:1530](../../src/freelancer/Client/login_mgr.cpp#L1530) |
| `[Freelancer]` | `AppGUID` | string | none | Application GUID in the braced text form `{xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx}`, parsed by `ParseGUID`; applied to the network client only if the key is present. | [login_mgr.cpp:1545](../../src/freelancer/Client/login_mgr.cpp#L1545) |
| `[ListServer]` | `hostname` | string | `FLListServer2` | Host name or IP of the multiplayer list server. | [login_mgr.cpp:1556](../../src/freelancer/Client/login_mgr.cpp#L1556) |
| `[ListServer]` | `port` | number → unsigned | `28810` (0x708a) | TCP/UDP port of the list server. Not validated. | [login_mgr.cpp:1560](../../src/freelancer/Client/login_mgr.cpp#L1560) |
| `[Login]` | `name` | wide string | `SinglePlayer` | Account name used for the login. | [login_mgr.cpp:1572](../../src/freelancer/Client/login_mgr.cpp#L1572) |
| `[Login]` | `pwd` | wide string | empty | Account password in clear text. | [login_mgr.cpp:1576](../../src/freelancer/Client/login_mgr.cpp#L1576) |
| `[Login]` | `char` | wide string | empty | Name of the character to select automatically. | [login_mgr.cpp:1580](../../src/freelancer/Client/login_mgr.cpp#L1580) |

`[Login]` is not present in the stock `Freelancer.ini`; it exists for auto-login. If the file cannot be read at all, `LoadConfig` returns silently and every default stays.

### 4.3 Data file loads

Read by `LoadClientIniData` ([dataconfig_progress.cpp:177](../../src/freelancer/Client/dataconfig_progress.cpp#L177)). For each `[Data]` line below the value is a path. All of them except `fonts`, `rich_fonts` and `intro` (resolved by their own loader) are resolved with `GetFileSysPath(value, Data root)` first. Each key is read once per occurrence; **repeating a key loads another file** into the same registry (they accumulate). Other `[Data]` keys (archetypes, universe, ...) are handled by other modules ([freelancer-ini.md](freelancer-ini.md)).

| Section | Key | Loader | Format documented in |
|---|---|---|---|
| `[Data]` | `fonts` | `LoadFonts` | [section 5.1](#51-fonts-file-truetype) |
| `[Data]` | `rich_fonts` | `LoadRichFontsFromIni` | [section 5.2](#52-rich-fonts-file) |
| `[Data]` | `sounds` | `LoadClientSoundArchIni` | [client-audio-misc.md](client-audio-misc.md#1-sound-archetypes-sound) |
| `[Data]` | `voices` | `LoadClientVoicesIni` | [client-audio-misc.md](client-audio-misc.md#2-voice-profiles-voice-sound) |
| `[Data]` | `effects` | `EffectArchLoadIni` | [client-world-fx.md](client-world-fx.md); `[EffectType]`/`[EffectLOD]` in [client-audio-misc.md](client-audio-misc.md#4-effect-types-and-lod-effecttype-effectlod) |
| `[Data]` | `effect_shapes` | queued, loaded at the end of engine start-up as a shape DB | [client-audio-misc.md](client-audio-misc.md#5-shape-databases-texture-and-shape) |
| `[Data]` | `debris` | `LoadDebrisArchFile` | [client-audio-misc.md](client-audio-misc.md#6-debris-debris) |
| `[Data]` | `intro` | `LoadIntroConfigFromIni` | [section 8](#8-intro-screen-configuration-data-intro) |
| `[Data]` | `HUD` | path stored; elements created when the player enters space | [client-ui-config-windows.md](client-ui-config-windows.md#4-hudini) |
| `[Data]` | `igraph` | `GraphLibraryLoad` | [client-audio-misc.md](client-audio-misc.md#8-graph-library-igraph) |
| `[Resources]` | `DLL` (repeated) | `LoadLibraryA` of each resource DLL, in order | [freelancer-ini.md](freelancer-ini.md) |

For `[Resources] DLL` a failed `LoadLibrary` logs an error but the slot is kept in the string-resource module list, so the ids of later DLLs keep their index. After the sections are read, `audio\SoundCFG.ini` (relative to Data) is loaded unconditionally if `Freelancer.ini` opened ([client-audio-misc.md](client-audio-misc.md#3-soundcfgini)).

---

## 5. Fonts

### 5.1 Fonts file (`[TrueType]`)

`[Data] fonts = ...` (for example `fonts\fonts.ini`). Loaded by `LoadFonts` ([font_imp.cpp:578](../../src/freelancer/Client/font_imp.cpp#L578)). One `[TrueType]` section per named font (other sections ignored).

| Key | Type | Default | Meaning / allowed values | Source |
|---|---|---|---|---|
| `nickname` | string (≤127 chars) | empty | Name that UI definitions use in their `font = ...` keys; looked up case-insensitively, **first** font with the name wins ([font_imp.cpp:669](../../src/freelancer/Client/font_imp.cpp#L669)). | [font_imp.cpp:604](../../src/freelancer/Client/font_imp.cpp#L604) |
| `font` | string (≤127 chars) | empty | GDI face name. | [font_imp.cpp:617](../../src/freelancer/Client/font_imp.cpp#L617) |
| `fixed_height` | number | `0.0` | Font height; the font is created non-bold, non-italic, non-underlined. Meaning of `0` is up to the font code; not validated. | [font_imp.cpp:630](../../src/freelancer/Client/font_imp.cpp#L630) |

A font that cannot be created logs `failed to create FONT font <nickname>` and is skipped from the count; loading continues.

### 5.2 Rich fonts file

`[Data] rich_fonts = ...`. Loaded by `LoadRichFontsFromIni` ([menu_fonts_load.cpp:46](../../src/freelancer/Client/menu_fonts_load.cpp#L46)); defines the 32 numbered fonts and the text styles used by rich-text windows.

`[TrueType]` (repeated; each `font` line defines one numbered font):

| Key | Type | Default | Meaning / allowed values | Source |
|---|---|---|---|---|
| `font` (repeated) | int, string, int | none | `font = <index>, <face name>, <size>`. Index `0 ... 31` is accepted; larger indices are silently ignored. Size is an integer. Always non-bold, non-italic, non-underlined. | [menu_fonts_load.cpp:79](../../src/freelancer/Client/menu_fonts_load.cpp#L79) |

`[Style]` (repeated; one text style per section; the style is registered at the end of the section):

| Key | Type | Default | Meaning / allowed values | Source |
|---|---|---|---|---|
| `name` | string | previous section's id, or `0` for the first | Style name; its **16-bit hash** is the style id (the name itself is not kept). A `[Style]` without `name` is registered under the id of the previous style (or `0`), replacing it. | [menu_fonts_load.cpp:119](../../src/freelancer/Client/menu_fonts_load.cpp#L119) |
| `font` | int | not set | Font index (into the `[TrueType]` table, `0 ... 31`; stored in 5 bits, not validated). | [menu_fonts_load.cpp:123](../../src/freelancer/Client/menu_fonts_load.cpp#L123) |
| `bold` | bool | not set | `true`/`1`/`false`/`0`. | [menu_fonts_load.cpp:134](../../src/freelancer/Client/menu_fonts_load.cpp#L134) |
| `italic` | bool | not set | as above. | [menu_fonts_load.cpp:143](../../src/freelancer/Client/menu_fonts_load.cpp#L143) |
| `underline` | bool | not set | as above. | [menu_fonts_load.cpp:152](../../src/freelancer/Client/menu_fonts_load.cpp#L152) |
| `color` | int, int, int | not set | Red, green, blue, each truncated to a byte (`0 ... 255`). Alpha is not part of the style. | [menu_fonts_load.cpp:161](../../src/freelancer/Client/menu_fonts_load.cpp#L161) |
| `justify` | string | not set | `left`, `center` or `right` (case-insensitive); any other word is ignored. May appear more than once; each valid occurrence adds a justification setting. | [menu_fonts_load.cpp:170](../../src/freelancer/Client/menu_fonts_load.cpp#L170) |

"Not set" means the attribute is not overridden by the style, so the text keeps whatever the surrounding style specifies.

---

## 6. Mouse cursors: `mouse.ini`

Fixed name `mouse.ini`, relative to Data. The same file is loaded twice: as a shape database (`[Texture]`/`[Shape]` sections, see [client-audio-misc.md](client-audio-misc.md#5-shape-databases-texture-and-shape)) and by `LoadCursors` ([input_manager.cpp:834](../../src/freelancer/Client/input_manager.cpp#L834)) for the `[Cursor]` sections. At most **20** cursors are kept; further `[Cursor]` sections are parsed but not stored. Cursor names are looked up case-insensitively; the code asks for `DEFAULT` (the cursor most recently set as default), `Arrow`, `Cross`, `Stretch`, `fire`, `fire_friendly`, `fire_neutral`, `friendly`, `hostile`, `neutral` ([input_manager.cpp:1142](../../src/freelancer/Client/input_manager.cpp#L1142)). A cursor that does not exist logs `missing cursor`.

`[Cursor]` (repeated). Keys that come **before** `nickname` in a section are skipped, and a section without `nickname` is dropped, so write `nickname` first.

| Key | Type | Default | Meaning / allowed values | Source |
|---|---|---|---|---|
| `nickname` | string | none | Cursor name. | [input_manager.cpp:862](../../src/freelancer/Client/input_manager.cpp#L862) |
| `shape` | string (≤23 chars) | empty | Name of a shape from the `[Shape]` entries of the same file. | [input_manager.cpp:887](../../src/freelancer/Client/input_manager.cpp#L887) |
| `hotspot` | number, number | `0, 0` | Offset of the click point in pixels. | [input_manager.cpp:901](../../src/freelancer/Client/input_manager.cpp#L901) |
| `blend` | number → int | `1` | Blend mode: `0` = alpha blending off; `1 ... 5` choose one of five source/destination blend-factor pairs; any other value uses the default pair. | [input_manager.cpp:907](../../src/freelancer/Client/input_manager.cpp#L907), [input_manager.cpp:778](../../src/freelancer/Client/input_manager.cpp#L778) |
| `color` | number ×4 | `255,255,255,255` | R, G, B, A, each clamped to `0 ... 255`; an alpha of `0` is replaced by `255`. | [input_manager.cpp:911](../../src/freelancer/Client/input_manager.cpp#L911) |
| `anim` | string, number, number | none | `anim = <format>, <frames>, <fps>`: animated cursor. The format is a `printf`-style shape-name template taking the frame number; the shape is replaced each frame. The frame index is `int(time * fps) % frames`; an `fps` of `0` freezes it on frame 0. Not validated (a frame count of `0` with a non-zero fps divides by zero). | [input_manager.cpp:924](../../src/freelancer/Client/input_manager.cpp#L924), [input_manager.cpp:625](../../src/freelancer/Client/input_manager.cpp#L625) |
| `angle` | number | `0` | Initial rotation in radians. | [input_manager.cpp:939](../../src/freelancer/Client/input_manager.cpp#L939) |
| `spin` | number | `0` | Rotation speed in radians per second; when non-zero the angle becomes `time * spin`. | [input_manager.cpp:943](../../src/freelancer/Client/input_manager.cpp#L943) |
| `scale` | number | `1.0` | Size multiplier. | [input_manager.cpp:947](../../src/freelancer/Client/input_manager.cpp#L947) |

---

## 7. Cameras and cockpits

### 7.1 `cameras.ini`

Fixed name `cameras.ini` ([space_state.cpp:552](../../src/freelancer/Client/space_state.cpp#L552)), resolved with `GetFullFilename` (Data root). Each section names one camera and each key is passed to that camera's property setter as a raw string.

| Section | Camera |
|---|---|
| `[WinCamera]` | the user-interface (window) camera |
| `[CockpitCamera]` | first-person cockpit camera |
| `[ThirdPersonCamera]` | chase camera |
| `[TurretCamera]` | turret camera |
| `[RearViewCamera]` | rear-view camera |
| `[DeathCamera]` | camera used after the player's ship is destroyed |

An unknown section name logs `Unknown camera type?` and the section is skipped. Every key goes to `SetProperty(name, value)`; a key that is not recognised logs `unknown property` and loading continues. Recognised keys (the same four for every camera; only the first parameter of the value is used and it is converted with `atof`, [space_camera.cpp:162](../../src/freelancer/Client/space_camera.cpp#L162)):

| Key | Type | Default | Meaning / allowed values | Source |
|---|---|---|---|---|
| `fovx` | number | camera-specific | **Full** horizontal field of view in degrees; the code halves it. Recomputes the vertical field for the current aspect ratio. | [space_camera.cpp:164](../../src/freelancer/Client/space_camera.cpp#L164) |
| `fovy` | number | camera-specific | **Full** vertical field of view in degrees (halved by the code); the horizontal angle follows from the aspect ratio. | [space_camera.cpp:189](../../src/freelancer/Client/space_camera.cpp#L189) |
| `znear` | number | camera-specific | Near clipping distance. | [space_camera.cpp:216](../../src/freelancer/Client/space_camera.cpp#L216) |
| `zfar` | number | camera-specific | Far clipping distance. | [space_camera.cpp:221](../../src/freelancer/Client/space_camera.cpp#L221) |

Not validated. The game cameras (`CockpitCamera`, `ThirdPersonCamera`, `TurretCamera`, `RearViewCamera`, `DeathCamera`) additionally remember the field of view set by `fovx`/`fovy` as their resting angle ([space_camera_game.cpp:282](../../src/freelancer/Client/space_camera_game.cpp#L282)).

### 7.2 Cockpit files

The cockpit file name comes from the ship archetype: the client-side ship key `cockpit = <path>` ([client_cliobj.cpp:2459](../../src/freelancer/Client/client_cliobj.cpp#L2459), see [archetypes-and-equipment.md](archetypes-and-equipment.md)), resolved relative to Data. When the ship has no cockpit, the game logs `ship has no cockpit definition` and uses `cockpits\cockpit.ini` ([cockpit_load.cpp:108](../../src/freelancer/Client/cockpit_load.cpp#L108)). The one file is read by four loaders in turn; each takes its own section and ignores the others.

**First person** — `FirstPersonCameraLoad` ([space_camera_first.cpp:585](../../src/freelancer/Client/space_camera_first.cpp#L585)). A missing file logs a warning but loading goes on. A section `[Camera]` logs `no longer supported`.

| Section | Key | Type | Default | Meaning / allowed values | Source |
|---|---|---|---|---|---|
| `[Cockpit]` | `mesh` | string | empty | Cockpit model. An empty value loads no mesh. If the model contains a part whose name contains `cam`, that part supplies the head position and orientation. | [space_camera_first.cpp:612](../../src/freelancer/Client/space_camera_first.cpp#L612) |
| `[Cockpit]` | `material_library` | string | empty | Material library for the cockpit mesh. | [space_camera_first.cpp:627](../../src/freelancer/Client/space_camera_first.cpp#L627) |
| `[Cockpit]` | `head_turn` | number, number | `30, 25` | Maximum head turn: horizontal and vertical angle (degrees; not validated). These two values are global to the cockpit camera. | [space_camera_first.cpp:642](../../src/freelancer/Client/space_camera_first.cpp#L642) |

After loading, the field of view of the cockpit camera must be 4:3 (`tan(half fovx) / tan(half fovy)` within 0.05 of 1.333) or `cockpit aspect ... is not PC-compatible?` is logged (warning only).

**Third person** — `ThirdPersonCameraLoad` ([space_camera_third.cpp:941](../../src/freelancer/Client/space_camera_third.cpp#L941)). No key is read. A `[ThirdPerson]` section logs `obsolete`; the chase-camera parameters come from the ship archetype (section 7.3). The same 4:3 check applies.

**Turret** — `TurretCameraLoad` ([space_camera_turret.cpp:354](../../src/freelancer/Client/space_camera_turret.cpp#L354)). `[Turret]` logs `obsolete`.

| Section | Key | Type | Default | Meaning / allowed values | Source |
|---|---|---|---|---|---|
| `[TurretCamera]` | `tether` | number ×3 | `0, 5, 25` | Camera offset from the turret mount (x, y, z). | [space_camera_turret.cpp:388](../../src/freelancer/Client/space_camera_turret.cpp#L388) |
| `[TurretCamera]` | `yaw_rotate_speed` | number | `2.0` | Yaw rotation speed. | [space_camera_turret.cpp:398](../../src/freelancer/Client/space_camera_turret.cpp#L398) |
| `[TurretCamera]` | `pitch_rotate_speed` | number | `1.5` | Pitch rotation speed. | [space_camera_turret.cpp:402](../../src/freelancer/Client/space_camera_turret.cpp#L402) |
| `[TurretCamera]` | `accel_speed` | number | `5.0` | Smoothing/acceleration rate of the camera follow. | [space_camera_turret.cpp:406](../../src/freelancer/Client/space_camera_turret.cpp#L406) |

**Rear view** — `RearViewCameraLoad` ([space_camera_rear.cpp:137](../../src/freelancer/Client/space_camera_rear.cpp#L137)). `[RearView]` logs `obsolete`.

| Section | Key | Type | Default | Meaning / allowed values | Source |
|---|---|---|---|---|---|
| `[RearViewCamera]` | `view_position` | number ×3 | `0, 5, 20` | Camera position behind the ship (x, y, z). A missing component reads as `0`. | [space_camera_rear.cpp:171](../../src/freelancer/Client/space_camera_rear.cpp#L171) |

### 7.3 Ship archetype camera keys

The chase (third-person) camera is tuned per ship in the ship archetype (`shiparch.ini` entries, read by the client's `FCliShip::ReadUnknown`, [client_cliobj.cpp:2440](../../src/freelancer/Client/client_cliobj.cpp#L2440)). The angle clamp is in [ship_camera_params.cpp:23](../../src/freelancer/Client/ship_camera_params.cpp#L23) (the helper `ShipCameraParamsReadValue` in the same file has no caller; the live parser is `client_cliobj.cpp`).

| Key | Type | Default | Meaning / allowed values | Source |
|---|---|---|---|---|
| `cockpit` | string | empty | Cockpit file for the ship (section 7.2). | [client_cliobj.cpp:2459](../../src/freelancer/Client/client_cliobj.cpp#L2459) |
| `camera_offset` | number, number | `0, 0` | Two parameters, stored as `camera_offset[0]`, `camera_offset[1]`; the chase camera builds its look offset as `{0, camera_offset[0], camera_offset[1]}` (y and z). Not validated. | [client_cliobj.cpp:2471](../../src/freelancer/Client/client_cliobj.cpp#L2471), [space_camera_third.h:63](../../src/freelancer/Client/space_camera_third.h#L63) |
| `camera_angular_slerp_multiplier` | number | `0.04` | Orientation-follow multiplier. | [client_cliobj.cpp:2478](../../src/freelancer/Client/client_cliobj.cpp#L2478) |
| `camera_angular_acceleration` | number | `0.07` | Orientation-follow acceleration. | [client_cliobj.cpp:2484](../../src/freelancer/Client/client_cliobj.cpp#L2484) |
| `camera_horizontal_turn_angle` | number (degrees) | `0.2` (stored value, radians) | Clamped to `0 ... 90` (a negative value becomes `0`, above 90 becomes `90`), then converted to radians. | [client_cliobj.cpp:2490](../../src/freelancer/Client/client_cliobj.cpp#L2490), [ship_camera_params.cpp:23](../../src/freelancer/Client/ship_camera_params.cpp#L23) |
| `camera_vertical_turn_up_angle` | number (degrees) | `0.2` (stored value, radians) | Same clamp and conversion. | [client_cliobj.cpp:2496](../../src/freelancer/Client/client_cliobj.cpp#L2496) |
| `camera_vertical_turn_down_angle` | number (degrees) | `0.2` (stored value, radians) | Same clamp and conversion. | [client_cliobj.cpp:2502](../../src/freelancer/Client/client_cliobj.cpp#L2502) |
| `camera_turn_look_ahead_slerp_amount` | number | `0.5` | Stored unchanged. | [client_cliobj.cpp:2508](../../src/freelancer/Client/client_cliobj.cpp#L2508) |

Defaults are the values set by the `FCliShip` constructor ([client_cliobj.cpp:2416](../../src/freelancer/Client/client_cliobj.cpp#L2416)). The other client ship keys read in the same function (`bay_doors_open_snd`, `bay_doors_close_snd`, `pilot_mesh`) are IDs created from a name with `CreateID`.

---

## 8. Intro screen configuration: `[Data] intro`

`LoadIntroConfigFromIni` ([dataconfig_progress.cpp:53](../../src/freelancer/Client/dataconfig_progress.cpp#L53)); path resolved against Data. A missing file leaves everything empty. Only `[Images]` is read.

| Key | Type | Default | Meaning / allowed values | Source |
|---|---|---|---|---|
| `TgaPath` | string (≤63 chars) | empty | Directory/prefix of the intro images. | [dataconfig_progress.cpp:69](../../src/freelancer/Client/dataconfig_progress.cpp#L69) |
| `ScreenType` | string (≤63) | empty | Image name for the type screen. | [dataconfig_progress.cpp:71](../../src/freelancer/Client/dataconfig_progress.cpp#L71) |
| `ScreenExit` | string (≤63) | empty | Exit screen image. | [dataconfig_progress.cpp:73](../../src/freelancer/Client/dataconfig_progress.cpp#L73) |
| `ScreenReset` | string (≤63) | empty | Reset screen image. | [dataconfig_progress.cpp:75](../../src/freelancer/Client/dataconfig_progress.cpp#L75) |
| `Screen1` ... `Screen4` | string (≤63) | empty | The four selectable intro screens (the code picks a base index 0, 1 or 2 at random after loading). | [dataconfig_progress.cpp:77](../../src/freelancer/Client/dataconfig_progress.cpp#L77) |
| `bDrawScripts` | bool | `true` | `true`/`1`/`false`/`0`. | [dataconfig_progress.cpp:85](../../src/freelancer/Client/dataconfig_progress.cpp#L85) |

Any other key logs `unknown ini parameter`. A key that is absent keeps its previous value, so the file can be loaded more than once.
