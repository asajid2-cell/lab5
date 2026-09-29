#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>  // std::function, we were getting this through SFML's headers by accident before
#include <iostream>
#include <memory>
#include <random>
#include <vector>

#include <SFML/Graphics.hpp>

const int WINDOW_WIDTH = 800;
const int WINDOW_HEIGHT = 800;
const int FPS_LIMIT = 30;

// How many frames one trip from the left edge to the right edge takes; at the 30 fps limit above that is a two second pass
const int FRAMES_PER_ANIMATION = 60;

// Which frame of the trip we are on. It wraps back round to 0, and that wrap is what makes the circle repeat the trip instead of stopping at the right edge
int frame = 0;

// How big the circle is, and how far down it rides; about a third of the way down the window
const float CIRCLE_RADIUS = 20.0f;
const float CIRCLE_Y = WINDOW_HEIGHT / 3.0f;

// Where we draw the graph. The unit square, both axes 0 to 1, gets stretched over this box; the margins are what keep the axes off the edge of the window
const float PLOT_LEFT = 0.15f * WINDOW_WIDTH;
const float PLOT_BOTTOM = 0.90f * WINDOW_HEIGHT;
const float PLOT_WIDTH = 0.70f * WINDOW_WIDTH;
const float PLOT_HEIGHT = 0.30f * WINDOW_HEIGHT;

// How many straight segments the curve is made of. We sample the tween once per segment, so more samples is a smoother line for more vertices
const int CURVE_SAMPLES = 100;

// Radius of the small dot that shows where along the curve we currently are
const float MARKER_RADIUS = 6.0f;

const float PI = 3.14159265f;

// Blends between a and b by the fraction t; 0 gives us a, 1 gives us b
float Lerp(float a, float b, float t) {
    return a + (b - a) * t;
}

// All of the easing functions below take the plain fraction t, 0 to 1, and hand back the eased fraction, also 0 to 1.
// They are what decides how the trip from a to b is timed; where it is slow and where it is fast. We keep them to just t
// so the same function can be used for the circle and for the graph.

float EaseLinear(float t) {
    // Constant speed, no easing at all
    return t;
}

float EaseInQuad(float t) {
    // Starts slow and picks up
    return t * t;
}

float EaseOutQuad(float t) {
    // Starts fast and coasts to a stop
    float u = 1.0f - t;
    return 1.0f - u * u;
}

float EaseInOutQuad(float t) {
    // Slow at both ends, fastest in the middle; the first half eases in and the second half is the mirror of it easing out
    if (t < 0.5f) {
        return 2.0f * t * t;
    }
    float u = 1.0f - t;
    return 1.0f - 2.0f * u * u;
}

float EaseInCubic(float t) {
    // Same as InQuad but a harder start
    return t * t * t;
}

float EaseOutCubic(float t) {
    float u = 1.0f - t;
    return 1.0f - u * u * u;
}

float EaseInOutCubic(float t) {
    if (t < 0.5f) {
        return 4.0f * t * t * t;
    }
    float u = 1.0f - t;
    return 1.0f - 4.0f * u * u * u;
}

float EaseInOutSine(float t) {
    // A gentle S curve, barely looks eased at all
    return -(std::cos(PI * t) - 1.0f) / 2.0f;
}

float EaseInOutBack(float t) {
    // These two are the constants easings.net uses. This one overshoots past b and then comes back, so the circle
    // briefly ends up further right than the end of its trip and the graph pokes above the box. Potential thing to
    // revisit later if it looks wrong on screen
    const float c1 = 1.70158f;
    const float c2 = c1 * 1.525f;

    float u = 2.0f * t;
    if (t < 0.5f) {
        return (u * u * ((c2 + 1.0f) * u - c2)) / 2.0f;
    }
    float v = u - 2.0f;
    return (v * v * ((c2 + 1.0f) * v + c2) + 2.0f) / 2.0f;
}

// global tween function
// A tween maps the fraction t onto the trip from a to b, so what comes back is a VALUE between the two of them, not a
// 0 to 1 fraction. That is what lets one function do both jobs for us; for the circle a and b are x positions on the
// screen, and for the graph they are 0 and 1 so the tween plots as the easing curve itself.
// The default is plain linear, and key 1 hands us the same function, so the curve we start on cannot disagree with
// what that key gives us
std::function<float(float, float, float)> tween = [](float a, float b, float t) {
    return Lerp(a, b, EaseLinear(t));
};

// Maps a point of the unit square onto the plot box. Screen y grows downwards and the tween's output grows upwards, so
// y has to be measured up from the bottom instead
sf::Vector2f PlotPoint(float x, float y) {
    return {PLOT_LEFT + x * PLOT_WIDTH, PLOT_BOTTOM - y * PLOT_HEIGHT};
}

