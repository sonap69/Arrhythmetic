#include <juce_gui_basics/juce_gui_basics.h>
#include "MainComponent.h"

class ArrhythmeticApplication : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override    { return JUCE_APPLICATION_NAME_STRING; }
    const juce::String getApplicationVersion() override { return JUCE_APPLICATION_VERSION_STRING; }
    bool moreThanOneInstanceAllowed() override          { return true; }

    void initialise (const juce::String&) override
    {
        // Title bar: the Greek name and its English twin. The Greek name is the word for
        // "calculator" (arithmomichani) with one extra rho: a machine with no rhythm.
        // The bytes are UTF-8, written as escapes so every compiler reads them the same way.
        const juce::String title = juce::String (juce::CharPointer_UTF8 ("\xce\x91\xcf\x81\xcf\x81\xcf\x85\xce\xb8\xce\xbc\xce\xbf\xce\xbc\xce\xb7\xcf\x87\xce\xb1\xce\xbd\xce\xae"))
                                 + " (" + getApplicationName() + ")";
        mainWindow = std::make_unique<MainWindow> (title);
    }
    void shutdown() override                            { mainWindow = nullptr; }
    void systemRequestedQuit() override                 { quit(); }

    class MainWindow : public juce::DocumentWindow
    {
    public:
        explicit MainWindow (const juce::String& name)
            : DocumentWindow (name, juce::Colour (0xff17181b), DocumentWindow::allButtons)
        {
            setUsingNativeTitleBar (true);
            setContentOwned (new MainComponent(), true);
            setResizable (true, true);
            setResizeLimits (1120, 680, 2600, 1400);
            centreWithSize (getWidth(), getHeight());
            setVisible (true);
            if (auto* c = getContentComponent())
                c->grabKeyboardFocus();
        }

        void closeButtonPressed() override { JUCEApplication::getInstance()->systemRequestedQuit(); }

    private:
        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainWindow)
    };

private:
    std::unique_ptr<MainWindow> mainWindow;
};

START_JUCE_APPLICATION (ArrhythmeticApplication)
