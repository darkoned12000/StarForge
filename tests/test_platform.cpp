#include <doctest/doctest.h>

#include "platform/window.hpp"

#include <SDL3/SDL.h>

#include <expected>
#include <string>
#include <utility>

using namespace starforge::platform;

namespace
{

SDL_Event makeKeyEvent(Uint32 type, SDL_WindowID windowID, SDL_Scancode scancode)
{
    SDL_Event event;
    SDL_zero(event);
    event.type = static_cast<SDL_EventType>(type);
    event.key.type = static_cast<SDL_EventType>(type);
    event.key.windowID = windowID;
    event.key.scancode = scancode;
    event.key.down = (type == SDL_EVENT_KEY_DOWN);
    return event;
}

SDL_Event makeTextEvent(SDL_WindowID windowID, const char* text)
{
    SDL_Event event;
    SDL_zero(event);
    event.type = SDL_EVENT_TEXT_INPUT;
    event.text.type = SDL_EVENT_TEXT_INPUT;
    event.text.windowID = windowID;
    event.text.text = text;
    return event;
}

SDL_Event makeMouseButtonEvent(Uint32 type, SDL_WindowID windowID, Uint8 button)
{
    SDL_Event event;
    SDL_zero(event);
    event.type = static_cast<SDL_EventType>(type);
    event.button.type = static_cast<SDL_EventType>(type);
    event.button.windowID = windowID;
    event.button.button = button;
    event.button.down = (type == SDL_EVENT_MOUSE_BUTTON_DOWN);
    return event;
}

SDL_Event makeMouseMotionEvent(SDL_WindowID windowID, float xrel, float yrel)
{
    SDL_Event event;
    SDL_zero(event);
    event.type = SDL_EVENT_MOUSE_MOTION;
    event.motion.type = SDL_EVENT_MOUSE_MOTION;
    event.motion.windowID = windowID;
    event.motion.xrel = xrel;
    event.motion.yrel = yrel;
    return event;
}

SDL_Event makeWindowEvent(Uint32 type, SDL_WindowID windowID)
{
    SDL_Event event;
    SDL_zero(event);
    event.type = static_cast<SDL_EventType>(type);
    event.window.type = static_cast<SDL_EventType>(type);
    event.window.windowID = windowID;
    return event;
}

} // namespace

TEST_CASE("Platform window: rejects invalid dimensions")
{
    auto badWidth = Window::create({.title = "bad", .width = 0, .height = 100});
    CHECK_FALSE(badWidth.has_value());

    auto badHeight = Window::create({.title = "bad", .width = 100, .height = -1});
    CHECK_FALSE(badHeight.has_value());
}

TEST_CASE("Platform window: null/moved-from guards are safe")
{
    SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");

    auto created = Window::create({.title = "test", .width = 320, .height = 240, .resizable = true});
    if (!created.has_value())
    {
        MESSAGE("Skipping window-dependent checks: " << created.error());
        return;
    }

    Window window = std::move(*created);

    // Move out of window into moved.
    Window moved = std::move(window);

    // Moved-from window should be safe to query and mutate.
    CHECK_FALSE(window.shouldClose());
    CHECK(window.isMinimized());
    CHECK(window.framebufferSize().width == 0);
    CHECK(window.framebufferSize().height == 0);
    CHECK_FALSE(window.takeResized());
    CHECK_FALSE(window.isKeyDown(PlatformKey::W));
    CHECK_FALSE(window.isMouseButtonDown(PlatformMouseButton::Left));

    float dx = 1.0f, dy = 2.0f, scroll = 3.0f;
    window.takeMouseMotion(dx, dy, scroll);
    CHECK(dx == 0.0f);
    CHECK(dy == 0.0f);
    CHECK(scroll == 0.0f);

    float mx = -1.0f, my = -1.0f;
    window.mousePosition(mx, my);
    CHECK(mx == 0.0f);
    CHECK(my == 0.0f);

    std::string text = "stale";
    window.takeTextInput(text);
    CHECK(text.empty());

    window.pollEvents();
    window.setRelativeMouseMode(true);

    auto surface = window.createVulkanSurface(VK_NULL_HANDLE);
    CHECK_FALSE(surface.has_value());

    // Moved-to window remains usable.
    CHECK_FALSE(moved.shouldClose());
    CHECK(moved.framebufferSize().width > 0);
    CHECK(moved.framebufferSize().height > 0);
}

