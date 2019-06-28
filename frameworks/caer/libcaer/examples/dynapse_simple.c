/*
*   Author: Federico Corradi, 2017
*  compile with:  gcc -std=c11 -pedantic -Wall -Wextra -O2 -o dynapse_simple dynapse_simple.c -D_DEFAULT_SOURCE=1 -lcaer
*/

#include <libcaer/libcaer.h>
#include <libcaer/devices/dynapse.h>
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

	// Open a DYNAPSE, give it a device ID of 1, and don't care about USB bus or SN restrictions.
	caerDeviceHandle dynapse_handle = caerDeviceOpen(1, CAER_DEVICE_DYNAPSE, 0, 0, NULL);
	if (dynapse_handle == NULL) {
		return (EXIT_FAILURE);
	}

	// Let's take a look at the information we have on the device.
	struct caer_dynapse_info dynapse_info = caerDynapseInfoGet(dynapse_handle);

	printf("%s --- ID: %d, Master: %d,  Logic: %d.\n", dynapse_info.deviceString, dynapse_info.deviceID,
		dynapse_info.deviceIsMaster, dynapse_info.logicVersion);

	// Send the default configuration before using the device.
	// No configuration is sent automatically!
	caerDeviceSendDefaultConfig(dynapse_handle);

	// Now let's get start getting some data from the device. We just loop in blocking mode,
	// no notification needed regarding new events. The shutdown notification, for example if
	// the device is disconnected, should be listened to.
	// This automatically turns on the AER and CHIP state machines.
	caerDeviceDataStart(dynapse_handle, NULL, NULL, NULL, &usbShutdownHandler, NULL);

	// Let's turn on blocking data-get mode to avoid wasting resources.
	caerDeviceConfigSet(dynapse_handle, CAER_HOST_CONFIG_DATAEXCHANGE, CAER_HOST_CONFIG_DATAEXCHANGE_BLOCKING, true);

	while (!atomic_load_explicit(&globalShutdown, memory_order_relaxed)) {
		caerEventPacketContainer packetContainer = caerDeviceDataGet(dynapse_handle);
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

			// Spike Events
			if (i == SPIKE_EVENT) {
				caerSpikeEventPacket spike = (caerSpikeEventPacket) packetHeader;

				// Get full timestamp and addresses of first event.
				caerSpikeEventConst firstEvent = caerSpikeEventPacketGetEventConst(spike, 0);

				int32_t ts      = caerSpikeEventGetTimestamp(firstEvent);
				uint16_t neuid  = caerSpikeEventGetNeuronID(firstEvent);
				uint16_t coreid = caerSpikeEventGetSourceCoreID(firstEvent);

				printf("First spike event - ts: %d, neu: %d, core: %d\n", ts, neuid, coreid);
			}
		}

		caerEventPacketContainerFree(packetContainer);
	}

	caerDeviceDataStop(dynapse_handle);

	caerDeviceClose(&dynapse_handle);

	printf("Shutdown successful.\n");

	return (EXIT_SUCCESS);
}
