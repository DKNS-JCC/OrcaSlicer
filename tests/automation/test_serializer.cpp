#include <catch2/catch_all.hpp>
#include "slic3r/GUI/Automation/WidgetSerializer.hpp"

using namespace Slic3r::GUI::Automation;

TEST_CASE("node_to_json emits the unified node shape", "[automation][serializer]") {
    UiNode n;
    n.backend = BackendKind::Wx;
    n.id      = "btn_slice";
    n.path    = "MainFrame/Panel[2]/Button[0]";
    n.klass   = "Button";
    n.label   = "Slice plate";
    n.rect    = {100, 200, 120, 32};
    n.enabled = true;
    n.visible = true;

    const nlohmann::json j = node_to_json(n, /*include_children*/ false);

    CHECK(j.at("backend") == "wx");
    CHECK(j.at("id") == "btn_slice");
    CHECK(j.at("path") == "MainFrame/Panel[2]/Button[0]");
    CHECK(j.at("class") == "Button");
    CHECK(j.at("label") == "Slice plate");
    CHECK(j.at("rect").at("x") == 100);
    CHECK(j.at("rect").at("w") == 120);
    CHECK(j.at("enabled") == true);
    CHECK(j.at("visible") == true);
    // `handle` must never leak into JSON.
    CHECK_FALSE(j.contains("handle"));
    // No value set -> no "value" key.
    CHECK_FALSE(j.contains("value"));
}
