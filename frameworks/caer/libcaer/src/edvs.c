#include "edvs.h"

#include <ctype.h>
#include <string.h>

#if defined(OS_UNIX)
#	include <sys/ioctl.h>
#	include <termios.h>
#	include <unistd.h>
#endif

static void edvsLog(enum caer_log_level logLevel, edvsHandle handle, const char *format, ...) ATTRIBUTE_FORMAT(3);
static bool serialThreadStart(edvsHandle handle);
static void serialThreadStop(edvsHandle handle);
static int serialThreadRun(void *handlePtr);
static void edvsEventTranslator(void *vhd, const uint8_t *buffer, size_t bytesSent);
static bool edvsSendBiases(edvsState state, int biasID);

static void edvsLog(enum caer_log_level logLevel, edvsHandle handle, const char *format, ...) {
	// Only log messages above the specified severity level.
	uint8_t systemLogLevel = atomic_load_explicit(&handle->state.deviceLogLevel, memory_order_relaxed);

	if (logLevel > systemLogLevel) {
		return;
	}

	va_list argumentList;
	va_start(argumentList, format);
	caerLogVAFull(systemLogLevel, logLevel, handle->info.deviceString, format, argumentList);
	va_end(argumentList);
}

ssize_t edvsFind(caerDeviceDiscoveryResult *discoveredDevices) {
	// Set to NULL initially (for error return).
	*discoveredDevices = NULL;

	struct sp_port **serialPortList = NULL;

	enum sp_return result = sp_list_ports(&serialPortList);
	if (result != SP_OK) {
		// Error, return right away.
		return (-1);
	}

	size_t i = 0, matches = 0;

	while (serialPortList[i++] != NULL) {
		// Skip undefined devices.
		if (sp_get_port_name(serialPortList[i]) == NULL || strlen(sp_get_port_name(serialPortList[i])) == 0) {
			continue;
		}

		// Try to open the serial device as an eDVS, with all supported baud-rates.
		caerLogDisable(true);
		caerDeviceHandle edvs = edvsOpen(0, sp_get_port_name(serialPortList[i]), CAER_HOST_CONFIG_SERIAL_BAUD_RATE_12M);
		if (edvs == NULL) {
			edvs = edvsOpen(0, sp_get_port_name(serialPortList[i]), CAER_HOST_CONFIG_SERIAL_BAUD_RATE_8M);
			if (edvs == NULL) {
				edvs = edvsOpen(0, sp_get_port_name(serialPortList[i]), CAER_HOST_CONFIG_SERIAL_BAUD_RATE_4M);
				if (edvs == NULL) {
					edvs = edvsOpen(0, sp_get_port_name(serialPortList[i]), CAER_HOST_CONFIG_SERIAL_BAUD_RATE_2M);
				}
			}
		}
		caerLogDisable(false);

		// Nothing worked, go to next candidate.
		if (edvs == NULL && errno != CAER_ERROR_COMMUNICATION) {
			continue;
		}

		// Successfully opened an eDVS.
		void *biggerDiscoveredDevices
			= realloc(*discoveredDevices, (matches + 1) * sizeof(struct caer_device_discovery_result));
		if (biggerDiscoveredDevices == NULL) {
			// Memory allocation failure!
			free(*discoveredDevices);
			*discoveredDevices = NULL;

			sp_free_port_list(serialPortList);

			return (-1);
		}

		// Memory allocation successful, get info.
		*discoveredDevices = biggerDiscoveredDevices;

		(*discoveredDevices)[matches].deviceType         = CAER_DEVICE_EDVS;
		(*discoveredDevices)[matches].deviceErrorOpen    = (errno == CAER_ERROR_COMMUNICATION);
		(*discoveredDevices)[matches].deviceErrorVersion = false; // No version check is done.
		struct caer_edvs_info *edvsInfoPtr               = &((*discoveredDevices)[matches].deviceInfo.edvsInfo);

		if (edvs != NULL) {
			*edvsInfoPtr = caerEDVSInfoGet(edvs);
		}

		// Set/Reset to invalid values, not part of discovery.
		edvsInfoPtr->deviceID     = -1;
		edvsInfoPtr->deviceString = NULL;

		if (edvs != NULL) {
			edvsClose(edvs);
		}

		matches++;
	}

	sp_free_port_list(serialPortList);

	return ((ssize_t) matches);
}

static inline bool serialPortWrite(edvsState state, const char *cmd) {
	size_t cmdLength = strlen(cmd);

	mtx_lock(&state->serialState.serialWriteLock);

	bool retVal = (sp_blocking_write(state->serialState.serialPort, cmd, cmdLength, 0) == (int) cmdLength);
	sp_drain(state->serialState.serialPort);

	mtx_unlock(&state->serialState.serialWriteLock);

	return (retVal);
}

static inline void freeAllDataMemory(edvsState state) {
	dataExchangeDestroy(&state->dataExchange);

	// Since the current event packets aren't necessarily
	// already assigned to the current packet container, we
	// free them separately from it.
	if (state->currentPackets.polarity != NULL) {
		free(&state->currentPackets.polarity->packetHeader);
		state->currentPackets.polarity = NULL;

		containerGenerationSetPacket(&state->container, POLARITY_EVENT, NULL);
	}

	if (state->currentPackets.special != NULL) {
		free(&state->currentPackets.special->packetHeader);
		state->currentPackets.special = NULL;

		containerGenerationSetPacket(&state->container, SPECIAL_EVENT, NULL);
	}

	containerGenerationDestroy(&state->container);
}

