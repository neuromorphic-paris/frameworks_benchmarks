#include <libcaercpp/devices/davis.hpp>
#include <atomic>
#include <csignal>

using namespace std;

static atomic_bool globalShutdown(false);

static void globalShutdownSignalHandler(int signal) {
	// Simply set the running flag to false on SIGTERM and SIGINT (CTRL+C) for global shutdown.
	if (signal == SIGTERM || signal == SIGINT) {
		globalShutdown.store(true);
	}
}

static void usbShutdownHandler(void *ptr) {
	(void) (ptr); // UNUSED.

	globalShutdown.store(true);
}

int main(void) {
// Install signal handler for global shutdown.
#if defined(_WIN32)
	if (signal(SIGTERM, &globalShutdownSignalHandler) == SIG_ERR) {
		libcaer::log::log(libcaer::log::logLevel::CRITICAL, "ShutdownAction",
			"Failed to set signal handler for SIGTERM. Error: %d.", errno);
		return (EXIT_FAILURE);
	}

	if (signal(SIGINT, &globalShutdownSignalHandler) == SIG_ERR) {
		libcaer::log::log(libcaer::log::logLevel::CRITICAL, "ShutdownAction",
			"Failed to set signal handler for SIGINT. Error: %d.", errno);
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
		libcaer::log::log(libcaer::log::logLevel::CRITICAL, "ShutdownAction",
			"Failed to set signal handler for SIGTERM. Error: %d.", errno);
		return (EXIT_FAILURE);
	}

	if (sigaction(SIGINT, &shutdownAction, NULL) == -1) {
		libcaer::log::log(libcaer::log::logLevel::CRITICAL, "ShutdownAction",
			"Failed to set signal handler for SIGINT. Error: %d.", errno);
		return (EXIT_FAILURE);
	}
#endif

	// Open a DAVIS on RaspberryPi, give it a device ID of 1.
	libcaer::devices::davisrpi davisHandle = libcaer::devices::davisrpi(1);

	// Let's take a look at the information we have on the device.
	struct caer_davis_info davis_info = davisHandle.infoGet();

	printf("%s --- ID: %d, Master: %d, DVS X: %d, DVS Y: %d, Logic: %d.\n", davis_info.deviceString,
		davis_info.deviceID, davis_info.deviceIsMaster, davis_info.dvsSizeX, davis_info.dvsSizeY,
		davis_info.logicVersion);

	// Send the default configuration before using the device.
	// No configuration is sent automatically!
	davisHandle.configSet(DAVIS_CONFIG_DDRAER, DAVIS_CONFIG_DDRAER_REQ_DELAY, 1); // in cycles @ LogicClock
	davisHandle.configSet(DAVIS_CONFIG_DDRAER, DAVIS_CONFIG_DDRAER_ACK_DELAY, 1); // in cycles @ LogicClock

	// Now let's get start getting some data from the device. We just loop in blocking mode,
	// no notification needed regarding new events. The shutdown notification, for example if
	// the device is disconnected, should be listened to.
	davisHandle.dataStart(nullptr, nullptr, nullptr, &usbShutdownHandler, nullptr);

	// Let's turn on blocking data-get mode to avoid wasting resources.
	davisHandle.configSet(CAER_HOST_CONFIG_DATAEXCHANGE, CAER_HOST_CONFIG_DATAEXCHANGE_BLOCKING, true);

	while (!globalShutdown.load(memory_order_relaxed)) {
		std::unique_ptr<libcaer::events::EventPacketContainer> packetContainer = davisHandle.dataGet();
		if (packetContainer == nullptr) {
			continue; // Skip if nothing there.
		}

		printf("\nTHIS SHOULD NOT HAPPEN IN BENCHMARK MODE!\nGot event container with %d packets (allocated).\n",
			packetContainer->size());
	}

	davisHandle.dataStop();

	// Close automatically done by destructor.

	printf("Shutdown successful.\n");

	return (EXIT_SUCCESS);
}
