# Doom 64 EX+ Enhanced

Doom 64 EX+ Enhanced is a fork of the Doom 64 EX+ engine, Its main goal is to show what the Doom 64 engine would have looked like if it hadn't been limited by the N64 console, and also aims to improve the vanilla experience, by adding new features to the engine, adding new features for modding, adding missing monsters from Doom 2, adding monsters cut from Doom 64 like the Hellhound and the red cyberdemon with dual rocket launchers that were cut during the game's development, adding more new custom monsters to the engine, adding new animations, reworked vanilla weapons for an improved vanilla experience, and all this while keeping the vanilla gameplay unchanged.

# Key Features and Differences from Doom 64 EX+

* **Missing Monsters from Doom 2:** added Zombie Chaingunner, Revenant, Arch Vile, Spider Mastermind they are already present in the EX+ engine but the sprites of Zombie Chaingunner and Spider Mastermind have been updated by DrPyspy's sprites.
* **missing weapons animations and new weapons animations:** Added new vanilla weapons animations reworked for the Punch, Shotgun, Super Shotgun while retaining their attack speed from the original game.
* **the cut monsters of Doom 64:** Added the Hellhound and Annihilator, a red cyberdemon with two rocket launchers in each hand, which was cut during game development.
* **fixes vanilla bugs:** fixes some vanilla bugs in the original Doom 64 engine that were never fixed by Midway Games in 1997.
* **Ultra Nightmare:** add a new difficulty called Ultra Nightmare which consists of replacing all the monsters on the map with monsters in nightmare mode.
* **rework the transparency of the nightmare:** the transparency of the nightmare was reworked in the engine because it was a big problem of the EX engine, the transparency of the nightmare was almost invisible and we had a lot of trouble distinguishing them in the game now it was corrected, and moreover there is a small bonus there is a new option which allows you to change the color of the transparency of the nightmare to your choices.
* **updated monsters sprites:** The sprites of some vanilla monsters were reworked and updated, like Zombie Man, Zombie Shotgun, IMP, IMP Nightmare, Pinky, Spectre, Hell Knight, Baron Of Hell they now use their own sprites instead of using the same sprites with a color palette to differentiate them.
* **Sound Caulking:** Added new reworked vanilla sounds on some vanilla monsters, monsters now use their own unique sounds instead of using some of the same sounds on some monsters.
* **adds new custom monsters:** Adding new custom monsters to the engine and that mappers can use them on their maps.
* **Enhanced Modding And Mapping:** extensive modding and mapping.
* **new options:** Added new options that allow you to enable or disable whether you want to return to the vanilla experience of the original game or to the enhanced vanilla experience of the engine.

# Compatibility Between Engines

Doom 64 EX+ Enhanced should be compatible with both the Doom 64 EX+ engine and Doom 64 Remastered engine of Nightdive Studios.

# Things Planned For The Engine In The Future

* **Priority:**
* update the engine source code with all the latest EX+ engine patches.
* rework the doom engine random system that was done by Lee Killough on their BOOM and MBF engines.
* **Optional:**
* Should my Complex DOOM 64 game mode return to my engine?

# Doom 64 EX+

Doom 64 EX+ is a community-driven continuation of Samuel "Kaiser" Villarreal's original Doom 64 EX project. The primary goal of Doom 64 EX+ is to faithfully recreate the classic Nintendo 64 experience while incorporating modern features, extensive modding capabilities, and performance enhancements.

## Key Features and Differences from Legacy Doom 64 EX

* **Modern IWAD Support:** Full, native support for the IWAD file from Nightdive Studios' official 2020 remaster of *DOOM 64*. Please note that the old ROM dump-based IWADs from older EX versions are **not** supported.
* **The Lost Levels:** Includes support for the "Lost Levels" campaign content introduced in the official remaster.
* **Enhanced Modding:**
    * **PWAD Support:** Load custom PWAD files to play user-created maps and content.
* **Upgraded Audio Engine:** Replaced the legacy FluidSynth with FMOD Studio for higher-quality, more reliable audio playback.
* **Optimized Performance:** Delivers superior performance, running smoother than even the official Nightdive remaster in many cases.
* **Quality of Life Fixes:**
    * Secrets now trigger a notification message upon discovery.
    * Expanded map support up to the `MAP40` slot.
    * Restored the "Medkit you REALLY need!" message.
    * Numerous bug fixes for a more stable experience.
* **Modernized Codebase:**
    * **SDL3 Integration:** As one of the first *Doom* source ports to standardize on SDL3, it leverages the latest in cross-platform library support.
    * **Simplified Internals:** The KEX-related code has been largely removed to align the project more closely with the structure of other popular *Doom* source ports, simplifying development and code portability.

### A Note on Modding

For modders looking to adapt existing work or create new content for Doom 64 EX+, please be aware of the following changes from older versions of EX:

* Map markers now use `DM_START` and `DM_END` and `DS_START` and `DS_END` (in line with the remaster).
* The old `G_START` and `G_END` graphic lump markers are no longer used. Instead, graphic assets are identified as follows:
    * The **first** graphic marker in a WAD **must** be named `SYMBOLS`.
    * The **last** graphic marker in a WAD **must** be named `MOUNTC`.

