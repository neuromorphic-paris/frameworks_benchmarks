#include "davis_rpi.h"

#include <fcntl.h>
#include <linux/spi/spidev.h>
#include <linux/types.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#define PIZERO_PERI_BASE 0x20000000
#define GPIO_REG_BASE (PIZERO_PERI_BASE + 0x200000) /* GPIO controller */
#define GPIO_REG_LEN 0xB4

// GPIO setup macros. Always use GPIO_INP(x) before using GPIO_OUT(x) or GPIO_ALT(x, y).
//#define GPIO_INP(gpioReg, gpioId) gpioReg[(gpioId) / 10] &= U32T(~(7 << (((gpioId) % 10) * 3)))
//#define GPIO_OUT(gpioReg, gpioId) gpioReg[(gpioId) / 10] |= U32T(1 << (((gpioId) % 10) * 3))
//#define GPIO_ALT(gpioReg, gpioId, altFunc) gpioReg[(gpioId) / 10] |= U32T(((altFunc) <= 3 ? (altFunc) + 4 : (altFunc)
//== 4 ? 3 : 2) << (((gpioId) % 10) * 3))

#define GPIO_SET(gpioReg, gpioId) gpioReg[7] = U32T(1 << (gpioId))  // sets   bits which are 1 ignores bits which are 0
#define GPIO_CLR(gpioReg, gpioId) gpioReg[10] = U32T(1 << (gpioId)) // clears bits which are 1 ignores bits which are 0

#define GPIO_GET(gpioReg, gpioId) (gpioReg[13] & U32T(1 << (gpioId))) // 0 if LOW, (1<<g) if HIGH

#define SPI_DEVICE0_CS0 "/dev/spidev0.0"
#define SPI_BITS_PER_WORD 8
#define SPI_SPEED_HZ (8 * 1000 * 1000)

#define GPIO_AER_REQ 5
#define GPIO_AER_ACK 3

// Data is in GPIOs 12-27, so just shift and mask (by cast to 16bit).
#define GPIO_AER_DATA0(gpioReg) U8T(gpioReg[13] >> 12)
#define GPIO_AER_DATA1(gpioReg) U8T(gpioReg[13] >> 20)

static bool initRPi(davisRPiHandle handle);
static void closeRPi(davisRPiHandle handle);

static bool gpioThreadStart(davisRPiHandle handle);
static void gpioThreadStop(davisRPiHandle handle);
static int gpioThreadRun(void *handlePtr);

static void davisRPiDataTranslator(davisRPiHandle handle, const uint8_t *buffer, size_t bufferSize);

static bool spiInit(davisRPiGPIO gpio);
static void spiClose(davisRPiGPIO gpio);
static bool spiSend(davisRPiGPIO gpio, uint8_t moduleAddr, uint8_t paramAddr, uint32_t param);
static bool spiReceive(davisRPiGPIO gpio, uint8_t moduleAddr, uint8_t paramAddr, uint32_t *param);

static bool handleChipBiasSend(davisRPiHandle state, uint8_t paramAddr, uint32_t param);
static bool handleChipBiasReceive(davisRPiHandle state, uint8_t paramAddr, uint32_t *param);

#if DAVIS_RPI_BENCHMARK == 1
static void davisRPiBenchmarkDataTranslator(davisRPiHandle handle, const uint8_t *buffer, size_t bufferSize);
static void setupGPIOTest(davisRPiHandle handle, enum benchmarkMode mode);
static void shutdownGPIOTest(davisRPiHandle handle);
#endif

ssize_t davisRPiFind(caerDeviceDiscoveryResult *discoveredDevices) {
	// Set to NULL initially (for error return).
	*discoveredDevices = NULL;

	// Try to open device to see if we are on a valid RPi.
	caerLogDisable(true);
	caerDeviceHandle rpi = davisRPiOpen(0, 0, 0, NULL);
	caerLogDisable(false);

	if (rpi == NULL && errno == CAER_ERROR_MEMORY_ALLOCATION) {
		// Memory allocation failure.
		return (-1);
	}

	if (rpi == NULL && errno == CAER_ERROR_OPEN_ACCESS) {
		// Cannot open, assume none present.
		return (0);
	}

	// Could open device, there can only ever be one, so allocate memory
	// and get all needed information into it.
	*discoveredDevices = calloc(1, sizeof(struct caer_device_discovery_result));
	if (*discoveredDevices == NULL) {
		if (rpi != NULL) {
			davisRPiClose(rpi);
		}
		return (-1);
	}

	// Transform from generic USB format into device discovery one.
	(*discoveredDevices)[0].deviceType         = CAER_DEVICE_DAVIS_RPI;
	(*discoveredDevices)[0].deviceErrorOpen    = (errno == CAER_ERROR_COMMUNICATION); // SPI open failure.
	(*discoveredDevices)[0].deviceErrorVersion = (errno == CAER_ERROR_LOGIC_VERSION); // Version check failure.
	struct caer_davis_info *davisInfoPtr       = &((*discoveredDevices)[0].deviceInfo.davisInfo);

	if (rpi != NULL) {
		*davisInfoPtr = caerDavisInfoGet(rpi);
	}

	// Set/Reset to invalid values, not part of discovery.
	davisInfoPtr->deviceID     = -1;
	davisInfoPtr->deviceString = NULL;

	if (rpi != NULL) {
		davisRPiClose(rpi);
	}
	return (1);
}

