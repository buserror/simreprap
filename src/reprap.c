/*
	simduino.c

	Copyright 2008, 2009 Michel Pollet <buserror@gmail.com>

 	This file is part of simavr.

	simavr is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	simavr is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with simavr.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdbool.h>
#include <libgen.h>
#include <pthread.h>

#include "sim_avr.h"
#include "avr_ioport.h"
#include "sim_elf.h"
#include "sim_hex.h"
#include "sim_gdb.h"
#include "avr_uart.h"
#include "avr_twi.h"

#include "history_avr.h"
#include "reprap_gl.h"

#include "button.h"
#include "reprap.h"
#include "arduidiot_pins.h"

#define PRINTER PP_COPE
#define PROGMEM
#define MB(board) (MOTHERBOARD==BOARD_##board)

#if (MOTHERBOARD == 632)
	#define __AVR_ATmega2560__
	#include "marlin/boards.h"
	#define ARDUIDIOT_PINS arduidiot_2560
	/*
	 * A 'spurious' write to an AVR GPIO was inserted in the firmware
	 * in the idle loop, and is trapped here; it allows the system
	 * to 'sleep' a little and not eat 100% CPU on the host
	 */
	#define MEGA_GPIOR0 0x3e
#else
	#define __AVR_ATmega644__
	#define ARDUIDIOT_PINS arduidiot_644
	/*
	 * A 'spurious' write to an AVR GPIO was inserted in the firmware
	 * in the idle loop, and is trapped here; it allows the system
	 * to 'sleep' a little and not eat 100% CPU on the host
	 */
	#define MEGA_GPIOR0 0x3e
#endif

#include "marlin/pins.h"
#include "marlin/Configuration.h"

/*
 * these are the sources of heat and cold to register to the heatpots
 */
enum {
	TALLY_AMBIANT = 0,
	TALLY_HOTEND_PWM,
	TALLY_HOTEND_FAN,
	TALLY_HOTBED = 1,
};

reprap_t reprap;

avr_t * avr = NULL;
elf_firmware_t code = {0};


#include "marlin/thermistortables.h"
const short temptable_1[][2] = THERMISTOR_EPCOS_100K;

// gnu hackery to make sure the parameter is expanded
#define _TERMISTOR_TABLE(num) \
		temptable_##num

#define TERMISTOR_TABLE(num) \
		temptable_1

/*
 * called when the AVR change any of the pins on port B
 * so lets update our buffer
 */
static void
hotbed_change_hook(
		struct avr_irq_t * irq,
		uint32_t value,
		void * param)
{
	heatpot_p hot = param;
//	printf("%s %d\n", __func__, value);
	heatpot_tally(hot, TALLY_HOTBED, value ? 1.6f : 0 );
}
static void
hotend_change_hook(
		struct avr_irq_t * irq,
		uint32_t value,
		void * param)
{
	heatpot_p hot = param;
//	printf("%s %d\n", __func__, value);
	heatpot_tally(hot, TALLY_HOTEND_PWM, value ? 2.0f : 0 );
}
static void
hotend_fan_change_hook(
		struct avr_irq_t * irq,
		uint32_t value,
		void * param)
{
	heatpot_p hot = param;
//	printf("%s %d\n", __func__, value);
	heatpot_tally(hot, TALLY_HOTEND_FAN, value ? -0.05 : 0 );
}



char avr_flash_path[1024];
int avr_flash_fd = 0;

// avr special flash initalization
// here: open and map a file to enable a persistent storage for the flash memory
void avr_special_init( avr_t * avr, void *data)
{
	// open the file
	avr_flash_fd = open(avr_flash_path, O_RDWR|O_CREAT, 0644);
	if (avr_flash_fd < 0) {
		perror(avr_flash_path);
		exit(1);
	}
	// resize and map the file the file
	(void)ftruncate(avr_flash_fd, avr->flashend + 1);
	ssize_t r = read(avr_flash_fd, avr->flash, avr->flashend + 1);
	if (r != avr->flashend + 1) {
		fprintf(stderr, "unable to load flash memory\n");
		perror(avr_flash_path);
		exit(1);
	}
}

