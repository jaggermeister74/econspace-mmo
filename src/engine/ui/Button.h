#pragma once

#include "raylib.h"
#include <string>
#include <functional>

// UI button: a rectangle with a label and an action. The action is stored in
// std::function — the button doesn't know what it does, it's supplied at creation.
class Button
{
public:
    Button(Rectangle bounds, std::string label, std::function<void()> onClick);

    // Draws the button and, unless disabled, invokes the action if it was clicked this frame.
    // A modal overlay can keep its background visible while preventing click-through.
    void Process(bool interactive = true);

private:
    Rectangle             bounds_;
    std::string           label_;
    std::function<void()> onClick_;
};
