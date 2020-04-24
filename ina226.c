#include <stdio.h>
#include <errno.h>
#include <stdint.h>
#include <time.h>
#include <math.h>
#include <unistd.h>
#include <wiringPiI2C.h>
#include <argp.h>
#include <stdbool.h>
#include "ina226.h"

#define INA226_ADDRESS 0x40

int fd;
uint64_t config;
float current_lsb;

const char *argp_program_version = "ina226 1.0";
const char *argp_program_bug_address = "https://github.com/jmfife/rpi-ina226";
static char doc[] = "Use INA226 chip in conjunction with a Raspberry Pi to measure DC voltage and current";
static char args_doc[] = "";
static struct argp_option options[] = {
	{ "emulate", 'e', 0, 0, "Run in emulation mode"},
	{ 0 }
};

struct arguments {
	bool emulate;
};

static error_t parse_opt(int key, char *arg, struct argp_state *state) {
	struct arguments *arguments = state->input;
	switch (key) {
		case 'e': arguments->emulate = true; break;
		case ARGP_KEY_ARG: return 0;
		default: return ARGP_ERR_UNKNOWN;
	}
	return 0;
}

static struct argp argp = { options, parse_opt, args_doc, doc, 0, 0, 0 };

uint16_t read16(int fd, uint8_t ad){
	uint16_t result = wiringPiI2CReadReg16(fd,ad);
	// Chip uses different endian
	return  (result<<8) | (result>>8);
}

void write16(int fd, uint8_t ad, uint16_t value) {
	// Chip uses different endian
	wiringPiI2CWriteReg16(fd,ad,(value<<8)|(value>>8));
}

// R of shunt resistor in ohm. Max current in Amp
void ina226_calibrate(float r_shunt, float max_current) {
	current_lsb = max_current / (1 << 15);
	float calib = 0.00512 / (current_lsb * r_shunt);
	uint16_t calib_reg = (uint16_t) floorf(calib);
	current_lsb = 0.00512 / (r_shunt * calib_reg);

	//printf("LSB %f\n",current_lsb);
	//printf("Calib %f\n",calib);
	//printf("Calib R%#06x / %d\n",calib_reg,calib_reg);

	write16(fd,INA226_REG_CALIBRATION, calib_reg);
}

void ina226_configure(uint8_t bus, uint8_t shunt, uint8_t average, uint8_t mode) {
	config = (average<<9) | (bus<<6) | (shunt<<3) | mode;
	write16(fd,INA226_REG_CONFIGURATION, config);
}

uint16_t ina226_conversion_ready() {
	return read16(fd,INA226_REG_MASK_ENABLE) & INA226_MASK_ENABLE_CVRF;
}

void ina226_wait() {
	uint8_t average = (config>>9) & 7;
	uint8_t bus = (config>>6) & 7;
	uint8_t shunt = (config>>3) & 7;

	uint32_t total_wait = (wait[bus] + wait[shunt] + (average ? avgwaits[bus>shunt ? bus : shunt] : 0)) * averages[average];

	usleep(total_wait+1000);

	int count=0;
	while(!ina226_conversion_ready()){
		count++;
	}
	//printf("%d\n",count);
}

void ina226_read(float *voltage, float *current, float *power, float* shunt_voltage) {
	if (voltage) {
		uint16_t voltage_reg = read16(fd,INA226_REG_BUS_VOLTAGE);
		*voltage = (float) voltage_reg * 1.25e-3;
	}

	if (current) {
		int16_t current_reg = (int16_t) read16(fd,INA226_REG_CURRENT);
		*current = (float) current_reg * 1000.0 * current_lsb;
	}

	if (power) {
		int16_t power_reg = (int16_t) read16(fd,INA226_REG_POWER);
		*power = (float) power_reg * 25000.0 * current_lsb;
	}

	if (shunt_voltage) {
		int16_t shunt_voltage_reg = (int16_t) read16(fd,INA226_REG_SHUNT_VOLTAGE);
		*shunt_voltage = (float) shunt_voltage_reg * 2.5e-3;
	}
}

inline void ina226_reset() {
	write16(fd, INA226_REG_CONFIGURATION, config = INA226_RESET);
}

inline void ina226_disable() {
	write16(fd, INA226_REG_CONFIGURATION, config = INA226_MODE_OFF);
}

int main(int argc, char *argv[]) {
	struct arguments arguments;

	arguments.emulate = false;

	argp_parse(&argp, argc, argv, 0, 0, &arguments);

	// const float kwh_price = 0.13;
	float voltage, current, power, shunt;
	// float energy, price;
	time_t rawtime;
	// char buffer[80];
	// int trig=1;

	if !arguments.emulate {
		fd = wiringPiI2CSetup(INA226_ADDRESS);
		}
		if(fd < 0) {
			printf("Device not found");
			return -1;
		}

		//printf("Manufacturer 0x%X Chip 0x%X\n",read16(fd,INA226_REG_MANUFACTURER),read16(fd,INA226_REG_DIE_ID));

		// Shunt resistor (Ohm), Max Current (Amp)
		ina226_calibrate(0.009055, 100.0);

		// BUS / SHUNT / Averages / Mode
		ina226_configure(INA226_TIME_8MS, INA226_TIME_8MS, INA226_AVERAGES_16, INA226_MODE_SHUNT_BUS_CONTINUOUS);
	}

	// Header
	//printf("Time, timestamp, bus voltage(V), current (mA), power (mW), shunt voltage (mV), annual energy (kWh), cost ($)\n");

	for(;;){
		//ina226_configure(INA226_TIME_8MS, INA226_TIME_8MS, INA226_AVERAGES_16, INA226_MODE_SHUNT_BUS_TRIGGERED);
		//ina226_wait();

		if !arguments.emulate {
			// Read
			ina226_read(&voltage, &current, &power, &shunt);
			// energy = voltage*current*24*365.25/1000000;
			// price = energy * kwh_price;
		 }

		// Timestamp / Date
		time(&rawtime);
		// struct tm *info = localtime( &rawtime );
		// strftime(buffer,80,"%Y-%m-%d %H:%M:%S", info);

		// printf("%s, %d, %.3f, %.3f, %.3f, %.3f, %.3f, %.2f\n",buffer,(int)rawtime,voltage,current,voltage*current,shunt,energy,price);
		printf("{\"ts\": %d, \"Voltage_V\": %.3f, \"Current_mA\": %.3f}\n", (int)rawtime, voltage, current);
		fflush(NULL);

		usleep(5000000);
	}

	ina226_disable();

	return 0;
}