caerDeviceHandle edvsOpen(uint16_t deviceID, const char *serialPortName, uint32_t serialBaudRate) {
	errno = 0;

	caerLog(CAER_LOG_DEBUG, __func__, "Initializing %s.", EDVS_DEVICE_NAME);

	edvsHandle handle = calloc(1, sizeof(*handle));
	if (handle == NULL) {
		// Failed to allocate memory for device handle!
		caerLog(CAER_LOG_CRITICAL, __func__, "Failed to allocate memory for device handle.");
		errno = CAER_ERROR_MEMORY_ALLOCATION;
		return (NULL);
	}

	// Set main deviceType correctly right away.
	handle->deviceType = CAER_DEVICE_EDVS;

	edvsState state = &handle->state;

	// Initialize state variables to default values (if not zero, taken care of by calloc above).
	dataExchangeSettingsInit(&state->dataExchange);

	// Packet settings (size (in events) and time interval (in µs)).
	containerGenerationSettingsInit(&state->container);

	// Logging settings (initialize to global log-level).
	enum caer_log_level globalLogLevel = caerLogLevelGet();
	atomic_store(&state->deviceLogLevel, globalLogLevel);

	// Set device string.
	size_t fullLogStringLength = (size_t) snprintf(NULL, 0, "%s ID-%" PRIu16, EDVS_DEVICE_NAME, deviceID);

	char *fullLogString = malloc(fullLogStringLength + 1);
	if (fullLogString == NULL) {
		free(handle);

		caerLog(CAER_LOG_CRITICAL, __func__, "Failed to allocate memory for device string.");
		errno = CAER_ERROR_MEMORY_ALLOCATION;
		return (NULL);
	}

	snprintf(fullLogString, fullLogStringLength + 1, "%s ID-%" PRIu16, EDVS_DEVICE_NAME, deviceID);

	handle->info.deviceString = fullLogString;

	// Initialize mutex lock for writes (reads never happen concurrently,
	// and only on one thread).
	if (mtx_init(&state->serialState.serialWriteLock, mtx_plain) != thrd_success) {
		edvsLog(CAER_LOG_ERROR, handle, "Failed to initialize serial write lock.");

		free(handle->info.deviceString);
		free(handle);

		errno = CAER_ERROR_RESOURCE_ALLOCATION;
		return (NULL);
	}

	// Try to open an eDVS device on a specific serial port.
	enum sp_return retVal = sp_get_port_by_name(serialPortName, &state->serialState.serialPort);
	if (retVal != SP_OK) {
		edvsLog(CAER_LOG_CRITICAL, handle, "Failed to get serial port on '%s'.", serialPortName);

		mtx_destroy(&state->serialState.serialWriteLock);
		free(handle->info.deviceString);
		free(handle);

		errno = CAER_ERROR_OPEN_ACCESS;
		return (NULL);
	}

	// Open the serial port.
	retVal = sp_open(state->serialState.serialPort, SP_MODE_READ_WRITE);
	if (retVal != SP_OK) {
		edvsLog(CAER_LOG_ERROR, handle, "Failed to open serial port, error: %d.", retVal);

		sp_free_port(state->serialState.serialPort);
		mtx_destroy(&state->serialState.serialWriteLock);
		free(handle->info.deviceString);
		free(handle);

		errno = CAER_ERROR_OPEN_ACCESS;
		return (NULL);
	}

#if defined(OS_UNIX)
	// Set exclusive access to serial port. Only needed on Unix (TIOCEXCL flag).
	int serialFd = -1;

	retVal = sp_get_port_handle(state->serialState.serialPort, &serialFd);

	if (retVal == SP_OK && serialFd >= 0) {
		ioctl(serialFd, TIOCEXCL);
	}
#endif

	// Set communication configuration.
	sp_set_baudrate(state->serialState.serialPort, (int) serialBaudRate);
	sp_set_bits(state->serialState.serialPort, 8);
	sp_set_stopbits(state->serialState.serialPort, 1);
	sp_set_parity(state->serialState.serialPort, SP_PARITY_NONE);
	sp_set_flowcontrol(state->serialState.serialPort, SP_FLOWCONTROL_RTSCTS);

	// At this point there might be garbage, due to old attempts to open,
	// for example with a wrong baud-rate, so we first flush the write pipe
	// by sending a \n and then read out any available garbage data.
	sp_flush(state->serialState.serialPort, SP_BUF_BOTH);

	serialPortWrite(state, "\n");

	char garbage[256];
	sp_blocking_read(state->serialState.serialPort, garbage, 256, 50);

	sp_flush(state->serialState.serialPort, SP_BUF_BOTH);

	const char *cmdReset = "R\n";
	if (!serialPortWrite(state, cmdReset)) {
		edvsLog(CAER_LOG_ERROR, handle, "Failed to send reset command.");

		sp_close(state->serialState.serialPort);
		sp_free_port(state->serialState.serialPort);
		mtx_destroy(&state->serialState.serialWriteLock);
		free(handle->info.deviceString);
		free(handle);

		errno = CAER_ERROR_OPEN_ACCESS;
		return (NULL);
	}

	// Wait for reset to happen.
	struct timespec waitResetSleep = {.tv_sec = 0, .tv_nsec = 400000000};
	thrd_sleep(&waitResetSleep, NULL);

	// Get startup message.
	char startMessage[1024];
	int bytesRead = sp_blocking_read(state->serialState.serialPort, startMessage, 1024, 100);
	if (bytesRead < 0) {
		edvsLog(CAER_LOG_ERROR, handle, "Failed to read startup message.");

		sp_close(state->serialState.serialPort);
		sp_free_port(state->serialState.serialPort);
		mtx_destroy(&state->serialState.serialWriteLock);
		free(handle->info.deviceString);
		free(handle);

		errno = CAER_ERROR_OPEN_ACCESS;
		return (NULL);
	}

	// Print startup message.
	startMessage[bytesRead] = '\0';

	for (size_t i = 0; i < (size_t) bytesRead; i++) {
		// Remove newlines for log printing.
		if (!isprint(startMessage[i])) {
			startMessage[i] = ' ';
		}
	}

	edvsLog(CAER_LOG_INFO, handle, "Startup message: '%s' (%d bytes).", startMessage, bytesRead);

	// Extract model from startup message. This tells us if we really connected
	// to an eDVS device.
	if (strstr(startMessage, EDVS_DEVICE_NAME) == NULL) {
		edvsLog(CAER_LOG_ERROR, handle, "This does not appear to be an eDVS device (according to startup message).");

		sp_close(state->serialState.serialPort);
		sp_free_port(state->serialState.serialPort);
		mtx_destroy(&state->serialState.serialWriteLock);
		free(handle->info.deviceString);
		free(handle);

		errno = CAER_ERROR_OPEN_ACCESS;
		return (NULL);
	}

	const char *cmdNoEcho = "!U0\n";
	if (!serialPortWrite(state, cmdNoEcho)) {
		edvsLog(CAER_LOG_ERROR, handle, "Failed to send echo disable command.");

		sp_close(state->serialState.serialPort);
		sp_free_port(state->serialState.serialPort);
		mtx_destroy(&state->serialState.serialWriteLock);
		free(handle->info.deviceString);
		free(handle);

		errno = CAER_ERROR_COMMUNICATION;
		return (NULL);
	}

	const char *cmdEventFormat = "!E2\n";
	if (!serialPortWrite(state, cmdEventFormat)) {
		edvsLog(CAER_LOG_ERROR, handle, "Failed to send event format command.");

		sp_close(state->serialState.serialPort);
		sp_free_port(state->serialState.serialPort);
		mtx_destroy(&state->serialState.serialWriteLock);
		free(handle->info.deviceString);
		free(handle);

		errno = CAER_ERROR_COMMUNICATION;
		return (NULL);
	}

	// Setup serial port communication.
	atomic_store(&state->serialState.serialReadSize, 1024);

	// Populate info variables based on data from device.
	handle->info.deviceID       = I16T(deviceID);
	handle->info.deviceIsMaster = true;
	handle->info.dvsSizeX       = EDVS_ARRAY_SIZE_X;
	handle->info.dvsSizeY       = EDVS_ARRAY_SIZE_Y;
	strncpy(handle->info.serialPortName, serialPortName, 63);
	handle->info.serialPortName[63] = '\0';
	handle->info.serialBaudRate     = serialBaudRate;

	edvsLog(CAER_LOG_DEBUG, handle, "Initialized device successfully on port '%s'.",
		sp_get_port_name(state->serialState.serialPort));

	return ((caerDeviceHandle) handle);
}

