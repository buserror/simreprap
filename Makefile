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

target=	reprap
firm_src = ${wildcard atmega*.c}
firmware = ${firm_src:.c=.hex}

SIMAVR	= shared/simavr
LIBC3	= shared/libc3

IPATH = .
IPATH += src
IPATH += $(LIBC3)/src
IPATH += $(LIBC3)/srcgl
IPATH += ${SIMAVR}/include
IPATH += ${SIMAVR}/simavr/sim
IPATH += ${SIMAVR}/examples/parts
IPATH += ${SIMAVR}/examples/shared

VPATH = src
VPATH += ${SIMAVR}/examples/parts
VPATH += ${SIMAVR}/examples/shared

# for the Open Motion Controller board
CPPFLAGS += -DMOTHERBOARD=91
CPPFLAGS += ${shell pkg-config --cflags pangocairo}

include ${SIMAVR}/examples/Makefile.opengl

LDFLAGS += ${shell pkg-config --libs pangocairo}
LDFLAGS += -lpthread -lutil -ldl
LDFLAGS += -lm
LDFLAGS += -Wl,-rpath $(LIBC3)/${OBJ}/.libs -L$(LIBC3)/${OBJ}/.libs -lc3 -lc3gl
LDFLAGS += -Wl,-rpath ${SIMAVR}/simavr/${OBJ} -L${SIMAVR}/simavr/${OBJ} 

CPPFLAGS	+= ${patsubst %,-I%,${subst :, ,${IPATH}}}

all: obj ${firmware} ${target}

include ${SIMAVR}/Makefile.common

board = ${OBJ}/${target}.elf

${board} : ${OBJ}/arduidiot_pins.o
${board} : ${OBJ}/button.o
${board} : ${OBJ}/uart_pty.o
${board} : ${OBJ}/thermistor.o
${board} : ${OBJ}/heatpot.o
${board} : ${OBJ}/stepper.o
${board} : ${OBJ}/${target}.o
${board} : ${OBJ}/${target}_gl.o

build-simavr:
	$(MAKE) -C $(SIMAVR) CC="$(CC)" CFLAGS="$(CFLAGS)" build-simavr

build-libc3:
	$(MAKE) -C $(LIBC3) CC="$(CC)" CFLAGS="$(CFLAGS)"

${target}:  build-simavr build-libc3 ${board}
	@echo $@ done

clean: clean-${OBJ}
	rm -rf *.a *.axf ${target} *.vcd
	$(MAKE) -C $(LIBC3) CC="$(CC)" CFLAGS="$(CFLAGS)" clean
	$(MAKE) -C $(SIMAVR)/simavr CC="$(CC)" CFLAGS="$(CFLAGS)" clean
	

