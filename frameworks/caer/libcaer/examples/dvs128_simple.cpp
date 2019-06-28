#include <libcaercpp/devices/dvs128.hpp>
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

	// Open a DVS128, give it a device ID of 1, and don't care about USB bus or SN restrictions.
	libcaer::devices::dvs128 dvs128Handle = libcaer::devices::dvs128(1, 0, 0, "");

	// Let's take a look at the information we have on the device.
	struct caer_dvs128_info dvs128_info = dvs128Handle.infoGet();

	printf("%s --- ID: %d, Master: %d, DVS X: %d, DVS Y: %d, Logic: %d.\n", dvs128_info.deviceString,
		dvs128_info.deviceID, dvs128_info.deviceIsMaster, dvs128_info.dvsSizeX, dvs128_info.dvsSizeY,
		dvs128_info.logicVersion);

	// Send the default configuration before using the device.
	// No configuration is sent automatically!
	dvs128Handle.sendDefaultConfig();

	// Tweak some biases, to increase bandwidth in this case.
	dvs128Handle.configSet(DVS128_CONFIG_BIAS, DVS128_CONFIG_BIAS_PR, 695);
	dvs128Handle.configSet(DVS128_CONFIG_BIAS, DVS128_CONFIG_BIAS_FOLL, 867);

	// Let's verify they really changed!
	uint32_t prBias   = dvs128Handle.configGet(DVS128_CONFIG_BIAS, DVS128_CONFIG_BIAS_PR);
	uint32_t follBias = dvs128Handle.configGet(DVS128_CONFIG_BIAS, DVS128_CONFIG_BIAS_FOLL);

	printf("New bias values --- PR: %d, FOLL: %d.\n", prBias, follBias);

	// Now let's get start getting some data from the device. We just loop in blocking mode,
	// no notification needed regarding new events. The shutdown notification, for example if
	// the device is disconnected, should be listened to.
	dvs128Handle.dataStart(nullptr, nullptr, nullptr, &usbShutdownHandler, nullptr);

	// Let's turn on blocking data-get mode to avoid wasting resources.
	dvs128Handle.configSet(CAER_HOST_CONFIG_DATAEXCHANGE, CAER_HOST_CONFIG_DATAEXCHANGE_BLOCKING, true);

	while (!globalShutdown.load(memory_order_relaxed)) {
		std::unique_ptr<libcaer::events::EventPacketContainer> packetContainer = dvs128Handle.dataGet();
		if (packetContainer == nullptr) {
			continue; // Skip if nothing there.
		}

		printf("\nGot event container with %d packets (allocated).\n", packetContainer->size());

		for (auto &packet : *packetContainer) {
			if (packet == nullptr) {
				printf("Packet is empty (not present).\n");
				continue; // Skip if nothing there.
			}

			printf("Packet of type %d -> %d events, %d capacity.\n", packet->getEventType(), packet->getEventNumber(),
				packet->getEventCapacity());

			if (packet->getEventType() == POLARITY_EVENT) {
				std::shared_ptr<const libcaer::events::PolarityEventPacket> polarity
					= std::static_pointer_cast<libcaer::events::PolarityEventPacket>(packet);

				// Get full timestamp and addresses of first event.
				const libcaer::events::PolarityEvent &firstEvent = (*polarity)[0];

				int32_t ts = firstEvent.getTimestamp();
				uint16_t x = firstEvent.getX();
				uint16_t y = firstEvent.getY();
				bool pol   = firstEvent.getPolarity();

				printf("First polarity event - ts: %d, x: %d, y: %d, pol: %d.\n", ts, x, y, pol);
			}
		}
	}

	dvs128Handle.dataStop();

	// Close automatically done by destructor.

	printf("Shutdown successful.\n");

	return (EXIT_SUCCESS);
}
