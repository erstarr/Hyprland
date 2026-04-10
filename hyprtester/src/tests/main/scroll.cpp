#include "../shared.hpp"
#include "../../shared.hpp"
#include "../../hyprctlCompat.hpp"
#include "tests.hpp"

static int  ret = 0;

static void testFocusCycling() {
    for (auto const& win : {"a", "b", "c", "d"}) {
        if (!Tests::spawnKitty(win)) {
            NLog::log("{}Failed to spawn kitty with win class `{}`", Colors::RED, win);
            ++TESTS_FAILED;
            ret = 1;
            return;
        }
    }

    OK(getFromSocket("/dispatch focuswindow class:a"));

    OK(getFromSocket("/dispatch movefocus r"));

    {
        auto str = getFromSocket("/activewindow");
        EXPECT_CONTAINS(str, "class: b");
    }

    OK(getFromSocket("/dispatch movefocus r"));

    {
        auto str = getFromSocket("/activewindow");
        EXPECT_CONTAINS(str, "class: c");
    }

    OK(getFromSocket("/dispatch movefocus r"));

    {
        auto str = getFromSocket("/activewindow");
        EXPECT_CONTAINS(str, "class: d");
    }

    OK(getFromSocket("/dispatch movewindow l"));

    {
        auto str = getFromSocket("/activewindow");
        EXPECT_CONTAINS(str, "class: d");
    }

    OK(getFromSocket("/dispatch movefocus u"));

    {
        auto str = getFromSocket("/activewindow");
        EXPECT_CONTAINS(str, "class: c");
    }

    // clean up
    NLog::log("{}Killing all windows", Colors::YELLOW);
    Tests::killAllWindows();
}

static void testFocusWrapping() {
    for (auto const& win : {"a", "b", "c", "d"}) {
        if (!Tests::spawnKitty(win)) {
            NLog::log("{}Failed to spawn kitty with win class `{}`", Colors::RED, win);
            ++TESTS_FAILED;
            ret = 1;
            return;
        }
    }

    // set wrap_focus to true
    OK(getFromSocket("/keyword scrolling:wrap_focus true"));

    OK(getFromSocket("/dispatch focuswindow class:a"));

    OK(getFromSocket("/dispatch layoutmsg focus l"));

    {
        auto str = getFromSocket("/activewindow");
        EXPECT_CONTAINS(str, "class: d");
    }

    OK(getFromSocket("/dispatch layoutmsg focus r"));

    {
        auto str = getFromSocket("/activewindow");
        EXPECT_CONTAINS(str, "class: a");
    }

    // set wrap_focus to false
    OK(getFromSocket("/keyword scrolling:wrap_focus false"));

    OK(getFromSocket("/dispatch focuswindow class:a"));

    OK(getFromSocket("/dispatch layoutmsg focus l"));

    {
        auto str = getFromSocket("/activewindow");
        EXPECT_CONTAINS(str, "class: a");
    }

    OK(getFromSocket("/dispatch focuswindow class:d"));

    OK(getFromSocket("/dispatch layoutmsg focus r"));

    {
        auto str = getFromSocket("/activewindow");
        EXPECT_CONTAINS(str, "class: d");
    }

    // clean up
    NLog::log("{}Killing all windows", Colors::YELLOW);
    Tests::killAllWindows();
}

