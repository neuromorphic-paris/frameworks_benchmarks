#ifndef LIBCAER_SRC_DAVIS_RPI_H_
#define LIBCAER_SRC_DAVIS_RPI_H_

#include "davis_common.h"

#define DAVIS_RPI_DEVICE_NAME "DAVISRPi"

#define DAVIS_RPI_REQUIRED_LOGIC_REVISION 18

#define DAVIS_RPI_MAX_TRANSACTION_NUM 4096
#define DAVIS_RPI_MAX_WAIT_REQ_COUNT 100

/**
 * Support benchmarking the GPIO data exchange performance on RPi,
 * using the appropriate StreamTester logic (MachXO3_IoT).
 */
#define DAVIS_RPI_BENCHMARK 0

#define DAVIS_RPI_BENCHMARK_LIMIT_BYTES (8 * 1024 * 1024)

enum benchmarkMode {
	ZEROS       = 0,
	ONES        = 1,
	COUNTER     = 2,
	SWITCHING   = 3,
	ALTERNATING = 4,
};

// Alternative, simplified biasing support.
#define DAVIS_BIAS_ADDRESS_MAX 36
#define DAVIS_CHIP_REG_LENGTH 7

struct davis_rpi_gpio {
	volatile uint32_t *gpioReg;
	int spiFd;
	mtx_t spiLock;
	atomic_uint_fast32_t threadState;
	thrd_t thread;
	void (*shutdownCallback)(void *shutdownCallbackPtr);
	void *shutdownCallbackPtr;
};

typedef struct davis_rpi_gpio *davisRPiGPIO;

struct davis_rpi_handle {
	struct davis_common_handle cHandle;
	// Data transfer via GPIO for RPi IoT variant.
	struct davis_rpi_gpio gpio;
	// Data transfer benchmarking and testing.
#if DAVIS_RPI_BENCHMARK == 1
	struct {
		enum benchmarkMode testMode;
		uint16_t expectedValue;
		size_t dataCount;
		size_t errorCount;
		struct timespec startTime;
	} benchmark;
#endif
	// Bias/chip config register control.
	struct {
		uint8_t currentBiasArray[DAVIS_BIAS_ADDRESS_MAX + 1][2];
		uint8_t currentChipRegister[DAVIS_CHIP_REG_LENGTH];
	} biasing;
};

typedef struct davis_rpi_handle *davisRPiHandle;

ssize_t davisRPiFind(caerDeviceDiscoveryResult *discoveredDevices);

// busNumberRestrict, devAddressRestrict and serialNumberRestrict are ignored, only one device connected.
caerDeviceHandle davisRPiOpen(
	uint16_t deviceID, uint8_t busNumberRestrict, uint8_t devAddressRestrict, const char *serialNumberRestrict);
bool davisRPiClose(caerDeviceHandle cdh);

bool davisRPiSendDefaultConfig(caerDeviceHandle cdh);
// Negative addresses are used for host-side configuration.
// Positive addresses (including zero) are used for device-side configuration.
bool davisRPiConfigSet(caerDeviceHandle cdh, int8_t modAddr, uint8_t paramAddr, uint32_t param);
bool davisRPiConfigGet(caerDeviceHandle cdh, int8_t modAddr, uint8_t paramAddr, uint32_t *param);

bool davisRPiDataStart(caerDeviceHandle handle, void (*dataNotifyIncrease)(void *ptr),
	void (*dataNotifyDecrease)(void *ptr), void *dataNotifyUserPtr, void (*dataShutdownNotify)(void *ptr),
	void *dataShutdownUserPtr);
bool davisRPiDataStop(caerDeviceHandle handle);
caerEventPacketContainer davisRPiDataGet(caerDeviceHandle handle);

#endif /* LIBCAER_SRC_DAVIS_RPI_H_ */
