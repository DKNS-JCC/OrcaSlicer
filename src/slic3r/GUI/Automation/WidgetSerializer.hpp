#pragma once
#include "IUiBackend.hpp"
#include <nlohmann/json.hpp>

namespace Slic3r { namespace GUI { namespace Automation {

// Serialize a node to the unified JSON shape from the design spec (§5).
// `include_children` controls recursion into UiNode::children.
nlohmann::json node_to_json(const UiNode& node, bool include_children);

// Serialize an application-state snapshot.
nlohmann::json app_state_to_json(const AppState& state);

}}} // namespace