TEST_CASE("Platform window: move semantics transfer state")
{
    SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");

    auto created = Window::create({.title = "test", .width = 320, .height = 240});
    if (!created.has_value())
    {
        MESSAGE("Skipping window-dependent checks: " << created.error());
        return;
    }

    Window window = std::move(*created);
    SDL_WindowID id = SDL_GetWindowID(window.sdlHandle());
    REQUIRE(id != 0);

    // Inject state through SDL events for this window.
    SDL_Event keyDown = makeKeyEvent(SDL_EVENT_KEY_DOWN, id, SDL_SCANCODE_W);
    SDL_Event text = makeTextEvent(id, "abc");
    SDL_Event motion = makeMouseMotionEvent(id, 5.0f, -2.0f);
    SDL_Event resized = makeWindowEvent(SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED, id);
    SDL_PushEvent(&keyDown);
    SDL_PushEvent(&text);
    SDL_PushEvent(&motion);
    SDL_PushEvent(&resized);

    window.pollEvents();
    CHECK(window.isKeyDown(PlatformKey::W));
    CHECK(window.takeResized());

    float dx = 0.0f, dy = 0.0f, scroll = 0.0f;
    window.takeMouseMotion(dx, dy, scroll);
    CHECK(dx == doctest::Approx(5.0f));
    CHECK(dy == doctest::Approx(-2.0f));

    std::string input;
    window.takeTextInput(input);
    CHECK(input == "abc");

    // Re-inject and move before polling to verify state transfer.
    SDL_Event keyA = makeKeyEvent(SDL_EVENT_KEY_DOWN, id, SDL_SCANCODE_A);
    SDL_Event text2 = makeTextEvent(id, "xyz");
    SDL_Event motion2 = makeMouseMotionEvent(id, 1.0f, 2.0f);
    SDL_Event resized2 = makeWindowEvent(SDL_EVENT_WINDOW_RESIZED, id);
    SDL_PushEvent(&keyA);
    SDL_PushEvent(&text2);
    SDL_PushEvent(&motion2);
    SDL_PushEvent(&resized2);
    window.pollEvents();

    Window moved = std::move(window);
    CHECK(moved.isKeyDown(PlatformKey::A));
    CHECK(moved.takeResized());
    moved.takeMouseMotion(dx, dy, scroll);
    CHECK(dx == doctest::Approx(1.0f));
    CHECK(dy == doctest::Approx(2.0f));
    moved.takeTextInput(input);
    CHECK(input == "xyz");

    // Moved-from should have clean default state.
    CHECK_FALSE(window.isKeyDown(PlatformKey::A));
    CHECK_FALSE(window.takeResized());
    window.takeMouseMotion(dx, dy, scroll);
    CHECK(dx == 0.0f);
    CHECK(dy == 0.0f);
    CHECK(scroll == 0.0f);
    window.takeTextInput(input);
    CHECK(input.empty());
}

