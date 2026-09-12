#pragma once
// ============================================================================
// graphy.h — Rock-Solid Generic Node Graph Backend for Ruby GG
// Decoupled, production-grade node graph architecture inspired directly by
// Unreal Engine's EdGraph/EdGraphSchema and Godot's GraphEdit.
//
// Key Architectural Patterns:
//   - Typed pin system & palette matching Unreal Engine Blueprint standards
//   - Schema connection validation with ECanCreateConnectionResponse logic
//   - Cycle detection via DFS back-edge detection for DAG validation
//   - Topological sorting (Kahn's algorithm) for evaluation order
//   - Subgraph composite containers (Unreal K2Node_Composite / Tunnel)
//   - Control-point-only reroute knots (Unreal UK2Node_Knot)
//   - Group/Comment frames with containment logic (Unreal SGraphNodeComment)
//   - Complete JSON serialization to and from QJsonObject
// ============================================================================

#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <memory>
#include <functional>
#include <QString>
#include <QColor>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonDocument>

namespace ruby::graph {

// ── Pin Types & Directions (Matching Unreal Engine Blueprint Palette) ─────────

enum class PinType {
    Exec,       // Control flow: White #FFFFFF
    Boolean,    // Boolean flag: Maroon #8C1414
    Byte,       // 8-bit unsigned integer / Enum: Dark Green #003628
    Int,        // 32-bit signed integer: Teal / Seafoam #03C46E
    Float,      // Single/Double floating point: Lime / Bright Green #5BFF0F
    String,     // String data: Bright Pink / Magenta #FF00A8
    Text,       // Localized display text: Salmon #CC4466
    Vector3,    // 3D vector / coordinate: Amber / Gold #FF9704
    Rotator,    // Euler rotation: Periwinkle #5A74FF
    Transform,  // Full 3D TRS transform: Orange #FF2C00
    Color,      // RGBA color: Sky Blue #0088FF
    Object,     // Engine object / Actor pointer: Sharp Blue #0066E8
    Delegate,   // Delegate / event hook: Bright Red #FF1414
    Wildcard    // Dynamic unresolved type: Dark Grey #4D4A4A
};

enum class PinDirection {
    Input,
    Output
};

/// Return exact Unreal Engine Blueprint styled QColor for a PinType
QColor pin_type_color(PinType type);
QString pin_type_name(PinType type);

// ── Unreal-style Connection Response ──────────────────────────────────────────

enum class ConnectionResponse {
    Make,                       // Legal connection
    Disallow,                   // Incompatible / cyclic / illegal
    BreakOthersA,               // Break existing on output pin
    BreakOthersB,               // Break existing on input pin (single input target)
    BreakOthersAB,              // Break both
    MakeWithConversion          // Auto-conversion / type coercion
};

struct PinConnectionResult {
    ConnectionResponse response = ConnectionResponse::Disallow;
    QString message;
    bool can_connect() const { return response != ConnectionResponse::Disallow; }
};

/// Check type compatibility / coercion
bool can_connect_types(PinType from, PinType to);

// ── Pin ──────────────────────────────────────────────────────────────────────

struct Pin {
    int id = 0;
    int node_id = 0;
    QString name;
    QString tooltip;
    PinType type = PinType::Exec;
    PinDirection dir = PinDirection::Input;
    QString default_value; // Inline value box when unconnected
    
    // Canvas layout coordinates cached during node layout
    float canvas_x = 0.0f;
    float canvas_y = 0.0f;

    QJsonObject to_json() const;
    static Pin from_json(const QJsonObject& obj);
};

// ── Connection (Wire) ────────────────────────────────────────────────────────

struct Connection {
    int id = 0;
    int from_node = 0;
    int from_pin  = 0;
    int to_node   = 0;
    int to_pin    = 0;

    bool operator==(const Connection& o) const {
        return from_pin == o.from_pin && to_pin == o.to_pin;
    }

    QJsonObject to_json() const;
    static Connection from_json(const QJsonObject& obj);
};

// ── Node Flags ───────────────────────────────────────────────────────────────

enum class NodeFlags : uint32_t {
    None         = 0,
    PureFunction = 1 << 0,  // Compact expression node without exec pins (Pure)
    Event        = 1 << 1,  // Red event entrypoint banner
    Branch       = 1 << 2,  // Control branch/condition node
    Reroute      = 1 << 3,  // Tiny circular knot pin (UK2Node_Knot)
    Subgraph     = 1 << 4,  // Hierarchical composite subgraph node (UK2Node_Composite)
    TunnelEntry  = 1 << 5,  // Subgraph tunnel input
    TunnelExit   = 1 << 6,  // Subgraph tunnel output
};

inline NodeFlags operator|(NodeFlags a, NodeFlags b) {
    return static_cast<NodeFlags>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}
inline bool operator&(NodeFlags a, NodeFlags b) {
    return (static_cast<uint32_t>(a) & static_cast<uint32_t>(b)) != 0;
}

// ── Comment Frame (Unreal SGraphNodeComment) ──────────────────────────────────

struct CommentFrame {
    int id = 0;
    QString title = "Comment";
    float x = 0.0f;
    float y = 0.0f;
    float width = 340.0f;
    float height = 220.0f;
    QColor color = QColor(45, 52, 64, 150);

