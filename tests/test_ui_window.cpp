#include <gtest/gtest.h>
#include <corium/corium.hpp>
#include <corium/ui/ui.hpp>

using namespace corium;
using namespace corium::ui;

using UiTestEvents = std::variant<
    QuitEvent,
    WindowResizeEvent,
    WindowMoveEvent,
    FramebufferResizeEvent,
    WindowMinimizeEvent,
    WindowMaximizeEvent,
    WindowFocusEvent,
    WindowRefreshEvent,
    WindowContentScaleEvent,
    WindowCloseEvent,
    MouseMoveEvent,
    MouseButtonEvent,
    MouseScrollEvent,
    MouseEnterEvent,
    KeyEvent,
    CharEvent
>;

TEST(UiWindowTest, NullWindowBackendCreation)
{
    NullWindow window;
    class MockSink {
    public:
        void post(UiTestEvents, EventPriority = EventPriority::Normal) {}
    } sink;

    WindowConfig config{"Test Null Window", 800, 600};
    EXPECT_TRUE(window.create(config, sink));
    EXPECT_FALSE(window.shouldClose());

    window.pollEvents();
    window.swapBuffers();
    EXPECT_EQ(window.backend().frameCount(), 1u);

    window.close();
    EXPECT_TRUE(window.shouldClose());
}

TEST(UiWindowTest, NullWindowAppLifecycle)
{
    using UiRuntime = RuntimeBuilder<>::WithEvents<UiTestEvents>::Build;

    class App : public WindowApp<App, NullWindow, UiRuntime::EventBusType> {
    public:
        bool resizeReceived = false;
        bool moveReceived = false;
        bool framebufferResizeReceived = false;
        bool minimizeReceived = false;
        bool maximizeReceived = false;
        bool focusReceived = false;
        bool refreshReceived = false;
        bool scaleReceived = false;
        bool mouseMoveReceived = false;
        bool scrollReceived = false;
        bool enterReceived = false;
        bool charReceived = false;
        bool renderCalled = false;

        App() : WindowApp(WindowConfig{"App Window", 1024, 768}) {}

        void onRegisterHandlers() {
            on([this](const WindowResizeEvent& e) {
                if (e.width == 1920 && e.height == 1080) resizeReceived = true;
            });
            on([this](const WindowMoveEvent& e) {
                if (e.x == 100 && e.y == 200) moveReceived = true;
            });
            on([this](const FramebufferResizeEvent& e) {
                if (e.width == 3840 && e.height == 2160) framebufferResizeReceived = true;
            });
            on([this](const WindowMinimizeEvent& e) {
                if (e.minimized) minimizeReceived = true;
            });
            on([this](const WindowMaximizeEvent& e) {
                if (e.maximized) maximizeReceived = true;
            });
            on([this](const WindowFocusEvent& e) {
                if (e.focused) focusReceived = true;
            });
            on([this](const WindowRefreshEvent&) {
                refreshReceived = true;
            });
            on([this](const WindowContentScaleEvent& e) {
                if (e.xscale == 2.0f && e.yscale == 2.0f) scaleReceived = true;
            });
            on([this](const MouseMoveEvent& e) {
                if (e.x == 100.0f && e.y == 200.0f) mouseMoveReceived = true;
            });
            on([this](const MouseScrollEvent& e) {
                if (e.xoffset == 0.0f && e.yoffset == 1.5f) scrollReceived = true;
            });
            on([this](const MouseEnterEvent& e) {
                if (e.entered) enterReceived = true;
            });
            on([this](const CharEvent& e) {
                if (e.codepoint == 0x0041) charReceived = true; // 'A'
            });
        }

        void onRender() {
            renderCalled = true;
        }
    };

    UiRuntime runtime;
    App app;
    runtime.initialize(app);

    // Simulate posting events through event sink
    runtime.eventSink().post(WindowResizeEvent{1920, 1080});
    runtime.eventSink().post(WindowMoveEvent{100, 200});
    runtime.eventSink().post(FramebufferResizeEvent{3840, 2160});
    runtime.eventSink().post(WindowMinimizeEvent{true});
    runtime.eventSink().post(WindowMaximizeEvent{true});
    runtime.eventSink().post(WindowFocusEvent{true});
    runtime.eventSink().post(WindowRefreshEvent{});
    runtime.eventSink().post(WindowContentScaleEvent{2.0f, 2.0f});
    runtime.eventSink().post(MouseMoveEvent{100.0f, 200.0f});
    runtime.eventSink().post(MouseScrollEvent{0.0f, 1.5f});
    runtime.eventSink().post(MouseEnterEvent{true});
    runtime.eventSink().post(CharEvent{0x0041});

    runtime.pump();
    app.render();

    EXPECT_TRUE(app.resizeReceived);
    EXPECT_TRUE(app.moveReceived);
    EXPECT_TRUE(app.framebufferResizeReceived);
    EXPECT_TRUE(app.minimizeReceived);
    EXPECT_TRUE(app.maximizeReceived);
    EXPECT_TRUE(app.focusReceived);
    EXPECT_TRUE(app.refreshReceived);
    EXPECT_TRUE(app.scaleReceived);
    EXPECT_TRUE(app.mouseMoveReceived);
    EXPECT_TRUE(app.scrollReceived);
    EXPECT_TRUE(app.enterReceived);
    EXPECT_TRUE(app.charReceived);
    EXPECT_TRUE(app.renderCalled);

    // Simulate window close event
    runtime.eventSink().post(WindowCloseEvent{});
    runtime.pump();

    EXPECT_TRUE(runtime.quitRequested());
    runtime.shutdown();
}

#if CORIUM_HAS_GLFW
TEST(UiWindowTest, GlfwCompileTimeEventFiltering)
{
    using RestrictedEvents = std::variant<QuitEvent, WindowCloseEvent>;

    class RestrictedSink {
    public:
        void post(RestrictedEvents, EventPriority = EventPriority::Normal) {}
    } sink;

    backends::GlfwWindowBackend backend;
    WindowConfig config{"Headless Filtering Test", 640, 480};
    config.noApi = true;

    // Backend creation should succeed and filter out unhandled event types compile-time
    bool created = backend.create(config, sink);
    if (created) {
        backend.close();
    }
}
#endif