bool edvsClose(caerDeviceHandle cdh) {
	edvsHandle handle = (edvsHandle) cdh;
	edvsState state   = &handle->state;

	edvsLog(CAER_LOG_DEBUG, handle, "Shutting down ...");

	// Close and free serial port.
	sp_close(state->serialState.serialPort);
	sp_free_port(state->serialState.serialPort);
	mtx_destroy(&state->serialState.serialWriteLock);

	edvsLog(CAER_LOG_DEBUG, handle, "Shutdown successful.");

	// Free memory.
	free(handle->info.deviceString);
	free(handle);

	return (true);
}

struct caer_edvs_info caerEDVSInfoGet(caerDeviceHandle cdh) {
	edvsHandle handle = (edvsHandle) cdh;

	// Check if the pointer is valid.
	if (handle == NULL) {
		struct caer_edvs_info emptyInfo = {0, .deviceString = NULL};
		return (emptyInfo);
	}

	// Check if device type is supported.
	if (handle->deviceType != CAER_DEVICE_EDVS) {
		struct caer_edvs_info emptyInfo = {0, .deviceString = NULL};
		return (emptyInfo);
	}

	// Return a copy of the device information.
	return (handle->info);
}

bool edvsSendDefaultConfig(caerDeviceHandle cdh) {
	edvsHandle handle = (edvsHandle) cdh;
	edvsState state   = &handle->state;

	// Set all biases to default value. Based on DSV128 Fast biases.
	caerIntegerToByteArray(1992, state->dvs.biases[EDVS_CONFIG_BIAS_CAS], BIAS_LENGTH);
	caerIntegerToByteArray(1108364, state->dvs.biases[EDVS_CONFIG_BIAS_INJGND], BIAS_LENGTH);
	caerIntegerToByteArray(16777215, state->dvs.biases[EDVS_CONFIG_BIAS_REQPD], BIAS_LENGTH);
	caerIntegerToByteArray(8159221, state->dvs.biases[EDVS_CONFIG_BIAS_PUX], BIAS_LENGTH);
	caerIntegerToByteArray(132, state->dvs.biases[EDVS_CONFIG_BIAS_DIFFOFF], BIAS_LENGTH);
	caerIntegerToByteArray(309590, state->dvs.biases[EDVS_CONFIG_BIAS_REQ], BIAS_LENGTH);
	caerIntegerToByteArray(969, state->dvs.biases[EDVS_CONFIG_BIAS_REFR], BIAS_LENGTH);
	caerIntegerToByteArray(16777215, state->dvs.biases[EDVS_CONFIG_BIAS_PUY], BIAS_LENGTH);
	caerIntegerToByteArray(209996, state->dvs.biases[EDVS_CONFIG_BIAS_DIFFON], BIAS_LENGTH);
	caerIntegerToByteArray(13125, state->dvs.biases[EDVS_CONFIG_BIAS_DIFF], BIAS_LENGTH);
	caerIntegerToByteArray(271, state->dvs.biases[EDVS_CONFIG_BIAS_FOLL], BIAS_LENGTH);
	caerIntegerToByteArray(217, state->dvs.biases[EDVS_CONFIG_BIAS_PR], BIAS_LENGTH);

	// Send ALL biases to device.
	return (edvsSendBiases(state, -1));
}

