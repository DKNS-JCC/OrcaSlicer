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

TEST_CASE("node_to_json includes children only for wx when requested",
          "[automation][serializer]") {
    UiNode parent;
    parent.backend = BackendKind::Wx;
    parent.klass   = "Panel";
    UiNode child;
    child.backend = BackendKind::Wx;
    child.klass   = "Button";
    child.label   = "OK";
    parent.children.push_back(child);

    const auto with    = node_to_json(parent, true);
    const auto without = node_to_json(parent, false);

    REQUIRE(with.contains("children"));
    CHECK(with.at("children").size() == 1);
    CHECK(with.at("children")[0].at("label") == "OK");
    CHECK_FALSE(without.contains("children"));
}

TEST_CASE("node_to_json emits value and imgui backend tag",
          "[automation][serializer]") {
    UiNode n;
    n.backend   = BackendKind::ImGui;
    n.klass     = "combo";
    n.has_value = true;
    n.value     = "PLA";
    const auto j = node_to_json(n, /*include_children*/ true);
    CHECK(j.at("backend") == "imgui");
    CHECK(j.at("value") == "PLA");
    CHECK_FALSE(j.contains("children")); // imgui items are flat
}

TEST_CASE("app_state_to_json shape", "[automation][serializer]") {
    AppState s;
    s.active_tab     = "preview";
    s.project_loaded = true;
    s.slicing        = true;
    s.slice_progress = 42;
    s.foreground     = true;
    s.modal_dialog   = std::string("Save changes?");
    const auto j = app_state_to_json(s);
    CHECK(j.at("active_tab") == "preview");
    CHECK(j.at("project_loaded") == true);
    CHECK(j.at("slice_progress") == 42);
    CHECK(j.at("modal_dialog") == "Save changes?");
}