static bool initRPi(davisRPiHandle handle) {
	davisRPiGPIO gpio = &handle->gpio;

	errno = 0;

	// Ensure global FDs are always uninitialized.
	gpio->spiFd = -1;

	int devGpioMemFd = open("/dev/gpiomem", O_RDWR | O_SYNC);
	if (devGpioMemFd < 0) {
		davisLog(CAER_LOG_CRITICAL, &handle->cHandle, "Failed to open '/dev/gpiomem'.");
		errno = CAER_ERROR_OPEN_ACCESS;
		return (false);
	}

	gpio->gpioReg
		= mmap(NULL, GPIO_REG_LEN, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_LOCKED, devGpioMemFd, GPIO_REG_BASE);

	close(devGpioMemFd);

	if (gpio->gpioReg == MAP_FAILED) {
		davisLog(CAER_LOG_CRITICAL, &handle->cHandle, "Failed to map GPIO memory region.");
		errno = CAER_ERROR_OPEN_ACCESS;
		return (false);
	}

	// Setup SPI. Upload done by separate tool, at boot.
	if (!spiInit(gpio)) {
		closeRPi(handle);

		davisLog(CAER_LOG_CRITICAL, &handle->cHandle, "Failed to initialize SPI.");
		errno = CAER_ERROR_COMMUNICATION;
		return (false);
	}

	// Initialize SPI lock last! This avoids having to track its initialization status
	// separately. We also don't have to destroy it in closeRPi(), but only later in exit.
	if (mtx_init(&gpio->spiLock, mtx_plain) != thrd_success) {
		closeRPi(handle);

		davisLog(CAER_LOG_CRITICAL, &handle->cHandle, "Failed to initialize SPI lock.");
		errno = CAER_ERROR_COMMUNICATION;
		return (false);
	}

#if DAVIS_RPI_BENCHMARK == 0
	// After CPLD reset, query logic version.
	uint32_t param = 0;
	spiConfigReceive(handle->cHandle.spiConfigPtr, DAVIS_CONFIG_SYSINFO, DAVIS_CONFIG_SYSINFO_LOGIC_VERSION, &param);

	if (param < DAVIS_RPI_REQUIRED_LOGIC_REVISION) {
		closeRPi(handle);
		mtx_destroy(&gpio->spiLock);

		davisLog(CAER_LOG_CRITICAL, &handle->cHandle,
			"Device logic version incorrect. You have version %" PRIu32 "; but version %" PRIu32
			" is required. Please update by following the Flashy documentation at "
			"'https://inivation.com/support/software/reflashing/'.",
			param, DAVIS_RPI_REQUIRED_LOGIC_REVISION);
		errno = CAER_ERROR_LOGIC_VERSION;
		return (false);
	}
#endif

	return (true);
}

static void closeRPi(davisRPiHandle handle) {
	davisRPiGPIO gpio = &handle->gpio;

	// SPI lock mutex destroyed in main exit.
	spiClose(gpio);

	// Unmap GPIO memory region.
	munmap((void *) gpio->gpioReg, GPIO_REG_LEN);
}

caerDeviceHandle davisRPiOpen(
	uint16_t deviceID, uint8_t busNumberRestrict, uint8_t devAddressRestrict, const char *serialNumberRestrict) {
	(void) (busNumberRestrict);
	(void) (devAddressRestrict);
	(void) (serialNumberRestrict);

	errno = 0;

	caerLog(CAER_LOG_DEBUG, __func__, "Initializing %s.", DAVIS_RPI_DEVICE_NAME);

	davisRPiHandle handle = calloc(1, sizeof(*handle));
	if (handle == NULL) {
		// Failed to allocate memory for device handle!
		caerLog(CAER_LOG_CRITICAL, __func__, "Failed to allocate memory for device handle.");

		errno = CAER_ERROR_MEMORY_ALLOCATION;
		return (NULL);
	}

	// Set main deviceType correctly right away.
	handle->cHandle.deviceType = CAER_DEVICE_DAVIS_RPI;

	// Setup common handling.
	handle->cHandle.spiConfigPtr = handle;

	davisCommonState state = &handle->cHandle.state;

	// Initialize state variables to default values (if not zero, taken care of by calloc above).
	dataExchangeSettingsInit(&state->dataExchange);

	// Packet settings (size (in events) and time interval (in µs)).
	containerGenerationSettingsInit(&state->container);

	// Logging settings (initialize to global log-level).
	enum caer_log_level globalLogLevel = caerLogLevelGet();
	atomic_store(&state->deviceLogLevel, globalLogLevel);

	// Set device string.
	size_t fullLogStringLength = (size_t) snprintf(NULL, 0, "%s ID-%" PRIu16, DAVIS_RPI_DEVICE_NAME, deviceID);

	char *fullLogString = malloc(fullLogStringLength + 1);
	if (fullLogString == NULL) {
		caerLog(CAER_LOG_CRITICAL, __func__, "Failed to allocate memory for device string.");

		free(handle);

		errno = CAER_ERROR_MEMORY_ALLOCATION;
		return (NULL);
	}

	snprintf(fullLogString, fullLogStringLength + 1, "%s ID-%" PRIu16, DAVIS_RPI_DEVICE_NAME, deviceID);

	handle->cHandle.info.deviceString = fullLogString;

	// Open the DAVIS device on the Raspberry Pi.
	if (!initRPi(handle)) {
		davisLog(CAER_LOG_CRITICAL, &handle->cHandle, "Failed to open device.");

		free(handle->cHandle.info.deviceString);
		free(handle);

		// errno set by initRPi().
		return (NULL);
	}

	// Populate info variables based on data from device.
	handle->cHandle.info.deviceID = I16T(deviceID);
	strncpy(handle->cHandle.info.deviceSerialNumber, "0001", 4 + 1);
	handle->cHandle.info.deviceUSBBusNumber     = 0;
	handle->cHandle.info.deviceUSBDeviceAddress = 0;

	handle->cHandle.info.firmwareVersion = 1;

	uint32_t param32 = 0;
	spiConfigReceive(handle->cHandle.spiConfigPtr, DAVIS_CONFIG_SYSINFO, DAVIS_CONFIG_SYSINFO_LOGIC_VERSION, &param32);
	handle->cHandle.info.logicVersion = I16T(param32);

	davisCommonInit(&handle->cHandle);

	davisLog(CAER_LOG_DEBUG, &handle->cHandle, "Initialized device successfully.");

	return ((caerDeviceHandle) handle);
}