bool edvsConfigSet(caerDeviceHandle cdh, int8_t modAddr, uint8_t paramAddr, uint32_t param) {
	edvsHandle handle = (edvsHandle) cdh;
	edvsState state   = &handle->state;

	switch (modAddr) {
		case CAER_HOST_CONFIG_SERIAL:
			switch (paramAddr) {
				case CAER_HOST_CONFIG_SERIAL_READ_SIZE:
					atomic_store(&state->serialState.serialReadSize, param);
					break;

				default:
					return (false);
					break;
			}
			break;

		case CAER_HOST_CONFIG_DATAEXCHANGE:
			return (dataExchangeConfigSet(&state->dataExchange, paramAddr, param));
			break;

		case CAER_HOST_CONFIG_PACKETS:
			return (containerGenerationConfigSet(&state->container, paramAddr, param));
			break;

		case CAER_HOST_CONFIG_LOG:
			switch (paramAddr) {
				case CAER_HOST_CONFIG_LOG_LEVEL:
					atomic_store(&state->deviceLogLevel, U8T(param));
					break;

				default:
					return (false);
					break;
			}
			break;

		case EDVS_CONFIG_DVS:
			switch (paramAddr) {
				case EDVS_CONFIG_DVS_RUN:
					if ((param == 1) && (!atomic_load(&state->dvs.running))) {
						const char *cmdStartDVS = "E+\n";
						if (!serialPortWrite(state, cmdStartDVS)) {
							return (false);
						}

						atomic_store(&state->dvs.running, true);
					}
					else if ((param == 0) && atomic_load(&state->dvs.running)) {
						const char *cmdStopDVS = "E-\n";
						if (!serialPortWrite(state, cmdStopDVS)) {
							return (false);
						}

						atomic_store(&state->dvs.running, false);
					}
					break;

				case EDVS_CONFIG_DVS_TIMESTAMP_RESET:
					if (param == 1) {
						atomic_store(&state->dvs.tsReset, true);
					}
					break;

				default:
					return (false);
					break;
			}
			break;

		case EDVS_CONFIG_BIAS:
			switch (paramAddr) {
				case EDVS_CONFIG_BIAS_CAS:
				case EDVS_CONFIG_BIAS_INJGND:
				case EDVS_CONFIG_BIAS_PUX:
				case EDVS_CONFIG_BIAS_PUY:
				case EDVS_CONFIG_BIAS_REQPD:
				case EDVS_CONFIG_BIAS_REQ:
				case EDVS_CONFIG_BIAS_FOLL:
				case EDVS_CONFIG_BIAS_PR:
				case EDVS_CONFIG_BIAS_REFR:
				case EDVS_CONFIG_BIAS_DIFF:
				case EDVS_CONFIG_BIAS_DIFFON:
				case EDVS_CONFIG_BIAS_DIFFOFF:
					caerIntegerToByteArray(param, state->dvs.biases[paramAddr], BIAS_LENGTH);
					return (edvsSendBiases(state, paramAddr));
					break;

				default:
					return (false);
					break;
			}
			break;

		default:
			return (false);
			break;
	}

	return (true);
}

