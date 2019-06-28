#include "visualizer_renderers.hpp"

#include <libcaercpp/events/frame.hpp>
#include <libcaercpp/events/imu6.hpp>
#include <libcaercpp/events/matrix4x4.hpp> // Render camera pose
#include <libcaercpp/events/point2d.hpp>
#include <libcaercpp/events/point4d.hpp>
#include <libcaercpp/events/polarity.hpp>
#include <libcaercpp/events/spike.hpp>

#include <libcaercpp/devices/davis.hpp>   // Only for constants.
#include <libcaercpp/devices/dynapse.hpp> // Only for constants.

#include "ext/sfml/helpers.hpp"
#include "ext/sfml/line.hpp"

static void *caerVisualizerRendererPolarityEventsStateInit(caerVisualizerPublicState state);
static bool caerVisualizerRendererPolarityEvents(caerVisualizerPublicState state, caerEventPacketContainer container);
static const struct caer_visualizer_renderer_info rendererPolarityEvents(
	"Polarity", &caerVisualizerRendererPolarityEvents, false, &caerVisualizerRendererPolarityEventsStateInit, nullptr);

static void *caerVisualizerRendererFrameEventsStateInit(caerVisualizerPublicState state);
static void caerVisualizerRendererFrameEventsStateExit(caerVisualizerPublicState state);
static bool caerVisualizerRendererFrameEvents(caerVisualizerPublicState state, caerEventPacketContainer container);
static const struct caer_visualizer_renderer_info rendererFrameEvents("Frame", &caerVisualizerRendererFrameEvents,
	false, &caerVisualizerRendererFrameEventsStateInit, &caerVisualizerRendererFrameEventsStateExit);

static bool caerVisualizerRendererIMU6Events(caerVisualizerPublicState state, caerEventPacketContainer container);
static const struct caer_visualizer_renderer_info rendererIMU6Events("IMU_6-axes", &caerVisualizerRendererIMU6Events);

static bool caerVisualizerRendererPoint2DEvents(caerVisualizerPublicState state, caerEventPacketContainer container);
static const struct caer_visualizer_renderer_info rendererPoint2DEvents(
	"2D_Points", &caerVisualizerRendererPoint2DEvents);

static bool caerVisualizerRendererSpikeEvents(caerVisualizerPublicState state, caerEventPacketContainer container);
static const struct caer_visualizer_renderer_info rendererSpikeEvents("Spikes", &caerVisualizerRendererSpikeEvents);

static void *caerVisualizerRendererMatrix4x4EventsPoseStateInit(caerVisualizerPublicState state);
static void caerVisualizerRendererMatrix4x4EventsPoseStateExit(caerVisualizerPublicState state);
static bool caerVisualizerRendererMatrix4x4EventsPose(
	caerVisualizerPublicState state, caerEventPacketContainer container);
static const struct caer_visualizer_renderer_info rendererMatrix4x4EventsPose("Camera_pose",
	&caerVisualizerRendererMatrix4x4EventsPose, false, &caerVisualizerRendererMatrix4x4EventsPoseStateInit,
	&caerVisualizerRendererMatrix4x4EventsPoseStateExit);

static void *caerVisualizerRendererSpikeEventsRasterStateInit(caerVisualizerPublicState state);
static bool caerVisualizerRendererSpikeEventsRaster(
	caerVisualizerPublicState state, caerEventPacketContainer container);
static const struct caer_visualizer_renderer_info rendererSpikeEventsRaster("Spikes_Raster_Plot",
	&caerVisualizerRendererSpikeEventsRaster, false, &caerVisualizerRendererSpikeEventsRasterStateInit, nullptr);

static void *caerVisualizerRendererPolarityAndFrameEventsStateInit(caerVisualizerPublicState state);
static void caerVisualizerRendererPolarityAndFrameEventsStateExit(caerVisualizerPublicState state);
static bool caerVisualizerRendererPolarityAndFrameEvents(
	caerVisualizerPublicState state, caerEventPacketContainer container);
static const struct caer_visualizer_renderer_info rendererPolarityAndFrameEvents("Polarity_and_Frames",
	&caerVisualizerRendererPolarityAndFrameEvents, false, &caerVisualizerRendererPolarityAndFrameEventsStateInit,
	&caerVisualizerRendererPolarityAndFrameEventsStateExit);

static void *caerVisualizerRendererPolarityAnd2DEventsInit(caerVisualizerPublicState state);
static bool caerVisualizerRendererPolarityAnd2DEvents(
	caerVisualizerPublicState state, caerEventPacketContainer container);
static const struct caer_visualizer_renderer_info rendererPolarityAnd2DEvents("Polarity_and_2D_Points",
	&caerVisualizerRendererPolarityAnd2DEvents, false, &caerVisualizerRendererPolarityAnd2DEventsInit, nullptr);

const std::string caerVisualizerRendererListOptionsString = "Polarity,Frame,IMU_6-axes,2D_Points,Spikes,Spikes_Raster_"
															"Plot,Polarity_and_Frames,Camera_pose,Polarity_and_2D_"
															"Points";

const struct caer_visualizer_renderer_info caerVisualizerRendererList[] = {{"None", nullptr}, rendererPolarityEvents,
	rendererFrameEvents, rendererIMU6Events, rendererPoint2DEvents, rendererSpikeEvents, rendererSpikeEventsRaster,
	rendererPolarityAndFrameEvents, rendererMatrix4x4EventsPose, rendererPolarityAnd2DEvents};

const size_t caerVisualizerRendererListLength
	= (sizeof(caerVisualizerRendererList) / sizeof(struct caer_visualizer_renderer_info));

