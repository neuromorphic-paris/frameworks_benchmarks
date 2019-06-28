#ifndef VISUALIZER_H_
#define VISUALIZER_H_

#include "caer-sdk/utils.h"

#include <libcaer/events/packetContainer.h>
#include <GL/glew.h>
#include <SFML/Graphics.hpp>
#include <atomic>
#include <climits>

struct caer_visualizer_public_state {
	sshsNode eventSourceConfigNode;
	sshsNode visualizerConfigNode;
	uint32_t renderSizeX;
	uint32_t renderSizeY;
	std::atomic<float> renderZoomFactor;
	void *renderState; // Reserved for renderers to put their internal state into.
	sf::RenderWindow *renderWindow;
	sf::Font *font;
};

typedef const struct caer_visualizer_public_state *caerVisualizerPublicState;

// This function is intended to be called by Renderer State Init functions only!
void caerVisualizerResetRenderSize(caerVisualizerPublicState pubState, uint32_t newX, uint32_t newY);

// This define is for Render State Init functions that allocate no memory, to use as return value.
#define CAER_VISUALIZER_RENDER_INIT_NO_MEM ((void *) 0x01)

#endif /* VISUALIZER_H_ */
