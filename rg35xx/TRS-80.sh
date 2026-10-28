#!/bin/sh

APPDIR="$(dirname "$(readlink -f "$0")")/TRS-80"

cd "$APPDIR" || exit 1

export SDL_VIDEODRIVER=mali
export SDLTRS_RG35XX_INPUT=sdl
export SDLTRS_RG35XX_DEVICE=0
export SDLTRS_RG35XX_GUID='19000000010000000100000000010000'
export SDLTRS_RG35XX_MAP='a=b0,b=b1,x=b3,y=b2,start=b7,select=b6,menu=b8,l1=b4,r1=b5,l2=b10,r2=b11,ls=b9,rs=b12,up=h0u,down=h0d,left=h0l,right=h0r,lx=a0,ly=a1,rx=a2,ry=a3'


# Launch SDLTRS and capture diagnostic output.
./sdl2trs \
    -noled \
    -noturbo \
    -m3 \
    -romfile3 "$APPDIR/roms/model3.rom" \
    -disk0 "$APPDIR/disks/ldos531-model3-boot.dsk" \
    -hard0 "$APPDIR/disks/games.hdv" \
    > /dev/null 2>&1

exit $?