bool davisRPiClose(caerDeviceHandle cdh) {
	davisRPiHandle handle = (davisRPiHandle) cdh;

	davisLog(CAER_LOG_DEBUG, &handle->cHandle, "Shutting down ...");

	// Close the device fully.
	// Destroy SPI mutex here, as it is initialized for sure.
	closeRPi(handle);
	mtx_destroy(&handle->gpio.spiLock);

	davisLog(CAER_LOG_DEBUG, &handle->cHandle, "Shutdown successful.");

	// Free memory.
	free(handle->cHandle.info.deviceString);
	free(handle);

	return (true);
}

bool davisRPiSendDefaultConfig(caerDeviceHandle cdh) {
	davisRPiHandle handle = (davisRPiHandle) cdh;

	// First send default chip/bias config.
	if (!davisCommonSendDefaultChipConfig(&handle->cHandle)) {
		return (false);
	}

	// Send default FPGA config.
	if (!davisCommonSendDefaultFPGAConfig(&handle->cHandle)) {
		return (false);
	}

	return (true);
}

bool davisRPiConfigSet(caerDeviceHandle cdh, int8_t modAddr, uint8_t paramAddr, uint32_t param) {
	davisRPiHandle handle = (davisRPiHandle) cdh;

	if (modAddr == DAVIS_CONFIG_DDRAER) {
		switch (paramAddr) {
			case DAVIS_CONFIG_DDRAER_RUN:
				return (spiConfigSend(handle->cHandle.spiConfigPtr, DAVIS_CONFIG_DDRAER, paramAddr, param));
				break;

			default:
				return (false);
				break;
		}
	}

	// Common config call.
	return (davisCommonConfigSet(&handle->cHandle, modAddr, paramAddr, param));
}

bool davisRPiConfigGet(caerDeviceHandle cdh, int8_t modAddr, uint8_t paramAddr, uint32_t *param) {
	davisRPiHandle handle = (davisRPiHandle) cdh;

	if (modAddr == DAVIS_CONFIG_DDRAER) {
		switch (paramAddr) {
			case DAVIS_CONFIG_DDRAER_RUN:
				return (spiConfigReceive(handle->cHandle.spiConfigPtr, DAVIS_CONFIG_DDRAER, paramAddr, param));
				break;

			default:
				return (false);
				break;
		}
	}

	// Common config call.
	return (davisCommonConfigGet(&handle->cHandle, modAddr, paramAddr, param));
}

static bool gpioThreadStart(davisRPiHandle handle) {
	// Start GPIO communication thread.
	if ((errno = thrd_create(&handle->gpio.thread, &gpioThreadRun, handle)) != thrd_success) {
		davisLog(CAER_LOG_CRITICAL, &handle->cHandle, "Failed to create GPIO thread. Error: %d.", errno);
		return (false);
	}

	while (atomic_load(&handle->gpio.threadState) == THR_IDLE) {
		thrd_yield();
	}

	return (true);
}

static void gpioThreadStop(davisRPiHandle handle) {
	// Shut down GPIO communication thread.
	atomic_store(&handle->gpio.threadState, THR_EXITED);

	// Wait for GPIO communication thread to terminate.
	if ((errno = thrd_join(handle->gpio.thread, NULL)) != thrd_success) {
		// This should never happen!
		davisLog(CAER_LOG_CRITICAL, &handle->cHandle, "Failed to join GPIO thread. Error: %d.", errno);
	}
}

static int gpioThreadRun(void *handlePtr) {
	davisRPiHandle handle = handlePtr;
	davisRPiGPIO gpio     = &handle->gpio;

	davisLog(CAER_LOG_DEBUG, &handle->cHandle, "Starting GPIO communication thread ...");

	// Set device thread name. Maximum length of 15 chars due to Linux limitations.
	char threadName[MAX_THREAD_NAME_LENGTH + 1]; // +1 for terminating NUL character.
	strncpy(threadName, handle->cHandle.info.deviceString, MAX_THREAD_NAME_LENGTH);
	threadName[MAX_THREAD_NAME_LENGTH] = '\0';

	thrd_set_name(threadName);

	// Allocate data memory. Up to two data points per transaction.
	uint8_t *data = malloc(DAVIS_RPI_MAX_TRANSACTION_NUM * 2 * sizeof(uint16_t));
	if (data == NULL) {
		davisLog(CAER_LOG_DEBUG, &handle->cHandle, "Failed to allocate memory for GPIO communication.");

		atomic_store(&gpio->threadState, THR_EXITED);
		return (EXIT_FAILURE);
	}

	// Signal data thread ready back to start function.
	atomic_store(&gpio->threadState, THR_RUNNING);

	davisLog(CAER_LOG_DEBUG, &handle->cHandle, "GPIO communication thread running.");

#if DAVIS_RPI_BENCHMARK == 1
	// Start GPIO testing.
	spiConfigSend(handle->cHandle.spiConfigPtr, DAVIS_CONFIG_DDRAER, DAVIS_CONFIG_DDRAER_RUN, true);
	setupGPIOTest(handle, ZEROS);
#endif

	// Handle GPIO port reading.
	while (atomic_load_explicit(&gpio->threadState, memory_order_relaxed) == THR_RUNNING) {
		size_t readTransactions = DAVIS_RPI_MAX_TRANSACTION_NUM;
		size_t dataSize         = 0;

		while (readTransactions-- > 0) {
			// Do transaction via DDR-AER. Is there a request?
			size_t noReqCount = 0;
			while (GPIO_GET(gpio->gpioReg, GPIO_AER_REQ) != 0) {
				// Track failed wait on requests, and simply break early once
				// the maximum is reached, to avoid dead-locking in here.
				noReqCount++;
				if (noReqCount == DAVIS_RPI_MAX_WAIT_REQ_COUNT) {
					goto processData;
				}
			}

			// Request is present, latch data.
			data[dataSize++] = GPIO_AER_DATA0(gpio->gpioReg);
			data[dataSize++] = GPIO_AER_DATA1(gpio->gpioReg);

			// ACK ACK! (active-low, so clear).
			GPIO_CLR(gpio->gpioReg, GPIO_AER_ACK);

			// Wait for REQ to go back off (high).
			while (GPIO_GET(gpio->gpioReg, GPIO_AER_REQ) == 0) {
				;
			}

			// Latch data again.
			data[dataSize++] = GPIO_AER_DATA0(gpio->gpioReg);
			data[dataSize++] = GPIO_AER_DATA1(gpio->gpioReg);

			// ACK ACK off! (active-low, so set).
			GPIO_SET(gpio->gpioReg, GPIO_AER_ACK);
		}

	// Translate data. Support testing/benchmarking.
	processData:
		if (dataSize > 0) {
#if DAVIS_RPI_BENCHMARK == 0
			davisRPiDataTranslator(handle, data, dataSize);
#else
			davisRPiBenchmarkDataTranslator(handle, data, dataSize);
#endif
		}

#if DAVIS_RPI_BENCHMARK == 1
		if (handle->benchmark.dataCount >= DAVIS_RPI_BENCHMARK_LIMIT_BYTES) {
			shutdownGPIOTest(handle);

			if (handle->benchmark.testMode == ALTERNATING) {
				// Last test just done.
				spiConfigSend(handle->cHandle.spiConfigPtr, DAVIS_CONFIG_DDRAER, DAVIS_CONFIG_DDRAER_RUN, false);

				// SPECIAL OUT: call exceptional shut-down callback and exit.
				if (gpio->shutdownCallback != NULL) {
					gpio->shutdownCallback(gpio->shutdownCallbackPtr);
				}
				break;
			}

			setupGPIOTest(handle, handle->benchmark.testMode + 1);
		}
#endif
	}

	free(data);

	// Ensure threadRun is false on termination.
	atomic_store(&gpio->threadState, THR_EXITED);

	davisLog(CAER_LOG_DEBUG, &handle->cHandle, "GPIO communication thread shut down.");

	return (EXIT_SUCCESS);
}

