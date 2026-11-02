# Doom 64 EX+ 5.2.0.0 – Online/LAN Co-op

This build adds a UDP network transport to Doom 64 EX+, so the existing
Chocolate Doom netcode works across machines (up to 4 players).

Every player needs this exact build. Game data (`DOOM64.WAD`, `DOOMSND.DLS`,
`Doom64.kpf`) is found automatically from a Steam/GOG install of the
DOOM 64 remaster, or can be placed next to `DOOM64.exe`.

## Hosting

    DOOM64.exe -server -warp 1 -skill 3

Press any key in the host window once everyone has joined. To start
automatically when a number of players (host included) are connected:

    DOOM64.exe -server -nodes 2 -warp 1

For internet play, forward UDP port 2342 on the host's router.

## Joining

    DOOM64.exe -connect 203.0.113.7
    DOOM64.exe -connect 203.0.113.7:2342
    DOOM64.exe -autojoin                   (find a server on the LAN)

## Options

| Parameter          | Meaning                                         |
|--------------------|-------------------------------------------------|
| `-port <n>`        | Server port, and default port for `-connect`    |
| `-nodes <n>`       | Start once n players are in; ignores key presses |
| `-playername <s>`  | Name shown to other players                     |
| `-deathmatch`      | Deathmatch instead of co-op                     |
| `-respawn`, `-respawnitem`, `-fast`, `-nomonsters` | Usual gameplay modifiers |

Further host settings are console variables, set on the host's command line
with `-setvars`, e.g. `-setvars sv_friendlyfire 1 sv_keepitems 1`.
