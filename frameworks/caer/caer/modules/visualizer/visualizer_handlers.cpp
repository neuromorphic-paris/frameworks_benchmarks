#include "visualizer_handlers.hpp"

#include <libcaercpp/devices/dynapse.hpp> // Only for constants.

#include "caer-sdk/mainloop.h"

#include <boost/algorithm/string.hpp>
#include <cmath>

// Default event handlers.
static void caerVisualizerEventHandlerNeuronMonitor(caerVisualizerPublicState state, const sf::Event &event);
static void caerVisualizerEventHandlerInput(caerVisualizerPublicState state, const sf::Event &event);

const std::string caerVisualizerEventHandlerListOptionsString = "Neuron_Monitor,Input";

const struct caer_visualizer_event_handler_info caerVisualizerEventHandlerList[] = {{"None", nullptr},
	{"Neuron_Monitor", &caerVisualizerEventHandlerNeuronMonitor}, {"Input", &caerVisualizerEventHandlerInput}};

const size_t caerVisualizerEventHandlerListLength
	= (sizeof(caerVisualizerEventHandlerList) / sizeof(struct caer_visualizer_event_handler_info));

static void caerVisualizerEventHandlerNeuronMonitor(caerVisualizerPublicState state, const sf::Event &event) {
	// This only works with actual hardware.
	const std::string moduleLibrary = sshsNodeGetStdString(state->eventSourceConfigNode, "moduleLibrary");
	if (moduleLibrary != "caer_dynapse") {
		return;
	}

	// On release of left click.
	if (event.type == sf::Event::MouseButtonReleased && event.mouseButton.button == sf::Mouse::Button::Left) {
		float positionX = (float) event.mouseButton.x;
		float positionY = (float) event.mouseButton.y;

		// Adjust coordinates according to zoom factor.
		float currentZoomFactor = state->renderZoomFactor.load();
		if (currentZoomFactor > 1.0f) {
			positionX = floorf(positionX / currentZoomFactor);
			positionY = floorf(positionY / currentZoomFactor);
		}
		else if (currentZoomFactor < 1.0f) {
			positionX = floorf(positionX * currentZoomFactor);
			positionY = floorf(positionY * currentZoomFactor);
		}

		// Transform into chip ID, core ID and neuron ID.
		const struct caer_spike_event val = caerDynapseSpikeEventFromXY(U16T(positionX), U16T(positionY));

		uint8_t chipId    = caerSpikeEventGetChipID(&val);
		uint8_t coreId    = caerSpikeEventGetSourceCoreID(&val);
		uint32_t neuronId = caerSpikeEventGetNeuronID(&val);

		// Set value via SSHS.
		sshsNode neuronMonitorNode = sshsGetRelativeNode(state->eventSourceConfigNode, "NeuronMonitor/");

		char monitorKey[] = "Ux_Cy";
		monitorKey[1]     = (char) (48 + chipId);
		monitorKey[4]     = (char) (48 + coreId);

		sshsNodePutInt(neuronMonitorNode, monitorKey, I32T(neuronId));

		caerLog(CAER_LOG_NOTICE, "Visualizer", "Monitoring neuron - chip ID: %d, core ID: %d, neuron ID: %d.", chipId,
			coreId, neuronId);
	}
}

static void caerVisualizerEventHandlerInput(caerVisualizerPublicState state, const sf::Event &event) {
	// This only works with an input module.
	const std::string moduleLibrary = sshsNodeGetStdString(state->eventSourceConfigNode, "moduleLibrary");
	if (!boost::algorithm::starts_with(moduleLibrary, "caer_input_")) {
		return;
	}

	// PAUSE.
	if (event.type == sf::Event::KeyPressed && event.key.code == sf::Keyboard::Key::Space) {
		bool pause = sshsNodeGetBool(state->eventSourceConfigNode, "pause");

		sshsNodePutBool(state->eventSourceConfigNode, "pause", !pause);
	}
	// SLOW DOWN.
	else if (event.type == sf::Event::KeyPressed && event.key.code == sf::Keyboard::Key::S) {
		int timeSlice = sshsNodeGetInt(state->eventSourceConfigNode, "PacketContainerInterval");

		sshsNodePutInt(state->eventSourceConfigNode, "PacketContainerInterval", timeSlice / 2);
	}
	// SPEED UP.
	else if (event.type == sf::Event::KeyPressed && event.key.code == sf::Keyboard::Key::F) {
		int timeSlice = sshsNodeGetInt(state->eventSourceConfigNode, "PacketContainerInterval");

		sshsNodePutInt(state->eventSourceConfigNode, "PacketContainerInterval", timeSlice * 2);
	}
}
