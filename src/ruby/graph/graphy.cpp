// ============================================================================
// graphy.cpp — Rock-Solid Generic Node Graph Backend for Ruby GG
// Inspired by Unreal Engine EdGraph/EdGraphSchema & Godot GraphEdit
// ============================================================================

#include "graphy.h"
#include <queue>
#include <stack>
#include <algorithm>

namespace ruby::graph {

// ── Pin Palette (Direct Unreal Engine GraphEditorSettings Values) ─────────────

QColor pin_type_color(PinType type) {
    switch (type) {
        case PinType::Exec:      return QColor(255, 255, 255);      // ExecutionPinTypeColor
        case PinType::Boolean:   return QColor(140, 20, 20);        // BooleanPinTypeColor (Maroon)
        case PinType::Byte:      return QColor(0, 54, 40);          // BytePinTypeColor (Dark Green)
        case PinType::Int:       return QColor(3, 196, 110);        // IntPinTypeColor (Teal/Seafoam)
        case PinType::Float:     return QColor(91, 255, 15);        // FloatPinTypeColor (Lime Green)
        case PinType::String:    return QColor(255, 0, 168);        // StringPinTypeColor (Bright Pink)
        case PinType::Text:      return QColor(204, 68, 102);       // TextPinTypeColor (Salmon)
        case PinType::Vector3:   return QColor(255, 151, 4);        // VectorPinTypeColor (Amber/Gold)
        case PinType::Rotator:   return QColor(90, 116, 255);       // RotatorPinTypeColor (Periwinkle)
        case PinType::Transform: return QColor(255, 44, 0);         // TransformPinTypeColor (Orange)
        case PinType::Color:     return QColor(0, 136, 255);        // Color / LinearColor (Sky Blue)
        case PinType::Object:    return QColor(0, 102, 232);        // ObjectPinTypeColor (Sharp Blue)
        case PinType::Delegate:  return QColor(255, 20, 20);        // DelegatePinTypeColor (Bright Red)
        case PinType::Wildcard:  return QColor(77, 74, 74);         // WildcardPinTypeColor (Dark Grey)
    }
    return QColor(200, 200, 200);
}

QString pin_type_name(PinType type) {
    switch (type) {
        case PinType::Exec:      return "Exec";
        case PinType::Boolean:   return "Boolean";
        case PinType::Byte:      return "Byte";
        case PinType::Int:       return "Int";
        case PinType::Float:     return "Float";
        case PinType::String:    return "String";
        case PinType::Text:      return "Text";
        case PinType::Vector3:   return "Vector3";
        case PinType::Rotator:   return "Rotator";
        case PinType::Transform: return "Transform";
        case PinType::Color:     return "Color";
        case PinType::Object:    return "Object";
        case PinType::Delegate:  return "Delegate";
        case PinType::Wildcard:  return "Wildcard";
    }
    return "Unknown";
}

bool can_connect_types(PinType from, PinType to) {
    if (from == to) return true;
    if (from == PinType::Wildcard || to == PinType::Wildcard) return true;

    // Control flow is strictly exclusive
    if (from == PinType::Exec || to == PinType::Exec) return false;

    // Int <-> Float
    if ((from == PinType::Int && to == PinType::Float) ||
        (from == PinType::Float && to == PinType::Int)) return true;

    // Byte <-> Int
    if ((from == PinType::Byte && to == PinType::Int) ||
        (from == PinType::Int && to == PinType::Byte)) return true;

    // Numbers & Vectors -> String / Text
    if (to == PinType::String || to == PinType::Text) {
        if (from == PinType::Int || from == PinType::Float ||
            from == PinType::Byte || from == PinType::Boolean ||
            from == PinType::Vector3 || from == PinType::Rotator) return true;
    }

    // Color <-> Vector3
    if ((from == PinType::Color && to == PinType::Vector3) ||
        (from == PinType::Vector3 && to == PinType::Color)) return true;

    return false;
}

// ── Pin Serialization ────────────────────────────────────────────────────────

QJsonObject Pin::to_json() const {
    QJsonObject o;
    o["id"] = id;
    o["node_id"] = node_id;
    o["name"] = name;
    o["tooltip"] = tooltip;
    o["type"] = static_cast<int>(type);
    o["dir"] = static_cast<int>(dir);
    o["default_value"] = default_value;
    return o;
}

Pin Pin::from_json(const QJsonObject& obj) {
    Pin p;
    p.id = obj["id"].toInt();
    p.node_id = obj["node_id"].toInt();
    p.name = obj["name"].toString();
    p.tooltip = obj["tooltip"].toString();
    p.type = static_cast<PinType>(obj["type"].toInt());
    p.dir = static_cast<PinDirection>(obj["dir"].toInt());
    p.default_value = obj["default_value"].toString();
    return p;
}

// ── Connection Serialization ─────────────────────────────────────────────────

QJsonObject Connection::to_json() const {
    QJsonObject o;
    o["id"] = id;
    o["from_node"] = from_node;
    o["from_pin"] = from_pin;
    o["to_node"] = to_node;
    o["to_pin"] = to_pin;
    return o;
}

Connection Connection::from_json(const QJsonObject& obj) {
    Connection c;
    c.id = obj["id"].toInt();
    c.from_node = obj["from_node"].toInt();
    c.from_pin = obj["from_pin"].toInt();
    c.to_node = obj["to_node"].toInt();
    c.to_pin = obj["to_pin"].toInt();
    return c;
}

// ── CommentFrame Serialization ───────────────────────────────────────────────

QJsonObject CommentFrame::to_json() const {
    QJsonObject o;
    o["id"] = id;
    o["title"] = title;
    o["x"] = x;
    o["y"] = y;
    o["width"] = width;
    o["height"] = height;
    o["color"] = color.name(QColor::HexArgb);
    return o;
}

CommentFrame CommentFrame::from_json(const QJsonObject& obj) {
    CommentFrame c;
    c.id = obj["id"].toInt();
    c.title = obj["title"].toString("Comment");
    c.x = obj["x"].toDouble(0.0);
    c.y = obj["y"].toDouble(0.0);
    c.width = obj["width"].toDouble(340.0);
    c.height = obj["height"].toDouble(220.0);
    if (obj.contains("color")) {
        c.color = QColor(obj["color"].toString());
    }
    return c;
}

// ── Node Implementation ──────────────────────────────────────────────────────

Node::Node(int id, const QString& title, const QString& category)
    : m_id(id), m_title(title), m_category(category) {}

Pin* Node::find_pin(int pin_id) {
    for (auto& p : m_inputs) {
        if (p.id == pin_id) return &p;
    }
    for (auto& p : m_outputs) {
        if (p.id == pin_id) return &p;
    }
    return nullptr;
}

const Pin* Node::find_pin(int pin_id) const {
    for (const auto& p : m_inputs) {
        if (p.id == pin_id) return &p;
    }
    for (const auto& p : m_outputs) {
        if (p.id == pin_id) return &p;
    }
    return nullptr;
}

Pin& Node::add_input(int id, const QString& name, PinType type, const QString& default_val) {
    Pin p;
    p.id = id;
    p.node_id = m_id;
    p.name = name;
    p.type = type;
    p.dir = PinDirection::Input;
    p.default_value = default_val;
    m_inputs.push_back(p);
    return m_inputs.back();
}

Pin& Node::add_output(int id, const QString& name, PinType type) {
    Pin p;
    p.id = id;
    p.node_id = m_id;
    p.name = name;
    p.type = type;
    p.dir = PinDirection::Output;
    m_outputs.push_back(p);
    return m_outputs.back();
}

QJsonObject Node::to_json() const {
    QJsonObject o;
    o["id"] = m_id;
    o["title"] = m_title;
    o["subtitle"] = m_subtitle;
    o["category"] = m_category;
    o["flags"] = static_cast<int>(m_flags);
    o["x"] = m_x;
    o["y"] = m_y;
    o["width"] = m_width;
    o["height"] = m_height;

    QJsonArray in_arr;
    for (const auto& pin : m_inputs) in_arr.append(pin.to_json());
    o["inputs"] = in_arr;

    QJsonArray out_arr;
    for (const auto& pin : m_outputs) out_arr.append(pin.to_json());
    o["outputs"] = out_arr;

    if (m_subgraph) {
        o["subgraph"] = m_subgraph->to_json();
    }
    return o;
}

std::shared_ptr<Node> Node::from_json(const QJsonObject& obj) {
    auto n = std::make_shared<Node>();
    n->m_id = obj["id"].toInt();
    n->m_title = obj["title"].toString();
    n->m_subtitle = obj["subtitle"].toString();
    n->m_category = obj["category"].toString();
    n->m_flags = static_cast<NodeFlags>(obj["flags"].toInt());
    n->m_x = obj["x"].toDouble();
    n->m_y = obj["y"].toDouble();
    n->m_width = obj["width"].toDouble(190.0);
    n->m_height = obj["height"].toDouble(80.0);

    const QJsonArray in_arr = obj["inputs"].toArray();
    for (const auto& v : in_arr) {
        n->m_inputs.push_back(Pin::from_json(v.toObject()));
    }

    const QJsonArray out_arr = obj["outputs"].toArray();
    for (const auto& v : out_arr) {
        n->m_outputs.push_back(Pin::from_json(v.toObject()));
    }

    if (obj.contains("subgraph")) {
        auto sub = std::make_shared<Graph>();
        sub->from_json(obj["subgraph"].toObject());
        n->m_subgraph = sub;
    }
    return n;
}

// ── Graph Engine Implementation ──────────────────────────────────────────────

std::shared_ptr<Node> Graph::create_node(const QString& title, const QString& category, float x, float y) {
    auto n = std::make_shared<Node>(next_node_id(), title, category);
    n->set_pos(x, y);
    m_nodes.push_back(n);
    return n;
}

std::shared_ptr<Node> Graph::create_reroute_knot(PinType type, float x, float y) {
    auto n = std::make_shared<Node>(next_node_id(), "", "Utility");
    n->set_flags(NodeFlags::Reroute);
    n->set_pos(x, y);
    n->set_size(24.0f, 24.0f);
    n->add_input(next_pin_id(), "", type);
    n->add_output(next_pin_id(), "", type);
    m_nodes.push_back(n);
    return n;
}

std::shared_ptr<Node> Graph::create_subgraph_node(const QString& title, float x, float y) {
    auto n = std::make_shared<Node>(next_node_id(), title, "Composite");
    n->set_flags(NodeFlags::Subgraph);
    n->set_pos(x, y);
    n->set_subtitle("Collapsed Subgraph");
    auto inner = std::make_shared<Graph>();
    n->set_subgraph(inner);

    // Create entry and exit tunnels inside subgraph (Unreal UK2Node_Tunnel)
    auto entry = inner->create_node("Inputs", "Tunnel", 50.0f, 100.0f);
    entry->set_flags(NodeFlags::TunnelEntry);
    auto exit  = inner->create_node("Outputs", "Tunnel", 400.0f, 100.0f);
    exit->set_flags(NodeFlags::TunnelExit);

    m_nodes.push_back(n);
    return n;
}

void Graph::add_node(std::shared_ptr<Node> node) {
    if (!node) return;
    if (find_node(node->id())) return;
    m_nodes.push_back(node);
}

bool Graph::remove_node(int node_id) {
    auto it = std::find_if(m_nodes.begin(), m_nodes.end(), [node_id](const std::shared_ptr<Node>& n) {
        return n && n->id() == node_id;
    });
    if (it == m_nodes.end()) return false;

    auto n = *it;
    for (const auto& p : n->inputs()) disconnect_pin(p.id);
    for (const auto& p : n->outputs()) disconnect_pin(p.id);

    m_nodes.erase(it);
    return true;
}

std::shared_ptr<Node> Graph::find_node(int node_id) const {
    for (const auto& n : m_nodes) {
        if (n && n->id() == node_id) return n;
    }
    return nullptr;
}

CommentFrame& Graph::create_comment(const QString& title, float x, float y, float w, float h) {
    CommentFrame cf;
    cf.id = next_comment_id();
    cf.title = title;
    cf.x = x;
    cf.y = y;
    cf.width = w;
    cf.height = h;
    m_comments.push_back(cf);
    return m_comments.back();
}

bool Graph::remove_comment(int comment_id) {
    auto it = std::find_if(m_comments.begin(), m_comments.end(), [comment_id](const CommentFrame& c) {
        return c.id == comment_id;
    });
    if (it == m_comments.end()) return false;
    m_comments.erase(it);
    return true;
}

Pin* Graph::find_pin(int pin_id) {
    for (auto& n : m_nodes) {
        if (!n) continue;
        if (Pin* p = n->find_pin(pin_id)) return p;
    }
    return nullptr;
}

const Pin* Graph::find_pin(int pin_id) const {
    for (const auto& n : m_nodes) {
        if (!n) continue;
        if (const Pin* p = n->find_pin(pin_id)) return p;
    }
    return nullptr;
}

PinConnectionResult Graph::can_connect(int from_pin_id, int to_pin_id) const {
    PinConnectionResult res;
    const Pin* p_from = find_pin(from_pin_id);
    const Pin* p_to   = find_pin(to_pin_id);

    if (!p_from || !p_to) {
        res.response = ConnectionResponse::Disallow;
        res.message = "Invalid pin IDs";
        return res;
    }

    if (p_from->dir != PinDirection::Output || p_to->dir != PinDirection::Input) {
        res.response = ConnectionResponse::Disallow;
        res.message = "Directions must be Output -> Input";
        return res;
    }

    if (p_from->node_id == p_to->node_id) {
        res.response = ConnectionResponse::Disallow;
        res.message = "Cannot connect pins on the same node";
        return res;
    }

    if (!can_connect_types(p_from->type, p_to->type)) {
        res.response = ConnectionResponse::Disallow;
        res.message = QString("Cannot convert %1 to %2")
                          .arg(pin_type_name(p_from->type), pin_type_name(p_to->type));
        return res;
    }

    for (const auto& c : m_connections) {
        if (c.from_pin == from_pin_id && c.to_pin == to_pin_id) {
            res.response = ConnectionResponse::Disallow;
            res.message = "Connection already exists";
            return res;
        }
    }

    // Input pins for data in Unreal only accept one connection at a time
    if (p_to->type != PinType::Exec) {
        for (const auto& c : m_connections) {
            if (c.to_pin == to_pin_id) {
                res.response = ConnectionResponse::BreakOthersB;
                res.message = "Replace existing connection";
                return res;
            }
        }
    }

    if (p_from->type != p_to->type) {
        res.response = ConnectionResponse::MakeWithConversion;
        res.message = QString("Coerce %1 to %2").arg(pin_type_name(p_from->type), pin_type_name(p_to->type));
        return res;
    }

    res.response = ConnectionResponse::Make;
    return res;
}

bool Graph::connect(int from_pin_id, int to_pin_id) {
    auto check = can_connect(from_pin_id, to_pin_id);
    if (!check.can_connect()) return false;

    if (check.response == ConnectionResponse::BreakOthersB) {
        disconnect_pin(to_pin_id);
    }

    const Pin* p_from = find_pin(from_pin_id);
    const Pin* p_to   = find_pin(to_pin_id);

    Connection c;
    c.id = next_conn_id();
    c.from_node = p_from->node_id;
    c.from_pin  = from_pin_id;
    c.to_node   = p_to->node_id;
    c.to_pin    = to_pin_id;
    m_connections.push_back(c);

    // Prevent cycle in DAG
    if (has_cycle()) {
        m_connections.pop_back();
        return false;
    }

    return true;
}

bool Graph::disconnect(int connection_id) {
    auto it = std::find_if(m_connections.begin(), m_connections.end(), [connection_id](const Connection& c) {
        return c.id == connection_id;
    });
    if (it == m_connections.end()) return false;
    m_connections.erase(it);
    return true;
}

bool Graph::disconnect_pin(int pin_id) {
    auto it = std::remove_if(m_connections.begin(), m_connections.end(), [pin_id](const Connection& c) {
        return c.from_pin == pin_id || c.to_pin == pin_id;
    });
    bool removed = (it != m_connections.end());
    m_connections.erase(it, m_connections.end());
    return removed;
}

std::vector<Connection> Graph::connections_for_pin(int pin_id) const {
    std::vector<Connection> res;
    for (const auto& c : m_connections) {
        if (c.from_pin == pin_id || c.to_pin == pin_id) {
            res.push_back(c);
        }
    }
    return res;
}

// ── Cycle Detection & Topological Sort ───────────────────────────────────────

bool Graph::has_cycle() const {
    std::unordered_map<int, std::vector<int>> adj;
    for (const auto& n : m_nodes) {
        if (n) adj[n->id()] = {};
    }
    for (const auto& c : m_connections) {
        adj[c.from_node].push_back(c.to_node);
    }

    std::unordered_map<int, int> state;
    for (const auto& n : m_nodes) {
        if (!n) continue;
        state[n->id()] = 0;
    }

    std::function<bool(int)> dfs = [&](int u) -> bool {
        state[u] = 1;
        for (int v : adj[u]) {
            if (state[v] == 1) return true;
            if (state[v] == 0 && dfs(v)) return true;
        }
        state[u] = 2;
        return false;
    };

    for (const auto& n : m_nodes) {
        if (!n) continue;
        if (state[n->id()] == 0) {
            if (dfs(n->id())) return true;
        }
    }
    return false;
}

std::vector<std::shared_ptr<Node>> Graph::topological_sort() const {
    std::vector<std::shared_ptr<Node>> order;
    if (has_cycle()) return order;

    std::unordered_map<int, int> in_degree;
    std::unordered_map<int, std::vector<int>> adj;

    for (const auto& n : m_nodes) {
        if (!n) continue;
        in_degree[n->id()] = 0;
        adj[n->id()] = {};
    }

    for (const auto& c : m_connections) {
        adj[c.from_node].push_back(c.to_node);
        in_degree[c.to_node]++;
    }

    std::queue<int> q;
    for (const auto& pair : in_degree) {
        if (pair.second == 0) q.push(pair.first);
    }

    while (!q.empty()) {
        int u = q.front();
        q.pop();
        if (auto node = find_node(u)) order.push_back(node);
        for (int v : adj[u]) {
            in_degree[v]--;
            if (in_degree[v] == 0) q.push(v);
        }
    }

    return order;
}

// ── JSON Serialization ───────────────────────────────────────────────────────

QJsonObject Graph::to_json() const {
    QJsonObject root;
    root["version"] = 1;
    root["next_node_id"] = m_next_node_id;
    root["next_pin_id"] = m_next_pin_id;
    root["next_conn_id"] = m_next_conn_id;
    root["next_comment_id"] = m_next_comment_id;

    QJsonArray node_arr;
    for (const auto& n : m_nodes) {
        if (n) node_arr.append(n->to_json());
    }
    root["nodes"] = node_arr;

    QJsonArray conn_arr;
    for (const auto& c : m_connections) {
        conn_arr.append(c.to_json());
    }
    root["connections"] = conn_arr;

    QJsonArray comm_arr;
    for (const auto& c : m_comments) {
        comm_arr.append(c.to_json());
    }
    root["comments"] = comm_arr;

    return root;
}

bool Graph::from_json(const QJsonObject& obj) {
    clear();
    m_next_node_id = obj["next_node_id"].toInt(100);
    m_next_pin_id = obj["next_pin_id"].toInt(1000);
    m_next_conn_id = obj["next_conn_id"].toInt(5000);
    m_next_comment_id = obj["next_comment_id"].toInt(9000);

    const QJsonArray node_arr = obj["nodes"].toArray();
    for (const auto& v : node_arr) {
        m_nodes.push_back(Node::from_json(v.toObject()));
    }

    const QJsonArray conn_arr = obj["connections"].toArray();
    for (const auto& v : conn_arr) {
        m_connections.push_back(Connection::from_json(v.toObject()));
    }

    const QJsonArray comm_arr = obj["comments"].toArray();
    for (const auto& v : comm_arr) {
        m_comments.push_back(CommentFrame::from_json(v.toObject()));
    }

    return true;
}

QString Graph::to_json_string(bool pretty) const {
    QJsonDocument doc(to_json());
    return QString::fromUtf8(doc.toJson(pretty ? QJsonDocument::Indented : QJsonDocument::Compact));
}

bool Graph::from_json_string(const QString& str) {
    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(str.toUtf8(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) return false;
    return from_json(doc.object());
}

void Graph::clear() {
    m_nodes.clear();
    m_connections.clear();
    m_comments.clear();
}

// ── Demo Showcase Graph Factory: Decoded Swordigo SCL Infrastructure ─────────
// Grounded in Scener/data/decoded_scenes/platforms.scl (dropping_plat0_d2return)
// and Scener/data/decoded_scenes/traps.scl (door_bars & keyframe animation).

std::shared_ptr<Graph> Graph::create_demo_graph() {
    auto g = std::make_shared<Graph>();

    // =========================================================================
    // GROUP 1: SCL Template: dropping_plat0_d2return (Crumbling Trap Platform)
    // =========================================================================

    // 1. OnCollide Event (CollisionShape Component ID: 107)
    auto n_oncollide = g->create_node("OnCollide Event", "Events", 70.0f, 120.0f);
    n_oncollide->set_flags(NodeFlags::Event);
    n_oncollide->set_subtitle("CollisionShape [ID: 107]");
    int p_oc_exec   = g->next_pin_id();
    int p_oc_self   = g->next_pin_id();
    int p_oc_target = g->next_pin_id();
    int p_oc_normal = g->next_pin_id();
    n_oncollide->add_output(p_oc_exec, "", PinType::Exec);
    n_oncollide->add_output(p_oc_self, "self", PinType::Object);
    n_oncollide->add_output(p_oc_target, "target", PinType::Object);
    n_oncollide->add_output(p_oc_normal, "normal", PinType::Vector3);

    // 2. Check: Target is Hero? (target:identifier() == "hero")
    auto n_check_hero = g->create_node("Target Is Hero?", "Script", 380.0f, 150.0f);
    n_check_hero->set_flags(NodeFlags::PureFunction);
    n_check_hero->set_subtitle("target:identifier() == 'hero'");
    int p_ch_tgt = g->next_pin_id();
    int p_ch_exp = g->next_pin_id();
    int p_ch_res = g->next_pin_id();
    n_check_hero->add_input(p_ch_tgt, "Target", PinType::Object);
    n_check_hero->add_input(p_ch_exp, "Expected ID", PinType::String, "hero");
    n_check_hero->add_output(p_ch_res, "Is Hero", PinType::Boolean);

    // 3. Check: Downward Stomp? (normal:y() < -0.3)
    auto n_check_stomp = g->create_node("Downward Stomp?", "Math", 380.0f, 310.0f);
    n_check_stomp->set_flags(NodeFlags::PureFunction);
    n_check_stomp->set_subtitle("normal:y() < -0.30");
    int p_cs_nrm = g->next_pin_id();
    int p_cs_max = g->next_pin_id();
    int p_cs_res = g->next_pin_id();
    n_check_stomp->add_input(p_cs_nrm, "Normal", PinType::Vector3);
    n_check_stomp->add_input(p_cs_max, "Max Y", PinType::Float, "-0.30");
    n_check_stomp->add_output(p_cs_res, "Is Stomp", PinType::Boolean);

    // 4. Check: CollisionShape.IsEnabled(self, 107)
    auto n_check_enabled = g->create_node("CollisionShape.IsEnabled", "Collision", 380.0f, 470.0f);
    n_check_enabled->set_flags(NodeFlags::PureFunction);
    n_check_enabled->set_subtitle("Trigger Active [ID: 107]");
    int p_ce_self = g->next_pin_id();
    int p_ce_id   = g->next_pin_id();
    int p_ce_res  = g->next_pin_id();
    n_check_enabled->add_input(p_ce_self, "Object", PinType::Object);
    n_check_enabled->add_input(p_ce_id, "Shape ID", PinType::Int, "107");
    n_check_enabled->add_output(p_ce_res, "Is Active", PinType::Boolean);

    // 5. Logical AND (Stomp && Hero && Enabled)
    auto n_and = g->create_node("Boolean AND", "Logic", 660.0f, 290.0f);
    n_and->set_flags(NodeFlags::PureFunction);
    n_and->set_subtitle("All Conditions Met");
    int p_and_a   = g->next_pin_id();
    int p_and_b   = g->next_pin_id();
    int p_and_c   = g->next_pin_id();
    int p_and_res = g->next_pin_id();
    n_and->add_input(p_and_a, "Condition A", PinType::Boolean);
    n_and->add_input(p_and_b, "Condition B", PinType::Boolean);
    n_and->add_input(p_and_c, "Condition C", PinType::Boolean);
    n_and->add_output(p_and_res, "Result", PinType::Boolean);

    // 6. Branch (Control Flow)
    auto n_branch = g->create_node("Branch", "Flow Control", 660.0f, 110.0f);
    n_branch->set_flags(NodeFlags::Branch);
    int p_br_in    = g->next_pin_id();
    int p_br_cond  = g->next_pin_id();
    int p_br_true  = g->next_pin_id();
    int p_br_false = g->next_pin_id();
    n_branch->add_input(p_br_in, "", PinType::Exec);
    n_branch->add_input(p_br_cond, "Condition", PinType::Boolean);
    n_branch->add_output(p_br_true, "True", PinType::Exec);
    n_branch->add_output(p_br_false, "False", PinType::Exec);

    // 7. CollisionShape.SetEnabled(self, 107, false)
    auto n_disable = g->create_node("CollisionShape.SetEnabled", "Collision", 930.0f, 110.0f);
    n_disable->set_subtitle("Disable Trigger [ID: 107]");
    int p_dis_in   = g->next_pin_id();
    int p_dis_obj  = g->next_pin_id();
    int p_dis_id   = g->next_pin_id();
    int p_dis_val  = g->next_pin_id();
    int p_dis_out  = g->next_pin_id();
    n_disable->add_input(p_dis_in, "", PinType::Exec);
    n_disable->add_input(p_dis_obj, "Target", PinType::Object);
    n_disable->add_input(p_dis_id, "Shape ID", PinType::Int, "107");
    n_disable->add_input(p_dis_val, "Enabled", PinType::Boolean, "false");
    n_disable->add_output(p_dis_out, "", PinType::Exec);

    // 8. TransformController.TranslateBy (Rumble vibration shake)
    auto n_shake = g->create_node("TransformController.TranslateBy", "Transform", 1230.0f, 110.0f);
    n_shake->set_subtitle("Platform Rumble Shake");
    int p_shk_in    = g->next_pin_id();
    int p_shk_obj   = g->next_pin_id();
    int p_shk_delta = g->next_pin_id();
    int p_shk_dur   = g->next_pin_id();
    int p_shk_out   = g->next_pin_id();
    n_shake->add_input(p_shk_in, "", PinType::Exec);
    n_shake->add_input(p_shk_obj, "Target", PinType::Object);
    n_shake->add_input(p_shk_delta, "Offset", PinType::Vector3, "(10, 0, 0)");
    n_shake->add_input(p_shk_dur, "Duration", PinType::Float, "0.10");
    n_shake->add_output(p_shk_out, "", PinType::Exec);

    // 9. SoundLibrary.PlayEffect("chirp1")
    auto n_sfx = g->create_node("SoundLibrary.PlayEffect", "Audio", 1520.0f, 110.0f);
    n_sfx->set_subtitle("Play Sound: chirp1");
    int p_sfx_in   = g->next_pin_id();
    int p_sfx_clip = g->next_pin_id();
    int p_sfx_vol  = g->next_pin_id();
    int p_sfx_out  = g->next_pin_id();
    n_sfx->add_input(p_sfx_in, "", PinType::Exec);
    n_sfx->add_input(p_sfx_clip, "Clip", PinType::String, "chirp1");
    n_sfx->add_input(p_sfx_vol, "Volume", PinType::Float, "1.00");
    n_sfx->add_output(p_sfx_out, "", PinType::Exec);

    // 10. PhysicsObject.SetEnabled(self, true) (Drop platform)
    auto n_phys = g->create_node("PhysicsObject.SetEnabled", "Physics", 1520.0f, 310.0f);
    n_phys->set_subtitle("Drop Platform [ID: 1]");
    int p_phy_in   = g->next_pin_id();
    int p_phy_obj  = g->next_pin_id();
    int p_phy_en   = g->next_pin_id();
    int p_phy_spd  = g->next_pin_id();
    int p_phy_out  = g->next_pin_id();
    n_phys->add_input(p_phy_in, "", PinType::Exec);
    n_phys->add_input(p_phy_obj, "Target", PinType::Object);
    n_phys->add_input(p_phy_en, "Physics", PinType::Boolean, "true");
    n_phys->add_input(p_phy_spd, "Max Speed", PinType::Float, "700.0");
    n_phys->add_output(p_phy_out, "", PinType::Exec);

    // Reroute Knot for self object pointer
    auto knot_self = g->create_reroute_knot(PinType::Object, 340.0f, 220.0f);
    int p_knot_self_in  = knot_self->inputs()[0].id;
    int p_knot_self_out = knot_self->outputs()[0].id;

    // Connect Group 1 Cables
    g->connect(p_oc_exec, p_br_in);
    g->connect(p_oc_self, p_knot_self_in);
    g->connect(p_knot_self_out, p_ce_self);
    g->connect(p_knot_self_out, p_dis_obj);
    g->connect(p_knot_self_out, p_shk_obj);
    g->connect(p_knot_self_out, p_phy_obj);
    g->connect(p_oc_target, p_ch_tgt);
    g->connect(p_oc_normal, p_cs_nrm);
    g->connect(p_ch_res, p_and_a);
    g->connect(p_cs_res, p_and_b);
    g->connect(p_ce_res, p_and_c);
    g->connect(p_and_res, p_br_cond);
    g->connect(p_br_true, p_dis_in);
    g->connect(p_dis_out, p_shk_in);
    g->connect(p_shk_out, p_sfx_in);
    g->connect(p_sfx_out, p_phy_in);

    // Comment Frame for Crumbling Platform
    auto& cf_trap = g->create_comment("SCL Template: dropping_plat0_d2return (Crumbling Trap Platform)", 40.0f, 50.0f, 1720.0f, 580.0f);
    cf_trap.color = QColor(22, 50, 80, 140);

    // =========================================================================
    // GROUP 2: SCL Template: door_bars & Keyframe Animation Infrastructure
    // =========================================================================

    // 11. ModelComponent (Mesh ID: 1)
    auto n_model = g->create_node("ModelComponent", "Rendering", 70.0f, 750.0f);
    n_model->set_subtitle("fencetrap1.pod [ID: 1]");
    int p_mdl_name = g->next_pin_id();
    int p_mdl_yrot = g->next_pin_id();
    int p_mdl_diff = g->next_pin_id();
    int p_mdl_emis = g->next_pin_id();
    int p_mdl_id   = g->next_pin_id();
    n_model->add_input(p_mdl_name, "Model Name", PinType::String, "fencetrap1");
    n_model->add_input(p_mdl_yrot, "Y Rotation", PinType::Float, "1.5708");
    n_model->add_input(p_mdl_diff, "Diffuse Color", PinType::Color, "(1, 1, 1, 1)");
    n_model->add_input(p_mdl_emis, "Emission", PinType::Float, "0.00");
    n_model->add_output(p_mdl_id, "Model ID", PinType::Int);

    // Reroute Knot for Model ID
    auto knot_model = g->create_reroute_knot(PinType::Int, 360.0f, 810.0f);
    int p_km_in  = knot_model->inputs()[0].id;
    int p_km_out = knot_model->outputs()[0].id;

    // 12. KeyframeAnimation Component (ID: 101)
    auto n_keyframe = g->create_node("KeyframeAnimation", "Animation", 440.0f, 720.0f);
    n_keyframe->set_subtitle("fencetrap1 Clip [ID: 101]");
    int p_kf_mid  = g->next_pin_id();
    int p_kf_clip = g->next_pin_id();
    int p_kf_spd  = g->next_pin_id();
    int p_kf_rep  = g->next_pin_id();
    int p_kf_aid  = g->next_pin_id();
    n_keyframe->add_input(p_kf_mid, "Model ID", PinType::Int);
    n_keyframe->add_input(p_kf_clip, "Clip Name", PinType::String, "fencetrap1");
    n_keyframe->add_input(p_kf_spd, "Speed Mult", PinType::Float, "0.20");
    n_keyframe->add_input(p_kf_rep, "Repeating", PinType::Boolean, "false");
    n_keyframe->add_output(p_kf_aid, "Anim ID", PinType::Int);

    // 13. AnimationController Component (ID: 105)
    auto n_anim_ctrl = g->create_node("AnimationController", "Animation", 440.0f, 920.0f);
    n_anim_ctrl->set_subtitle("Controller Dispatcher [ID: 105]");
    int p_ac_mid = g->next_pin_id();
    int p_ac_def = g->next_pin_id();
    int p_ac_cid = g->next_pin_id();
    n_anim_ctrl->add_input(p_ac_mid, "Model ID", PinType::Int);
    n_anim_ctrl->add_input(p_ac_def, "Default Anim", PinType::Int, "0");
    n_anim_ctrl->add_output(p_ac_cid, "Controller ID", PinType::Int);

    // 14. SoundEffect Component (ID: 3)
    auto n_sound_comp = g->create_node("SoundEffectComponent", "Audio", 800.0f, 920.0f);
    n_sound_comp->set_subtitle("door_close.wav [ID: 3]");
    int p_se_name = g->next_pin_id();
    int p_se_vol  = g->next_pin_id();
    int p_se_sid  = g->next_pin_id();
    n_sound_comp->add_input(p_se_name, "Name", PinType::String, "door_close");
    n_sound_comp->add_input(p_se_vol, "Volume", PinType::Float, "1.00");
    n_sound_comp->add_output(p_se_sid, "Sound ID", PinType::Int);

    // 15. DoorController Component (ID: 107)
    auto n_door = g->create_node("DoorControllerComponent", "Gameplay", 1120.0f, 720.0f);
    n_door->set_subtitle("Gate State Machine [ID: 107]");
    int p_dr_exec  = g->next_pin_id();
    int p_dr_acid  = g->next_pin_id();
    int p_dr_anid  = g->next_pin_id();
    int p_dr_snd   = g->next_pin_id();
    int p_dr_open  = g->next_pin_id();
    int p_dr_out   = g->next_pin_id();
    int p_dr_state = g->next_pin_id();
    n_door->add_input(p_dr_exec, "", PinType::Exec);
    n_door->add_input(p_dr_acid, "Anim Controller ID", PinType::Int);
    n_door->add_input(p_dr_anid, "Keyframe Anim ID", PinType::Int);
    n_door->add_input(p_dr_snd, "Close Sound ID", PinType::Int);
    n_door->add_input(p_dr_open, "Initial Open", PinType::Boolean, "true");
    n_door->add_output(p_dr_out, "", PinType::Exec);
    n_door->add_output(p_dr_state, "Gate Is Open", PinType::Boolean);

    // 16. Action: Set Gate Open State
    auto n_toggle = g->create_node("Set Gate Open State", "Gameplay", 1460.0f, 720.0f);
    n_toggle->set_subtitle("Toggle Gate Open / Closed");
    int p_tg_in    = g->next_pin_id();
    int p_tg_state = g->next_pin_id();
    int p_tg_out   = g->next_pin_id();
    n_toggle->add_input(p_tg_in, "", PinType::Exec);
    n_toggle->add_input(p_tg_state, "New State", PinType::Boolean, "false");
    n_toggle->add_output(p_tg_out, "", PinType::Exec);

    // Connect Group 2 Cables
    g->connect(p_mdl_id, p_km_in);
    g->connect(p_km_out, p_kf_mid);
    g->connect(p_km_out, p_ac_mid);
    g->connect(p_ac_cid, p_dr_acid);
    g->connect(p_kf_aid, p_dr_anid);
    g->connect(p_se_sid, p_dr_snd);
    g->connect(p_dr_out, p_tg_in);
    g->connect(p_dr_state, p_tg_state);

    // Comment Frame for Gate & Animation Assembly
    auto& cf_gate = g->create_comment("SCL Template: door_bars (Animation & Mesh Component Assembly)", 40.0f, 660.0f, 1720.0f, 440.0f);
    cf_gate.color = QColor(70, 45, 20, 140);

    return g;
}

} // namespace ruby::graph