static void *caerVisualizerRendererPolarityEventsStateInit(caerVisualizerPublicState state) {
	sshsNodeCreateBool(state->visualizerConfigNode, "DoubleSpacedAddresses", false, SSHS_FLAGS_NORMAL,
		"Space DVS addresses apart by doubling them, this is useful for the CDAVIS sensor to put them as they are in "
		"the pixel array.");

	return (CAER_VISUALIZER_RENDER_INIT_NO_MEM); // No allocated memory.
}

static bool caerVisualizerRendererPolarityEvents(caerVisualizerPublicState state, caerEventPacketContainer container) {
	UNUSED_ARGUMENT(state);

	caerEventPacketHeader polarityPacketHeader
		= caerEventPacketContainerFindEventPacketByType(container, POLARITY_EVENT);

	// No packet of requested type or empty packet (no valid events).
	if (polarityPacketHeader == nullptr || caerEventPacketHeaderGetEventValid(polarityPacketHeader) == 0) {
		return (false);
	}

	bool doubleSpacedAddresses = sshsNodeGetBool(state->visualizerConfigNode, "DoubleSpacedAddresses");

	const libcaer::events::PolarityEventPacket polarityPacket(polarityPacketHeader, false);

	std::vector<sf::Vertex> vertices;
	vertices.reserve((size_t) polarityPacket.getEventValid() * 4);

	// Render all valid events.
	for (const auto &polarityEvent : polarityPacket) {
		if (!polarityEvent.isValid()) {
			continue; // Skip invalid events.
		}

		uint16_t x = polarityEvent.getX();
		uint16_t y = polarityEvent.getY();

		if (doubleSpacedAddresses) {
			x = U16T(x << 1);
			y = U16T(y << 1);
		}

		// ON polarity (green), OFF polarity (red).
		sfml::Helpers::addPixelVertices(vertices, x, y, state->renderZoomFactor.load(std::memory_order_relaxed),
			(polarityEvent.getPolarity()) ? (sf::Color::Green) : (sf::Color::Red));
	}

	state->renderWindow->draw(vertices.data(), vertices.size(), sf::Quads);

	return (true);
}

struct renderer_frame_events_state {
	sf::Sprite sprite;
	sf::Texture texture;
	std::vector<uint8_t> pixels;
};

typedef struct renderer_frame_events_state *rendererFrameEventsState;

static void *caerVisualizerRendererFrameEventsStateInit(caerVisualizerPublicState state) {
	// Add configuration for ROI region.
	sshsNodeCreate(state->visualizerConfigNode, "ROIRegion", 0, 0, 2, SSHS_FLAGS_NORMAL,
		"Selects which ROI region to display. 0 is the standard image, 1 is for debug (reset read), 2 is for debug "
		"(signal read).");

	// Allocate memory via C++ for renderer state, since we use C++ objects directly.
	rendererFrameEventsState renderState = new renderer_frame_events_state();

	// Create texture representing frame, set smoothing.
	renderState->texture.create(state->renderSizeX, state->renderSizeY);
	renderState->texture.setSmooth(false);

	// Assign texture to sprite.
	renderState->sprite.setTexture(renderState->texture);

	// 32-bit RGBA pixels (8-bit per channel), standard CG layout.
	renderState->pixels.reserve(state->renderSizeX * state->renderSizeY * 4);

	return (renderState);
}

static void caerVisualizerRendererFrameEventsStateExit(caerVisualizerPublicState state) {
	rendererFrameEventsState renderState = (rendererFrameEventsState) state->renderState;

	delete renderState;
}

static bool caerVisualizerRendererFrameEvents(caerVisualizerPublicState state, caerEventPacketContainer container) {
	rendererFrameEventsState renderState = (rendererFrameEventsState) state->renderState;

	caerEventPacketHeader framePacketHeader = caerEventPacketContainerFindEventPacketByType(container, FRAME_EVENT);

	// No packet of requested type or empty packet (no valid events).
	if ((framePacketHeader == nullptr) || (caerEventPacketHeaderGetEventValid(framePacketHeader) == 0)) {
		return (false);
	}

	int roiRegionSelect = sshsNodeGetInt(state->visualizerConfigNode, "ROIRegion");

	const libcaer::events::FrameEventPacket framePacket(framePacketHeader, false);

	// Only operate on the last, valid frame for the selected ROI region.
	const libcaer::events::FrameEvent *frame = {nullptr};

	for (const auto &f : framePacket) {
		if (f.isValid() && (f.getROIIdentifier() == roiRegionSelect)) {
			frame = &f;
		}
	}

	if (frame == nullptr) {
		return (false);
	}

	// 32-bit RGBA pixels (8-bit per channel), standard CG layout.
	switch (frame->getChannelNumber()) {
		case libcaer::events::FrameEvent::colorChannels::GRAYSCALE: {
			for (size_t srcIdx = 0, dstIdx = 0; srcIdx < frame->getPixelsMaxIndex();) {
				uint8_t greyValue             = U8T(frame->getPixelArrayUnsafe()[srcIdx++] >> 8);
				renderState->pixels[dstIdx++] = greyValue; // R
				renderState->pixels[dstIdx++] = greyValue; // G
				renderState->pixels[dstIdx++] = greyValue; // B
				renderState->pixels[dstIdx++] = UINT8_MAX; // A
			}
			break;
		}

		case libcaer::events::FrameEvent::colorChannels::RGB: {
			for (size_t srcIdx = 0, dstIdx = 0; srcIdx < frame->getPixelsMaxIndex();) {
				renderState->pixels[dstIdx++] = U8T(frame->getPixelArrayUnsafe()[srcIdx++] >> 8); // R
				renderState->pixels[dstIdx++] = U8T(frame->getPixelArrayUnsafe()[srcIdx++] >> 8); // G
				renderState->pixels[dstIdx++] = U8T(frame->getPixelArrayUnsafe()[srcIdx++] >> 8); // B
				renderState->pixels[dstIdx++] = UINT8_MAX;                                        // A
			}
			break;
		}

		case libcaer::events::FrameEvent::colorChannels::RGBA: {
			for (size_t srcIdx = 0, dstIdx = 0; srcIdx < frame->getPixelsMaxIndex();) {
				renderState->pixels[dstIdx++] = U8T(frame->getPixelArrayUnsafe()[srcIdx++] >> 8); // R
				renderState->pixels[dstIdx++] = U8T(frame->getPixelArrayUnsafe()[srcIdx++] >> 8); // G
				renderState->pixels[dstIdx++] = U8T(frame->getPixelArrayUnsafe()[srcIdx++] >> 8); // B
				renderState->pixels[dstIdx++] = U8T(frame->getPixelArrayUnsafe()[srcIdx++] >> 8); // A
			}
			break;
		}
	}

	renderState->texture.update(renderState->pixels.data(), U32T(frame->getLengthX()), U32T(frame->getLengthY()),
		U32T(frame->getPositionX()), U32T(frame->getPositionY()));

	renderState->sprite.setTextureRect(
		sf::IntRect(frame->getPositionX(), frame->getPositionY(), frame->getLengthX(), frame->getLengthY()));

	float zoomFactor = state->renderZoomFactor.load(std::memory_order_relaxed);

	renderState->sprite.setPosition(
		(float) frame->getPositionX() * zoomFactor, (float) frame->getPositionY() * zoomFactor);

	renderState->sprite.setScale(zoomFactor, zoomFactor);

	state->renderWindow->draw(renderState->sprite);

	return (true);
}

