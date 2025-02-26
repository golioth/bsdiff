# golioth/bsdiff

This is a fork of https://github.com/Blockstream/esp32_bsdiff (which itself is a fork of
https://github.com/mendsley/bsdiff).

The primary change made in this fork is that `bspatch()` has been redesigned as a state machine,
in order to process patch data in small, incremental chunks. This is useful to avoid having to
store the entire patch file in RAM or flash.

## Patch file format

The format of the patch file is different from standard `bsdiff`.

The patch file is a sequence of blocks:

```
| X | Y | Z | X bytes of diff ... | Y bytes of extra ... |
```

where X, Y, Z are 64-bit integers.

The patching algorithm is basically:

1. Add X bytes from old file to X diff bytes from patch and write the
   resulting X bytes to new file.
2. Read Y extra bytes from patch and write them to new file.
3. Seek forward Z bytes in old file (might be negative).

## Run unit tests

To run unit tests (requires ESP-IDF to be installed at `$IDF_INSTALL_PATH`):

```sh
cd test
source $IDF_INSTALL_PATH/export.sh
./run.sh
```

The unit tests compile and run for your host machine, not the ESP32.

(The remainder of this README is from the upstream repo.)

# bspatch for esp32

This project adds support for bspatch to the esp32 with some changes: no compression (bz2), no header and changed the interfaces to allow streaming all inputs and outputs.

The functionality in particular can be useful to perform OTA firmware upgrades using compressed diffs between the running firmware and the target firwmare.

At the moment on the esp32 only applying the patch is supported (with the patch generation happening on a computer or anyhow a device with the adequate resources).

Important: `esp32_bsdiff` is incompatible with the version usually available. Build `esp32_bsdiff` as described below.


## Usage

To use with an ESP-IDF project you include this repo in the components directory.

```
$ cd components
$ git submodule add git@github.com:blockstream/esp32_bsdiff.git esp32_bsdiff
```

To build bsdiff and bspatch for your computer:
```
gcc -O2 -DBSDIFF_EXECUTABLE -o esp32_bsdiff components/esp32_bsdiff/bsdiff.c
gcc -O2 -DBSPATCH_EXECUTABLE -o esp32_bspatch components/esp32_bsdiff/bspatch.c
```

Usage of the command line tools are unchanged from bsdiff.
```"usage: %s oldfile newfile patchfile```

For bspatch it requires to pass in as a third argument the new file size in bytes shifting the patch filename to forth.
```"usage: %s oldfile newfile newsize patchfile```

To compress the resulting patch one may use the following (and use miniz.h to decompress on the esp32):

```
python -c "import zlib; import sys; open(sys.argv[2], 'wb').write(zlib.compress(open(sys.argv[1], 'rb').read(), 9))" uncompress_patch_file.bin compressed_patch_file.bin
```
