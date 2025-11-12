# `AdvancedDark` for EZ Flash Omega (not for Definitive Edition!)

_A fork of [SimpleLight by Sterophonick](https://github.com/Sterophonick/SimpleLight)_

## Features

- All features of SimpleLight
- Updated FATFS module to v0.16
- An easier way to build using Dev Containers with Visual Studio Code!
- Custom EMU build mode that generates a version of the ezkernel that can be used with emulators for testing features without needing to flash the cartridge every time (tested with mGBA)
- A revised project structure since the original [ez-flash/omega-kernel](https://github.com/ez-flash/omega-kernel) is a mess
- Lots of AI slop since I am not a good C developer (I wouldn't even attempt working on this without assistance)

### Ideas for future features

- Integrated file management (copy/move/delete/rename files on the SD card)
- Hide system files option (hidden SYSTEM and BACKUP folders to get a cleaner view)
- Removal of Chinese language (I don't speak Chinese nor do I have the motivation to support it)
- Integrate QoL features from [SimpleLight++](https://github.com/Deko29/SimpleLight-pp)
- Custom themes loaded from SD card if possible
- Background music maybe

## Installation

This is not yet ready for use, so if you wanna try you have to build it yourself and use it at your own risk.

NEVER try to flash the `ezkernel_emu.bin` to your EZ Flash Omega, it will not work and may brick your cartridge! It is only for testing the firmware in emulators.

### Building

Linux only for now, use WSL2 if you're on Windows.

#### Setup Build Environment with Dev Containers

1. Install [Visual Studio Code](https://code.visualstudio.com/) and [Docker](https://www.docker.com/get-started).
2. Install the [Dev Containers extension](https://marketplace.visualstudio.com/items?itemName=ms-vscode-remote.remote-containers) for Visual Studio Code.
3. Clone this repository to your local machine.
4. Open the cloned repository folder in Visual Studio Code.
5. When prompted by Visual Studio Code, reopen the folder in a Dev Container.
6. Once the Dev Container is built and running, open a terminal in Visual Studio Code (Terminal -> New Terminal).

#### Build the Firmware for EZ Flash Omega

```bash
make
```

To flash just copy the `ezkernel.bin` to your sd card and hold R while turning on the GBA.

#### Build the EMU Version for Testing in Emulators

```bash
make EMU=1
```

You can then load the `ezkernel_emu.bin` in an emulator that supports GBA flashcarts like mGBA.

##### Modifying the disk image for EMU builds

The EMU build includes a disk image that is mounted as fake SD card storage. By default, this is `diskimg/disk.bin`. You can replace this file with your own disk image. It should not be larger than 16MB since the max rom size is 32MB and the firmware itself takes up some space.

To replace the disk image:

```bash
sudo apt-get update
sudo apt-get install -y mtools

truncate -s 16M diskimg/disk.bin
mkfs.vfat -F 16 diskimg/disk.bin

mmd  -i diskimg/disk.bin ::/SYSTEM
mmd  -i diskimg/disk.bin ::/SYSTEM/PLUG
mmd  -i diskimg/disk.bin ::/GAMES
mmd  -i diskimg/disk.bin ::/HOMEBREW

mcopy -i diskimg/disk.bin SYSTEM/RECENT.TXT ::/SYSTEM/RECENT.TXT
mcopy -i diskimg/disk.bin ansi_console.gba ::/HOMEBREW/
```

### Credits

[EZ-FLASH](https://www.ezflash.cn/) - The original firmware & hardware creators\
ChaN - FatFS library\
Kuwanger - PogoShell plugin integration\
Sterophonick - SIMPLE theme for EZO & EZODE\
fluBBa - SMSAdvance, MSXAdvance, Cologne for GBA, Goomba for GBA (Original), PCEAdvance, PocketNES, SNESAdvance, Wasabi, NGPAdvance, SwanAdvance\
[Jaga](https://github.com/EvilJagaGenius) - [Jaga's Goomba Color fork](https://github.com/EvilJagaGenius/jagoombacolor)\
and everyone else who contributed to SimpleLight!