    QJsonObject to_json() const;
    static CommentFrame from_json(const QJsonObject& obj);
};

// ── Node (Unreal EdGraphNode) ─────────────────────────────────────────────────

class Graph;

class Node {
public:
    Node() = default;
    Node(int id, const QString& title, const QString& category = "Math");

    int id() const { return m_id; }
    void set_id(int id) { m_id = id; }

    const QString& title() const { return m_title; }
    void set_title(const QString& title) { m_title = title; }

    const QString& subtitle() const { return m_subtitle; }
    void set_subtitle(const QString& subtitle) { m_subtitle = subtitle; }

    const QString& category() const { return m_category; }
    void set_category(const QString& cat) { m_category = cat; }

    NodeFlags flags() const { return m_flags; }
    void set_flags(NodeFlags f) { m_flags = f; }

    float x() const { return m_x; }
    float y() const { return m_y; }
    void set_pos(float x, float y) { m_x = x; m_y = y; }

    float width() const { return m_width; }
    float height() const { return m_height; }
    void set_size(float w, float h) { m_width = w; m_height = h; }

    // Pin queries & mutations
    const std::vector<Pin>& inputs() const { return m_inputs; }
    const std::vector<Pin>& outputs() const { return m_outputs; }
    std::vector<Pin>& inputs_mut() { return m_inputs; }
    std::vector<Pin>& outputs_mut() { return m_outputs; }

    Pin* find_pin(int pin_id);
    const Pin* find_pin(int pin_id) const;

    Pin& add_input(int id, const QString& name, PinType type, const QString& default_val = "");
    Pin& add_output(int id, const QString& name, PinType type);

    // Subgraph access (UK2Node_Composite)
    std::shared_ptr<Graph> subgraph() const { return m_subgraph; }
    void set_subgraph(std::shared_ptr<Graph> g) { m_subgraph = g; }

    QJsonObject to_json() const;
    static std::shared_ptr<Node> from_json(const QJsonObject& obj);

private:
    int m_id = 0;
    QString m_title;
    QString m_subtitle;
    QString m_category;
    NodeFlags m_flags = NodeFlags::None;
    float m_x = 0.0f;
    float m_y = 0.0f;
    float m_width = 190.0f;
    float m_height = 80.0f;

    std::vector<Pin> m_inputs;
    std::vector<Pin> m_outputs;

    std::shared_ptr<Graph> m_subgraph;
};

// ── Graph Engine (Unreal EdGraph) ─────────────────────────────────────────────

class Graph {
public:
    Graph() = default;
    ~Graph() = default;

    // Node operations
    std::shared_ptr<Node> create_node(const QString& title, const QString& category = "Generic", float x = 0.0f, float y = 0.0f);
    std::shared_ptr<Node> create_reroute_knot(PinType type, float x, float y);
    std::shared_ptr<Node> create_subgraph_node(const QString& title, float x, float y);
    void add_node(std::shared_ptr<Node> node);
    bool remove_node(int node_id);
    std::shared_ptr<Node> find_node(int node_id) const;
    const std::vector<std::shared_ptr<Node>>& nodes() const { return m_nodes; }

    // Comment frame operations
    CommentFrame& create_comment(const QString& title, float x, float y, float w, float h);
    bool remove_comment(int comment_id);
    const std::vector<CommentFrame>& comments() const { return m_comments; }
    std::vector<CommentFrame>& comments_mut() { return m_comments; }

    // Pin lookup across all nodes
    Pin* find_pin(int pin_id);
    const Pin* find_pin(int pin_id) const;

    // Connection operations & Schema verification
    PinConnectionResult can_connect(int from_pin_id, int to_pin_id) const;
    bool connect(int from_pin_id, int to_pin_id);
    bool disconnect(int connection_id);
    bool disconnect_pin(int pin_id);
    const std::vector<Connection>& connections() const { return m_connections; }
    std::vector<Connection> connections_for_pin(int pin_id) const;

    // Graph algorithms (Cycle detection & Topological Sorting)
    bool has_cycle() const;
    std::vector<std::shared_ptr<Node>> topological_sort() const;

    // JSON serialization
    QJsonObject to_json() const;
    bool from_json(const QJsonObject& obj);
    QString to_json_string(bool pretty = true) const;
    bool from_json_string(const QString& str);

    // ID allocation
    int next_node_id() { return ++m_next_node_id; }
    int next_pin_id() { return ++m_next_pin_id; }
    int next_conn_id() { return ++m_next_conn_id; }
    int next_comment_id() { return ++m_next_comment_id; }

    void clear();

    // Default pre-populated showcase graph
    static std::shared_ptr<Graph> create_demo_graph();

private:
    std::vector<std::shared_ptr<Node>> m_nodes;
    std::vector<Connection> m_connections;
    std::vector<CommentFrame> m_comments;

    int m_next_node_id = 100;
    int m_next_pin_id = 1000;
    int m_next_conn_id = 5000;
    int m_next_comment_id = 9000;
};

} // namespace ruby::graph