bool davisRPiDataStart(caerDeviceHandle cdh, void (*dataNotifyIncrease)(void *ptr),
	void (*dataNotifyDecrease)(void *ptr), void *dataNotifyUserPtr, void (*dataShutdownNotify)(void *ptr),
	void *dataShutdownUserPtr) {
	davisRPiHandle handle  = (davisRPiHandle) cdh;
	davisCommonState state = &handle->cHandle.state;

	handle->gpio.shutdownCallback    = dataShutdownNotify;
	handle->gpio.shutdownCallbackPtr = dataShutdownUserPtr;

	if (!davisCommonDataStart(&handle->cHandle, dataNotifyIncrease, dataNotifyDecrease, dataNotifyUserPtr)) {
		return (false);
	}

	if (!gpioThreadStart(handle)) {
		freeAllDataMemory(state);

		davisLog(CAER_LOG_CRITICAL, &handle->cHandle, "Failed to start GPIO data transfers.");
		return (false);
	}

#if DAVIS_RPI_BENCHMARK == 0
	if (dataExchangeStartProducers(&state->dataExchange)) {
		// Enable data transfer on DDR-AER interface.
		davisRPiConfigSet(cdh, DAVIS_CONFIG_MUX, DAVIS_CONFIG_MUX_RUN_CHIP, true);

		// Wait 200 ms for biases to stabilize.
		struct timespec biasEnSleep = {.tv_sec = 0, .tv_nsec = 200000000};
		thrd_sleep(&biasEnSleep, NULL);

		davisRPiConfigSet(cdh, DAVIS_CONFIG_DDRAER, DAVIS_CONFIG_DDRAER_RUN, true);
		davisRPiConfigSet(cdh, DAVIS_CONFIG_MUX, DAVIS_CONFIG_MUX_TIMESTAMP_RUN, true);
		davisRPiConfigSet(cdh, DAVIS_CONFIG_MUX, DAVIS_CONFIG_MUX_RUN, true);

		// Wait 50 ms for data transfer to be ready.
		struct timespec noDataSleep = {.tv_sec = 0, .tv_nsec = 50000000};
		thrd_sleep(&noDataSleep, NULL);

		davisRPiConfigSet(cdh, DAVIS_CONFIG_DVS, DAVIS_CONFIG_DVS_RUN, true);
		davisRPiConfigSet(cdh, DAVIS_CONFIG_APS, DAVIS_CONFIG_APS_RUN, true);
		davisRPiConfigSet(cdh, DAVIS_CONFIG_IMU, DAVIS_CONFIG_IMU_RUN_ACCELEROMETER, true);
		davisRPiConfigSet(cdh, DAVIS_CONFIG_IMU, DAVIS_CONFIG_IMU_RUN_GYROSCOPE, true);
		davisRPiConfigSet(cdh, DAVIS_CONFIG_IMU, DAVIS_CONFIG_IMU_RUN_TEMPERATURE, true);
		davisRPiConfigSet(cdh, DAVIS_CONFIG_EXTINPUT, DAVIS_CONFIG_EXTINPUT_RUN_DETECTOR, true);
	}
#endif

	return (true);
}

bool davisRPiDataStop(caerDeviceHandle cdh) {
	davisRPiHandle handle  = (davisRPiHandle) cdh;
	davisCommonState state = &handle->cHandle.state;

#if DAVIS_RPI_BENCHMARK == 0
	if (dataExchangeStopProducers(&state->dataExchange)) {
		// Disable data transfer on DDR-AER interface. Reverse order of enabling.
		davisRPiConfigSet(cdh, DAVIS_CONFIG_DVS, DAVIS_CONFIG_DVS_RUN, false);
		davisRPiConfigSet(cdh, DAVIS_CONFIG_APS, DAVIS_CONFIG_APS_RUN, false);
		davisRPiConfigSet(cdh, DAVIS_CONFIG_IMU, DAVIS_CONFIG_IMU_RUN_ACCELEROMETER, false);
		davisRPiConfigSet(cdh, DAVIS_CONFIG_IMU, DAVIS_CONFIG_IMU_RUN_GYROSCOPE, false);
		davisRPiConfigSet(cdh, DAVIS_CONFIG_IMU, DAVIS_CONFIG_IMU_RUN_TEMPERATURE, false);
		davisRPiConfigSet(cdh, DAVIS_CONFIG_EXTINPUT, DAVIS_CONFIG_EXTINPUT_RUN_DETECTOR, false);

		davisRPiConfigSet(cdh, DAVIS_CONFIG_MUX, DAVIS_CONFIG_MUX_RUN, false);
		davisRPiConfigSet(cdh, DAVIS_CONFIG_MUX, DAVIS_CONFIG_MUX_TIMESTAMP_RUN, false);
		davisRPiConfigSet(cdh, DAVIS_CONFIG_DDRAER, DAVIS_CONFIG_DDRAER_RUN, false);

		davisRPiConfigSet(cdh, DAVIS_CONFIG_MUX, DAVIS_CONFIG_MUX_RUN_CHIP, false);
	}
#endif

	gpioThreadStop(handle);

	davisCommonDataStop(&handle->cHandle);

	return (true);
}