#define RESET_LIMIT_POS(VAL, LIMIT) \
	if ((VAL) > (LIMIT)) {          \
		(VAL) = (LIMIT);            \
	}
#define RESET_LIMIT_NEG(VAL, LIMIT) \
	if ((VAL) < (LIMIT)) {          \
		(VAL) = (LIMIT);            \
	}

static bool caerVisualizerRendererIMU6Events(caerVisualizerPublicState state, caerEventPacketContainer container) {
	caerEventPacketHeader imu6PacketHeader = caerEventPacketContainerFindEventPacketByType(container, IMU6_EVENT);

	if (imu6PacketHeader == nullptr || caerEventPacketHeaderGetEventValid(imu6PacketHeader) == 0) {
		return (false);
	}

	const libcaer::events::IMU6EventPacket imu6Packet(imu6PacketHeader, false);

	float zoomFactor = state->renderZoomFactor.load(std::memory_order_relaxed);

	float scaleFactorAccel = 30 * zoomFactor;
	float scaleFactorGyro  = 15 * zoomFactor;
	float lineThickness    = 4 * zoomFactor;
	float maxSizeX         = (float) state->renderSizeX * zoomFactor;
	float maxSizeY         = (float) state->renderSizeY * zoomFactor;

	sf::Color accelColor = sf::Color::Green;
	sf::Color gyroColor  = sf::Color::Magenta;

	float centerPointX = maxSizeX / 2;
	float centerPointY = maxSizeY / 2;

	float accelX = 0, accelY = 0, accelZ = 0;
	float gyroX = 0, gyroY = 0, gyroZ = 0;
	float temp = 0;

	// Iterate over valid IMU events and average them.
	// This somewhat smoothes out the rendering.
	for (const auto &imu6Event : imu6Packet) {
		accelX += imu6Event.getAccelX();
		accelY += imu6Event.getAccelY();
		accelZ += imu6Event.getAccelZ();

		gyroX += imu6Event.getGyroX();
		gyroY += imu6Event.getGyroY();
		gyroZ += imu6Event.getGyroZ();

		temp += imu6Event.getTemp();
	}

	// Normalize values.
	int32_t validEvents = imu6Packet.getEventValid();

	accelX /= (float) validEvents;
	accelY /= (float) validEvents;
	accelZ /= (float) validEvents;

	gyroX /= (float) validEvents;
	gyroY /= (float) validEvents;
	gyroZ /= (float) validEvents;

	temp /= (float) validEvents;

	// Acceleration X, Y as lines. Z as a circle.
	float accelXScaled = centerPointX - accelX * scaleFactorAccel;
	RESET_LIMIT_POS(accelXScaled, maxSizeX - 2 - lineThickness);
	RESET_LIMIT_NEG(accelXScaled, 1 + lineThickness);
	float accelYScaled = centerPointY - accelY * scaleFactorAccel;
	RESET_LIMIT_POS(accelYScaled, maxSizeY - 2 - lineThickness);
	RESET_LIMIT_NEG(accelYScaled, 1 + lineThickness);
	float accelZScaled = fabsf(accelZ * scaleFactorAccel);
	RESET_LIMIT_POS(accelZScaled, centerPointY - 2 - lineThickness); // Circle max.
	RESET_LIMIT_NEG(accelZScaled, 1);                                // Circle min.

	sfml::Line accelLine(
		sf::Vector2f(centerPointX, centerPointY), sf::Vector2f(accelXScaled, accelYScaled), lineThickness, accelColor);
	state->renderWindow->draw(accelLine);

	sf::CircleShape accelCircle(accelZScaled);
	sfml::Helpers::setOriginToCenter(accelCircle);
	accelCircle.setFillColor(sf::Color::Transparent);
	accelCircle.setOutlineColor(accelColor);
	accelCircle.setOutlineThickness(-lineThickness);
	accelCircle.setPosition(sf::Vector2f(centerPointX, centerPointY));

	state->renderWindow->draw(accelCircle);

	// Gyroscope pitch(X), yaw(Y), roll(Z) as lines.
	float gyroXScaled = centerPointY + gyroX * scaleFactorGyro;
	RESET_LIMIT_POS(gyroXScaled, maxSizeY - 2 - lineThickness);
	RESET_LIMIT_NEG(gyroXScaled, 1 + lineThickness);
	float gyroYScaled = centerPointX + gyroY * scaleFactorGyro;
	RESET_LIMIT_POS(gyroYScaled, maxSizeX - 2 - lineThickness);
	RESET_LIMIT_NEG(gyroYScaled, 1 + lineThickness);
	float gyroZScaled = centerPointX - gyroZ * scaleFactorGyro;
	RESET_LIMIT_POS(gyroZScaled, maxSizeX - 2 - lineThickness);
	RESET_LIMIT_NEG(gyroZScaled, 1 + lineThickness);

	sfml::Line gyroLine1(
		sf::Vector2f(centerPointX, centerPointY), sf::Vector2f(gyroYScaled, gyroXScaled), lineThickness, gyroColor);
	state->renderWindow->draw(gyroLine1);

	sfml::Line gyroLine2(sf::Vector2f(centerPointX, centerPointY - 20), sf::Vector2f(gyroZScaled, centerPointY - 20),
		lineThickness, gyroColor);
	state->renderWindow->draw(gyroLine2);

	// TODO: enhance IMU renderer with more text info.
	if (state->font != nullptr) {
		char valStr[128];

		// Acceleration X/Y.
		snprintf(valStr, 128, "%.2f,%.2f g", (double) accelX, (double) accelY);

		sf::Text accelText(valStr, *state->font, 30);
		sfml::Helpers::setTextColor(accelText, accelColor);
		accelText.setPosition(sf::Vector2f(accelXScaled, accelYScaled));

		state->renderWindow->draw(accelText);

		// Acceleration Z.
		snprintf(valStr, 128, "%.2f g", (double) accelZ);

		sf::Text accelTextZ(valStr, *state->font, 30);
		sfml::Helpers::setTextColor(accelTextZ, accelColor);
		accelTextZ.setPosition(sf::Vector2f(centerPointX, centerPointY + accelZScaled + lineThickness));

		state->renderWindow->draw(accelTextZ);

		// Temperature.
		snprintf(valStr, 128, "Temp: %.2f C", (double) temp);

		sf::Text tempText(valStr, *state->font, 30);
		sfml::Helpers::setTextColor(tempText, sf::Color::White);
		tempText.setPosition(sf::Vector2f(0, 0));

		state->renderWindow->draw(tempText);
	}

	return (true);
}

