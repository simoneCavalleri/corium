#include <gtest/gtest.h>
#include <corium/corium.hpp>
#include <corium/ui/ui.hpp>

using namespace corium;
using namespace corium::ui;

using UiTestEvents = std::variant<
    QuitEvent,
    WindowResizeEvent,
    MouseMoveEvent,
    MouseButtonEvent,
    KeyEvent,
    WindowCloseEvent
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
        bool mouseMoveReceived = false;
        bool renderCalled = false;

        App() : WindowApp(WindowConfig{"App Window", 1024, 768}) {}

        void onRegisterHandlers() {
            on([this](const WindowResizeEvent& e) {
                if (e.width == 1920 && e.height == 1080) {
                    resizeReceived = true;
                }
            });

            on([this](const MouseMoveEvent& e) {
                if (e.x == 100.0f && e.y == 200.0f) {
                    mouseMoveReceived = true;
                }
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
    runtime.eventSink().post(MouseMoveEvent{100.0f, 200.0f});

    runtime.pump();
    app.render();

    EXPECT_TRUE(app.resizeReceived);
    EXPECT_TRUE(app.mouseMoveReceived);
    EXPECT_TRUE(app.renderCalled);

    // Simulate window close event
    runtime.eventSink().post(WindowCloseEvent{});
    runtime.pump();

    EXPECT_TRUE(runtime.quitRequested());
    runtime.shutdown();
}