caerEventPacketContainer davisRPiDataGet(caerDeviceHandle cdh) {
	davisRPiHandle handle = (davisRPiHandle) cdh;

	return (dataExchangeGet(&handle->cHandle.state.dataExchange, &handle->gpio.threadState));
}

#if DAVIS_RPI_BENCHMARK == 1
static void davisRPiBenchmarkDataTranslator(davisRPiHandle handle, const uint8_t *buffer, size_t bufferSize) {
	// Return right away if not running anymore. This prevents useless work if many
	// buffers are still waiting when shut down.
	if (atomic_load(&handle->gpio.threadState) != THR_RUNNING) {
		return;
	}

	for (size_t bufferPos = 0; bufferPos < bufferSize; bufferPos += 2) {
		uint16_t value = le16toh(*((const uint16_t *) (&buffer[bufferPos])));

		if (value != handle->benchmark.expectedValue) {
			davisLog(CAER_LOG_ERROR, &handle->cHandle, "Failed benchmark test, unexpected value of %d, instead of %d.",
				value, handle->benchmark.expectedValue);
			handle->benchmark.errorCount++;

			handle->benchmark.expectedValue = value;
		}

		switch (handle->benchmark.testMode) {
			case ZEROS:
				handle->benchmark.expectedValue = 0;

				break;

			case ONES:
				handle->benchmark.expectedValue = 0xFFFF;

				break;

			case SWITCHING:
				if (handle->benchmark.expectedValue == 0xFFFF) {
					handle->benchmark.expectedValue = 0;
				}
				else {
					handle->benchmark.expectedValue = 0xFFFF;
				}

				break;

			case ALTERNATING:
				if (handle->benchmark.expectedValue == 0x5555) {
					handle->benchmark.expectedValue = 0xAAAA;
				}
				else {
					handle->benchmark.expectedValue = 0x5555;
				}

				break;

			case COUNTER:
			default:
				handle->benchmark.expectedValue++;

				break;
		}
	}

	// Count events.
	handle->benchmark.dataCount += bufferSize;
}

static void setupGPIOTest(davisRPiHandle handle, enum benchmarkMode mode) {
	// Reset global variables.
	handle->benchmark.dataCount  = 0;
	handle->benchmark.errorCount = 0;

	if (mode == ONES) {
		handle->benchmark.expectedValue = 0xFFFF;
	}
	else if (mode == ALTERNATING) {
		handle->benchmark.expectedValue = 0x5555;
	}
	else {
		handle->benchmark.expectedValue = 0;
	}

	// Set global test mode variable.
	handle->benchmark.testMode = mode;

	// Set test mode.
	spiConfigSend(handle->cHandle.spiConfigPtr, 0x00, 0x09, mode);

	// Enable test.
	spiConfigSend(handle->cHandle.spiConfigPtr, 0x00, 0x08, true);

	// Remember test start time.
	portable_clock_gettime_monotonic(&handle->benchmark.startTime);
}

static void shutdownGPIOTest(davisRPiHandle handle) {
	// Get test end time.
	struct timespec endTime;
	portable_clock_gettime_monotonic(&endTime);

	// Disable current tests.
	spiConfigSend(handle->cHandle.spiConfigPtr, 0x00, 0x08, false);

	// Drain FIFOs by disabling.
	spiConfigSend(handle->cHandle.spiConfigPtr, DAVIS_CONFIG_DDRAER, DAVIS_CONFIG_DDRAER_RUN, false);
	sleep(1);
	spiConfigSend(handle->cHandle.spiConfigPtr, DAVIS_CONFIG_DDRAER, DAVIS_CONFIG_DDRAER_RUN, true);

	// Check if test was successful.
	if (handle->benchmark.errorCount == 0) {
		davisLog(CAER_LOG_ERROR, &handle->cHandle, "Test %d successful (%zu bytes). No errors encountered.",
			handle->benchmark.testMode, handle->benchmark.dataCount);
	}
	else {
		davisLog(CAER_LOG_ERROR, &handle->cHandle,
			"Test %d failed (%zu bytes). %zu errors encountered. See the console for more details.",
			handle->benchmark.testMode, handle->benchmark.dataCount, handle->benchmark.errorCount);
	}

	// Calculate bandwidth.
	uint64_t diffNanoTime = (uint64_t)(((int64_t)(endTime.tv_sec - handle->benchmark.startTime.tv_sec) * 1000000000LL)
									   + (int64_t)(endTime.tv_nsec - handle->benchmark.startTime.tv_nsec));

	double diffSecondTime = ((double) diffNanoTime) / ((double) 1000000000ULL);

	double bytesPerSecond = ((double) handle->benchmark.dataCount) / diffSecondTime;

	davisLog(CAER_LOG_ERROR, &handle->cHandle, "Test %d: bandwidth of %g bytes/second (%zu bytes in %g seconds).",
		handle->benchmark.testMode, bytesPerSecond, handle->benchmark.dataCount, diffSecondTime);
}
#endif

static void davisRPiDataTranslator(davisRPiHandle handle, const uint8_t *buffer, size_t bufferSize) {
	// Return right away if not running anymore. This prevents useless work if many
	// buffers are still waiting when shut down, as well as incorrect event sequences
	// if a TS_RESET is stuck on ring-buffer commit further down, and detects shut-down;
	// then any subsequent buffers should also detect shut-down and not be handled.
	if (atomic_load(&handle->gpio.threadState) != THR_RUNNING) {
		return;
	}

	davisCommonEventTranslator(&handle->cHandle, buffer, bufferSize, &handle->gpio.threadState);
}