You can find PWADs that have been specifically adapted for EX+ on [ModDB](https://www.moddb.com/games/doom-64/downloads/). Look for files designated for "EX+" or "EX Plus".

## License

The source code for Doom 64 EX+ is released under the terms of the original **Doom Source License**.

## Dependencies

Before compiling from source, you must have the necessary development libraries installed on your system.

#### Core Dependencies:

* **SDL3:** The core framework for windowing, input, and graphics.
* **ZLib:** A library for data compression.
* **LibPNG:** A library for reading and writing PNG image files.
* **OpenGL:** A graphics API for rendering.
* **FMOD Studio:** The audio engine.
    * **Note for Linux and BSD Users:** FMOD is a **proprietary, closed-source audio API**. Due to its licensing, it is not available in the official software repositories of most distributions and operating systems. You must download the FMOD Studio SDK manually from the [FMOD website](https://www.fmod.com/download). Please be aware of the FMOD licensing terms, which are free for non-commercial projects but have restrictions.

#### Launcher Dependency (Windows Only):

* **Microsoft .NET 6.0 (or higher):** The optional launcher application requires the .NET runtime to function. You can download it from the [official Microsoft website](https://dotnet.microsoft.com/en-us/download/dotnet/6.0).

## Installation and Compiling

### Step 1: Get the Source Code

First, clone the official repository to your local machine:

```bash
git clone https://github.com/atsb/Doom64EX-Plus
```

### Step 2: Acquire Game Data

Doom 64 EX+ requires asset files from your legally owned copy of the official *DOOM 64* remaster (e.g., from Steam or GOG).

### Step 3: Compile for Your Platform

#### GNU/Linux & BSD

The repository includes build scripts for easy compilation. Ensure all dependencies, including the manually downloaded FMOD libraries, are installed and accessible to your compiler first.

* **Linux:**
    ```bash
    ./build.sh
    ```
* **FreeBSD:**
    ```bash
    ./build_freebsd.sh
    ```
* **OpenBSD:**
    ```bash
    ./build_openbsd.sh
    ```
* **Raspberry Pi 3 (Raspbian):**
    ```bash
    ./build_rpi3_raspbian.sh
    ```

**Data File Paths (Linux/BSD):** The engine searches for asset files in the following order:
1.  `~/.local/share/doom64ex-plus/` (if compiled with `-DDOOM_UNIX_INSTALL`)
2.  A system-wide directory like `/usr/share/games/doom64ex-plus` (if specified with `-DDOOM_UNIX_SYSTEM_DATADIR` at compile time).
3.  The current directory where the executable is located.

Save data is stored in the executable's directory.

#### macOS

The recommended method is to install the pre-compiled version from [MacSourcePorts](https://macsourceports.com/game/doom64).

For manual compilation, you must install the dependencies via a package manager like MacPorts. Then, use the provided **XCode project file**, which is the only supported method.

**Data File Path (macOS):** Place your asset files in:
`/Users/your_username/Library/Application Support/doom64ex-plus/`

#### Windows

For Windows, use the Visual Studio solution file provided in the `Windows` directory of the repository to compile for either 32-bit or 64-bit systems.

**Data File Path (Windows):** Place the three asset files (`DOOM64.wad`, `doom64ex-plus.wad`, `doomsnd.sf2`) in the **same directory** as the compiled `DOOM64EX+.exe`.

## Usage

Once compiled and your asset files are in the correct location, simply run the executable:

* **Windows:** Double-click `DOOM64EX+.exe`. You may also use the optional `DOOM64EX+ Launcher.exe` for more configuration options.
    * **NVIDIA Users:** If you experience stuttering, it is highly recommended to disable VSync for this application in your NVIDIA Control Panel.
* **Linux/BSD/macOS:** Launch the executable from your terminal:
    ```bash
    ./doom64ex-plus
    ```

Enjoy the game! For support or to join the community, check out the official Discord server.

# Credits

* **Midway Games:** for the creation of the game DOOM64.
* **Kaiser:** for having done the EX engine.
* **Gibbon:** for having done the EX+ engine.
* **DrPyspy:** for having creating the sprites for the Zombie Chaingunner, the Revenant, the Arch Vile, the Spider Mastermind and the animations for the missing weapons the Punch, the Shotgun and the Super Shotgun in DOOM64 style.
* **Team GEC:** for having creating the Hellhound sprites which is a monster that was cut from the game during development.
* **Onox792:** for having creating the sprites of Annihilator, who is a red cyberdemon and who has two rocket launchers in each hand, and who is a monster that was cut from the game during development.
* **Jaxxoon R:** for having done the Sound Caulking.
* **DrDoctor:**  for having done the reworked version sprites of the Hell Knight and the Hell Baron, and for the sprites of the Imp with a mouth.
* **Hieronymus:** for having creating this beautiful logo DOOM 64 EX+ Enhanced in 3D !
