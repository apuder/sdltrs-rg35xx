PROG	 = sdl2trs

SRCS	+= debug.c
SRCS	+= dis.c
SRCS	+= error.c
SRCS	+= main.c
SRCS	+= trs_cassette.c
SRCS	+= trs_clones.c
SRCS	+= trs_cmd_rom.c
SRCS	+= trs_cp500.c
SRCS	+= trs_disk.c
SRCS	+= trs_hard.c
SRCS	+= trs_imp_exp.c
SRCS	+= trs_interrupt.c
SRCS	+= trs_io.c
SRCS	+= trs_memory.c
SRCS	+= trs_mkdisk.c
SRCS	+= trs_options.c
SRCS	+= trs_printer.c
SRCS	+= trs_sdl_gui.c
SRCS	+= trs_sdl_interface.c
SRCS	+= trs_sdl_keyboard.c
SRCS	+= trs_sdl_keymap.c
SRCS	+= trs_state_save.c
SRCS	+= trs_stringy.c
SRCS	+= trs_uart.c
SRCS	+= z80.c
SRCS	+= PasteManager.c

CPPSRCS += rg35xx/RG35XXControls.cpp
CPPSRCS += rg35xx/KeyboardRG35XXControls.cpp
CPPSRCS += rg35xx/RG35XXInputRouter.cpp
CPPSRCS += rg35xx/RG1Mapping.cpp
CPPSRCS += rg35xx/SDLRG35XXControls.cpp
CPPSRCS += rg35xx/SystemVolume.cpp
CPPSRCS += rg35xx/SoftKeyboard.cpp
CPPSRCS += rg35xx/SoftKeyboardRenderer.cpp
CPPSRCS += rg35xx/TrsKeyboardAdapter.cpp
CPPSRCS += rg35xx/rg35xx_bridge.cpp

OBJS	 = ${SRCS:.c=.o} ${CPPSRCS:.cpp=.o}
TEST_OBJS = rg35xx_tests.o trs_sdl_keymap.o ${CPPSRCS:.cpp=.o}
TEST_PROG = rg35xx_tests

.PHONY: all bsd clean depend os2 sdl2 test-rg35xx win32 win64

all:
	@echo "make (bsd | clean | depend | os2 | sdl2 | win32 | win64)"

bsd:
	make -f BSDmakefile

clean:
	rm -f ${OBJS} ${PROG} ${TEST_OBJS} ${TEST_PROG} rg1_keyboard_tests.o rg1_keyboard_tests

depend:
	makedepend -Y -- ${CFLAGS} -- ${SRCS} 2>&1 | \
		(egrep -v 'cannot find|not in' || true)

os2:	SDL_INC	?= $(shell sdl2-config --cflags)
os2:	SDL_LIB	?= $(shell sdl2-config --libs)
os2:	LDFLAGS	+= -Zomf
os2:	${PROG}

sdl2:	ENDIAN	 = $(shell echo "ab" | od -x | grep "6261" > /dev/null || echo "-Dbig_endian")
sdl2:	SDL_INC	?= $(shell sdl2-config --cflags)
sdl2:	SDL_LIB	?= $(shell sdl2-config --libs)
sdl2:	READLINE?= -DREADLINE
sdl2:	${PROG}

test-rg35xx: SDL_INC ?= $(shell sdl2-config --cflags)
test-rg35xx: SDL_LIB ?= $(shell sdl2-config --libs)
test-rg35xx: CXXFLAGS += ${SDL_INC}
test-rg35xx: ${TEST_PROG} rg1_keyboard_tests
	./${TEST_PROG}
	./rg1_keyboard_tests

win32:	MINGW	?= \MinGW
win32:	CC	 = ${MINGW}\bin\gcc.exe
win32:	CXX	 = ${MINGW}\bin\g++.exe
win32:	SDL_INC	?= -I${MINGW}\include\SDL2
win32:	SDL_LIB	?= -L${MINGW}\lib -lmingw32 -lSDL2main -lSDL2
win32:	${PROG}

win64:	MINGW64	?= \MinGW64
win64:	CC	 = ${MINGW64}\bin\gcc.exe
win64:	CXX	 = ${MINGW64}\bin\g++.exe
win64:	SDL_INC	?= -I${MINGW64}\include\SDL2
win64:	SDL_LIB	?= -L${MINGW64}\lib -lmingw32 -lSDL2main -lSDL2
win64:	${PROG}

MSYS		:=$(if $(findstring Msys, $(shell uname -a)), Msys)
SSPLIB		:=$(if ${MSYS},-lssp,)
READLINELIBS	 =$(if ${READLINE},-lreadline,)
MACROS		+= -DROM_PATH='"${ROM_PATH}"'
ZBX		?= -DZBX
CFLAGS		?= -g -O2 -Wall
CFLAGS		+= ${SDL_INC} ${ENDIAN} ${MACROS} ${READLINE} ${ZBX}
CXXFLAGS	?= -g -O2 -Wall
CXXFLAGS	+= -std=c++11 ${SDL_INC}

${PROG}: ${OBJS}
	${CXX} -o ${PROG} ${OBJS} ${SDL_LIB} ${LDFLAGS} ${READLINELIBS} ${SSPLIB}

${TEST_PROG}: ${TEST_OBJS}
	${CXX} -o ${TEST_PROG} ${TEST_OBJS} ${SDL_LIB} ${LDFLAGS}

rg35xx_tests.o: ../tests/rg35xx_tests.cpp
	${CXX} ${CXXFLAGS} -c -o $@ $<

rg1_keyboard_tests.o: ../tests/rg1_keyboard_tests.cpp
	${CXX} ${CXXFLAGS} -c -o $@ $<

rg1_keyboard_tests: rg1_keyboard_tests.o trs_sdl_keyboard.o trs_sdl_keymap.o ${CPPSRCS:.cpp=.o}
	${CXX} -o $@ $^ ${SDL_LIB} ${LDFLAGS}
