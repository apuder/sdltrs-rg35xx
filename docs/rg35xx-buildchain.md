# Building and installing TRS-80 directly on RG35XXPRO

This project documents an on-device build and install flow that uses the
toolchain already present on RG35XXPRO.

## Build script

Use [`scripts/build-rg35xxpro-on-device.sh`](../scripts/build-rg35xxpro-on-device.sh).

The script:

1. Accepts the RG35XXPRO IP address as its only argument.
2. Connects over SSH as `root` with password `root`.
3. Checks device date/time and validates it against host time.
4. If clock skew is too large, tries automatic sync (NTP first, then host time).
5. Aborts with an error if time cannot be corrected.
6. Runs `git clone --recursive https://github.com/apuder/sdltrs-rg35xx.git` on the device.
7. Checks whether `pasmo` is available and installs it with `apt-get` if missing.
8. Runs `./autogen.sh` to generate `configure`.
9. Runs `./configure` in the cloned repo.
10. Runs `make -j<N>` to build `sdl2trs`.
11. Runs `python3 rg35xx/build.py` to generate `LAUNCH.CMD` and the disk images.
12. Downloads `model3.rom` from
    `https://github.com/lkesteloot/trs80/raw/refs/heads/master/packages/trs80-emulator/roms/model3.rom`.
13. Installs files under `/mnt/mmc/Roms/APPS/`:

    ```text
    /mnt/mmc/Roms/APPS/
      TRS-80.sh
      TRS-80/
        sdl2trs
        roms/model3.rom
        disks/games.hdv
        disks/empty.hdv
        disks/ldos531-model3-boot.dsk
        LIST.TXT
        LAUNCH.CMD
        manifest.csv
        XTRSHARD-LICENSE.txt
    ```

14. Exits SSH automatically after a successful installation.

## Host prerequisites

Install `sshpass` so the script can provide the device password noninteractively:

```sh
sudo apt install sshpass
```

## Usage

From repository root:

```sh
./scripts/build-rg35xxpro-on-device.sh <RG35XXPRO_IP>
```

Example:

```sh
./scripts/build-rg35xxpro-on-device.sh 192.168.1.42
```

## Notes

- The script currently expects device credentials `root/root`.
- Clock skew tolerance is 1 hour (`3600` seconds).
- The script fails if `sdltrs-rg35xx` already exists on device.
- The script requires `aclocal`, `autoconf`, and `automake` on device because
  it runs `./autogen.sh` before `./configure`.
- The script requires `apt-get` to install `pasmo` when needed.
- The script uses `curl` or `wget` on device to download `model3.rom`.
- On success, TRS-80 is installed under `/mnt/mmc/Roms/APPS/TRS-80` and
  `/mnt/mmc/Roms/APPS/TRS-80.sh`.