bool edvsConfigGet(caerDeviceHandle cdh, int8_t modAddr, uint8_t paramAddr, uint32_t *param) {
	edvsHandle handle = (edvsHandle) cdh;
	edvsState state   = &handle->state;

	switch (modAddr) {
		case CAER_HOST_CONFIG_SERIAL:
			switch (paramAddr) {
				case CAER_HOST_CONFIG_SERIAL_READ_SIZE:
					*param = U32T(atomic_load(&state->serialState.serialReadSize));
					break;

				default:
					return (false);
					break;
			}
			break;

		case CAER_HOST_CONFIG_DATAEXCHANGE:
			return (dataExchangeConfigGet(&state->dataExchange, paramAddr, param));
			break;

		case CAER_HOST_CONFIG_PACKETS:
			return (containerGenerationConfigGet(&state->container, paramAddr, param));
			break;

		case CAER_HOST_CONFIG_LOG:
			switch (paramAddr) {
				case CAER_HOST_CONFIG_LOG_LEVEL:
					*param = atomic_load(&state->deviceLogLevel);
					break;

				default:
					return (false);
					break;
			}
			break;

		case EDVS_CONFIG_DVS:
			switch (paramAddr) {
				case EDVS_CONFIG_DVS_RUN:
					*param = atomic_load(&state->dvs.running);
					break;

				case EDVS_CONFIG_DVS_TIMESTAMP_RESET:
					// Always false because it's an impulse, it resets itself automatically.
					*param = false;
					break;

				default:
					return (false);
					break;
			}
			break;

		case EDVS_CONFIG_BIAS:
			switch (paramAddr) {
				case EDVS_CONFIG_BIAS_CAS:
				case EDVS_CONFIG_BIAS_INJGND:
				case EDVS_CONFIG_BIAS_PUX:
				case EDVS_CONFIG_BIAS_PUY:
				case EDVS_CONFIG_BIAS_REQPD:
				case EDVS_CONFIG_BIAS_REQ:
				case EDVS_CONFIG_BIAS_FOLL:
				case EDVS_CONFIG_BIAS_PR:
				case EDVS_CONFIG_BIAS_REFR:
				case EDVS_CONFIG_BIAS_DIFF:
				case EDVS_CONFIG_BIAS_DIFFON:
				case EDVS_CONFIG_BIAS_DIFFOFF:
					*param = caerByteArrayToInteger(state->dvs.biases[paramAddr], BIAS_LENGTH);
					break;

				default:
					return (false);
					break;
			}
			break;

		default:
			return (false);
			break;
	}

	return (true);
}

static bool serialThreadStart(edvsHandle handle) {
	// Start serial communication thread.
	if ((errno = thrd_create(&handle->state.serialState.serialThread, &serialThreadRun, handle)) != thrd_success) {
		edvsLog(CAER_LOG_CRITICAL, handle, "Failed to create serial thread. Error: %d.", errno);
		return (false);
	}

	while (atomic_load(&handle->state.serialState.serialThreadState) == THR_IDLE) {
		thrd_yield();
	}

	return (true);
}

static void serialThreadStop(edvsHandle handle) {
	// Shut down serial communication thread.
	atomic_store(&handle->state.serialState.serialThreadState, THR_EXITED);

	// Wait for serial communication thread to terminate.
	if ((errno = thrd_join(handle->state.serialState.serialThread, NULL)) != thrd_success) {
		// This should never happen!
		edvsLog(CAER_LOG_CRITICAL, handle, "Failed to join serial thread. Error: %d.", errno);
	}
}

static int serialThreadRun(void *handlePtr) {
	edvsHandle handle = handlePtr;
	edvsState state   = &handle->state;

	edvsLog(CAER_LOG_DEBUG, handle, "Starting serial communication thread ...");

	// Set device thread name. Maximum length of 15 chars due to Linux limitations.
	char threadName[MAX_THREAD_NAME_LENGTH + 1]; // +1 for terminating NUL character.
	strncpy(threadName, handle->info.deviceString, MAX_THREAD_NAME_LENGTH);
	threadName[MAX_THREAD_NAME_LENGTH] = '\0';

	thrd_set_name(threadName);

	// Signal data thread ready back to start function.
	atomic_store(&state->serialState.serialThreadState, THR_RUNNING);

	edvsLog(CAER_LOG_DEBUG, handle, "Serial communication thread running.");

	// Handle serial port reading (wait on data, 10 ms timeout).
	while (atomic_load_explicit(&state->serialState.serialThreadState, memory_order_relaxed) == THR_RUNNING) {
		size_t readSize = atomic_load_explicit(&state->serialState.serialReadSize, memory_order_relaxed);

		// Wait for at least 16 full events to be present in the buffer.
		int bytesAvailable = 0;

		while ((bytesAvailable < (16 * EDVS_EVENT_SIZE))
			   && atomic_load_explicit(&state->serialState.serialThreadState, memory_order_relaxed) == THR_RUNNING) {
			bytesAvailable = sp_input_waiting(state->serialState.serialPort);
		}

		if ((size_t) bytesAvailable < readSize) {
			readSize = (size_t) bytesAvailable;
		}

		// Ensure read size is a multiple of event size.
		readSize &= (size_t) ~0x03;

		uint8_t dataBuffer[readSize];
		int bytesRead = sp_blocking_read(state->serialState.serialPort, dataBuffer, readSize, 10);
		if (bytesRead < 0) {
			// ERROR: call exceptional shut-down callback and exit.
			if (state->serialState.serialShutdownCallback != NULL) {
				state->serialState.serialShutdownCallback(state->serialState.serialShutdownCallbackPtr);
			}
			break;
		}

		if (bytesRead >= EDVS_EVENT_SIZE) {
			// Read something (at least 1 possible event), process it and try again.
			edvsEventTranslator(handle, dataBuffer, (size_t) bytesRead);
		}
	}

	// Ensure threadRun is false on termination.
	atomic_store(&state->serialState.serialThreadState, THR_EXITED);

	edvsLog(CAER_LOG_DEBUG, handle, "Serial communication thread shut down.");

	return (EXIT_SUCCESS);
}