// avr special flash deinitalization
// here: cleanup the persistent storage
void avr_special_deinit( avr_t* avr, void *data)
{
	puts(__func__);
	lseek(avr_flash_fd, SEEK_SET, 0);
	ssize_t r = write(avr_flash_fd, avr->flash, avr->flashend + 1);
	if (r != avr->flashend + 1) {
		fprintf(stderr, "unable to load flash memory\n");
		perror(avr_flash_path);
	}
	close(avr_flash_fd);
	uart_pty_stop(&reprap.uart_pty);
}


static void
reprap_relief_callback(
		struct avr_t * avr,
		avr_io_addr_t addr,
		uint8_t v,
		void * param)
{
//	printf("%s write %x\n", __func__, addr);
	static uint16_t tick = 0;
	if (!(tick++ & 0xf))
		usleep(100);
}

static void *
avr_run_thread(
		void * ignore)
{
	while (1) {
		avr_run(avr);
	}
	return NULL;
}

void
reprap_init(
		avr_t * avr,
		reprap_p r)
{
	r->avr = avr;
	uart_pty_init(avr, &r->uart_pty);
	uart_pty_connect(&r->uart_pty, '0');

	heatpot_init(avr, &r->hotend, "hotend", 28.0f);
	heatpot_init(avr, &r->hotbed, "hotbed", 25.0f);

	/* Add a constant drift toward ambiant temperature, this is
	 * kept running all the time, and provide the 'cooling' effect
	 * on the heatpot.
	 * The heatpot is reevaluated very 100ms (aka HEATPOT_RESAMPLE_US)
	 */
	heatpot_tally(&r->hotend, TALLY_AMBIANT, -0.5f);
	heatpot_tally(&r->hotbed, TALLY_AMBIANT, -0.3f);

	/* HOTEND thermistor gets initialized, attached to a heatpot */
	thermistor_init(avr, &r->therm_hotend, TEMP_0_PIN,
			(short*)TERMISTOR_TABLE(TEMP_SENSOR_0),
			sizeof(TERMISTOR_TABLE(TEMP_SENSOR_0)) / sizeof(short) / 2,
			OVERSAMPLENR, 25.0f);
	/* connect heatpot temp output to thermistor */
	avr_connect_irq(r->hotend.irq + IRQ_HEATPOT_TEMP_OUT,
			r->therm_hotend.irq + IRQ_TERM_TEMP_VALUE_IN);

	/* HOTBED thermistor gets initialized, attached to a heatpot */
	thermistor_init(avr, &r->therm_hotbed, TEMP_BED_PIN,
			(short*)TERMISTOR_TABLE(TEMP_SENSOR_BED),
			sizeof(TERMISTOR_TABLE(TEMP_SENSOR_BED)) / sizeof(short) / 2,
			OVERSAMPLENR, 30.0f);
	avr_connect_irq(r->hotbed.irq + IRQ_HEATPOT_TEMP_OUT,
			r->therm_hotbed.irq + IRQ_TERM_TEMP_VALUE_IN);

	/* SPARE thermistor gets initialized, attached to a heatpot */
	thermistor_init(avr, &r->therm_spare, TEMP_1_PIN,
			(short*)TERMISTOR_TABLE(TEMP_SENSOR_1),
			sizeof(TERMISTOR_TABLE(TEMP_SENSOR_1)) / sizeof(short) / 2,
			OVERSAMPLENR, 25.0f);
	avr_connect_irq(r->hotend.irq + IRQ_HEATPOT_TEMP_OUT,
			r->therm_spare.irq + IRQ_TERM_TEMP_VALUE_IN);

/* This prints a meaningful message if PINS are not defined */
#define ARDU(_pin) (_pin == -1 ? \
			printf("%s:%d pin %s is undefined\n", \
					__func__,__LINE__,#_pin), NULL : \
		get_ardu_irq(avr, _pin, ARDUIDIOT_PINS))

	avr_irq_register_notify(
			ARDU(HEATER_0_PIN),
			hotend_change_hook, &r->hotend);
	avr_irq_register_notify(
			ARDU(HEATER_BED_PIN),
			hotbed_change_hook, &r->hotbed);
	if (FAN_PIN != -1)
		avr_irq_register_notify(
				ARDU(FAN_PIN),
				hotend_fan_change_hook, &r->hotend);

	//avr_irq_register_notify()
	float axis_pp_per_mm[] = DEFAULT_AXIS_STEPS_PER_UNIT;	// from Marlin!

	{
		avr_irq_t * e = ARDU(X_ENABLE_PIN);
		avr_irq_t * s = ARDU(X_STEP_PIN);
		avr_irq_t * d = ARDU(X_DIR_PIN);
		avr_irq_t * m = X_MIN_PIN == -1 ? ARDU(X_MAX_PIN) : ARDU(X_MIN_PIN);

		stepper_init(avr, &r->stepper[AXIS_X], "X", axis_pp_per_mm[0],
				X_MAX_POS / 2, X_MAX_POS, X_MAX_POS-1);
		stepper_connect(&r->stepper[AXIS_X], s, d, e, m, NULL,
				stepper_endstop_inverted | stepper_enable_inverted|
				stepper_direction_inverted);
	}
	{
		avr_irq_t * e = get_ardu_irq(avr, Y_ENABLE_PIN, ARDUIDIOT_PINS);
		avr_irq_t * s = get_ardu_irq(avr, Y_STEP_PIN, ARDUIDIOT_PINS);
		avr_irq_t * d = get_ardu_irq(avr, Y_DIR_PIN, ARDUIDIOT_PINS);
		avr_irq_t * m = get_ardu_irq(avr, Y_MIN_PIN, ARDUIDIOT_PINS);

		stepper_init(avr, &r->stepper[AXIS_Y], "Y", axis_pp_per_mm[1],
				Y_MAX_POS / 2, Y_MAX_POS, -1);
		stepper_connect(&r->stepper[AXIS_Y], s, d, e, m, NULL,
				stepper_endstop_inverted | stepper_enable_inverted|
				stepper_direction_inverted);
	}
	{
		avr_irq_t * e = get_ardu_irq(avr, Z_ENABLE_PIN, ARDUIDIOT_PINS);
		avr_irq_t * s = get_ardu_irq(avr, Z_STEP_PIN, ARDUIDIOT_PINS);
		avr_irq_t * d = get_ardu_irq(avr, Z_DIR_PIN, ARDUIDIOT_PINS);
		avr_irq_t * m = get_ardu_irq(avr, Z_MIN_PIN, ARDUIDIOT_PINS);

		stepper_init(avr, &r->stepper[AXIS_Z], "Z", axis_pp_per_mm[2], 20,
				Z_MAX_POS, Z_MAX_POS-1);
		stepper_connect(&r->stepper[AXIS_Z], s, d, e, m,
			get_ardu_irq(avr, Z_PROBE_PIN, ARDUIDIOT_PINS),
				stepper_endstop_inverted | stepper_enable_inverted|
				stepper_direction_inverted|stepper_zero_inverted);
	}
#if defined(E0_DIR_PIN) && E0_DIR_PIN != -1
	{
		avr_irq_t * e = get_ardu_irq(avr, E0_ENABLE_PIN, ARDUIDIOT_PINS);
		avr_irq_t * s = get_ardu_irq(avr, E0_STEP_PIN, ARDUIDIOT_PINS);
		avr_irq_t * d = get_ardu_irq(avr, E0_DIR_PIN, ARDUIDIOT_PINS);

		stepper_init(avr, &r->stepper[AXIS_E], "E", axis_pp_per_mm[3], 0, 0, 0);
		stepper_connect(&r->stepper[AXIS_E], s, d, e, NULL, NULL,
				stepper_enable_inverted|stepper_direction_inverted);
	}
#else
	{
		avr_irq_t * e = get_ardu_irq(avr, PLATFORM_ENABLE_PIN, ARDUIDIOT_PINS);
		avr_irq_t * s = get_ardu_irq(avr, PLATFORM_STEP_PIN, ARDUIDIOT_PINS);
		avr_irq_t * d = get_ardu_irq(avr, PLATFORM_DIR_PIN, ARDUIDIOT_PINS);
		avr_irq_t * m = get_ardu_irq(avr, Z_PROBE_PIN, ARDUIDIOT_PINS);

		stepper_init(avr, &r->stepper[AXIS_PLATFORM], "PLATFORM", axis_pp_per_mm[2], 10,
				20, 20 - 1);
		stepper_connect(&r->stepper[AXIS_PLATFORM], s, d, e, m, NULL,
				stepper_endstop_inverted | stepper_enable_inverted|
				stepper_direction_inverted);
	}
	{
		avr_irq_t * e = get_ardu_irq(avr, Ec_ENABLE_PIN, ARDUIDIOT_PINS);
		avr_irq_t * s = get_ardu_irq(avr, Ec_STEP_PIN, ARDUIDIOT_PINS);
		avr_irq_t * d = get_ardu_irq(avr, Ec_DIR_PIN, ARDUIDIOT_PINS);

		stepper_init(avr, &r->stepper[AXIS_CYAN], "CYAN", axis_pp_per_mm[3], 0, 0, 0);
		stepper_connect(&r->stepper[AXIS_CYAN], s, d, e, NULL, NULL,
				stepper_enable_inverted|stepper_direction_inverted);
	}
	{
		avr_irq_t * e = get_ardu_irq(avr, Ey_ENABLE_PIN, ARDUIDIOT_PINS);
		avr_irq_t * s = get_ardu_irq(avr, Ey_STEP_PIN, ARDUIDIOT_PINS);
		avr_irq_t * d = get_ardu_irq(avr, Ey_DIR_PIN, ARDUIDIOT_PINS);

		stepper_init(avr, &r->stepper[AXIS_YELLOW], "YELLOW", axis_pp_per_mm[3], 0, 0, 0);
		stepper_connect(&r->stepper[AXIS_YELLOW], s, d, e, NULL, NULL,
				stepper_enable_inverted|stepper_direction_inverted);
	}
	{
		avr_irq_t * e = get_ardu_irq(avr, Em_ENABLE_PIN, ARDUIDIOT_PINS);
		avr_irq_t * s = get_ardu_irq(avr, Em_STEP_PIN, ARDUIDIOT_PINS);
		avr_irq_t * d = get_ardu_irq(avr, Em_DIR_PIN, ARDUIDIOT_PINS);

		stepper_init(avr, &r->stepper[AXIS_MAGENTA], "MAGENTA", axis_pp_per_mm[3], 0, 0, 0);
		stepper_connect(&r->stepper[AXIS_MAGENTA], s, d, e, NULL, NULL,
				stepper_enable_inverted|stepper_direction_inverted);
	}
	{
		avr_irq_t * e = get_ardu_irq(avr, Ek_ENABLE_PIN, ARDUIDIOT_PINS);
		avr_irq_t * s = get_ardu_irq(avr, Ek_STEP_PIN, ARDUIDIOT_PINS);
		avr_irq_t * d = get_ardu_irq(avr, Ek_DIR_PIN, ARDUIDIOT_PINS);

		stepper_init(avr, &r->stepper[AXIS_KEY], "KEY", axis_pp_per_mm[3], 0, 0, 0);
		stepper_connect(&r->stepper[AXIS_KEY], s, d, e, NULL, NULL,
				stepper_enable_inverted|stepper_direction_inverted);
	}
	{
		avr_irq_t * e = get_ardu_irq(avr, Ew_ENABLE_PIN, ARDUIDIOT_PINS);
		avr_irq_t * s = get_ardu_irq(avr, Ew_STEP_PIN, ARDUIDIOT_PINS);
		avr_irq_t * d = get_ardu_irq(avr, Ew_DIR_PIN, ARDUIDIOT_PINS);

		stepper_init(avr, &r->stepper[AXIS_WHITE], "WHITE", axis_pp_per_mm[3], 0, 0, 0);
		stepper_connect(&r->stepper[AXIS_WHITE], s, d, e, NULL, NULL,
				stepper_enable_inverted|stepper_direction_inverted);
	}
	{
		avr_irq_t * e = get_ardu_irq(avr, MIXER_ENABLE_PIN, ARDUIDIOT_PINS);
		avr_irq_t * s = get_ardu_irq(avr, MIXER_STEP_PIN, ARDUIDIOT_PINS);
		avr_irq_t * d = get_ardu_irq(avr, MIXER_DIR_PIN, ARDUIDIOT_PINS);

		stepper_init(avr, &r->stepper[AXIS_MIXER], "MIXER", axis_pp_per_mm[3], 0, 0, 0);
		stepper_connect(&r->stepper[AXIS_MIXER], s, d, e, NULL, NULL,
				stepper_enable_inverted|stepper_direction_inverted);
	}
	{
		avr_irq_t * e = get_ardu_irq(avr, ER_ENABLE_PIN, ARDUIDIOT_PINS);
		avr_irq_t * s = get_ardu_irq(avr, ER_STEP_PIN, ARDUIDIOT_PINS);
		avr_irq_t * d = get_ardu_irq(avr, ER_DIR_PIN, ARDUIDIOT_PINS);

		stepper_init(avr, &r->stepper[AXIS_RETRACT], "RETRACT", axis_pp_per_mm[3], 0, 0, 0);
		stepper_connect(&r->stepper[AXIS_RETRACT], s, d, e, NULL, NULL,
				stepper_enable_inverted|stepper_direction_inverted);
	}
#endif

	avr_vcd_init(r->avr, "simreprap_trace.vcd", &r->vcd_file, 1000 /*usec*/);
	avr_vcd_add_signal(&r->vcd_file, ARDU(HEATER_0_PIN), 1, "HEATER_0_PIN");
	avr_vcd_add_signal(&r->vcd_file, ARDU(HEATER_BED_PIN), 1, "HEATER_BED_PIN");

	const char * nm[IRQ_STEPPER_COUNT] = {
			"DIR", "STEP", "E",
	};
	for (int i = 0; i < AXIS_COUNT; i++) {
		stepper_p s = &r->stepper[i];
		for (int qi = 0; qi < IRQ_STEPPER_COUNT; qi++) {
			if (!nm[qi])
				continue;
			char name[32];
			sprintf(name, "%s_%s", s->name, nm[qi]);
			avr_vcd_add_signal(&r->vcd_file, s->irq + qi, 1, name);
		}
	}
	avr_irq_t * irq;
	irq = avr_get_interrupt_irq(r->avr, AVR_INT_ANY);
	irq[0].flags |= IRQ_FLAG_FILTERED;
	irq[1].flags |= IRQ_FLAG_FILTERED;
	avr_vcd_add_signal(&r->vcd_file, irq, 1, "I_G_PENDING");
	avr_vcd_add_signal(&r->vcd_file, irq + 1, 8, "I_G_RUNNING");

#define USART0_RX_vect_num 25
	irq = avr_get_interrupt_irq(r->avr, USART0_RX_vect_num);
	irq[0].flags |= IRQ_FLAG_FILTERED;
	irq[1].flags |= IRQ_FLAG_FILTERED;
	avr_vcd_add_signal(&r->vcd_file, irq, 1, "I_UART0_RX_P");
	avr_vcd_add_signal(&r->vcd_file, irq + 1, 1, "I_UART0_RX_R");

#define TIMER1_COMPA_vect_num	17
	irq = avr_get_interrupt_irq(r->avr, TIMER1_COMPA_vect_num);
	irq[0].flags |= IRQ_FLAG_FILTERED;
	irq[1].flags |= IRQ_FLAG_FILTERED;
	avr_vcd_add_signal(&r->vcd_file, irq, 1, "I_T1CA_P");
	avr_vcd_add_signal(&r->vcd_file, irq + 1, 1, "I_T1CA_R");

#define TIMER3_COMPA_vect_num	32
	irq = avr_get_interrupt_irq(r->avr, TIMER3_COMPA_vect_num);
	irq[0].flags |= IRQ_FLAG_FILTERED;
	irq[1].flags |= IRQ_FLAG_FILTERED;
	avr_vcd_add_signal(&r->vcd_file, irq, 1, "I_T3CA_P");
	avr_vcd_add_signal(&r->vcd_file, irq + 1, 1, "I_T3CA_R");

#define TIMER3_COMPC_vect_num	34
	irq = avr_get_interrupt_irq(r->avr, TIMER3_COMPC_vect_num);
	irq[0].flags |= IRQ_FLAG_FILTERED;
	irq[1].flags |= IRQ_FLAG_FILTERED;
	avr_vcd_add_signal(&r->vcd_file, irq, 1, "I_T3CC_P");
	avr_vcd_add_signal(&r->vcd_file, irq + 1, 1, "I_T3CC_R");

#define TIMER0_COMPA_vect_num	21
	irq = avr_get_interrupt_irq(r->avr, TIMER0_COMPA_vect_num);
	irq[0].flags |= IRQ_FLAG_FILTERED;
	irq[1].flags |= IRQ_FLAG_FILTERED;
	avr_vcd_add_signal(&r->vcd_file, irq, 1, "I_T0CA_P");
	avr_vcd_add_signal(&r->vcd_file, irq + 1, 1, "I_T0CA_R");

	avr_vcd_add_signal(&r->vcd_file,
			avr_io_getirq(r->avr, AVR_IOCTL_UART_GETIRQ('0'), UART_IRQ_INPUT), 8,
			"UART_IN");

	avr_vcd_add_signal(&r->vcd_file,
			avr_io_getirq(r->avr, AVR_IOCTL_TWI_GETIRQ(0), TWI_IRQ_STATUS), 8,
			"TWI_STATE");

	/* Init the path plotter engine */
	pathplot_init(avr, &r->pathplot, r->stepper);
}