static bool caerVisualizerRendererPoint2DEvents(caerVisualizerPublicState state, caerEventPacketContainer container) {
	UNUSED_ARGUMENT(state);

	caerEventPacketHeader point2DPacketHeader = caerEventPacketContainerFindEventPacketByType(container, POINT2D_EVENT);

	if (point2DPacketHeader == nullptr || caerEventPacketHeaderGetEventValid(point2DPacketHeader) == 0) {
		return (false);
	}

	const libcaer::events::Point2DEventPacket point2DPacket(point2DPacketHeader, false);

	std::vector<sf::Vertex> vertices;
	vertices.reserve((size_t) point2DPacket.getEventValid() * 4);

	// Render all valid events.
	for (const auto &point2DEvent : point2DPacket) {
		if (!point2DEvent.isValid()) {
			continue; // Skip invalid events.
		}

		// Render points in color blue.
		sfml::Helpers::addPixelVertices(vertices, point2DEvent.getX(), point2DEvent.getY(),
			state->renderZoomFactor.load(std::memory_order_relaxed), sf::Color::Blue);
	}

	state->renderWindow->draw(vertices.data(), vertices.size(), sf::Quads);

	return (true);
}

static inline sf::Color dynapseCoreIdToColor(uint8_t coreId) {
	if (coreId == 3) {
		return (sf::Color::Yellow);
	}
	else if (coreId == 2) {
		return (sf::Color::Red);
	}
	else if (coreId == 1) {
		return (sf::Color::Magenta);
	}

	// Core ID 0 has default.
	return (sf::Color::Green);
}

static bool caerVisualizerRendererSpikeEvents(caerVisualizerPublicState state, caerEventPacketContainer container) {
	UNUSED_ARGUMENT(state);

	caerEventPacketHeader spikePacketHeader = caerEventPacketContainerFindEventPacketByType(container, SPIKE_EVENT);

	if (spikePacketHeader == nullptr || caerEventPacketHeaderGetEventValid(spikePacketHeader) == 0) {
		return (false);
	}

	const libcaer::events::SpikeEventPacket spikePacket(spikePacketHeader, false);

	std::vector<sf::Vertex> vertices;
	vertices.reserve((size_t) spikePacket.getEventValid() * 4);

	// Render all valid events.
	for (const auto &spikeEvent : spikePacket) {
		if (!spikeEvent.isValid()) {
			continue; // Skip invalid events.
		}

		// Render spikes with different colors based on core ID.
		uint8_t coreId = spikeEvent.getSourceCoreID();
		sfml::Helpers::addPixelVertices(vertices, libcaer::devices::dynapse::spikeEventGetX(spikeEvent),
			libcaer::devices::dynapse::spikeEventGetY(spikeEvent),
			state->renderZoomFactor.load(std::memory_order_relaxed), dynapseCoreIdToColor(coreId));
	}

	state->renderWindow->draw(vertices.data(), vertices.size(), sf::Quads);

	return (true);
}