static void testSwapcolWrapping() {
    for (auto const& win : {"a", "b", "c", "d"}) {
        if (!Tests::spawnKitty(win)) {
            NLog::log("{}Failed to spawn kitty with win class `{}`", Colors::RED, win);
            ++TESTS_FAILED;
            ret = 1;
            return;
        }
    }

    // set wrap_swapcol to true
    OK(getFromSocket("/keyword scrolling:wrap_swapcol true"));

    OK(getFromSocket("/dispatch focuswindow class:a"));

    OK(getFromSocket("/dispatch layoutmsg swapcol l"));
    OK(getFromSocket("/dispatch layoutmsg focus l"));

    {
        auto str = getFromSocket("/activewindow");
        EXPECT_CONTAINS(str, "class: c");
    }

    // clean up
    NLog::log("{}Killing all windows", Colors::YELLOW);
    Tests::killAllWindows();

    for (auto const& win : {"a", "b", "c", "d"}) {
        if (!Tests::spawnKitty(win)) {
            NLog::log("{}Failed to spawn kitty with win class `{}`", Colors::RED, win);
            ++TESTS_FAILED;
            ret = 1;
            return;
        }
    }

    OK(getFromSocket("/dispatch focuswindow class:d"));
    OK(getFromSocket("/dispatch layoutmsg swapcol r"));
    OK(getFromSocket("/dispatch layoutmsg focus r"));

    {
        auto str = getFromSocket("/activewindow");
        EXPECT_CONTAINS(str, "class: b");
    }

    // clean up
    NLog::log("{}Killing all windows", Colors::YELLOW);
    Tests::killAllWindows();

    for (auto const& win : {"a", "b", "c", "d"}) {
        if (!Tests::spawnKitty(win)) {
            NLog::log("{}Failed to spawn kitty with win class `{}`", Colors::RED, win);
            ++TESTS_FAILED;
            ret = 1;
            return;
        }
    }

    // set wrap_swapcol to false
    OK(getFromSocket("/keyword scrolling:wrap_swapcol false"));

    OK(getFromSocket("/dispatch focuswindow class:a"));

    OK(getFromSocket("/dispatch layoutmsg swapcol l"));
    OK(getFromSocket("/dispatch layoutmsg focus r"));

    {
        auto str = getFromSocket("/activewindow");
        EXPECT_CONTAINS(str, "class: b");
    }

    OK(getFromSocket("/dispatch focuswindow class:d"));

    OK(getFromSocket("/dispatch layoutmsg swapcol r"));
    OK(getFromSocket("/dispatch layoutmsg focus l"));

    {
        auto str = getFromSocket("/activewindow");
        EXPECT_CONTAINS(str, "class: c");
    }

    // clean up
    NLog::log("{}Killing all windows", Colors::YELLOW);
    Tests::killAllWindows();
}

static bool testWindowRule() {
    NLog::log("{}Testing Scrolling Width", Colors::GREEN);

    // inject a new rule.
    OK(getFromSocket("/keyword windowrule[scrolling-width]:match:class kitty_scroll"));
    OK(getFromSocket("/keyword windowrule[scrolling-width]:scrolling_width 0.1"));

    if (!Tests::spawnKitty("kitty_scroll")) {
        NLog::log("{}Failed to spawn kitty with win class `kitty_scroll`", Colors::RED);
        return false;
    }

    if (!Tests::spawnKitty("kitty_scroll")) {
        NLog::log("{}Failed to spawn kitty with win class `kitty_scroll`", Colors::RED);
        return false;
    }

    EXPECT(Tests::windowCount(), 2);

    // not the greatest test, but as long as res and gaps don't change, we good.
    EXPECT_CONTAINS(getFromSocket("/activewindow"), "size: 174,1036");

    NLog::log("{}Killing all windows", Colors::YELLOW);
    Tests::killAllWindows();
    EXPECT(Tests::windowCount(), 0);
    return true;
}

