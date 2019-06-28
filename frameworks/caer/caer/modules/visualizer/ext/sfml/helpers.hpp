#ifndef EXT_SFML_HELPERS_HPP_
#define EXT_SFML_HELPERS_HPP_

#include <SFML/Graphics.hpp>

namespace sfml {
class Helpers {
public:
	static void setOriginToCenter(sf::Shape &shape) {
		shape.setOrigin(sf::Vector2f(shape.getLocalBounds().width / 2.0f, shape.getLocalBounds().height / 2.0f));
	}

	static void addPixelVertices(std::vector<sf::Vertex> &vec, float x, float y, float zoomFactor,
		const sf::Color &color = sf::Color::White, bool scaleInitialPosition = true) {
		sf::Vector2f pos(x, y);

		if (scaleInitialPosition) {
			pos.x *= zoomFactor;
			pos.y *= zoomFactor;
		}

		sf::Vertex vtx(pos);

		vtx.color = color;

		// Quads need four vertices. Color stays the same.
		// Position changes by one in the clockwise sense.
		vec.push_back(vtx);
		vtx.position.x += zoomFactor;
		vec.push_back(vtx);
		vtx.position.y += zoomFactor;
		vec.push_back(vtx);
		vtx.position.x -= zoomFactor;
		vec.push_back(vtx);
	}

	static void setTextColor(sf::Text &text, const sf::Color &color) {
#if SFML_VERSION_MAJOR == 2 && SFML_VERSION_MINOR == 3
		text.setColor(color);
#else
		text.setFillColor(color);
#endif
	}
};
}

#endif /* EXT_SFML_HELPERS_HPP_ */
