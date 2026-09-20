// ============================================================================
// mobile_main_window.cpp — Mobile-First Main Window for Ruby GG Android
// ============================================================================

#include "mobile_main_window.h"
#include "mobile_asset_browser.h"
#include "mobile_code_editor.h"
#include "mobile_viewport_widget.h"

#include <QStatusBar>
#include <QMessageBox>
#include <QFileInfo>

#ifdef Q_OS_ANDROID
#include <QCoreApplication>
#include <QApplication>
#include <QJniObject>
#include <jni.h>
#endif

namespace ruby::android {

MobileMainWindow::MobileMainWindow(QWidget* parent)
    : QMainWindow(parent)
{
    setWindowTitle(QStringLiteral("Ruby GG Mobile"));
    setStyleSheet(QStringLiteral("QMainWindow { background: #0E1016; color: #E0E6F5; }"));

    m_stack = new QStackedWidget(this);
    setCentralWidget(m_stack);

    // Page 0: Hub / Asset Browser (Portrait)
    m_hub = new MobileAssetBrowser(m_stack);
    m_stack->addWidget(m_hub);

    // Page 1: Single-File Code Editor (Portrait)
    m_code_editor = new MobileCodeEditor(m_stack);
    m_stack->addWidget(m_code_editor);

    // Page 2: 3D Scene Viewport (Landscape)
    m_viewport = new MobileViewportWidget(m_stack);
    m_stack->addWidget(m_viewport);

    // Wire Hub navigation
    connect(m_hub, &MobileAssetBrowser::sceneOpenRequested, this, [this](const QString& path, bool visual) {
        if (visual) show_visual_viewport(path);
        else show_code_editor(path);
    });
    connect(m_hub, &MobileAssetBrowser::modelOpenRequested, this, &MobileMainWindow::show_visual_viewport);
    connect(m_hub, &MobileAssetBrowser::codeOpenRequested, this, &MobileMainWindow::show_code_editor);
    connect(m_hub, &MobileAssetBrowser::statusMessage, this, &MobileMainWindow::on_status_message);

    // Wire Code Editor navigation
    connect(m_code_editor, &MobileCodeEditor::backToHubRequested, this, &MobileMainWindow::show_hub);
    connect(m_code_editor, &MobileCodeEditor::switchToVisualRequested, this, &MobileMainWindow::show_visual_viewport);
    connect(m_code_editor, &MobileCodeEditor::statusMessage, this, &MobileMainWindow::on_status_message);

    // Wire Viewport navigation
    connect(m_viewport, &MobileViewportWidget::backToHubRequested, this, &MobileMainWindow::show_hub);
    connect(m_viewport, &MobileViewportWidget::switchToCodeRequested, this, &MobileMainWindow::show_code_editor);
    connect(m_viewport, &MobileViewportWidget::statusMessage, this, &MobileMainWindow::on_status_message);

    // Start in Portrait Hub
    show_hub();

#ifdef Q_OS_ANDROID
    // Notify Java layer that Qt environment and UI stack are initialized
    QJniObject::callStaticMethod<void>(
        "in/aevora/ruby/RubyActivity",
        "onNativeInitialized",
        "()V");
#endif
}

void MobileMainWindow::request_orientation(bool landscape) {
#ifdef Q_OS_ANDROID
    QJniObject activity = QNativeInterface::QAndroidApplication::context();
    if (activity.isValid()) {
        if (landscape) {
            activity.callMethod<void>("setOrientationLandscape");
            activity.callMethod<void>("setImmersiveMode", "(Z)V", static_cast<jboolean>(true));
        } else {
            activity.callMethod<void>("setOrientationPortrait");
            activity.callMethod<void>("setImmersiveMode", "(Z)V", static_cast<jboolean>(false));
        }
    }
#else
    // On desktop / test environment: adjust window dimensions to simulate mobile aspect ratio
    if (landscape) {
        resize(880, 480); // Landscape phone aspect
    } else {
        resize(480, 840); // Portrait phone aspect
    }
#endif
}

bool MobileMainWindow::check_save_dirty_file() {
    if (m_code_editor->is_dirty()) {
        auto res = QMessageBox::question(
            this, QStringLiteral("Unsaved Changes"),
            QStringLiteral("Save changes to '%1' before leaving?").arg(QFileInfo(m_active_file_path).fileName()),
            QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
        if (res == QMessageBox::Save) {
            return m_code_editor->save_file();
        } else if (res == QMessageBox::Cancel) {
            return false;
        }
    }
    if (m_viewport->is_dirty()) {
        auto res = QMessageBox::question(
            this, QStringLiteral("Unsaved Changes"),
            QStringLiteral("Save scene changes to '%1' before leaving?").arg(QFileInfo(m_active_file_path).fileName()),
            QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
        if (res == QMessageBox::Save) {
            return m_viewport->save_current_scene();
        } else if (res == QMessageBox::Cancel) {
            return false;
        }
    }
    return true;
}

void MobileMainWindow::show_hub() {
    if (!check_save_dirty_file()) return;

    request_orientation(false); // Portrait for Hub
    m_stack->setCurrentIndex(PageHub);
}

void MobileMainWindow::show_code_editor(const QString& file_path) {
    if (!file_path.isEmpty() && file_path != m_active_file_path) {
        if (!check_save_dirty_file()) return;
        m_active_file_path = file_path;
        m_code_editor->load_file(file_path);
    }

    request_orientation(false); // Portrait for Code Editor
    m_stack->setCurrentIndex(PageCodeEditor);
}

void MobileMainWindow::show_visual_viewport(const QString& file_path) {
    const QString target_path = file_path.isEmpty() ? m_active_file_path : file_path;
    if (!target_path.isEmpty()) {
        if (target_path != m_active_file_path) {
            if (!check_save_dirty_file()) return;
            m_active_file_path = target_path;
        }

        const QString ext = QFileInfo(target_path).suffix().toLower();
        if (ext == QStringLiteral("scene")) {
            m_viewport->load_scene(target_path.toStdString());
        } else if (ext == QStringLiteral("pod") || ext == QStringLiteral("glb")) {
            m_viewport->load_model(target_path.toStdString());
        }
    }

    request_orientation(true); // Landscape for 3D Viewport
    m_stack->setCurrentIndex(PageVisualViewport);
}

void MobileMainWindow::open_file(const QString& file_path, bool prefer_visual) {
    if (file_path.isEmpty()) return;
    const QString ext = QFileInfo(file_path).suffix().toLower();
    if (ext == QStringLiteral("scene")) {
        if (prefer_visual) {
            show_visual_viewport(file_path);
        } else {
            show_code_editor(file_path);
        }
    } else if (ext == QStringLiteral("pod") || ext == QStringLiteral("glb") || ext == QStringLiteral("gltf") || ext == QStringLiteral("obj")) {
        show_visual_viewport(file_path);
    } else {
        show_code_editor(file_path);
    }
}

void MobileMainWindow::on_storage_permission_updated() {
    if (m_hub) {
        m_hub->refresh();
        on_status_message(QStringLiteral("Storage permissions granted"));
    }
}

void MobileMainWindow::on_status_message(const QString& msg) {
    if (statusBar()) {
        statusBar()->showMessage(msg, 3500);
    }
}

} // namespace ruby::android

#ifdef Q_OS_ANDROID
extern "C" {

JNIEXPORT void JNICALL
Java_in_aevora_ruby_RubyActivity_nativeOnFileOpened(JNIEnv* env, jclass /*clazz*/, jstring path) {
    if (!path) return;
    const char* str = env->GetStringUTFChars(path, nullptr);
    QString qpath = QString::fromUtf8(str);
    env->ReleaseStringUTFChars(path, str);

    QMetaObject::invokeMethod(qApp, [qpath]() {
        for (QWidget* w : QApplication::topLevelWidgets()) {
            if (auto* mw = qobject_cast<ruby::android::MobileMainWindow*>(w)) {
                mw->open_file(qpath, true);
                break;
            }
        }
    }, Qt::QueuedConnection);
}

JNIEXPORT void JNICALL
Java_in_aevora_ruby_RubyActivity_nativeOnStoragePermissionGranted(JNIEnv* /*env*/, jclass /*clazz*/, jboolean granted) {
    if (granted) {
        QMetaObject::invokeMethod(qApp, []() {
            for (QWidget* w : QApplication::topLevelWidgets()) {
                if (auto* mw = qobject_cast<ruby::android::MobileMainWindow*>(w)) {
                    mw->on_storage_permission_updated();
                    break;
                }
            }
        }, Qt::QueuedConnection);
    }
}

// Backward compatibility stub
JNIEXPORT void JNICALL
Java_com_openswordigo_ruby_RubyActivity_nativeOnFileOpened(JNIEnv* env, jclass clazz, jstring path) {
    Java_in_aevora_ruby_RubyActivity_nativeOnFileOpened(env, clazz, path);
}

JNIEXPORT void JNICALL
Java_com_openswordigo_ruby_RubyActivity_nativeOnStoragePermissionGranted(JNIEnv* env, jclass clazz, jboolean granted) {
    Java_in_aevora_ruby_RubyActivity_nativeOnStoragePermissionGranted(env, clazz, granted);
}

} // extern "C"
#endif