bool edvsDataStart(caerDeviceHandle cdh, void (*dataNotifyIncrease)(void *ptr), void (*dataNotifyDecrease)(void *ptr),
	void *dataNotifyUserPtr, void (*dataShutdownNotify)(void *ptr), void *dataShutdownUserPtr) {
	edvsHandle handle = (edvsHandle) cdh;
	edvsState state   = &handle->state;

	// Store new data available/not available anymore call-backs.
	dataExchangeSetNotify(&state->dataExchange, dataNotifyIncrease, dataNotifyDecrease, dataNotifyUserPtr);

	state->serialState.serialShutdownCallback    = dataShutdownNotify;
	state->serialState.serialShutdownCallbackPtr = dataShutdownUserPtr;

	containerGenerationCommitTimestampReset(&state->container);

	if (!dataExchangeBufferInit(&state->dataExchange)) {
		edvsLog(CAER_LOG_CRITICAL, handle, "Failed to initialize data exchange buffer.");
		return (false);
	}

	// Allocate packets.
	if (!containerGenerationAllocate(&state->container, EDVS_EVENT_TYPES)) {
		freeAllDataMemory(state);

		edvsLog(CAER_LOG_CRITICAL, handle, "Failed to allocate event packet container.");
		return (false);
	}

	state->currentPackets.polarity
		= caerPolarityEventPacketAllocate(EDVS_POLARITY_DEFAULT_SIZE, I16T(handle->info.deviceID), 0);
	if (state->currentPackets.polarity == NULL) {
		freeAllDataMemory(state);

		edvsLog(CAER_LOG_CRITICAL, handle, "Failed to allocate polarity event packet.");
		return (false);
	}

	state->currentPackets.special
		= caerSpecialEventPacketAllocate(EDVS_SPECIAL_DEFAULT_SIZE, I16T(handle->info.deviceID), 0);
	if (state->currentPackets.special == NULL) {
		freeAllDataMemory(state);

		edvsLog(CAER_LOG_CRITICAL, handle, "Failed to allocate special event packet.");
		return (false);
	}

	if (!serialThreadStart(handle)) {
		freeAllDataMemory(state);

		edvsLog(CAER_LOG_CRITICAL, handle, "Failed to start serial data transfers.");
		return (false);
	}

	if (dataExchangeStartProducers(&state->dataExchange)) {
		// Enable data transfer on USB end-point 6.
		edvsConfigSet((caerDeviceHandle) handle, EDVS_CONFIG_DVS, EDVS_CONFIG_DVS_RUN, true);
	}

	return (true);
}

bool edvsDataStop(caerDeviceHandle cdh) {
	edvsHandle handle = (edvsHandle) cdh;
	edvsState state   = &handle->state;

	if (dataExchangeStopProducers(&state->dataExchange)) {
		// Disable data transfer on USB end-point 6.
		edvsConfigSet((caerDeviceHandle) handle, EDVS_CONFIG_DVS, EDVS_CONFIG_DVS_RUN, false);
	}

	serialThreadStop(handle);

	dataExchangeBufferEmpty(&state->dataExchange);

	// Free current, uncommitted packets and ringbuffer.
	freeAllDataMemory(state);

	// Reset packet positions.
	state->currentPackets.polarityPosition = 0;
	state->currentPackets.specialPosition  = 0;

	return (true);
}

// Remember to properly free the returned memory after usage!
caerEventPacketContainer edvsDataGet(caerDeviceHandle cdh) {
	edvsHandle handle = (edvsHandle) cdh;
	edvsState state   = &handle->state;

	return (dataExchangeGet(&state->dataExchange, &state->serialState.serialThreadState));
}

#define TS_WRAP_ADD 0x10000
#define HIGH_BIT_MASK 0x80
#define LOW_BITS_MASK 0x7F

