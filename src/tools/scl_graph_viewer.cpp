// ============================================================================
// scl_graph_viewer.cpp — Standalone SCL Node Graph Interactive Viewer
// Displays prebuilt or live converted SCL graphs using the Graphy engine.
// ============================================================================

#include "ruby/graph/graphy.h"
#include "ruby/graph/graphy_canvas.h"
#include <QApplication>
#include <QMainWindow>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>
#include <QStatusBar>
#include <QToolBar>
#include <QLabel>
#include <QPushButton>
#include <QFileDialog>
#include <QMessageBox>
#include <iostream>

class SclGraphViewerWindow : public QMainWindow {
public:
    explicit SclGraphViewerWindow(const QString& initial_file = QString(), QWidget* parent = nullptr)
        : QMainWindow(parent)
    {
        setWindowTitle("Ruby SCL Visual Graph Viewer — Standalone");
        resize(1360, 850);

        m_canvas = new ruby::graph::GraphyCanvas(this);
        setCentralWidget(m_canvas);

        setup_toolbar();

        if (!initial_file.isEmpty() && QFile::exists(initial_file)) {
            load_graph_json(initial_file);
        } else if (QFile::exists("quest_vase_graph.json")) {
            load_graph_json("quest_vase_graph.json");
        } else {
            // Fallback to SCL demo factory
            auto g = ruby::graph::Graph::create_demo_graph();
            m_canvas->set_graph(g);
            statusBar()->showMessage("Loaded built-in Swordigo SCL template graph.");
        }
    }

    bool load_graph_json(const QString& filepath) {
        QFile file(filepath);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            statusBar()->showMessage(QString("Failed to open file: %1").arg(filepath));
            return false;
        }

        QString json_str = QTextStream(&file).readAll();
        file.close();

        auto g = std::make_shared<ruby::graph::Graph>();
        if (!g->from_json_string(json_str)) {
            statusBar()->showMessage("Invalid Graphy JSON format.");
            return false;
        }

        m_canvas->set_graph(g);
        m_canvas->frame_all();
        setWindowTitle(QString("Ruby SCL Visual Graph Viewer — %1").arg(QFileInfo(filepath).fileName()));
        statusBar()->showMessage(QString("Loaded %1 (%2 nodes, %3 connections)")
            .arg(QFileInfo(filepath).fileName())
            .arg(g->nodes().size())
            .arg(g->connections().size()));
        return true;
    }

private:
    void setup_toolbar() {
        QToolBar* tb = addToolBar("Navigation");
        tb->setMovable(false);

        auto btn_open = new QPushButton("Open Graph JSON...", this);
        connect(btn_open, &QPushButton::clicked, this, [this]() {
            QString path = QFileDialog::getOpenFileName(this, "Open Graphy JSON", "", "Graph JSON (*.json);;All Files (*)");
            if (!path.isEmpty()) {
                load_graph_json(path);
            }
        });
        tb->addWidget(btn_open);

        tb->addSeparator();

        auto btn_frame = new QPushButton("Frame All (F)", this);
        connect(btn_frame, &QPushButton::clicked, this, [this]() {
            m_canvas->frame_all();
        });
        tb->addWidget(btn_frame);

        auto btn_reload_scl = new QPushButton("Load SCL Demo", this);
        connect(btn_reload_scl, &QPushButton::clicked, this, [this]() {
            auto g = ruby::graph::Graph::create_demo_graph();
            m_canvas->set_graph(g);
            m_canvas->frame_all();
            statusBar()->showMessage("Loaded built-in Swordigo SCL template graph.");
        });
        tb->addWidget(btn_reload_scl);

        tb->addSeparator();
        tb->addWidget(new QLabel("  [Alt+Drag]: Slice Wires  |  [Alt+Click]: Sever Pin  |  [Double-Click]: Reroute Knot  |  [F]: Frame All", this));
    }

private:
    ruby::graph::GraphyCanvas* m_canvas = nullptr;
};

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    app.setApplicationName("Ruby SCL Graph Viewer");

    QString target_file;
    if (argc > 1) {
        target_file = QString::fromUtf8(argv[1]);
    }

    SclGraphViewerWindow win(target_file);
    win.show();

    return app.exec();
}