static void testScrollingViewBehaviourDispatchFocusWindowFollowFocusFalse() {

    /*
     focuswindow DOES NOT move the scrolling view when follow_focus = 0
     ---------------------------------------------------------------------------------
    */

    // ensure variables are correctly set for the test
    OK(getFromSocket("/keyword scrolling:follow_focus 0"));

    if (!Tests::spawnKitty("a")) {
        NLog::log("{}Failed to spawn kitty with win class `a`", Colors::RED);
        ++TESTS_FAILED;
        ret = 1;
        return;
    }

    OK(getFromSocket("/dispatch layoutmsg colresize 0.8"));

    if (!Tests::spawnKitty("b")) {
        NLog::log("{}Failed to spawn kitty with class `b`", Colors::RED);
        ++TESTS_FAILED;
        ret = 1;
        return;
    }

    OK(getFromSocket("/dispatch focuswindow class:a"));

    // if the view does not move, we expect the x coordinate of the window of class "a" to be negative, as it would be to the left of the viewport
    const std::string posA  = Tests::getWindowAttribute(getFromSocket("/activewindow"), "at:");
    const int         posAx = std::stoi(posA.substr(4, posA.find(',') - 4));

    if (posAx < 0) {
        NLog::log("{}Passed: {}Expected the x coordinate of window of class \"a\" to be < 0, got {}.", Colors::GREEN, Colors::RESET, posAx);
        TESTS_PASSED++;
    } else {
        NLog::log("{}Failed: {}Expected the x coordinate of window of class \"a\" to be < 0, got {}.", Colors::RED, Colors::RESET, posAx);
        ++TESTS_FAILED;
        ret = 1;
        return;
    }

    // clean up

    // to revert the changes made to config
    NLog::log("{}Restoring config state", Colors::YELLOW);
    OK(getFromSocket("/keyword scrolling:follow_focus 1"));

    // kill all windows
    NLog::log("{}Killing all windows", Colors::YELLOW);
    Tests::killAllWindows();
    EXPECT(Tests::windowCount(), 0);
}


static void testScrollingViewBehaviourDispatchFocusWindowFollowFocustrue() {

    /*
     focuswindow DOES move the view when follow_focus != 0
     --------------------------------------------------------------------
    */

    if (!Tests::spawnKitty("a")) {
        NLog::log("{}Failed to spawn kitty with win class `a`", Colors::RED);
        ++TESTS_FAILED;
        ret = 1;
        return;
    }

    OK(getFromSocket("/dispatch layoutmsg colresize 0.8"));

    if (!Tests::spawnKitty("b")) {
        NLog::log("{}Failed to spawn kitty with class `b`", Colors::RED);
        ++TESTS_FAILED;
        ret = 1;
        return;
    }

    OK(getFromSocket("/dispatch focuswindow class:a"));

    // If the view does not move, we expect the x coordinate of the window of class "a" to be negative, as it would be to the left of the viewport.
    // If it is not, the view moved, which is what we expect to happen.
    const std::string posA  = Tests::getWindowAttribute(getFromSocket("/activewindow"), "at:");
    const int         posAx = std::stoi(posA.substr(4, posA.find(',') - 4));

    if (posAx < 0) {
        NLog::log("{}Failed: {}Expected the x coordinate of window of class \"a\" to be >= 0, got {}.", Colors::RED, Colors::RESET, posAx);
        ++TESTS_FAILED;
        ret = 1;
        return;
    } else {
        NLog::log("{}Passed: {}Expected the x coordinate of window of class \"a\" to be >= 0, got {}.", Colors::GREEN, Colors::RESET, posAx);
        TESTS_PASSED++;
    }

    // clean up

    // kill all windows
    NLog::log("{}Killing all windows", Colors::YELLOW);
    Tests::killAllWindows();
    EXPECT(Tests::windowCount(), 0);
}