static bool spiInit(davisRPiGPIO gpio) {
	gpio->spiFd = open(SPI_DEVICE0_CS0, O_RDWR | O_SYNC);
	if (gpio->spiFd < 0) {
		return (false);
	}

	uint8_t spiMode = SPI_MODE_0;

	if ((ioctl(gpio->spiFd, SPI_IOC_WR_MODE, &spiMode) < 0) || (ioctl(gpio->spiFd, SPI_IOC_RD_MODE, &spiMode) < 0)) {
		return (false);
	}

	uint8_t spiBitsPerWord = SPI_BITS_PER_WORD;

	if ((ioctl(gpio->spiFd, SPI_IOC_WR_BITS_PER_WORD, &spiBitsPerWord) < 0)
		|| (ioctl(gpio->spiFd, SPI_IOC_RD_BITS_PER_WORD, &spiBitsPerWord) < 0)) {
		return (false);
	}

	uint32_t spiSpeedHz = SPI_SPEED_HZ;

	if ((ioctl(gpio->spiFd, SPI_IOC_WR_MAX_SPEED_HZ, &spiSpeedHz) < 0)
		|| (ioctl(gpio->spiFd, SPI_IOC_RD_MAX_SPEED_HZ, &spiSpeedHz) < 0)) {
		return (false);
	}

	return (true);
}

static void spiClose(davisRPiGPIO gpio) {
	if (gpio->spiFd >= 0) {
		close(gpio->spiFd);
	}
}

static inline bool spiTransfer(davisRPiGPIO gpio, uint8_t *spiOutput, uint8_t *spiInput) {
	struct spi_ioc_transfer spiTransfer;
	memset(&spiTransfer, 0, sizeof(struct spi_ioc_transfer));

	spiTransfer.tx_buf        = (__u64) spiOutput;
	spiTransfer.rx_buf        = (__u64) spiInput;
	spiTransfer.len           = SPI_CONFIG_MSG_SIZE;
	spiTransfer.speed_hz      = SPI_SPEED_HZ;
	spiTransfer.bits_per_word = SPI_BITS_PER_WORD;
	spiTransfer.cs_change     = false; // Documentation is misleading, see
									   // 'https://github.com/beagleboard/kernel/issues/85#issuecomment-32304365'.

	mtx_lock(&gpio->spiLock);

	int result = ioctl(gpio->spiFd, SPI_IOC_MESSAGE(1), &spiTransfer);

	mtx_unlock(&gpio->spiLock);

	return ((result < 0) ? (false) : (true));
}

static bool spiSend(davisRPiGPIO gpio, uint8_t moduleAddr, uint8_t paramAddr, uint32_t param) {
	uint8_t spiOutput[SPI_CONFIG_MSG_SIZE] = {0};

	// Highest bit of first byte is zero to indicate write operation.
	spiOutput[0] = (moduleAddr & 0x7F);
	spiOutput[1] = paramAddr;
	spiOutput[2] = (uint8_t)(param >> 24);
	spiOutput[3] = (uint8_t)(param >> 16);
	spiOutput[4] = (uint8_t)(param >> 8);
	spiOutput[5] = (uint8_t)(param >> 0);

	return (spiTransfer(gpio, spiOutput, NULL));
}

static bool spiReceive(davisRPiGPIO gpio, uint8_t moduleAddr, uint8_t paramAddr, uint32_t *param) {
	uint8_t spiOutput[SPI_CONFIG_MSG_SIZE] = {0};
	uint8_t spiInput[SPI_CONFIG_MSG_SIZE]  = {0};

	// Highest bit of first byte is one to indicate read operation.
	spiOutput[0] = (moduleAddr | 0x80);
	spiOutput[1] = paramAddr;

	if (!spiTransfer(gpio, spiOutput, spiInput)) {
		return (false);
	}

	*param = 0;
	*param |= U32T(spiInput[2] << 24);
	*param |= U32T(spiInput[3] << 16);
	*param |= U32T(spiInput[4] << 8);
	*param |= U32T(spiInput[5] << 0);

	return (true);
}

static inline uint8_t setBitInByte(uint8_t byteIn, uint8_t idx, bool value) {
	if (value) {
		// Flip bit on if enabled.
		return (U8T(byteIn | U8T(0x01 << idx)));
	}
	else {
		// Flip bit off if disabled.
		return (U8T(byteIn & U8T(~(0x01 << idx))));
	}
}

static inline uint8_t getBitInByte(uint8_t byteIn, uint8_t idx) {
	return ((byteIn >> idx) & 0x01);
}