static void edvsEventTranslator(void *vhd, const uint8_t *buffer, size_t bytesSent) {
	edvsHandle handle = vhd;
	edvsState state   = &handle->state;

	// Return right away if not running anymore. This prevents useless work if many
	// buffers are still waiting when shut down, as well as incorrect event sequences
	// if a TS_RESET is stuck on ring-buffer commit further down, and detects shut-down;
	// then any subsequent buffers should also detect shut-down and not be handled.
	if (atomic_load(&state->serialState.serialThreadState) != THR_RUNNING) {
		return;
	}

	size_t i = 0;
	while (i < bytesSent) {
		uint8_t yByte = buffer[i];

		if ((yByte & HIGH_BIT_MASK) != HIGH_BIT_MASK) {
			edvsLog(
				CAER_LOG_NOTICE, handle, "Data not aligned, skipping to next data byte (%zu of %zu).", i, bytesSent);
			i++;
			continue;
		}

		if ((i + 3) >= bytesSent) {
			// Cannot fetch next event data, we're done with this buffer.
			return;
		}

		// Allocate new packets for next iteration as needed.
		if (!containerGenerationAllocate(&state->container, EDVS_EVENT_TYPES)) {
			edvsLog(CAER_LOG_CRITICAL, handle, "Failed to allocate event packet container.");
			return;
		}

		if (state->currentPackets.polarity == NULL) {
			state->currentPackets.polarity = caerPolarityEventPacketAllocate(
				EDVS_POLARITY_DEFAULT_SIZE, I16T(handle->info.deviceID), state->timestamps.wrapOverflow);
			if (state->currentPackets.polarity == NULL) {
				edvsLog(CAER_LOG_CRITICAL, handle, "Failed to allocate polarity event packet.");
				return;
			}
		}
		else if (state->currentPackets.polarityPosition
				 >= caerEventPacketHeaderGetEventCapacity((caerEventPacketHeader) state->currentPackets.polarity)) {
			// If not committed, let's check if any of the packets has reached its maximum
			// capacity limit. If yes, we grow them to accomodate new events.
			caerPolarityEventPacket grownPacket = (caerPolarityEventPacket) caerEventPacketGrow(
				(caerEventPacketHeader) state->currentPackets.polarity, state->currentPackets.polarityPosition * 2);
			if (grownPacket == NULL) {
				edvsLog(CAER_LOG_CRITICAL, handle, "Failed to grow polarity event packet.");
				return;
			}

			state->currentPackets.polarity = grownPacket;
		}

		if (state->currentPackets.special == NULL) {
			state->currentPackets.special = caerSpecialEventPacketAllocate(
				EDVS_SPECIAL_DEFAULT_SIZE, I16T(handle->info.deviceID), state->timestamps.wrapOverflow);
			if (state->currentPackets.special == NULL) {
				edvsLog(CAER_LOG_CRITICAL, handle, "Failed to allocate special event packet.");
				return;
			}
		}
		else if (state->currentPackets.specialPosition
				 >= caerEventPacketHeaderGetEventCapacity((caerEventPacketHeader) state->currentPackets.special)) {
			// If not committed, let's check if any of the packets has reached its maximum
			// capacity limit. If yes, we grow them to accomodate new events.
			caerSpecialEventPacket grownPacket = (caerSpecialEventPacket) caerEventPacketGrow(
				(caerEventPacketHeader) state->currentPackets.special, state->currentPackets.specialPosition * 2);
			if (grownPacket == NULL) {
				edvsLog(CAER_LOG_CRITICAL, handle, "Failed to grow special event packet.");
				return;
			}

			state->currentPackets.special = grownPacket;
		}

		bool tsReset   = false;
		bool tsBigWrap = false;

		uint8_t xByte   = buffer[i + 1];
		uint8_t ts1Byte = buffer[i + 2];
		uint8_t ts2Byte = buffer[i + 3];

		uint16_t shortTS = U16T((ts1Byte << 8) | ts2Byte);

		// Timestamp reset.
		if (atomic_load(&state->dvs.tsReset)) {
			atomic_store(&state->dvs.tsReset, false);

			// Send TS reset command to device. Ignore errors.
			const char *cmdTSReset = "!ET0\n";
			serialPortWrite(state, cmdTSReset);

			state->timestamps.wrapOverflow = 0;
			state->timestamps.wrapAdd      = 0;
			state->timestamps.lastShort    = 0;
			state->timestamps.last         = 0;
			state->timestamps.current      = 0;
			containerGenerationCommitTimestampReset(&state->container);
			containerGenerationCommitTimestampInit(&state->container, state->timestamps.current);

			// Defer timestamp reset event to later, so we commit it
			// alone, in its own packet.
			// Commit packets when doing a reset to clearly separate them.
			tsReset = true;
		}
		else {
			bool tsWrap = (shortTS < state->timestamps.lastShort);

			// Timestamp big wrap.
			if (tsWrap && (state->timestamps.wrapAdd == (INT32_MAX - (TS_WRAP_ADD - 1)))) {
				// Reset wrapAdd to zero at this point, so we can again
				// start detecting overruns of the 32bit value.
				state->timestamps.wrapAdd = 0;

				state->timestamps.lastShort = 0;

				state->timestamps.last    = 0;
				state->timestamps.current = 0;

				// Increment TSOverflow counter.
				state->timestamps.wrapOverflow++;

				caerSpecialEvent currentEvent = caerSpecialEventPacketGetEvent(
					state->currentPackets.special, state->currentPackets.specialPosition++);
				caerSpecialEventSetTimestamp(currentEvent, INT32_MAX);
				caerSpecialEventSetType(currentEvent, TIMESTAMP_WRAP);
				caerSpecialEventValidate(currentEvent, state->currentPackets.special);

				// Commit packets to separate before wrap from after cleanly.
				tsBigWrap = true;
			}
			else {
				if (tsWrap) {
					// Timestamp normal wrap (every ~65 ms).
					state->timestamps.wrapAdd += TS_WRAP_ADD;

					state->timestamps.lastShort = 0;
				}
				else {
					// Not a wrap, set this to track wrapping.
					state->timestamps.lastShort = shortTS;
				}

				// Expand to 32 bits. (Tick is 1µs already.)
				state->timestamps.last    = state->timestamps.current;
				state->timestamps.current = state->timestamps.wrapAdd + shortTS;
				containerGenerationCommitTimestampInit(&state->container, state->timestamps.current);

				// Check monotonicity of timestamps.
				checkMonotonicTimestamp(state->timestamps.current, state->timestamps.last, handle->info.deviceString,
					&handle->state.deviceLogLevel);

				uint8_t x     = (xByte & LOW_BITS_MASK);
				uint8_t y     = (yByte & LOW_BITS_MASK);
				bool polarity = (xByte & HIGH_BIT_MASK);

				// Check range conformity.
				if ((x < EDVS_ARRAY_SIZE_X) && (y < EDVS_ARRAY_SIZE_Y)) {
					caerPolarityEvent currentEvent = caerPolarityEventPacketGetEvent(
						state->currentPackets.polarity, state->currentPackets.polarityPosition++);
					caerPolarityEventSetTimestamp(currentEvent, state->timestamps.current);
					caerPolarityEventSetPolarity(currentEvent, polarity);
					caerPolarityEventSetY(currentEvent, y);
					caerPolarityEventSetX(currentEvent, x);
					caerPolarityEventValidate(currentEvent, state->currentPackets.polarity);
				}
				else {
					if (x >= EDVS_ARRAY_SIZE_X) {
						edvsLog(CAER_LOG_ALERT, handle, "X address out of range (0-%d): %" PRIu16 ".",
							EDVS_ARRAY_SIZE_X - 1, x);
					}
					if (y >= EDVS_ARRAY_SIZE_Y) {
						edvsLog(CAER_LOG_ALERT, handle, "Y address out of range (0-%d): %" PRIu16 ".",
							EDVS_ARRAY_SIZE_Y - 1, y);
					}
				}
			}
		}

		// Thresholds on which to trigger packet container commit.
		// tsReset and tsBigWrap are already defined above.
		// Trigger if any of the global container-wide thresholds are met.
		int32_t currentPacketContainerCommitSize = containerGenerationGetMaxPacketSize(&state->container);
		bool containerSizeCommit
			= (currentPacketContainerCommitSize > 0)
			  && ((state->currentPackets.polarityPosition >= currentPacketContainerCommitSize)
					 || (state->currentPackets.specialPosition >= currentPacketContainerCommitSize));

		bool containerTimeCommit = containerGenerationIsCommitTimestampElapsed(
			&state->container, state->timestamps.wrapOverflow, state->timestamps.current);

		// NOTE: with the current EDVS architecture, currentTimestamp always comes together
		// with an event, so the very first event that matches this threshold will be
		// also part of the committed packet container. This doesn't break any of the invariants.

		// Commit packet containers to the ring-buffer, so they can be processed by the
		// main-loop, when any of the required conditions are met.
		if (tsReset || tsBigWrap || containerSizeCommit || containerTimeCommit) {
			// One or more of the commit triggers are hit. Set the packet container up to contain
			// any non-empty packets. Empty packets are not forwarded to save memory.
			bool emptyContainerCommit = true;

			if (state->currentPackets.polarityPosition > 0) {
				containerGenerationSetPacket(
					&state->container, POLARITY_EVENT, (caerEventPacketHeader) state->currentPackets.polarity);

				state->currentPackets.polarity         = NULL;
				state->currentPackets.polarityPosition = 0;
				emptyContainerCommit                   = false;
			}

			if (state->currentPackets.specialPosition > 0) {
				containerGenerationSetPacket(
					&state->container, SPECIAL_EVENT, (caerEventPacketHeader) state->currentPackets.special);

				state->currentPackets.special         = NULL;
				state->currentPackets.specialPosition = 0;
				emptyContainerCommit                  = false;
			}

			containerGenerationExecute(&state->container, emptyContainerCommit, tsReset, state->timestamps.wrapOverflow,
				state->timestamps.current, &state->dataExchange, &state->serialState.serialThreadState,
				handle->info.deviceID, handle->info.deviceString, &handle->state.deviceLogLevel);
		}

		i += 4;
	}
}

static bool edvsSendBiases(edvsState state, int biasID) {
	// Biases are already stored in an array with the same format as expected by
	// the device, we can thus send them directly.
	char cmdSetBias[128];
	size_t startBias = (size_t) biasID;
	size_t stopBias  = startBias + 1;

	// With -1 as ID, we program all biases.
	if (biasID == -1) {
		startBias = 0;
		stopBias  = BIAS_NUMBER;
	}

	for (size_t i = startBias; i < stopBias; i++) {
		snprintf(cmdSetBias, 128, "!B%zu=%" PRIu32 "\n", i, caerByteArrayToInteger(state->dvs.biases[i], BIAS_LENGTH));

		if (!serialPortWrite(state, cmdSetBias)) {
			return (false);
		}
	}

	// Flush biases to chip.
	const char *cmdFlushBiases = "!BF\n";
	if (!serialPortWrite(state, cmdFlushBiases)) {
		return (false);
	}

	return (true);
}
