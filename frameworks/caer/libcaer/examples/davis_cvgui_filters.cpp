#include <libcaercpp/devices/davis.hpp>
#include <libcaercpp/filters/dvs_noise.hpp>
#include <atomic>
#include <csignal>
#include <opencv2/core.hpp>
#include <opencv2/highgui/highgui.hpp>

#if !defined(LIBCAER_HAVE_OPENCV) || LIBCAER_HAVE_OPENCV == 0
#error "This example requires OpenCV support in libcaer to be enabled."
#endif

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

	// Open a DAVIS, give it a device ID of 1, and don't care about USB bus or SN restrictions.
	libcaer::devices::davis davisHandle = libcaer::devices::davis(1);

	// Let's take a look at the information we have on the device.
	struct caer_davis_info davis_info = davisHandle.infoGet();

	printf("%s --- ID: %d, Master: %d, DVS X: %d, DVS Y: %d, Logic: %d.\n", davis_info.deviceString,
		davis_info.deviceID, davis_info.deviceIsMaster, davis_info.dvsSizeX, davis_info.dvsSizeY,
		davis_info.logicVersion);

	// Send the default configuration before using the device.
	// No configuration is sent automatically!
	davisHandle.sendDefaultConfig();

	// Enable hardware filters if present.
	if (davis_info.dvsHasBackgroundActivityFilter) {
		davisHandle.configSet(DAVIS_CONFIG_DVS, DAVIS_CONFIG_DVS_FILTER_BACKGROUND_ACTIVITY_TIME, 8);
		davisHandle.configSet(DAVIS_CONFIG_DVS, DAVIS_CONFIG_DVS_FILTER_BACKGROUND_ACTIVITY, true);

		davisHandle.configSet(DAVIS_CONFIG_DVS, DAVIS_CONFIG_DVS_FILTER_REFRACTORY_PERIOD_TIME, 1);
		davisHandle.configSet(DAVIS_CONFIG_DVS, DAVIS_CONFIG_DVS_FILTER_REFRACTORY_PERIOD, true);
	}

	// Add full-sized software filter to reduce DVS noise.
	libcaer::filters::DVSNoise dvsNoiseFilter = libcaer::filters::DVSNoise(davis_info.dvsSizeX, davis_info.dvsSizeX);

	dvsNoiseFilter.configSet(CAER_FILTER_DVS_BACKGROUND_ACTIVITY_TWO_LEVELS, true);
	dvsNoiseFilter.configSet(CAER_FILTER_DVS_BACKGROUND_ACTIVITY_CHECK_POLARITY, true);
	dvsNoiseFilter.configSet(CAER_FILTER_DVS_BACKGROUND_ACTIVITY_SUPPORT_MIN, 2);
	dvsNoiseFilter.configSet(CAER_FILTER_DVS_BACKGROUND_ACTIVITY_SUPPORT_MAX, 8);
	dvsNoiseFilter.configSet(CAER_FILTER_DVS_BACKGROUND_ACTIVITY_TIME, 2000);
	dvsNoiseFilter.configSet(CAER_FILTER_DVS_BACKGROUND_ACTIVITY_ENABLE, true);

	dvsNoiseFilter.configSet(CAER_FILTER_DVS_REFRACTORY_PERIOD_TIME, 200);
	dvsNoiseFilter.configSet(CAER_FILTER_DVS_REFRACTORY_PERIOD_ENABLE, true);

	dvsNoiseFilter.configSet(CAER_FILTER_DVS_HOTPIXEL_ENABLE, true);
	dvsNoiseFilter.configSet(CAER_FILTER_DVS_HOTPIXEL_LEARN, true);

	// Now let's get start getting some data from the device. We just loop in blocking mode,
	// no notification needed regarding new events. The shutdown notification, for example if
	// the device is disconnected, should be listened to.
	davisHandle.dataStart(nullptr, nullptr, nullptr, &usbShutdownHandler, nullptr);

	// Let's turn on blocking data-get mode to avoid wasting resources.
	davisHandle.configSet(CAER_HOST_CONFIG_DATAEXCHANGE, CAER_HOST_CONFIG_DATAEXCHANGE_BLOCKING, true);

	// Disable APS (frames) and IMU, not used for showing event filtering.
	davisHandle.configSet(DAVIS_CONFIG_APS, DAVIS_CONFIG_APS_RUN, false);
	davisHandle.configSet(DAVIS_CONFIG_IMU, DAVIS_CONFIG_IMU_RUN, false);

	cv::namedWindow("PLOT_EVENTS",
		cv::WindowFlags::WINDOW_AUTOSIZE | cv::WindowFlags::WINDOW_KEEPRATIO | cv::WindowFlags::WINDOW_GUI_EXPANDED);

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
				std::shared_ptr<libcaer::events::PolarityEventPacket> polarity
					= std::static_pointer_cast<libcaer::events::PolarityEventPacket>(packet);

				dvsNoiseFilter.apply(*polarity);

				printf("Got polarity packet with %d events, after filtering remaining %d events.\n",
					polarity->getEventNumber(), polarity->getEventValid());

				cv::Mat cvEvents(davis_info.dvsSizeY, davis_info.dvsSizeX, CV_8UC3, cv::Vec3b{127, 127, 127});
				for (const auto &e : *polarity) {
					// Discard invalid events (filtered out).
					if (!e.isValid()) {
						continue;
					}

					cvEvents.at<cv::Vec3b>(e.getY(), e.getX())
						= e.getPolarity() ? cv::Vec3b{255, 255, 255} : cv::Vec3b{0, 0, 0};
				}

				cv::imshow("PLOT_EVENTS", cvEvents);
				cv::waitKey(1);
			}
		}
	}

	davisHandle.dataStop();

	// Close automatically done by destructor.

	cv::destroyWindow("PLOT_EVENTS");

	printf("Shutdown successful.\n");

	return (EXIT_SUCCESS);
}