static bool handleChipBiasSend(davisRPiHandle state, uint8_t paramAddr, uint32_t param) {
	// All addresses below 128 are biases, 128 and up are chip configuration register elements.

	// Entry delay.
	usleep(500);

	if (paramAddr <= DAVIS_BIAS_ADDRESS_MAX) {
		// Handle biases.
		if ((state->biasing.currentBiasArray[paramAddr][0] == U8T(param >> 8))
			&& (state->biasing.currentBiasArray[paramAddr][1] == U8T(param >> 0))) {
			// No changes, return right away.
			return (true);
		}

		// Store new values.
		state->biasing.currentBiasArray[paramAddr][0] = U8T(param >> 8);
		state->biasing.currentBiasArray[paramAddr][1] = U8T(param >> 0);

		uint8_t biasVal0, biasVal1;

		if (paramAddr < 8) {
			// Flip and reverse coarse bits, due to an on-chip routing mistake.
			biasVal0 = ((((state->biasing.currentBiasArray[paramAddr][0] & 0x01) ^ 0x01) << 4) & 0x10);
			biasVal0
				= (uint8_t)(biasVal0 | ((((state->biasing.currentBiasArray[paramAddr][1] & 0x80) ^ 0x80) >> 2) & 0x20));
			biasVal0 = (uint8_t)(biasVal0 | (((state->biasing.currentBiasArray[paramAddr][1] & 0x40) ^ 0x40) & 0x40));

			biasVal1 = state->biasing.currentBiasArray[paramAddr][1] & 0x3F;
		}
		else if (paramAddr < 35) {
			// The first byte of a coarse/fine bias needs to have the coarse bits
			// flipped and reversed, due to an on-chip routing mistake.
			biasVal0 = state->biasing.currentBiasArray[paramAddr][0] ^ 0x70;
			biasVal0 = (uint8_t)((biasVal0 & ~0x50) | ((biasVal0 & 0x40) >> 2) | ((biasVal0 & 0x10) << 2));

			biasVal1 = state->biasing.currentBiasArray[paramAddr][1];
		}
		else {
			// SSN/SSP are fine as-is.
			biasVal0 = state->biasing.currentBiasArray[paramAddr][0];
			biasVal1 = state->biasing.currentBiasArray[paramAddr][1];
		}

		// Write bias: 8bit address + 16bit value to register 0.
		uint32_t value = 0;
		value |= U32T(paramAddr << 16);
		value |= U32T(biasVal0 << 8);
		value |= U32T(biasVal1 << 0);

		if (!spiSend(&state->gpio, DAVIS_CONFIG_BIAS, 0, value)) {
			return (false);
		}

		// Wait 480us for bias write to complete, so sleep for 1ms.
		usleep(480 * 2);

		return (true);
	}
	else {
		// Handle chip configuration.
		// Store old value for later change detection.
		uint8_t oldChipRegister[DAVIS_CHIP_REG_LENGTH] = {0};
		memcpy(oldChipRegister, state->biasing.currentChipRegister, DAVIS_CHIP_REG_LENGTH);

		switch (paramAddr) {
			case 128: // DigitalMux0
				state->biasing.currentChipRegister[5]
					= (uint8_t)((state->biasing.currentChipRegister[5] & 0xF0) | (U8T(param) & 0x0F));
				break;

			case 129: // DigitalMux1
				state->biasing.currentChipRegister[5]
					= (uint8_t)((state->biasing.currentChipRegister[5] & 0x0F) | ((U8T(param) << 4) & 0xF0));
				break;

			case 130: // DigitalMux2
				state->biasing.currentChipRegister[6]
					= (uint8_t)((state->biasing.currentChipRegister[6] & 0xF0) | (U8T(param) & 0x0F));
				break;

			case 131: // DigitalMux3
				state->biasing.currentChipRegister[6]
					= (uint8_t)((state->biasing.currentChipRegister[6] & 0x0F) | ((U8T(param) << 4) & 0xF0));
				break;

			case 132: // AnalogMux0
				state->biasing.currentChipRegister[0]
					= (uint8_t)((state->biasing.currentChipRegister[0] & 0x0F) | ((U8T(param) << 4) & 0xF0));
				break;

			case 133: // AnalogMux1
				state->biasing.currentChipRegister[1]
					= (uint8_t)((state->biasing.currentChipRegister[1] & 0xF0) | (U8T(param) & 0x0F));
				break;

			case 134: // AnalogMux2
				state->biasing.currentChipRegister[1]
					= (uint8_t)((state->biasing.currentChipRegister[1] & 0x0F) | ((U8T(param) << 4) & 0xF0));
				break;

			case 135: // BiasMux0
				state->biasing.currentChipRegister[0]
					= (uint8_t)((state->biasing.currentChipRegister[0] & 0xF0) | (U8T(param) & 0x0F));
				break;

			case 136: // ResetCalibNeuron
				state->biasing.currentChipRegister[2]
					= setBitInByte(state->biasing.currentChipRegister[2], 0, (U8T(param) & 0x01));
				break;

			case 137: // TypeNCalibNeuron
				state->biasing.currentChipRegister[2]
					= setBitInByte(state->biasing.currentChipRegister[2], 1, (U8T(param) & 0x01));
				break;

			case 138: // ResetTestPixel
				state->biasing.currentChipRegister[2]
					= setBitInByte(state->biasing.currentChipRegister[2], 2, (U8T(param) & 0x01));
				break;

			case 140: // AERnArow
				state->biasing.currentChipRegister[2]
					= setBitInByte(state->biasing.currentChipRegister[2], 4, (U8T(param) & 0x01));
				break;

			case 141: // UseAOut
				state->biasing.currentChipRegister[2]
					= setBitInByte(state->biasing.currentChipRegister[2], 5, (U8T(param) & 0x01));
				break;

			case 142: // GlobalShutter
				state->biasing.currentChipRegister[2]
					= setBitInByte(state->biasing.currentChipRegister[2], 6, (U8T(param) & 0x01));
				break;

			case 143: // SelectGrayCounter
				state->biasing.currentChipRegister[2]
					= setBitInByte(state->biasing.currentChipRegister[2], 7, (U8T(param) & 0x01));
				break;

			default:
				return (false);
		}

		// Check if value changed, only send out if it did.
		if (memcmp(oldChipRegister, state->biasing.currentChipRegister, DAVIS_CHIP_REG_LENGTH) == 0) {
			return (true);
		}

		// Write config chain lower bits: 32bits to register 1.
		uint32_t value = 0;
		value |= U32T(state->biasing.currentChipRegister[3] << 24);
		value |= U32T(state->biasing.currentChipRegister[2] << 16);
		value |= U32T(state->biasing.currentChipRegister[1] << 8);
		value |= U32T(state->biasing.currentChipRegister[0] << 0);

		if (!spiSend(&state->gpio, DAVIS_CONFIG_BIAS, 1, value)) {
			return (false);
		}

		// Write config chain upper bits: 24bits to register 2.
		value = 0;
		value |= U32T(state->biasing.currentChipRegister[6] << 16);
		value |= U32T(state->biasing.currentChipRegister[5] << 8);
		value |= U32T(state->biasing.currentChipRegister[4] << 0);

		if (!spiSend(&state->gpio, DAVIS_CONFIG_BIAS, 2, value)) {
			return (false);
		}

		// Wait 700us for chip configuration write to complete, so sleep for 2ms.
		usleep(700 * 2);

		return (true);
	}
}