// Matrix4x4
// How many ts and points will be drawn
// what is the size of the plot (can be zoomed)
#define WORLD_X 640
#define WORLD_Y 480
#define TIMETOT 3000000
#define NUMPACKETS 30000

struct caer_visualizer_pose_matrix {
	caerMatrix4x4EventPacket mem;
	int currentCounter;
	int worldXPosition;
	int32_t firstTs;
};

typedef struct caer_visualizer_pose_matrix *caerVisualizerPoseMatrix;

static void *caerVisualizerRendererMatrix4x4EventsPoseStateInit(caerVisualizerPublicState state) {
	// Reset render size to allow for more neurons and timesteps to be displayed.
	caerVisualizerResetRenderSize(state, WORLD_X, WORLD_Y);

	caerVisualizerPoseMatrix mem = new caer_visualizer_pose_matrix();

	return (mem); // No allocated memory.
}

static void caerVisualizerRendererMatrix4x4EventsPoseStateExit(caerVisualizerPublicState state) {
	caerVisualizerPoseMatrix mem = (caerVisualizerPoseMatrix) state->renderState;

	delete mem;
}

static bool caerVisualizerRendererMatrix4x4EventsPose(
	caerVisualizerPublicState state, caerEventPacketContainer container) {
	UNUSED_ARGUMENT(state);

	caerMatrix4x4EventPacket pkg
		= (caerMatrix4x4EventPacket) caerEventPacketContainerFindEventPacketByType(container, MATRIX4x4_EVENT);
	caerEventPacketHeader matrix4x4PacketHeader = &pkg->packetHeader;

	if (matrix4x4PacketHeader == nullptr || caerEventPacketHeaderGetEventValid(matrix4x4PacketHeader) == 0) {
		return (false);
	}

	caerVisualizerPoseMatrix memInt = (caerVisualizerPoseMatrix) state->renderState;
	if (memInt->mem == nullptr) {
		memInt->mem = (caerMatrix4x4EventPacket) caerMatrix4x4EventPacketAllocate(
			NUMPACKETS, 0, caerEventPacketHeaderGetEventTSOverflow(&pkg->packetHeader));
		memInt->firstTs = INT32_MAX; // max it will be fixed to its real value at first step
	}

	const libcaer::events::Matrix4x4EventPacket matrix4x4Packet(matrix4x4PacketHeader, false);

	// find max and min TS, event packets MUST be ordered by time, that's
	// an invariant property, so we can just select first and last event.
	// Also time is always positive, so we can use unsigned ints.
	int32_t minTimestamp = matrix4x4Packet[0].getTimestamp();
	if (minTimestamp < memInt->firstTs) {
		memInt->firstTs = minTimestamp;
	}
	int32_t maxTimestamp = matrix4x4Packet[-1].getTimestamp();

	// time span, +1 to divide space correctly in scaleX.
	int32_t timeSpan = maxTimestamp - memInt->firstTs + 1;
	if (timeSpan >= TIMETOT) {
		memInt->firstTs = maxTimestamp;
		// invalidate events to clear plot
		for (int32_t pn = 0; pn < NUMPACKETS; pn++) {
			caerMatrix4x4Event thisEvent = caerMatrix4x4EventPacketGetEvent(memInt->mem, pn);
			if (caerMatrix4x4EventIsValid(thisEvent)) {
				caerMatrix4x4EventInvalidate(thisEvent, memInt->mem);
			}
		}
	}

	// Get render sizes, subtract 2px for middle borders.
	float zoomFactor = state->renderZoomFactor.load(std::memory_order_relaxed);

	float sizeX = (float) (state->renderSizeX - 2) * zoomFactor;
	float sizeY = (float) (state->renderSizeY - 2) * zoomFactor;

	// Two plots in each of X and Y directions.
	float scaleX = (sizeX) / (float) TIMETOT;
	float scaleY = (sizeY / 2.0f) / (float) WORLD_Y;

	for (const auto &matrixEvent : matrix4x4Packet) {
		// only show valid events
		if (!matrixEvent.isValid()) {
			continue;
		}

		int32_t ts = matrixEvent.getTimestamp();
		ts         = ts - memInt->firstTs;

		// X is based on time.
		int32_t plotX = (int) floorf((float) ts * scaleX);

		float m00 = matrixEvent.getM00();
		float m01 = matrixEvent.getM01();
		float m02 = matrixEvent.getM02();
		float m03 = matrixEvent.getM03();

		float m10 = matrixEvent.getM10();
		float m11 = matrixEvent.getM11();
		float m12 = matrixEvent.getM12();
		float m13 = matrixEvent.getM13();

		float m20 = matrixEvent.getM20();
		float m21 = matrixEvent.getM21();
		float m22 = matrixEvent.getM22();
		float m23 = matrixEvent.getM23();

		float m30 = matrixEvent.getM30();
		float m31 = matrixEvent.getM31();
		float m32 = matrixEvent.getM32();
		float m33 = matrixEvent.getM33();

		// what x position to update
		memInt->currentCounter += 1;
		if (memInt->currentCounter == (int) sizeX) {
			memInt->currentCounter = 0;
		}

		// x position
		if (memInt->worldXPosition == NUMPACKETS) {
			memInt->worldXPosition = 0;
		}

		caerMatrix4x4Event thisEvent = caerMatrix4x4EventPacketGetEvent(memInt->mem, memInt->worldXPosition);
		caerMatrix4x4EventSetM00(thisEvent, m00);
		caerMatrix4x4EventSetM01(thisEvent, m01);
		caerMatrix4x4EventSetM02(thisEvent, m02);
		caerMatrix4x4EventSetM03(thisEvent, m03);
		caerMatrix4x4EventSetM10(thisEvent, m10);
		caerMatrix4x4EventSetM11(thisEvent, m11);
		caerMatrix4x4EventSetM12(thisEvent, m12);
		caerMatrix4x4EventSetM13(thisEvent, m13);
		caerMatrix4x4EventSetM20(thisEvent, m20);
		caerMatrix4x4EventSetM21(thisEvent, m21);
		caerMatrix4x4EventSetM22(thisEvent, m22);
		caerMatrix4x4EventSetM23(thisEvent, m23);
		caerMatrix4x4EventSetM30(thisEvent, m30);
		caerMatrix4x4EventSetM31(thisEvent, m31);
		caerMatrix4x4EventSetM32(thisEvent, m32);
		caerMatrix4x4EventSetM33(thisEvent, m33);

		// set timestamp accordingly
		caerMatrix4x4EventSetTimestamp(thisEvent, plotX);

		// validate event
		caerMatrix4x4EventValidate(thisEvent, memInt->mem);

		// increment counter
		memInt->worldXPosition += 1;
	}

	// init vertices
	std::vector<sf::Vertex> vertices;
	vertices.reserve((size_t) matrix4x4Packet.getEventNumber() * 4);

	// draw all points
	for (int i = 0; i < memInt->worldXPosition; i++) {
		caerMatrix4x4Event thisEvent = caerMatrix4x4EventPacketGetEvent(memInt->mem, i);
		if (caerMatrix4x4EventIsValid(thisEvent)) {
			float mm00 = caerMatrix4x4EventGetM00(thisEvent);
			float mm01 = caerMatrix4x4EventGetM01(thisEvent);
			float mm02 = caerMatrix4x4EventGetM02(thisEvent);
			float mm03 = caerMatrix4x4EventGetM03(thisEvent);

			float mm10 = caerMatrix4x4EventGetM10(thisEvent);
			float mm11 = caerMatrix4x4EventGetM11(thisEvent);
			float mm12 = caerMatrix4x4EventGetM12(thisEvent);
			float mm13 = caerMatrix4x4EventGetM13(thisEvent);

			float mm20 = caerMatrix4x4EventGetM20(thisEvent);
			float mm21 = caerMatrix4x4EventGetM21(thisEvent);
			float mm22 = caerMatrix4x4EventGetM22(thisEvent);
			float mm23 = caerMatrix4x4EventGetM23(thisEvent);

			float mm30 = caerMatrix4x4EventGetM30(thisEvent);
			float mm31 = caerMatrix4x4EventGetM31(thisEvent);
			float mm32 = caerMatrix4x4EventGetM32(thisEvent);
			float mm33 = caerMatrix4x4EventGetM33(thisEvent);

			// set timestamp, i.e. position on X
			float plotX = caerMatrix4x4EventGetTimestamp(thisEvent);

			if (mm00 >= 0) {
				sfml::Helpers::addPixelVertices(vertices, plotX, ((mm00) * (WORLD_Y) + (WORLD_Y / 2)) * scaleY,
					zoomFactor, sf::Color::White, false);
			}
			else {
				sfml::Helpers::addPixelVertices(vertices, plotX, ((mm00 * (-1)) * (WORLD_Y) - (WORLD_Y / 2)) * scaleY,
					zoomFactor, sf::Color::White, false);
			}
			if (mm01 >= 0) {
				sfml::Helpers::addPixelVertices(vertices, plotX, ((mm01) * (WORLD_Y) + (WORLD_Y / 2)) * scaleY,
					zoomFactor, sf::Color::White, false);
			}
			else {
				sfml::Helpers::addPixelVertices(vertices, plotX, ((mm01 * (-1)) * (WORLD_Y) - (WORLD_Y / 2)) * scaleY,
					zoomFactor, sf::Color::White, false);
			}
			if (mm02 >= 0) {
				sfml::Helpers::addPixelVertices(vertices, plotX, ((mm02) * (WORLD_Y) + (WORLD_Y / 2)) * scaleY,
					zoomFactor, sf::Color::White, false);
			}
			else {
				sfml::Helpers::addPixelVertices(vertices, plotX, ((mm02 * (-1)) * (WORLD_Y) - (WORLD_Y / 2)) * scaleY,
					zoomFactor, sf::Color::White, false);
			}
			if (mm03 >= 0) {
				sfml::Helpers::addPixelVertices(vertices, plotX, ((mm03) * (WORLD_Y) + (WORLD_Y / 2)) * scaleY,
					zoomFactor, sf::Color::White, false);
			}
			else {
				sfml::Helpers::addPixelVertices(vertices, plotX, ((mm03 * (-1)) * (WORLD_Y) - (WORLD_Y / 2)) * scaleY,
					zoomFactor, sf::Color::White, false);
			}

			if (mm10 >= 0) {
				sfml::Helpers::addPixelVertices(
					vertices, plotX, ((mm10) * (WORLD_Y) + (WORLD_Y / 2)) * scaleY, zoomFactor, sf::Color::Blue, false);
			}
			else {
				sfml::Helpers::addPixelVertices(vertices, plotX, ((mm10 * (-1)) * (WORLD_Y) - (WORLD_Y / 2)) * scaleY,
					zoomFactor, sf::Color::Blue, false);
			}
			if (mm11 >= 0) {
				sfml::Helpers::addPixelVertices(
					vertices, plotX, ((mm11) * (WORLD_Y) + (WORLD_Y / 2)) * scaleY, zoomFactor, sf::Color::Blue, false);
			}
			else {
				sfml::Helpers::addPixelVertices(vertices, plotX, ((mm11 * (-1)) * (WORLD_Y) - (WORLD_Y / 2)) * scaleY,
					zoomFactor, sf::Color::Blue, false);
			}
			if (mm12 >= 0) {
				sfml::Helpers::addPixelVertices(
					vertices, plotX, ((mm12) * (WORLD_Y) + (WORLD_Y / 2)) * scaleY, zoomFactor, sf::Color::Blue, false);
			}
			else {
				sfml::Helpers::addPixelVertices(vertices, plotX, ((mm12 * (-1)) * (WORLD_Y) - (WORLD_Y / 2)) * scaleY,
					zoomFactor, sf::Color::Blue, false);
			}
			if (mm13 >= 0) {
				sfml::Helpers::addPixelVertices(
					vertices, plotX, ((mm13) * (WORLD_Y) + (WORLD_Y / 2)) * scaleY, zoomFactor, sf::Color::Blue, false);
			}
			else {
				sfml::Helpers::addPixelVertices(vertices, plotX, ((mm13 * (-1)) * (WORLD_Y) - (WORLD_Y / 2)) * scaleY,
					zoomFactor, sf::Color::Blue, false);
			}

			if (mm20 >= 0) {
				sfml::Helpers::addPixelVertices(
					vertices, plotX, ((mm20) * (WORLD_Y) + (WORLD_Y / 2)) * scaleY, zoomFactor, sf::Color::Red, false);
			}
			else {
				sfml::Helpers::addPixelVertices(vertices, plotX, ((mm20 * (-1)) * (WORLD_Y) - (WORLD_Y / 2)) * scaleY,
					zoomFactor, sf::Color::Red, false);
			}
			if (mm21 >= 0) {
				sfml::Helpers::addPixelVertices(
					vertices, plotX, ((mm21) * (WORLD_Y) + (WORLD_Y / 2)) * scaleY, zoomFactor, sf::Color::Red, false);
			}
			else {
				sfml::Helpers::addPixelVertices(vertices, plotX, ((mm21 * (-1)) * (WORLD_Y) - (WORLD_Y / 2)) * scaleY,
					zoomFactor, sf::Color::Red, false);
			}
			if (mm22 >= 0) {
				sfml::Helpers::addPixelVertices(
					vertices, plotX, ((mm22) * (WORLD_Y) + (WORLD_Y / 2)) * scaleY, zoomFactor, sf::Color::Red, false);
			}
			else {
				sfml::Helpers::addPixelVertices(vertices, plotX, ((mm22 * (-1)) * (WORLD_Y) - (WORLD_Y / 2)) * scaleY,
					zoomFactor, sf::Color::Red, false);
			}
			if (mm23 >= 0) {
				sfml::Helpers::addPixelVertices(
					vertices, plotX, ((mm23) * (WORLD_Y) + (WORLD_Y / 2)) * scaleY, zoomFactor, sf::Color::Red, false);
			}
			else {
				sfml::Helpers::addPixelVertices(vertices, plotX, ((mm23 * (-1)) * (WORLD_Y) - (WORLD_Y / 2)) * scaleY,
					zoomFactor, sf::Color::Red, false);
			}

			/*if(m30 >= 0){
			 sfml::Helpers::addPixelVertices(vertices, plotX, ((mm30)*(WORLD_Y)+(WORLD_Y/2))*scaleY, zoomFactor,
			 sf::Color::Yellow, false);
			 }else{
			 sfml::Helpers::addPixelVertices(vertices, plotX, ((mm30*(-1))*(WORLD_Y)-(WORLD_Y/2))*scaleY, zoomFactor,
			 sf::Color::Yellow, false);
			 }
			 if(m31 >= 0){
			 sfml::Helpers::addPixelVertices(vertices, plotX, ((mm31)*(WORLD_Y)+(WORLD_Y/2))*scaleY, zoomFactor,
			 sf::Color::Yellow, false);
			 }else{
			 sfml::Helpers::addPixelVertices(vertices, plotX, ((mm31*(-1))*(WORLD_Y)-(WORLD_Y/2))*scaleY, zoomFactor,
			 sf::Color::Yellow, false);
			 }
			 if(m32 >= 0){
			 sfml::Helpers::addPixelVertices(vertices, plotX, ((mm32)*(WORLD_Y)+(WORLD_Y/2))*scaleY, zoomFactor,
			 sf::Color::Yellow, false);
			 }else{
			 sfml::Helpers::addPixelVertices(vertices, plotX, ((mm32*(-1))*(WORLD_Y)-(WORLD_Y/2))*scaleY, zoomFactor,
			 sf::Color::Yellow, false);
			 }
			 if(m33 >= 0){
			 sfml::Helpers::addPixelVertices(vertices, plotX, ((mm33)*(WORLD_Y)+(WORLD_Y/2))*scaleY, zoomFactor,
			 sf::Color::Yellow, false);
			 }else{
			 sfml::Helpers::addPixelVertices(vertices, plotX, ((mm33*(-1))*(WORLD_Y)-(WORLD_Y/2))*scaleY, zoomFactor,
			 sf::Color::Yellow, false);
			 }*/
		} // only valid events
	}
	state->renderWindow->draw(vertices.data(), vertices.size(), sf::Quads);

	return (true);
}

