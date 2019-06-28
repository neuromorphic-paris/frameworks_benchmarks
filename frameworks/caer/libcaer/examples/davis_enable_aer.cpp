/*
* This C++ code shows how to open a DAVIS sensor,
* load the default biases, and enable the AER external control.
*
* After operating this routine, the program exits, leaving the DAVIS
* sensor in external AER mode.
*
* Compile with:
* g++ -std=c++11 -pedantic -Wall -Wextra -O2 -o davis_enable_aer davis_enable_aer.cpp -D_DEFAULT_SOURCE=1 -lcaer
*/
#include <libcaercpp/devices/davis.hpp>

using namespace std;

int main(void) {
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

	// Enable external AER control, keep chip runnning.
	davisHandle.configSet(DAVIS_CONFIG_MUX, DAVIS_CONFIG_MUX_FORCE_CHIP_BIAS_ENABLE, true);
	davisHandle.configSet(DAVIS_CONFIG_DVS, DAVIS_CONFIG_DVS_EXTERNAL_AER_CONTROL, true);

	// Close automatically done by destructor.

	return (EXIT_SUCCESS);
}