void handleInput(sf::Window& window, bool& shouldQuit) {
    while (const std::optional<sf::Event> event = window.pollEvent()) {
        if (event->is<sf::Event::Closed>()) {
            window.close();
            shouldQuit = true;
        }

        // Each digit key swaps in a different easing. A key press comes back as a pointer that is only non null when
        // the event really is a key press, so that is our check for one. The lambdas capture nothing because an
        // easing holds no state; it is just a function from t to an eased fraction, so there is nothing to capture
        if (const auto* keyPressed = event->getIf<sf::Event::KeyPressed>()) {
            switch (keyPressed->code) {
                case sf::Keyboard::Key::Num1:
                    tween = [](float a, float b, float t) { return Lerp(a, b, EaseLinear(t)); };
                    break;
                case sf::Keyboard::Key::Num2:
                    tween = [](float a, float b, float t) { return Lerp(a, b, EaseInQuad(t)); };
                    break;
                case sf::Keyboard::Key::Num3:
                    tween = [](float a, float b, float t) { return Lerp(a, b, EaseOutQuad(t)); };
                    break;
                case sf::Keyboard::Key::Num4:
                    tween = [](float a, float b, float t) { return Lerp(a, b, EaseInOutQuad(t)); };
                    break;
                case sf::Keyboard::Key::Num5:
                    tween = [](float a, float b, float t) { return Lerp(a, b, EaseInCubic(t)); };
                    break;
                case sf::Keyboard::Key::Num6:
                    tween = [](float a, float b, float t) { return Lerp(a, b, EaseOutCubic(t)); };
                    break;
                case sf::Keyboard::Key::Num7:
                    tween = [](float a, float b, float t) { return Lerp(a, b, EaseInOutCubic(t)); };
                    break;
                case sf::Keyboard::Key::Num8:
                    tween = [](float a, float b, float t) { return Lerp(a, b, EaseInOutSine(t)); };
                    break;
                case sf::Keyboard::Key::Num9:
                    tween = [](float a, float b, float t) { return Lerp(a, b, EaseInOutBack(t)); };
                    break;
                default:
                    break;  // most keys we don't care about
            }
        }
    }
}

void render(sf::RenderWindow& window) {
    // Clear with blue background (sky)
    window.clear(sf::Color::Black);

    // How far through the trip we are, 0 at the left end and just under 1 at the right. The circle and the dot on the
    // graph both read this one value, so they can never disagree with each other about where the animation is
    const float t = frame / static_cast<float>(FRAMES_PER_ANIMATION);

    // ====== ====== ======
    // Draw circle that moves between
    // the left/right of the screen.
    // Movement should be governed by the tween function.
    // ====== ====== ======
    sf::CircleShape circle(CIRCLE_RADIUS);
    circle.setFillColor(sf::Color::Cyan);
    // A circle is placed by the corner of its box, so we move the origin into the middle and then the position below
    // is the centre of the circle rather than its top left
    circle.setOrigin({CIRCLE_RADIUS, CIRCLE_RADIUS});
    // The tween picks the x; the endpoints stop a radius short of each edge so the whole circle stays on screen at
    // both ends of the trip
    circle.setPosition({tween(CIRCLE_RADIUS, WINDOW_WIDTH - CIRCLE_RADIUS, t), CIRCLE_Y});
    window.draw(circle);

    // ====== ====== ======
    // Draw tween function graph with a dot
    // on the current portion of the curve
    // ====== ====== ======
    // Sampling the tween with a of 0 and b of 1 leaves the easing on its own, so this ends up being a picture of the
    // curve the movement above is following
    sf::VertexArray curve(sf::PrimitiveType::LineStrip);
    for (int i = 0; i <= CURVE_SAMPLES; ++i) {
        const float x = i / static_cast<float>(CURVE_SAMPLES);
        curve.append(sf::Vertex{PlotPoint(x, tween(0.0f, 1.0f, x)), sf::Color::Cyan});
    }
    window.draw(curve);

    // The axes are two separate lines, one along the bottom and one up the left side. We use Lines and not LineStrip
    // here because a strip would join the four points in order and draw a diagonal back from (1, 0) to (0, 1)
    sf::VertexArray axes(sf::PrimitiveType::Lines);
    axes.append(sf::Vertex{PlotPoint(0.0f, 0.0f), sf::Color::White});
    axes.append(sf::Vertex{PlotPoint(1.0f, 0.0f), sf::Color::White});
    axes.append(sf::Vertex{PlotPoint(0.0f, 0.0f), sf::Color::White});
    axes.append(sf::Vertex{PlotPoint(0.0f, 1.0f), sf::Color::White});
    window.draw(axes);

    // Where we are along the curve right now; x is the same fraction t and y is the tween at that t, so the dot tracks
    // the circle exactly
    sf::CircleShape marker(MARKER_RADIUS);
    marker.setFillColor(sf::Color::Yellow);
    marker.setOrigin({MARKER_RADIUS, MARKER_RADIUS});
    marker.setPosition(PlotPoint(t, tween(0.0f, 1.0f, t)));
    window.draw(marker);

    window.display();
}

int main() {
    sf::RenderWindow window;

    try {
        // Initialize window
        window.create(sf::VideoMode({WINDOW_WIDTH, WINDOW_HEIGHT}), "Tween");
        window.setFramerateLimit(FPS_LIMIT);
        // Prevent key repeats.
        window.setKeyRepeatEnabled(false);

        bool shouldQuit = false;
        // Main game loop
        while (window.isOpen()) {
            handleInput(window, shouldQuit);
            if (shouldQuit) {
                break;
            }
            render(window);
            // Step the animation on. Wrapping here is what sends the circle back to the left for the next pass
            frame = (frame + 1) % FRAMES_PER_ANIMATION;
        }
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return -1;
    }
    return 0;
}
