#include "MockUiBackend.hpp"
#include <catch2/catch_all.hpp>

using namespace Slic3r::GUI::Automation;

TEST_CASE("MockUiBackend records calls", "[automation][mock]") {
    MockUiBackend mock;
    UiNode n; n.id = "btn_slice";
    mock.click(n, MouseButton::Left, false, {});
    REQUIRE(mock.clicked_ids.size() == 1);
    CHECK(mock.clicked_ids[0] == "btn_slice");
    CHECK(mock.click_buttons[0] == MouseButton::Left);
}