// How many timestemps and neurons to show per chip.
#define SPIKE_RASTER_PLOT_TIMESTEPS 500
#define SPIKE_RASTER_PLOT_NEURONS 256

static void *caerVisualizerRendererSpikeEventsRasterStateInit(caerVisualizerPublicState state) {
	// Reset render size to allow for more neurons and timesteps to be displayed.
	// This results in less scaling on the X and Y axes.
	// Also add 2 pixels on X/Y to compensate for the middle separation bars.
	caerVisualizerResetRenderSize(state, (SPIKE_RASTER_PLOT_TIMESTEPS * 2) + 2, (SPIKE_RASTER_PLOT_NEURONS * 2) + 2);

	return (CAER_VISUALIZER_RENDER_INIT_NO_MEM); // No allocated memory.
}

static bool caerVisualizerRendererSpikeEventsRaster(
	caerVisualizerPublicState state, caerEventPacketContainer container) {
	UNUSED_ARGUMENT(state);

	caerEventPacketHeader spikePacketHeader = caerEventPacketContainerFindEventPacketByType(container, SPIKE_EVENT);

	if (spikePacketHeader == nullptr || caerEventPacketHeaderGetEventValid(spikePacketHeader) == 0) {
		return (false);
	}

	const libcaer::events::SpikeEventPacket spikePacket(spikePacketHeader, false);

	// find max and min TS, event packets MUST be ordered by time, that's
	// an invariant property, so we can just select first and last event.
	// Also time is always positive, so we can use unsigned ints.
	uint32_t minTimestamp = U32T(spikePacket[0].getTimestamp());
	uint32_t maxTimestamp = U32T(spikePacket[-1].getTimestamp());

	// time span, +1 to divide space correctly in scaleX.
	uint32_t timeSpan = maxTimestamp - minTimestamp + 1;

	// Get render sizes, subtract 2px for middle borders.
	float zoomFactor = state->renderZoomFactor.load(std::memory_order_relaxed);

	float sizeX = (float) (state->renderSizeX - 2) * zoomFactor;
	float sizeY = (float) (state->renderSizeY - 2) * zoomFactor;

	// Two plots in each of X and Y directions.
	float scaleX = (sizeX / 2.0f) / (float) timeSpan;
	float scaleY = (sizeY / 2.0f) / (float) DYNAPSE_CONFIG_NUMNEURONS;

	std::vector<sf::Vertex> vertices;
	vertices.reserve((size_t) spikePacket.getEventNumber() * 4);

	// Render all spikes.
	for (const auto &spikeEvent : spikePacket) {
		uint32_t ts = U32T(spikeEvent.getTimestamp());
		ts          = ts - minTimestamp;

		// X is based on time.
		uint32_t plotX = U32T(floorf((float) ts * scaleX));

		uint8_t coreId = spikeEvent.getSourceCoreID();

		uint32_t linearIndex = spikeEvent.getNeuronID();
		linearIndex += (coreId * DYNAPSE_CONFIG_NUMNEURONS_CORE);

		// Y is based on all neurons.
		uint32_t plotY = U32T(floorf((float) linearIndex * scaleY));

		// Move plot X/Y based on chip ID, to get four quadrants with four chips.
		uint8_t chipId = spikeEvent.getChipID();

		if (chipId == DYNAPSE_CONFIG_DYNAPSE_U3) {
			plotX += (sizeX / 2) + 2; // +2 for middle border!
			plotY += (sizeY / 2) + 2; // +2 for middle border!
		}
		else if (chipId == DYNAPSE_CONFIG_DYNAPSE_U2) {
			plotY += (sizeY / 2) + 2; // +2 for middle border!
		}
		else if (chipId == DYNAPSE_CONFIG_DYNAPSE_U1) {
			plotX += (sizeX / 2) + 2; // +2 for middle border!
		}
		// DYNAPSE_CONFIG_DYNAPSE_U0 no changes.

		// Draw pixels of raster plot (some neurons might be merged due to aliasing).
		sfml::Helpers::addPixelVertices(vertices, plotX, plotY, zoomFactor, dynapseCoreIdToColor(coreId), false);
	}

	state->renderWindow->draw(vertices.data(), vertices.size(), sf::Quads);

	// Draw middle borders, only once!
	sfml::Line horizontalBorderLine(sf::Vector2f(0, (state->renderSizeY * zoomFactor) / 2),
		sf::Vector2f((state->renderSizeX * zoomFactor), (state->renderSizeY * zoomFactor) / 2), 2 * zoomFactor,
		sf::Color::White);
	state->renderWindow->draw(horizontalBorderLine);

	sfml::Line verticalBorderLine(sf::Vector2f((state->renderSizeX * zoomFactor) / 2, 0),
		sf::Vector2f((state->renderSizeX * zoomFactor) / 2, (state->renderSizeY * zoomFactor)), 2 * zoomFactor,
		sf::Color::White);
	state->renderWindow->draw(verticalBorderLine);

	return (true);
}

