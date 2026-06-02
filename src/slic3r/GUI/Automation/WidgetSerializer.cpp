#include "WidgetSerializer.hpp"

namespace Slic3r { namespace GUI { namespace Automation {

static const char* backend_name(BackendKind k) {
    return k == BackendKind::Wx ? "wx" : "imgui";
}

nlohmann::json node_to_json(const UiNode& node, bool include_children) {
    nlohmann::json j;
    j["backend"] = backend_name(node.backend);
    j["id"]      = node.id;
    j["path"]    = node.path;
    j["class"]   = node.klass;
    j["label"]   = node.label;
    j["rect"]    = { {"x", node.rect.x}, {"y", node.rect.y},
                     {"w", node.rect.w}, {"h", node.rect.h} };
    j["enabled"] = node.enabled;
    j["visible"] = node.visible;
    if (node.has_value)
        j["value"] = node.value;
    if (include_children && node.backend == BackendKind::Wx) {
        nlohmann::json arr = nlohmann::json::array();
        for (const UiNode& c : node.children)
            arr.push_back(node_to_json(c, true));
        j["children"] = std::move(arr);
    }
    return j;
}

nlohmann::json app_state_to_json(const AppState& s) {
    nlohmann::json j;
    j["active_tab"]     = s.active_tab;
    j["project_loaded"] = s.project_loaded;
    j["slicing"]        = s.slicing;
    j["slice_progress"] = s.slice_progress;
    j["foreground"]     = s.foreground;
    if (s.modal_dialog)
        j["modal_dialog"] = *s.modal_dialog;
    return j;
}

}}} // namespace
