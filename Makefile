#---------------------------------------------------------------------------------
# Amateur Surgeon 4 -- Nintendo Switch wrapper (32-bit / AArch32)
#
# Ships NO game code and NO game assets: the player's own APK (any name) is
# read at run time; its library (libgame.so) is unpacked from it on the first
# launch; its assets are read from the APK itself.
#
# The build is the android32 runtime's (runtime/runtime.mk: devkitARM +
# libnx32 + mesa32 from portlibs32/); ./build.sh runs it in the toolchain
# container. Output: amateursurgeon4_nx.nsp, which the launcher NRO carries
# (launcher/).
#---------------------------------------------------------------------------------
TARGET               := amateursurgeon4_nx
# One id per port: change it if it clashes with another port's.
PORT_NPDM_PROGRAM_ID := 0x01000000000010A4
include runtime/runtime.mk

# The tiny shims include miniz's header (libnx32 ships it); nothing else to add.

.PHONY: check
check:
	@echo "run on the host: python3 runtime/tools/gen_imports.py --check"