static bool handleChipBiasReceive(davisRPiHandle state, uint8_t paramAddr, uint32_t *param) {
	// All addresses below 128 are biases, 128 and up are chip configuration register elements.
	*param = 0;

	if (paramAddr <= DAVIS_BIAS_ADDRESS_MAX) {
		// Handle biases.
		// Get value directly from FX3 memory. Device doesn't support reads.
		*param |= U32T(state->biasing.currentBiasArray[paramAddr][0] << 8);
		*param |= U32T(state->biasing.currentBiasArray[paramAddr][1] << 0);

		return (true);
	}
	else {
		// Handle chip configuration.
		// Get value directly from FX3 memory. Device doesn't support reads.
		switch (paramAddr) {
			case 128: // DigitalMux0
				*param |= (state->biasing.currentChipRegister[5] & 0x0F);
				break;

			case 129: // DigitalMux1
				*param |= ((state->biasing.currentChipRegister[5] >> 4) & 0x0F);
				break;

			case 130: // DigitalMux2
				*param |= (state->biasing.currentChipRegister[6] & 0x0F);
				break;

			case 131: // DigitalMux3
				*param |= ((state->biasing.currentChipRegister[6] >> 4) & 0x0F);
				break;

			case 132: // AnalogMux0
				*param |= ((state->biasing.currentChipRegister[0] >> 4) & 0x0F);
				break;

			case 133: // AnalogMux1
				*param |= (state->biasing.currentChipRegister[1] & 0x0F);
				break;

			case 134: // AnalogMux2
				*param |= ((state->biasing.currentChipRegister[1] >> 4) & 0x0F);
				break;

			case 135: // BiasMux0
				*param |= (state->biasing.currentChipRegister[0] & 0x0F);
				break;

			case 136: // ResetCalibNeuron
				*param |= (uint8_t) getBitInByte(state->biasing.currentChipRegister[2], 0);
				break;

			case 137: // TypeNCalibNeuron
				*param |= (uint8_t) getBitInByte(state->biasing.currentChipRegister[2], 1);
				break;

			case 138: // ResetTestPixel
				*param |= (uint8_t) getBitInByte(state->biasing.currentChipRegister[2], 2);
				break;

			case 140: // AERnArow
				*param |= (uint8_t) getBitInByte(state->biasing.currentChipRegister[2], 4);
				break;

			case 141: // UseAOut
				*param |= (uint8_t) getBitInByte(state->biasing.currentChipRegister[2], 5);
				break;

			case 142: // GlobalShutter
				*param |= (uint8_t) getBitInByte(state->biasing.currentChipRegister[2], 6);
				break;

			case 143: // SelectGrayCounter
				*param |= (uint8_t) getBitInByte(state->biasing.currentChipRegister[2], 7);
				break;

			default:
				return (false);
		}

		return (true);
	}
}

static bool spiConfigSendMultiple(void *state, spiConfigParams configs, uint16_t numConfigs) {
	for (size_t i = 0; i < numConfigs; i++) {
		// Param must be in big-endian format.
		configs[i].param = htobe32(configs[i].param);

		if (!spiConfigSend(state, configs[i].moduleAddr, configs[i].paramAddr, configs[i].param)) {
			return (false);
		}
	}

	return (true);
}

static bool spiConfigSendMultipleAsync(void *state, spiConfigParams configs, uint16_t numConfigs,
	void (*configSendCallback)(void *configSendCallbackPtr, int status), void *configSendCallbackPtr) {
	for (size_t i = 0; i < numConfigs; i++) {
		// Param must be in big-endian format.
		configs[i].param = htobe32(configs[i].param);

		if (!spiConfigSendAsync(state, configs[i].moduleAddr, configs[i].paramAddr, configs[i].param,
				configSendCallback, configSendCallbackPtr)) {
			return (false);
		}
	}

	return (true);
}

static bool spiConfigSend(void *state, uint8_t moduleAddr, uint8_t paramAddr, uint32_t param) {
	davisRPiHandle handle = (davisRPiHandle) state;

	// Handle biases/chip config separately.
	if (moduleAddr == DAVIS_CONFIG_BIAS) {
		return (handleChipBiasSend(handle, paramAddr, param));
	}

	// Standard SPI send.
	return (spiSend(&handle->gpio, moduleAddr, paramAddr, param));
}

static bool spiConfigSendAsync(void *state, uint8_t moduleAddr, uint8_t paramAddr, uint32_t param,
	void (*configSendCallback)(void *configSendCallbackPtr, int status), void *configSendCallbackPtr) {
	// Call normal spiConfigSend() and execute callback manually. There are no threading/dead-lock
	// issues here with using SPI directly.
	bool status = spiConfigSend(state, moduleAddr, paramAddr, param);

	if (configSendCallback != NULL) {
		(*configSendCallback)(configSendCallbackPtr, !status); // Success is status=0.
	}

	return (status);
}

static bool spiConfigReceive(void *state, uint8_t moduleAddr, uint8_t paramAddr, uint32_t *param) {
	davisRPiHandle handle = (davisRPiHandle) state;

	// Handle biases/chip config separately.
	if (moduleAddr == DAVIS_CONFIG_BIAS) {
		return (handleChipBiasReceive(handle, paramAddr, param));
	}

	// Standard SPI receive.
	return (spiReceive(&handle->gpio, moduleAddr, paramAddr, param));
}

static bool spiConfigReceiveAsync(void *state, uint8_t moduleAddr, uint8_t paramAddr,
	void (*configReceiveCallback)(void *configReceiveCallbackPtr, int status, uint32_t param),
	void *configReceiveCallbackPtr) {
	// Call normal spiConfigReceive() and execute callback manually. There are no threading/dead-lock
	// issues here with using SPI directly.
	uint32_t param = 0;
	bool status    = spiConfigReceive(state, moduleAddr, paramAddr, &param);

	if (configReceiveCallback != NULL) {
		(*configReceiveCallback)(configReceiveCallbackPtr, !status, param); // Success is status=0.
	}

	return (status);
}
