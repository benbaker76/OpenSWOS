
 OPENSWOS SETUP
 ==============

 OpenSWOS does not include Sensible World of Soccer's graphics, sound or
 team data. OpenSWOS Setup makes them, once, from a copy of the game you
 own:

 - the PC version (from GOG): the installed game's folder
 - the Amiga version: the two SWOS 96/97 floppies (.adf), a hard disk
   image with the game on it (.hdf, e.g. ASWOS's swos.hdf), a WHDLoad
   install (.lha), or a .zip of any of these; sensiblesoccer.de's 24/25
   season works too
 - optional: the Atari Jaguar cartridge image (Sensible Soccer
   International Edition) for the Jaguar sprites, and a SWOS 2020 install
   for its pitches

 The setup shows where to get each one (the links on the right of its
 row), and finds what you download by itself: it watches your Downloads
 folder, and finds the GOG game once it is installed.

 This download is the setup on its own, for Windows, macOS and Linux, with
 the Amiga program (amiga/). The Windows installer and the game's zips for
 Windows, Linux and macOS have the setup already; you need this one for
 the Amiga, or for a game that is on another computer.

 Downloads, the other versions and the changelog:
        https://baker76.itch.io/openswos


 RUNNING IT
 ----------

 Unpack the zip as it is: the programs find their files (setup/, data/)
 in the folder above them.

 Windows   Windows\openswos-setup.exe
 macOS     macOS/OpenSWOS Setup.app (Apple silicon and Intel Macs). The
           first time, right-click it and choose Open: it is not signed.
 Linux     Linux/openswos-setup (x86-64), or Linux/swosdata

 Two tabs: "PC - Mac - Linux" for the game on this computer, "Amiga" for
 an Amiga. Download what a row asks for from its links, and it is found.
 Or press "Search a folder..." and pick a folder your game is in: the
 setup looks through it and every folder inside it. Or type, paste or
 browse ("...") a location for each one. Then press Install.

 The data goes where the game looks for it (the "Into" line shows it):

   Windows   Documents\SWOS\assets
   macOS     ~/Library/Application Support/SWOS/assets
   Linux     ~/.swos/assets


 THE AMIGA
 ---------

 The setup cannot run on the Amiga; run it on a PC or Mac, on its Amiga
 tab. The Amiga version of OpenSWOS needs both the Amiga version of the
 game and the PC version. Then make one of:

 - SWOS drawer: copy it, with SWOS.info, to your Amiga's hard drive, or
   to the drive of FS-UAE / WinUAE. Double-click SWOS in it.
 - WHDLoad package, OpenSWOSAGA-WHDL.lha: unpack it on an Amiga with
   WHDLoad, open the OpenSWOSAGA-NE drawer and double-click OpenSWOS. It
   starts the game without a Kickstart image; Del quits.

 Both are made from the Amiga program in amiga/ here (or SWOS-Amiga.zip,
 with "Choose..."), and go to your Downloads folder unless you change it.
 You need an AGA Amiga (A1200, A4000) with a 68020 and 8 MB of fast
 memory.


 FROM THE COMMAND LINE
 ---------------------

 swosdata does the same without a window:

   swosdata --scan FOLDER                 what it finds there
   swosdata --scan FOLDER --out ASSETS    make the data in ASSETS
   swosdata --installed ASSETS            what an earlier run made
   swosdata --whdload OUT.lha --drawer SWOS --assets ASSETS
                                          the WHDLoad package

 PROBLEMS
 --------

 Found a bug? Please report it at github.com/benbaker76/OpenSWOS/issues
 with what happened. The setup writes setup.log beside the folder it
 installs to: attach it too.


 "Sensible World of Soccer" (C) 1994-97 Sensible Software. OpenSWOS is a
 fan project and is not affiliated with Sensible Software or its current
 rights holders.