bool recording = false;
static int
_cmd_record(
		wordexp_t * l)
{
	if (!recording) {
		printf("Starting VCD trace; press 's' to stop\n");
		avr_vcd_start(&reprap.vcd_file);
		recording = true;
	} else {
		printf("Already recording! Press 's' to stop first...\n");
	}
	return 0;
}

static const history_cmd_t cmd_record = {
	.names = { "record", "r", "vcd", },
	.usage = "start recording the VCD file",
	.help = "start recording the VCD file",
	.parameter_map = 0,
	.execute = _cmd_record,
};
HISTORY_CMD_REGISTER(cmd_record);

static int
_cmd_stop(
		wordexp_t * l)
{
	if (!recording && !reprap.pathplot.last_cycle) {
		printf("Not recording Anything! Press 'r' or 'p' first!\n");
	}
	if (recording) {
		printf("Stopping VCD trace\n");
		avr_vcd_stop(&reprap.vcd_file);
		recording = false;
	}
	if (reprap.pathplot.last_cycle) {
		printf("Generating SVG from recording; migth take a while\n");
		pathplot_stop(&reprap.pathplot, "pathplot.svg");
		printf("SVG Generated\n");
	}
	return 0;
}

static const history_cmd_t cmd_stop = {
	.names = { "stop", "s", },
	.usage = "stop recording the VCD/SVG file",
	.help = "stop recording the VCD or SVG file",
	.parameter_map = 0,
	.execute = _cmd_stop,
};
HISTORY_CMD_REGISTER(cmd_stop);

