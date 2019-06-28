#include <libcaer/libcaer.h>
#include <libcaer/devices/davis.h>
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

static void usbShutdownHandler(void *ptr) {
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

	// Open a DAVIS (Master), give it a device ID of 1, and don't care about USB bus or SN restrictions.
	caerDeviceHandle davis1_handle = caerDeviceOpen(1, CAER_DEVICE_DAVIS, 0, 0, NULL);
	if (davis1_handle == NULL) {
		return (EXIT_FAILURE);
	}

	// Open another DAVIS (Slave), give it a device ID of 2, and don't care about USB bus or SN restrictions.
	caerDeviceHandle davis2_handle = caerDeviceOpen(2, CAER_DEVICE_DAVIS, 0, 0, NULL);
	if (davis2_handle == NULL) {
		return (EXIT_FAILURE);
	}

	// Send the default configuration before using the device.
	// No configuration is sent automatically!
	caerDeviceSendDefaultConfig(davis1_handle);
	caerDeviceSendDefaultConfig(davis2_handle);

	// Now let's get start getting some data from the device. We just loop in blocking mode,
	// no notification needed regarding new events. The shutdown notification, for example if
	// the device is disconnected, should be listened to.
	caerDeviceDataStart(davis1_handle, NULL, NULL, NULL, &usbShutdownHandler, NULL);
	caerDeviceDataStart(davis2_handle, NULL, NULL, NULL, &usbShutdownHandler, NULL);

	// Let's turn on blocking data-get mode to avoid wasting resources.
	caerDeviceConfigSet(davis1_handle, CAER_HOST_CONFIG_DATAEXCHANGE, CAER_HOST_CONFIG_DATAEXCHANGE_BLOCKING, true);
	caerDeviceConfigSet(davis2_handle, CAER_HOST_CONFIG_DATAEXCHANGE, CAER_HOST_CONFIG_DATAEXCHANGE_BLOCKING, true);

	// Reset master timestamps to start from a common point in time.
	caerDeviceConfigSet(davis1_handle, DAVIS_CONFIG_MUX, DAVIS_CONFIG_MUX_TIMESTAMP_RESET, true);

	while (!atomic_load_explicit(&globalShutdown, memory_order_relaxed)) {
		caerEventPacketContainer packetContainer1 = caerDeviceDataGet(davis1_handle);
		caerEventPacketContainer packetContainer2 = caerDeviceDataGet(davis2_handle);

		if (packetContainer1 == NULL && packetContainer2 == NULL) {
			continue; // Skip if nothing there.
		}

		if (packetContainer1 != NULL) {
			int32_t packetNum1 = caerEventPacketContainerGetEventPacketsNumber(packetContainer1);
			printf("\nGot first event container with %d packets (allocated).\n", packetNum1);
		}

		if (packetContainer2 != NULL) {
			int32_t packetNum2 = caerEventPacketContainerGetEventPacketsNumber(packetContainer2);
			printf("\nGot second event container with %d packets (allocated).\n", packetNum2);
		}

		caerEventPacketContainerFree(packetContainer1);
		caerEventPacketContainerFree(packetContainer2);
	}

	caerDeviceDataStop(davis1_handle);
	caerDeviceDataStop(davis2_handle);

	caerDeviceClose(&davis1_handle);
	caerDeviceClose(&davis2_handle);

	printf("Shutdown successful.\n");

	return (EXIT_SUCCESS);
}
