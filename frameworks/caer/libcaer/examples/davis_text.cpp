#include <libcaercpp/devices/davis.hpp>

#include <atomic>
#include <csignal>
#include <fstream>
#include <iostream>

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

int main(int argc, char **argv) {
	// Check for input argument (file name).
	if (argc != 2) {
		std::cout << "Pass file name as argument!" << std::endl;
		return (EXIT_FAILURE);
	}

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

	// Open file for writing.
	std::fstream fileOutput;
	fileOutput.open(argv[1], std::fstream::app);

	// Open a DAVIS, give it a device ID of 1, and don't care about USB bus or SN restrictions.
	libcaer::devices::davis davisHandle = libcaer::devices::davis(1);

	// Send the default configuration before using the device.
	// No configuration is sent automatically!
	davisHandle.sendDefaultConfig();

	// Now let's get start getting some data from the device. We just loop in blocking mode,
	// no notification needed regarding new events. The shutdown notification, for example if
	// the device is disconnected, should be listened to.
	davisHandle.dataStart(nullptr, nullptr, nullptr, &usbShutdownHandler, nullptr);

	// Let's turn on blocking data-get mode to avoid wasting resources.
	davisHandle.configSet(CAER_HOST_CONFIG_DATAEXCHANGE, CAER_HOST_CONFIG_DATAEXCHANGE_BLOCKING, true);

	std::cout << "Started logging to file ..." << std::endl;

	while (!globalShutdown.load(memory_order_relaxed)) {
		std::unique_ptr<libcaer::events::EventPacketContainer> packetContainer = davisHandle.dataGet();
		if (packetContainer == nullptr) {
			continue; // Skip if nothing there.
		}

		for (auto &packet : *packetContainer) {
			if (packet == nullptr) {
				continue; // Skip if nothing there.
			}

			if (packet->getEventType() == POLARITY_EVENT) {
				std::shared_ptr<const libcaer::events::PolarityEventPacket> polarity
					= std::static_pointer_cast<libcaer::events::PolarityEventPacket>(packet);

				// Print out timestamps and addresses.
				for (const auto &evt : *polarity) {
					int64_t ts = evt.getTimestamp64(*polarity);
					uint16_t x = evt.getX();
					uint16_t y = evt.getY();
					bool pol   = evt.getPolarity();

					fileOutput << "DVS " << ts << " " << x << " " << y << " " << pol << std::endl;
				}
			}
			else if (packet->getEventType() == IMU6_EVENT) {
				std::shared_ptr<const libcaer::events::IMU6EventPacket> imu
					= std::static_pointer_cast<libcaer::events::IMU6EventPacket>(packet);

				// Print out timestamps and data.
				for (const auto &evt : *imu) {
					int64_t ts   = evt.getTimestamp64(*imu);
					float accelX = evt.getAccelX();
					float accelY = evt.getAccelY();
					float accelZ = evt.getAccelZ();
					float gyroX  = evt.getGyroX();
					float gyroY  = evt.getGyroY();
					float gyroZ  = evt.getGyroZ();

					fileOutput << "IMU6 " << ts << " " << accelX << " " << accelY << " " << accelZ << " " << gyroX
							   << " " << gyroY << " " << gyroZ << std::endl;
				}
			}
		}
	}

	davisHandle.dataStop();

	// Close automatically done by destructor.

	std::cout << "Stopped logging to file." << std::endl;

	return (EXIT_SUCCESS);
}