static void *caerVisualizerRendererPolarityAndFrameEventsStateInit(caerVisualizerPublicState state) {
	caerVisualizerRendererPolarityEventsStateInit(state);
	return (caerVisualizerRendererFrameEventsStateInit(state));
}

static void caerVisualizerRendererPolarityAndFrameEventsStateExit(caerVisualizerPublicState state) {
	caerVisualizerRendererFrameEventsStateExit(state);
}

static bool caerVisualizerRendererPolarityAndFrameEvents(
	caerVisualizerPublicState state, caerEventPacketContainer container) {
	bool drewFrameEvents = caerVisualizerRendererFrameEvents(state, container);

	bool drewPolarityEvents = caerVisualizerRendererPolarityEvents(state, container);

	return (drewFrameEvents || drewPolarityEvents);
}

static void *caerVisualizerRendererPolarityAnd2DEventsInit(caerVisualizerPublicState state) {
	return (caerVisualizerRendererPolarityEventsStateInit(state));
}

static bool caerVisualizerRendererPolarityAnd2DEvents(
	caerVisualizerPublicState state, caerEventPacketContainer container) {
	bool drewPolarityEvents = caerVisualizerRendererPolarityEvents(state, container);

	bool drew2DEvents = caerVisualizerRendererPoint2DEvents(state, container);

	return (drewPolarityEvents || drew2DEvents);
}