TEST_CASE("Platform window: event filtering ignores other windows")
{
    SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");

    auto created = Window::create({.title = "test", .width = 320, .height = 240});
    if (!created.has_value())
    {
        MESSAGE("Skipping window-dependent checks: " << created.error());
        return;
    }

    Window window = std::move(*created);
    SDL_WindowID id = SDL_GetWindowID(window.sdlHandle());
    REQUIRE(id != 0);

    // Clear any startup events.
    window.pollEvents();
    window.takeResized();
    {
        float dx = 0.0f, dy = 0.0f, scroll = 0.0f;
        window.takeMouseMotion(dx, dy, scroll);
        std::string t;
        window.takeTextInput(t);
    }

    const SDL_WindowID otherId = id + 100000; // fake/other window id

    SDL_Event otherKey = makeKeyEvent(SDL_EVENT_KEY_DOWN, otherId, SDL_SCANCODE_W);
    SDL_Event otherText = makeTextEvent(otherId, "bad");
    SDL_Event otherMouse = makeMouseButtonEvent(SDL_EVENT_MOUSE_BUTTON_DOWN, otherId, SDL_BUTTON_LEFT);
    SDL_Event otherMotion = makeMouseMotionEvent(otherId, 9.0f, 9.0f);
    SDL_Event otherResize = makeWindowEvent(SDL_EVENT_WINDOW_RESIZED, otherId);
    SDL_PushEvent(&otherKey);
    SDL_PushEvent(&otherText);
    SDL_PushEvent(&otherMouse);
    SDL_PushEvent(&otherMotion);
    SDL_PushEvent(&otherResize);

    window.pollEvents();

    CHECK_FALSE(window.isKeyDown(PlatformKey::W));
    CHECK_FALSE(window.isMouseButtonDown(PlatformMouseButton::Left));
    CHECK_FALSE(window.takeResized());

    float dx = 0.0f, dy = 0.0f, scroll = 0.0f;
    window.takeMouseMotion(dx, dy, scroll);
    CHECK(dx == 0.0f);
    CHECK(dy == 0.0f);
    CHECK(scroll == 0.0f);

    std::string text;
    window.takeTextInput(text);
    CHECK(text.empty());

    // Events for this window should still be processed.
    SDL_Event ownKey = makeKeyEvent(SDL_EVENT_KEY_DOWN, id, SDL_SCANCODE_W);
    SDL_PushEvent(&ownKey);
    window.pollEvents();
    CHECK(window.isKeyDown(PlatformKey::W));
}

TEST_CASE("Platform window: invalid surface request is rejected")
{
    SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");

    auto created = Window::create({.title = "test", .width = 320, .height = 240});
    if (!created.has_value())
    {
        MESSAGE("Skipping window-dependent checks: " << created.error());
        return;
    }

    auto surface = created->createVulkanSurface(VK_NULL_HANDLE);
    CHECK_FALSE(surface.has_value());
}

TEST_CASE("Platform window: multi-window refcount and event deferral")
{
    SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");

    auto first = Window::create({.title = "first", .width = 320, .height = 240});
    if (!first.has_value())
    {
        MESSAGE("Skipping window-dependent checks: " << first.error());
        return;
    }

    auto second = Window::create({.title = "second", .width = 320, .height = 240});
    if (!second.has_value())
    {
        MESSAGE("Skipping window-dependent checks: " << second.error());
        return;
    }

    SDL_WindowID firstId = SDL_GetWindowID(first->sdlHandle());
    SDL_WindowID secondId = SDL_GetWindowID(second->sdlHandle());
    REQUIRE(firstId != 0);
    REQUIRE(secondId != 0);
    REQUIRE(firstId != secondId);

    // Event targeted at second window should be deferred by first window and
    // still be visible when second window polls.
    SDL_Event keyForSecond = makeKeyEvent(SDL_EVENT_KEY_DOWN, secondId, SDL_SCANCODE_W);
    SDL_PushEvent(&keyForSecond);
    first->pollEvents();
    CHECK_FALSE(first->isKeyDown(PlatformKey::W));

    second->pollEvents();
    CHECK(second->isKeyDown(PlatformKey::W));

    // Destroying first window must not break second window (refcounted SDL init).
    first = std::unexpected(std::string{"released"});
    CHECK(second->framebufferSize().width > 0);

    SDL_Event keyForSecond2 = makeKeyEvent(SDL_EVENT_KEY_DOWN, secondId, SDL_SCANCODE_A);
    SDL_PushEvent(&keyForSecond2);
    second->pollEvents();
    CHECK(second->isKeyDown(PlatformKey::A));
}
