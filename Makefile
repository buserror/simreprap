# 
# 	Copyright 2008, 2012 Michel Pollet <buserror@gmail.com>
#
#	This file is part of simreprap.
#
#	simavr is free software: you can redistribute it and/or modify
#	it under the terms of the GNU General Public License as published by
#	the Free Software Foundation, either version 3 of the License, or
#	(at your option) any later version.
#
#	simavr is distributed in the hope that it will be useful,
#	but WITHOUT ANY WARRANTY; without even the implied warranty of
#	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#	GNU General Public License for more details.
#
#	You should have received a copy of the GNU General Public License
#	along with simavr.  If not, see <http://www.gnu.org/licenses/>.

CC = gcc
target=	reprap
firm_src = ${wildcard atmega*.c}
firmware = ${firm_src:.c=.hex}

SIMAVR_R	= shared/simavr
LIBC3		= shared/libc3
FTGL		= shared/libfreetype-gl

IPATH = .
IPATH += src
IPATH += $(LIBC3)/src
IPATH += $(LIBC3)/srcgl
IPATH += ${SIMAVR_R}/include
IPATH += ${SIMAVR_R}/simavr/sim
IPATH += ${SIMAVR_R}/examples/parts
IPATH += ${SIMAVR_R}/examples/shared
IPATH += ${FTGL}

VPATH = src
VPATH += ${SIMAVR_R}/examples/parts
VPATH += ${SIMAVR_R}/examples/shared

# requires libfreetype6-dev
FTC	  = ${shell PATH="$(PATH):/usr/X11/bin" which freetype-config}

# for the Open Motion Controller board
CPPFLAGS := -DMOTHERBOARD=91
# libdevil-dev provides IL.pc
CPPFLAGS += ${shell pkg-config --cflags IL 2>/dev/null}
CPPFLAGS += ${shell $(FTC) --cflags}
CPPFLAGS += ${patsubst %,-I%,${subst :, ,${IPATH}}}

LDFLAGS = ${shell pkg-config --libs IL 2>/dev/null}
LDFLAGS += -Wl,-rpath $(LIBC3)/${OBJ}/.libs -L$(LIBC3)/${OBJ}/.libs -lc3 -lc3gl
LDFLAGS += -Wl,-rpath ${SIMAVR_R}/simavr/${OBJ} -L${SIMAVR_R}/simavr/${OBJ} 
LDFLAGS += -L${FTGL}/${OBJ} -lfreetype-gl ${shell $(FTC) --libs} 
LDFLAGS += -lpthread -lutil -ldl
LDFLAGS += -lm

include ${SIMAVR_R}/examples/Makefile.opengl

all: obj ${firmware} ${target}

include ${SIMAVR_R}/Makefile.common

board = ${OBJ}/${target}.elf

${board} : ${OBJ}/c3text.o
${board} : ${OBJ}/arduidiot_pins.o
${board} : ${OBJ}/button.o
${board} : ${OBJ}/uart_pty.o
${board} : ${OBJ}/thermistor.o
${board} : ${OBJ}/heatpot.o
${board} : ${OBJ}/stepper.o
${board} : ${OBJ}/${target}.o
${board} : ${OBJ}/${target}_gl.o

build-simavr:
	$(MAKE) -C $(SIMAVR_R) CC="$(CC)" CFLAGS="$(CFLAGS)" build-simavr
build-libc3:
	$(MAKE) -C $(LIBC3) CC="$(CC)" CFLAGS="$(CFLAGS)"
build-ftgl:
	$(MAKE) -C $(FTGL) CC="$(CC)" CPPFLAGS="$(CPPFLAGS)" \
		CFLAGS="$(CFLAGS)" lib

${target}:  build-simavr build-libc3 build-ftgl ${board}
	@echo $@ done

clean: clean-${OBJ}
	rm -rf *.a *.axf ${target} *.vcd
	$(MAKE) -C $(LIBC3) CC="$(CC)" CFLAGS="$(CFLAGS)" clean
	$(MAKE) -C $(SIMAVR_R)/simavr CC="$(CC)" CFLAGS="$(CFLAGS)" clean
	$(MAKE) -C $(FTGL) CC="$(CC)" CPPFLAGS="$(CPPFLAGS)" \
		CFLAGS="$(CFLAGS)" clean	

