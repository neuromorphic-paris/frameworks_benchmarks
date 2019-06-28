#ifndef MODULES_VISUALIZER_VISUALIZER_HANDLERS_H_
#define MODULES_VISUALIZER_VISUALIZER_HANDLERS_H_

#include "visualizer.hpp"

typedef void (*caerVisualizerEventHandler)(caerVisualizerPublicState state, const sf::Event &event);

struct caer_visualizer_event_handler_info {
	const std::string name;
	caerVisualizerEventHandler eventHandler;

	caer_visualizer_event_handler_info(const std::string &n, caerVisualizerEventHandler e) : name(n), eventHandler(e) {
	}
};

typedef const struct caer_visualizer_event_handler_info *caerVisualizerEventHandlerInfo;

extern const std::string caerVisualizerEventHandlerListOptionsString;
extern const struct caer_visualizer_event_handler_info caerVisualizerEventHandlerList[];
extern const size_t caerVisualizerEventHandlerListLength;

#endif /* MODULES_VISUALIZER_VISUALIZER_HANDLERS_H_ */