static void testScrollingViewBehaviourFocusFallback() {

    /*
     Focus fallback from killing a floating window onto a tiled window must NOT move scrolling view, regardless of follow_focus
     --------------------------------------------------------------------------------------------------------------------------
    */

    // ensure variables are correctly set for the test
    OK(getFromSocket("/keyword scrolling:follow_focus 0"));

    if (!Tests::spawnKitty("a")) {
        NLog::log("{}Failed to spawn kitty with win class `a`", Colors::RED);
        ++TESTS_FAILED;
        ret = 1;
        return;
    }

    OK(getFromSocket("/dispatch layoutmsg colresize 0.8"));

    if (!Tests::spawnKitty("b")) {
        NLog::log("{}Failed to spawn kitty with class `b`", Colors::RED);
        ++TESTS_FAILED;
        ret = 1;
        return;
    }

    if (!Tests::spawnKitty("c")) {
        NLog::log("{}Failed to spawn kitty with class `c`", Colors::RED);
        ++TESTS_FAILED;
        ret = 1;
        return;
    }

    // make it float - the view now mush have shifted to fit window class:b
    OK(getFromSocket("/dispatch setfloating active"));

    // establish focus history
    OK(getFromSocket("/dispatch focuswindow class:c"));
    OK(getFromSocket("/dispatch focuswindow class:a"));
    OK(getFromSocket("/dispatch focuswindow class:c"));

    // kill the floating window
    // Expect the focus to fall back to the left tiled window
    OK(getFromSocket("/dispatch killwindow class:c"));
    Tests::waitUntilWindowsN(2);

    // The focus now must have fallen back to tiled window of class "a".

    // If the view did not move, we expect currently focused window's (class:a) to have "at: " x coordinat value <0 (must be left of the viewport)

    const std::string currentWindowPos  = Tests::getWindowAttribute(getFromSocket("/activewindow"), "at:");
    const std::string currentWindowPosX = currentWindowPos.substr(4, currentWindowPos.find(',') - 4);

    // test pass
    if (std::stoi(currentWindowPosX) < 0) {
        NLog ::log("{}Passed: {}Expected the x coordinate of window of class \"a\" to be < 0, got {}.", Colors ::GREEN, Colors::RESET, currentWindowPosX);
        TESTS_PASSED++;
    }
    // test fail
    else {
        NLog::log("{}Failed: {}Expected the x coordinate of window of class \"a\" to be < 0, got {}.", Colors::RED, Colors::RESET, currentWindowPosX);
        ++TESTS_FAILED;
        ret = 1;
        return;
    }

    // clean up

    // to revert the changes made to config
    NLog::log("{}Restoring config state", Colors::YELLOW);
    OK(getFromSocket("/keyword scrolling:follow_focus 1"));

    // kill all windows
    NLog::log("{}Killing all windows", Colors::YELLOW);
    Tests::killAllWindows();
    EXPECT(Tests::windowCount(), 0);
}



static bool test() {
    NLog::log("{}Testing Scroll layout", Colors::GREEN);

    // setup
    OK(getFromSocket("/dispatch workspace name:scroll"));
    OK(getFromSocket("/keyword general:layout scrolling"));

    // test
    NLog::log("{}Testing focus cycling", Colors::GREEN);
    testFocusCycling();

    // test
    NLog::log("{}Testing focus wrap", Colors::GREEN);
    testFocusWrapping();

    // test
    NLog::log("{}Testing swapcol wrap", Colors::GREEN);
    testSwapcolWrapping();
    
    testWindowRule();

    // test
    NLog::log("{}Testing scrolling view behaviour: focuswindow dispatch SHOULD NOT move scrolling view when follow_focus = false", Colors::GREEN);
    testScrollingViewBehaviourDispatchFocusWindowFollowFocusFalse();


    // test
    NLog::log("{}Testing scrolling view behaviour: focuswindow dispatch SHOULD move scrolling view when follow_focus = true", Colors::GREEN);
    testScrollingViewBehaviourDispatchFocusWindowFollowFocustrue();



    // test
    NLog::log("{}Testing scrolling view behaviour: focus fallback from floating window to a tiled window should not move scrolling view", Colors::GREEN);
    testScrollingViewBehaviourFocusFallback();



    // clean up
    NLog::log("Cleaning up", Colors::YELLOW);
    OK(getFromSocket("/dispatch workspace 1"));
    OK(getFromSocket("/reload"));

    return !ret;
}

REGISTER_TEST_FN(test);
