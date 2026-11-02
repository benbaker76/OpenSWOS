# Installing OpenSWOS

<!-- The same guide is on the web at https://openswos.net/install/ (packaging/web/site/install):
     change both together. -->

This guide takes you from nothing to playing, one step at a time. It takes about five
minutes.

**In short:** download OpenSWOS, then run **OpenSWOS Setup** once. It shows you where to get
the game, finds what you download, and makes the game's data from it. That's it.

- [1. What you need](#1-what-you-need)
- [2. Download](#2-download)
- [3. Install the game](#3-install-the-game): [Windows](#windows) · [macOS](#macos) · [Linux](#linux) · [Amiga](#amiga) · [Android](#android) · [iPhone and iPad](#iphone-and-ipad)
- [4. Run OpenSWOS Setup](#4-run-openswos-setup)
- [4a. World 2026-27 (optional)](#4a-world-2026-27-optional)
- [5. Play](#5-play)
- [If something goes wrong](#if-something-goes-wrong)

---

## 1. What you need

**Your own copy of *Sensible World of Soccer*.** OpenSWOS is a new engine for the game, but
the game's graphics, music, sound and teams are still owned by their makers, so they can't
come with OpenSWOS. The setup makes them from your copy instead.

| Your copy | Where to get it |
|---|---|
| **The PC version** (easiest) | Buy *Sensible World of Soccer 96/97* on [GOG](https://www.gog.com/en/game/sensible_world_of_soccer_9697) and install it. The [PC CD version](https://archive.org/details/msdos_Sensible_World_of_Soccer_96-97_1996) works too. |
| **The Amiga version** | Any of: the two SWOS 96/97 floppies as disk images (`.adf`); a hard disk image with the game on it (`.hdf`, e.g. ASWOS's `swos.hdf`); a WHDLoad install (`.lha`); or a `.zip` of any of these. sensiblesoccer.de's [24/25 season](https://www.sensiblesoccer.de/index.php?&view=newsdetails&id=206) (`swos2425.hdf` or the WHDLoad `.lha`) works too. |

- **For the PC, Mac and Linux game,** one of the two is enough. With both, you get the Amiga's
  music, sound and loading screens as well.
- **For the Amiga version of OpenSWOS,** you need both: the Amiga version for the game's own
  files, the PC version for its AGA pitches and pictures.

You don't have to remember any of this: the setup lists what you need, with the links, and
says what each one gives.

Two extras are optional, for players who have them: the **Atari Jaguar** cartridge image
(for the Jaguar's sprites) and a **SWOS 2020** install (for its pitches).

---

## 2. Download

Get OpenSWOS from **<https://baker76.itch.io/openswos>**. That page and **openswos.net** are
the only official places to get it: OpenSWOS is free, and never asks you to pay or sign up
anywhere else. Pick the file for your computer:

| Your computer | Download |
|---|---|
| **Windows** (10 or later) | `SWOS-Setup.exe` — the installer |
| **Mac** (Apple silicon or Intel) | `SWOS-macOS-app.zip` |
| **Linux** (PC or Raspberry Pi) | `SWOS-portable.zip`, and `OpenSWOS-Setup.zip` for the setup window |
| **Amiga** (A1200/A4000 or an emulator) | `OpenSWOS-Setup.zip`, to run on a PC or Mac: it has the Amiga program in it |
| **Android** (8.0 or later) | `OpenSWOS-Android.apk`, and the setup for your PC or Mac (above) |
| **iPhone or iPad** (iOS 15 or later) | `OpenSWOS-iOS.ipa`, and the setup for your PC or Mac (above) |

---

## 3. Install the game

### Windows

1. Run **`SWOS-Setup.exe`**.
   - If Windows says *"Windows protected your PC"*, click **More info**, then **Run anyway**.
     OpenSWOS isn't signed by Microsoft, which is why it asks.
2. Click **Next** through the installer.
3. On the last page, leave **"Set up the game data from your copy of Sensible World of
   Soccer"** ticked and click **Finish**. OpenSWOS Setup opens: go to
   [step 4](#4-run-openswos-setup).

Later, you can open the setup again from the Start menu: **OpenSWOS → OpenSWOS Setup**.

### macOS

1. Double-click **`SWOS-macOS-app.zip`**. You get an **OpenSWOS** folder holding
   **SWOS.app** and **OpenSWOS Setup.app**.
2. Drag the **OpenSWOS** folder to **Applications** (or anywhere you like).
3. Open **OpenSWOS Setup.app**. **The first time, right-click it (or Control-click) and
   choose Open**, then **Open** again. OpenSWOS isn't signed by Apple, so a plain
   double-click is refused the first time. Then go to [step 4](#4-run-openswos-setup).

Do the same right-click → **Open** the first time you start **SWOS.app**.

### Linux

1. Unzip **`SWOS-portable.zip`** into a folder of its own, e.g. `~/Games/OpenSWOS`.
2. Unzip **`OpenSWOS-Setup.zip`** anywhere, and run **`Linux/openswos-setup`** from it.
   Go to [step 4](#4-run-openswos-setup).

Which game file to run: **`SWOS.x86_64`** on a normal 64-bit PC, **`SWOS.aarch64`** on a
64-bit Raspberry Pi (Pi 4/5), **`SWOS.armhf`** on a 32-bit Pi. You need a Linux from 2021
or newer (Ubuntu 21.10, Debian 12, Fedora 35 or later).

### Amiga

The setup can't run on the Amiga itself, so you do this part on a PC or a Mac:

1. Unzip **`OpenSWOS-Setup.zip`** and run the setup for your computer:
   `Windows\openswos-setup.exe`, `macOS/OpenSWOS Setup.app` or `Linux/openswos-setup`.
2. Click the **Amiga** tab, and do [step 4](#4-run-openswos-setup) there.

You need an AGA Amiga with a 68020 or better (an A1200 or A4000, or an emulator set up as
one), with a hard drive, Workbench and 8 MB of fast memory.

### Android

The setup can't run on a phone either, so the game's data is made on a PC or Mac and copied
over:

1. On your PC or Mac, install OpenSWOS (above) and do [step 4](#4-run-openswos-setup). It
   makes a folder called **`assets`** (Windows: `Documents\SWOS\assets`; Mac:
   `~/Library/Application Support/SWOS/assets`; Linux: `~/.swos/assets`).
2. On the phone, open **`OpenSWOS-Android.apk`** (download it on the phone, or copy it over)
   and allow installing it when Android asks: it isn't from the Play Store.
3. Start **OpenSWOS** once: it says it needs the game's data, and where it goes.
4. Connect the phone to the computer by USB (choose **File transfer** on the phone) and copy
   the **`assets`** folder into **`Android/data/net.openswos.game/files`** on the phone.
5. Start OpenSWOS again.

### iPhone and iPad

iPhones and iPads install apps only from the App Store, so **`OpenSWOS-iOS.ipa`** is installed
with a sideloading tool, [AltStore](https://altstore.io) or [Sideloadly](https://sideloadly.io),
which signs it with your own Apple ID (with a free Apple ID it has to be refreshed every 7
days; the tool does that for you). Then:

1. On your PC or Mac, install OpenSWOS (above) and do [step 4](#4-run-openswos-setup), which
   makes the **`assets`** folder (see Android, above).
2. Start **OpenSWOS** on the phone once, so its folder appears.
3. Copy the **`assets`** folder into it: from a Mac, Finder → your phone → **Files** →
   **OpenSWOS**; from Windows, the Apple Devices app (or iTunes) → **File Sharing** →
   **OpenSWOS**; or on the phone, the **Files** app → **On My iPhone** → **OpenSWOS**.
4. Start OpenSWOS again.

On a phone, the stick and buttons are on the screen; a Bluetooth controller works too.

---

## 4. Run OpenSWOS Setup

This is the one step that matters. The setup has two tabs: **PC · Mac · Linux** for the game
on your computer, **Amiga** for an Amiga. When it opens, it looks like this:

![OpenSWOS Setup when it opens](docs/images/setup/setup-1-start.png)

**1. Get the game.** Each thing the game can use has a row: what it is, whether it's
**needed** or **optional**, what it gives, and on the right the **links to get it**. You
don't have to find the files afterwards:

- **What you download is found by itself.** The setup watches your **Downloads** folder: a
  disk image, hard disk image, WHDLoad `.lha` or `.zip` you download from those links turns
  up in its row a moment after the download finishes.
- **The GOG game is found by itself** once it's installed. If GOG's installer is still in
  your Downloads, the setup reminds you to run it.
- **Already have the game somewhere?** Click **Search a folder…** and pick a folder it's in.
  The setup looks through that folder **and every folder inside it**. Or click the **…**
  beside a row and pick the file or folder yourself, or paste its path into the box.

Each part it finds gets a green tick. Here it found the PC version and the Amiga floppies:

![OpenSWOS Setup after finding the game](docs/images/setup/setup-2-found.png)

Under the Amiga row it says which **season** your copy is. The 96/97 release is the one the
game was made from; a later season (like sensiblesoccer.de's 24/25) works too, but online
OpenSWOS plays 96/97's teams, so with the PC game there those come from it. A copy that
can't be used (a damaged download, or a different release) gets a red mark and says why.

**2. Install.** On the **PC · Mac · Linux** tab, click **Install**. It takes a few seconds.
When it says **Done**, you're finished:

![OpenSWOS Setup when it's done](docs/images/setup/setup-3-done.png)

- **Into** is where the game reads its data from. Leave it as it is.
- **Details** shows what will be made, and the log. Parts marked **skipped** need something
  you don't have; that's fine, the game plays without them.

**On the Amiga tab,** choose what to make for your Amiga, then click the button below:

![OpenSWOS Setup's Amiga tab](docs/images/setup/setup-4-amiga.png)

- **SWOS drawer:** a drawer to copy to your Amiga's hard drive (or to a folder FS-UAE or
  WinUAE uses as a hard drive). Double-click **SWOS** in it to play.
- **WHDLoad package:** `OpenSWOSAGA-WHDL.lha`, for an Amiga with
  [WHDLoad](https://whdload.de/) installed. Unpack it on the Amiga, open the
  **OpenSWOSAGA-NE** drawer and double-click **OpenSWOS**. No Kickstart image is needed.

Both go to your **Downloads** folder unless you click **Change…**. **Show it** opens the
folder when it's done:

![OpenSWOS Setup after making the WHDLoad package](docs/images/setup/setup-5-whdload.png)

You only need to do this once. OpenSWOS Setup remembers where your copy of the game is.
If a new version of OpenSWOS makes some of the data differently, the game tells you when it
starts: open OpenSWOS Setup, where those parts say **update needed**, and click **Update**.

*(The screenshots are from Windows; the setup looks the same on a Mac and on Linux.)*

---

## 4a. World 2026-27 (optional)

**World 2026-27** is today's football: the clubs, players and competitions of the 2026-27
season, from the [SWOS 96/97 Mod](https://github.com/aminta/swos-9697-mod) by Davide
Lorigliola. It's a download of its own, and the game plays 96/97 unless you ask for it.

1. **Get it.** In OpenSWOS Setup, the **World 2026-27** row has a link,
   **OpenSWOS-World-2627.zip**: click it. Like the game, the setup finds the download by
   itself (or click **…** beside the row and pick the zip).
2. **Install it.** Click **Install** (or **Update**). The world's step goes in with the rest.
3. **Turn it on.** In the game, choose **OPTIONS** on the main menu, then **WORLD**: it
   switches between **96/97** and **2026-27**. The game plays the new world from the next
   time it starts, so close it and start it again. (It's `world=2627` under `[general]` in
   `SWOS.ini`, if you'd rather set it there.)

   A career or competition you saved is always played in the world it was started in.

- **On an Amiga,** tick **Play World 2026-27 (its clubs and players) instead of 96/97** on
  the setup's **Amiga** tab before making the drawer or the WHDLoad package.
- **On a phone,** do steps 1 and 2 on your computer, then copy the **`assets`** folder over
  again as above (it holds the world too), and choose it in **OPTIONS → WORLD**.
- The world's data is its authors' ([credits](https://worldofswos.com/static/openswos/README.txt)).

---

## 5. Play

- **Windows:** start **OpenSWOS** from the Start menu (or the desktop, if you ticked that
  in the installer).
- **Mac:** open **SWOS.app** (right-click → **Open** the first time).
- **Linux:** run `SWOS.x86_64` (or the one for your Pi) from the folder you unzipped.
- **Amiga:** double-click **SWOS** in its drawer, or with WHDLoad, **OpenSWOS** in the
  **OpenSWOSAGA-NE** drawer. Under WHDLoad, **Del** quits back to Workbench.
- **Android, iPhone and iPad:** tap **OpenSWOS**. The stick (left) and FIRE (right) are on
  the screen, with PAUSE and ESC above FIRE.

Menus use the **arrow keys** and **Enter** (or a gamepad). In a match, the arrow keys
move and **Ctrl**, **Space** or **Enter** kick. All the keys and the gamepad buttons are in
[docs/PLAYING.md](docs/PLAYING.md).

**Playing online:** choose **MULTIPLAYER** on the main menu. Both players need the same
version of OpenSWOS.

---

## If something goes wrong

**The setup doesn't find my game.**
Click **Search a folder…** and pick the folder it's in, or click the **…** beside the
missing part and pick it by hand:
- *PC game*: the folder with `SPRITE.DAT` and a `DATA` folder in it. GOG puts it where you
  chose when installing, usually under `C:\GOG Games` or under `GOG Galaxy\Games` in
  Program Files.
- *Amiga version*: the `.adf`, `.hdf`, `.lha` or `.zip` file itself. For floppies, add both
  disks: the setup tells them apart by what's on them, not by their names.

**The Amiga row has a red mark.**
It says why. *"…its blocks on the disk are damaged"*: that download is broken, try another
(sensiblesoccer.de's `swos2425.hdf` and WHDLoad `.lha` are both good). *"…is not the 96/97
release or an update of it"*: it's an older SWOS (for example the original 1994 floppies),
which OpenSWOS can't use.

**"Make the drawer" / "Make the package" stays grey.**
The line under it says what's missing: usually the PC version (the Amiga version of
OpenSWOS needs both), or the Amiga program, `SWOS-Amiga.zip`, if you're not running the
setup from `OpenSWOS-Setup.zip`. Click **Choose…** and pick that zip.

**The game won't start, or starts without graphics or sound.**
The game data usually isn't installed. Run OpenSWOS Setup again and check it says
**Done**. The game writes what happened to `SWOS.log`, in the folder below.

**Windows or the Mac won't let me open it.**
OpenSWOS isn't signed. On Windows click **More info → Run anyway**; on a Mac right-click
the app and choose **Open**.

**My gamepad doesn't work.**
See [docs/PLAYING.md](docs/PLAYING.md#gamepad-not-detected).

**Where are my saves and settings?**

| Computer | Folder |
|---|---|
| Windows | `Documents\SWOS` |
| Mac | `~/Library/Application Support/SWOS` (in Finder: **Go → Go to Folder…**) |
| Linux | `~/.swos` |
| Amiga | the `save` folder inside the SWOS drawer (with WHDLoad: `OpenSWOSAGA-NE/data/save`) |

Saves are in `save`, the settings in `SWOS.ini` ([docs/SETTINGS.md](docs/SETTINGS.md)),
the game data from the setup in `assets`. Reinstalling or updating OpenSWOS never touches
them.

**Still stuck, or found a bug?** Please report it on
[github.com/benbaker76/OpenSWOS/issues](https://github.com/benbaker76/OpenSWOS/issues): say which version you have (the bottom
line of the main menu) and what happened, and attach your `SWOS.log`.
