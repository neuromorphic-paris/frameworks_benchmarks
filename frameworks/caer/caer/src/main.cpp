#include "caer-sdk/utils.h"
#include "config.h"
#include "config_server.h"
#include "log.h"
#include "mainloop.h"

int main(int argc, char **argv) {
	// Initialize config storage from file, support command-line overrides.
	// If no init from file needed, pass NULL.
	caerConfigInit(argc, argv);

	// Initialize logging sub-system.
	caerLogInit();

	// TODO: implement service mode, use boost::process.

	// Start the configuration server thread for run-time config changes.
	caerConfigServerStart();

	// Finally run the main event processing loop.
	caerMainloopRun();

	// After shutting down the mainloops, also shutdown the config server
	// thread if needed.
	caerConfigServerStop();

	return (EXIT_SUCCESS);
}