static int
_cmd_plot(
		wordexp_t * l)
{
	if (reprap.pathplot.last_cycle) {
		printf("Already recording a path; press 'l' to stop\n");
	} else {
		printf("Now recording X&Y as a path; press 'l' to stop\n");
		pathplot_start(&reprap.pathplot);
	}
	return 0;
}

static const history_cmd_t cmd_plot = {
	.names = { "plot", "pl" },
	.usage = "start recording Steppers in a SVG file",
	.help = "start recording Steppers in a SVG file",
	.parameter_map = 0,
	.execute = _cmd_plot,
};
HISTORY_CMD_REGISTER(cmd_plot);

int main(int argc, char *argv[])
{
	int trace = 0;
	int debug = 0;
	int nogui = 0;

	char path[256];
	strcpy(path, argv[0]);
	strcpy(path, dirname(path));
	strcpy(path, dirname(path));
	printf("Stripped base directory to '%s'\n", path);
	chdir(path);

	char fname[1024] = "";

	for (int i = 1; i < argc; ++i)
		if (!strcmp(argv[i], "-d"))
			debug++;
		else if (!strcmp(argv[i], "-t"))
			trace++;
		else if (!strcmp(argv[i], "-n"))
			nogui++;
		else {
			strncpy(fname, argv[i], 1024);
			printf("Loading: %s\n", fname);
		}

	if (0 == strlen(fname)) {
		printf("Missing filename.\n");
		printf("Usage: %s [-d -t -n...] filename.elf|filename.hex\n", argv[0]);
		exit(2);
	}

	avr = avr_make_mcu_by_name("atmega2560");
	if (!avr) {
		fprintf(stderr, "%s: Error creating the AVR core\n", argv[0]);
		exit(1);
	}
//	snprintf(avr_flash_path, sizeof(avr_flash_path), "%s/%s", pwd, "simduino_flash.bin");
	strcpy(avr_flash_path,  "reprap_flash.bin");
	// register our own functions
	avr->custom.init = avr_special_init;
	avr->custom.deinit = avr_special_deinit;
	avr_init(avr);
//	avr->frequency = 20000000;
	avr->frequency = 16000000;
	avr->aref = avr->avcc = avr->vcc = 5 * 1000;	// needed for ADC
	avr->log = 2;

	elf_firmware_t f;

	// try to load an ELF file, before trying the .hex
	if ((!strcmp(fname + strlen(fname)-4, ".axf") ||
			!strcmp(fname + strlen(fname)-4, ".elf")) &&
			elf_read_firmware(fname, &f) == 0) {
		printf("firmware %s f=%d mmcu=%s\n", fname, (int)f.frequency, f.mmcu);
		avr_load_firmware(avr, &f);
		code = f; // global

#if 1
		printf("RAMEND %d 0x%04x\n", avr->ramend, avr->ramend);
		for (int i = 0; i < code.symbolcount; i++) {
			if ((code.symbol[i]->addr >> 16) == 0x80 &&
					strncmp(code.symbol[i]->symbol, "__", 2) &&
					code.symbol[i]->symbol[0] != ' ' &&
					strcmp(code.symbol[i]->symbol, "_edata") &&
					strcmp(code.symbol[i]->symbol, "_end"))
				printf("%05x %s\n", code.symbol[i]->addr & 0xfffff,
						code.symbol[i]->symbol);
		}
#endif

	} else if (!strcmp(fname + strlen(fname)-4, ".hex")) {
		ihex_chunk_p chunks = NULL;
		int count = read_ihex_chunks(fname, &chunks);

		if (count <= 0) {
			fprintf(stderr, "%s: Unable to load %s\n", argv[0], fname);
			exit(1);
		}
		for (int i = 0; i < count; i++) {
			printf("%s %05x(%05x in AVR talk): %d bytes (%d words)\n", fname,
					chunks[i].baseaddr, chunks[i].baseaddr/2,
					chunks[i].size, chunks[i].size/2);
			memcpy(avr->flash + chunks[i].baseaddr, chunks[i].data,
					chunks[i].size);
			if (chunks[i].baseaddr > avr->reset_pc)
				avr->pc = avr->reset_pc = chunks[i].baseaddr;
			if (chunks[i].baseaddr + chunks[i].size > avr->codeend)
				avr->codeend = chunks[i].baseaddr + chunks[i].size;
		}
		free_ihex_chunks(chunks);
	} else {
		printf("No idea how to load '%s'\n", fname);
	}
	avr->trace = trace;

	// even if not setup at startup, activate gdb if crashing
	avr->gdb_port = 1234;
	if (debug) {
		printf("AVR is stopped, waiting on gdb on port %d. "
				"Use 'target remote :%d' in avr-gdb\n",
				avr->gdb_port, avr->gdb_port);
		avr->state = cpu_Stopped;
		avr_gdb_init(avr);
	}

	// Marlin doesn't loop, sleep, so we don't know when it's idle
	// I changed Marlin to do a spurious write to the GPIOR0 register
	// so we can trap it
	avr_register_io_write(avr, MEGA_GPIOR0, reprap_relief_callback, NULL);

	reprap_init(avr, &reprap);

	history_avr_init();

	pthread_t run;
	pthread_create(&run, NULL, avr_run_thread, NULL);

	if (0 == nogui) {
		gl_init(argc, argv);
		gl_runloop();
	} else {
		while (1) {
			history_avr_idle();
		}
	}
}
