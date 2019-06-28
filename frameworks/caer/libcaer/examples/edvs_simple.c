#include <libcaer/libcaer.h>
#include <libcaer/devices/edvs.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdio.h>

static atomic_bool globalShutdown = ATOMIC_VAR_INIT(false);

static void globalShutdownSignalHandler(int signal) {
	// Simply set the running flag to false on SIGTERM and SIGINT (CTRL+C) for global shutdown.
	if (signal == SIGTERM || signal == SIGINT) {
		atomic_store(&globalShutdown, true);
	}
}

static void serialShutdownHandler(void *ptr) {
	(void) (ptr); // UNUSED.

	atomic_store(&globalShutdown, true);
}

int main(void) {
// Install signal handler for global shutdown.
#if defined(_WIN32)
	if (signal(SIGTERM, &globalShutdownSignalHandler) == SIG_ERR) {
		caerLog(CAER_LOG_CRITICAL, "ShutdownAction", "Failed to set signal handler for SIGTERM. Error: %d.", errno);
		return (EXIT_FAILURE);
	}

	if (signal(SIGINT, &globalShutdownSignalHandler) == SIG_ERR) {
		caerLog(CAER_LOG_CRITICAL, "ShutdownAction", "Failed to set signal handler for SIGINT. Error: %d.", errno);
		return (EXIT_FAILURE);
	}
#else
	struct sigaction shutdownAction;

	shutdownAction.sa_handler = &globalShutdownSignalHandler;
	shutdownAction.sa_flags   = 0;
	sigemptyset(&shutdownAction.sa_mask);
	sigaddset(&shutdownAction.sa_mask, SIGTERM);
	sigaddset(&shutdownAction.sa_mask, SIGINT);

	if (sigaction(SIGTERM, &shutdownAction, NULL) == -1) {
		caerLog(CAER_LOG_CRITICAL, "ShutdownAction", "Failed to set signal handler for SIGTERM. Error: %d.", errno);
		return (EXIT_FAILURE);
	}

	if (sigaction(SIGINT, &shutdownAction, NULL) == -1) {
		caerLog(CAER_LOG_CRITICAL, "ShutdownAction", "Failed to set signal handler for SIGINT. Error: %d.", errno);
		return (EXIT_FAILURE);
	}
#endif

	// Open an EDVS-4337, give it a device ID of 1, on the default Linux USB serial port.
	caerDeviceHandle edvs_handle
		= caerDeviceOpenSerial(1, CAER_DEVICE_EDVS, "/dev/ttyUSB0", CAER_HOST_CONFIG_SERIAL_BAUD_RATE_12M);
	if (edvs_handle == NULL) {
		return (EXIT_FAILURE);
	}

	// Let's take a look at the information we have on the device.
	struct caer_edvs_info edvs_info = caerEDVSInfoGet(edvs_handle);

	printf("%s --- ID: %d, Master: %d, DVS X: %d, DVS Y: %d.\n", edvs_info.deviceString, edvs_info.deviceID,
		edvs_info.deviceIsMaster, edvs_info.dvsSizeX, edvs_info.dvsSizeY);

	// Send the default configuration before using the device.
	// No configuration is sent automatically!
	caerDeviceSendDefaultConfig(edvs_handle);

	// Tweak some biases, to increase bandwidth in this case.
	caerDeviceConfigSet(edvs_handle, EDVS_CONFIG_BIAS, EDVS_CONFIG_BIAS_PR, 695);
	caerDeviceConfigSet(edvs_handle, EDVS_CONFIG_BIAS, EDVS_CONFIG_BIAS_FOLL, 867);

	// Let's verify they really changed!
	uint32_t prBias, follBias;
	caerDeviceConfigGet(edvs_handle, EDVS_CONFIG_BIAS, EDVS_CONFIG_BIAS_PR, &prBias);
	caerDeviceConfigGet(edvs_handle, EDVS_CONFIG_BIAS, EDVS_CONFIG_BIAS_FOLL, &follBias);

	printf("New bias values --- PR: %d, FOLL: %d.\n", prBias, follBias);

	// Now let's get start getting some data from the device. We just loop in blocking mode,
	// no notification needed regarding new events. The shutdown notification, for example if
	// the device is disconnected, should be listened to.
	caerDeviceDataStart(edvs_handle, NULL, NULL, NULL, &serialShutdownHandler, NULL);

	// Let's turn on blocking data-get mode to avoid wasting resources.
	caerDeviceConfigSet(edvs_handle, CAER_HOST_CONFIG_DATAEXCHANGE, CAER_HOST_CONFIG_DATAEXCHANGE_BLOCKING, true);

	while (!atomic_load_explicit(&globalShutdown, memory_order_relaxed)) {
		caerEventPacketContainer packetContainer = caerDeviceDataGet(edvs_handle);
		if (packetContainer == NULL) {
			continue; // Skip if nothing there.
		}

		int32_t packetNum = caerEventPacketContainerGetEventPacketsNumber(packetContainer);

		printf("\nGot event container with %d packets (allocated).\n", packetNum);

		for (int32_t i = 0; i < packetNum; i++) {
			caerEventPacketHeader packetHeader = caerEventPacketContainerGetEventPacket(packetContainer, i);
			if (packetHeader == NULL) {
				printf("Packet %d is empty (not present).\n", i);
				continue; // Skip if nothing there.
			}

			printf("Packet %d of type %d -> size is %d.\n", i, caerEventPacketHeaderGetEventType(packetHeader),
				caerEventPacketHeaderGetEventNumber(packetHeader));

			// Packet 0 is always the special events packet for EDVS-4337, while packet is the polarity events packet.
			if (i == POLARITY_EVENT) {
				caerPolarityEventPacket polarity = (caerPolarityEventPacket) packetHeader;

				// Get full timestamp and addresses of first event.
				caerPolarityEvent firstEvent = caerPolarityEventPacketGetEvent(polarity, 0);

				int32_t ts = caerPolarityEventGetTimestamp(firstEvent);
				uint16_t x = caerPolarityEventGetX(firstEvent);
				uint16_t y = caerPolarityEventGetY(firstEvent);
				bool pol   = caerPolarityEventGetPolarity(firstEvent);

				printf("First polarity event - ts: %d, x: %d, y: %d, pol: %d.\n", ts, x, y, pol);
			}
		}

		caerEventPacketContainerFree(packetContainer);
	}

	caerDeviceDataStop(edvs_handle);

	caerDeviceClose(&edvs_handle);

	printf("Shutdown successful.\n");

	return (EXIT_SUCCESS);
}
