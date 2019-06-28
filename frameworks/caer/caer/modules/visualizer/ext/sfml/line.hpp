#ifndef EXT_SFML_LINE_HPP_
#define EXT_SFML_LINE_HPP_

#include <SFML/Graphics.hpp>
#include <cmath>

namespace sfml {
class Line : public sf::Drawable {
private:
	sf::Vertex vertices[4];
	float thickness;
	sf::Color color;

public:
	Line(const sf::Vector2f &point1, const sf::Vector2f &point2, float t = 5.0f, const sf::Color &c = sf::Color::White)
		: thickness(t), color(c) {
		sf::Vector2f direction     = point2 - point1;
		sf::Vector2f unitDirection = direction / std::sqrt(direction.x * direction.x + direction.y * direction.y);
		sf::Vector2f unitPerpendicular(-unitDirection.y, unitDirection.x);

		sf::Vector2f offset = (thickness / 2.0f) * unitPerpendicular;

		vertices[0].position = point1 + offset;
		vertices[1].position = point2 + offset;
		vertices[2].position = point2 - offset;
		vertices[3].position = point1 - offset;

		for (size_t i = 0; i < 4; i++) {
			vertices[i].color = color;
		}
	}

	void draw(sf::RenderTarget &target, sf::RenderStates states) const {
		target.draw(vertices, 4, sf::Quads);
	}
};
}

#endif /* EXT_SFML_LINE_HPP_ */
