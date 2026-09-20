#include "scene_orientation.h"

#include <QGuiApplication>
#include <QScreen>
#include <QDebug>

#if defined(Q_OS_ANDROID)
#  include <QCoreApplication>
#  include <QJniObject>
#  include <QtCore/qnativeinterface.h>
#  include <jni.h>
#endif

namespace ruby::android {

SceneOrientation* SceneOrientation::s_instance = nullptr;

namespace {

// android.content.pm.ActivityInfo orientation constants. Spelled out as plain
// ints so this translation unit does not need the Android SDK headers; the
// values are part of the stable public Android API.
constexpr int kOrientationUnspecified      = -1; // SCREEN_ORIENTATION_UNSPECIFIED
constexpr int kOrientationLandscape        =  0; // SCREEN_ORIENTATION_LANDSCAPE
constexpr int kOrientationPortrait         =  1; // SCREEN_ORIENTATION_PORTRAIT
constexpr int kOrientationSensorLandscape  =  6; // SCREEN_ORIENTATION_SENSOR_LANDSCAPE

} // namespace

SceneOrientation::SceneOrientation(QObject* parent)
    : QObject(parent)
{
    s_instance = this;
}

SceneOrientation::~SceneOrientation()
{
    if (s_instance == this) {
        s_instance = nullptr;
    }
}

bool SceneOrientation::isSupported() const
{
#if defined(Q_OS_ANDROID)
    return true;
#else
    return false;
#endif
}

bool SceneOrientation::isLandscape() const
{
    if (m_landscapeLocked) return true;
    if (auto* screen = QGuiApplication::primaryScreen()) {
        const auto o = screen->orientation();
        return o == Qt::LandscapeOrientation || o == Qt::InvertedLandscapeOrientation;
    }
    return false;
}

void SceneOrientation::updateInsets(int top, int bottom, int left, int right)
{
    qreal dpr = 1.0;
    if (auto* screen = QGuiApplication::primaryScreen()) {
        dpr = screen->devicePixelRatio();
        if (dpr < 0.1) dpr = 1.0;
    }

    m_safeInsetTop = top / dpr;
    m_safeInsetBottom = bottom / dpr;
    m_safeInsetLeft = left / dpr;
    m_safeInsetRight = right / dpr;

    emit insetsChanged();
}

void SceneOrientation::vibrateTouch(int durationMs)
{
#if defined(Q_OS_ANDROID)
    QJniObject activity = QNativeInterface::QAndroidApplication::context();
    if (activity.isValid()) {
        activity.callMethod<void>("vibrateTouch", "(I)V", jint(durationMs));
    }
#else
    Q_UNUSED(durationMs);
#endif
}

bool SceneOrientation::hasAllFilesAccess()
{
#if defined(Q_OS_ANDROID)
    QJniObject activity = QNativeInterface::QAndroidApplication::context();
    if (activity.isValid()) {
        return activity.callMethod<jboolean>("hasAllFilesAccess");
    }
#endif
    return true;
}

void SceneOrientation::requestAllFilesAccess()
{
#if defined(Q_OS_ANDROID)
    QJniObject activity = QNativeInterface::QAndroidApplication::context();
    if (activity.isValid()) {
        activity.callMethod<void>("checkAndRequestAllFilesAccess");
    }
#endif
}

void SceneOrientation::moveToBack()
{
#if defined(Q_OS_ANDROID)
    QJniObject activity = QNativeInterface::QAndroidApplication::context();
    if (activity.isValid()) {
        activity.callMethod<void>("moveToBack");
    }
#endif
}

bool SceneOrientation::applyPolicy(int androidOrientation, const QString& name)
{
#if defined(Q_OS_ANDROID)
    QJniObject activity = QNativeInterface::QAndroidApplication::context();
    if (!activity.isValid()) {
        qWarning() << "SceneOrientation: no Android activity context; "
                      "cannot set requested orientation to" << androidOrientation;
        return false;
    }

    // Call RubyActivity helper methods which run on the Android UI looper
    if (androidOrientation == kOrientationLandscape) {
        activity.callMethod<void>("forceLandscape");
    } else if (androidOrientation == kOrientationSensorLandscape) {
        activity.callMethod<void>("setOrientationLandscape");
    } else if (androidOrientation == kOrientationPortrait) {
        activity.callMethod<void>("setOrientationPortrait");
    } else {
        activity.callMethod<void>("setOrientationAuto");
    }
#else
    Q_UNUSED(androidOrientation);
    qInfo() << "SceneOrientation: not on Android — orientation policy" << name
            << "recorded but not applied (the QML portrait guard still holds).";
#endif

    if (m_policyName != name) {
        m_policyName = name;
        emit orientationPolicyChanged();
    }
    return true;
}

bool SceneOrientation::lockLandscape()
{
    const bool ok = applyPolicy(kOrientationLandscape, QStringLiteral("landscape"));
    m_landscapeLocked = true;
    return ok;
}

bool SceneOrientation::lockSensorLandscape()
{
    const bool ok = applyPolicy(kOrientationSensorLandscape, QStringLiteral("sensor-landscape"));
    m_landscapeLocked = true;
    return ok;
}

bool SceneOrientation::lockPortrait()
{
    const bool ok = applyPolicy(kOrientationPortrait, QStringLiteral("portrait"));
    m_landscapeLocked = false;
    return ok;
}

bool SceneOrientation::unlock()
{
    const bool ok = applyPolicy(kOrientationUnspecified, QStringLiteral("unspecified"));
    m_landscapeLocked = false;
    return ok;
}

void SceneOrientation::handleFileOpen(const QString& path)
{
    emit fileOpenRequested(path);
}

} // namespace ruby::android

#if defined(Q_OS_ANDROID)
extern "C" {

JNIEXPORT void JNICALL
Java_in_aevora_ruby_RubyActivity_nativeUpdateWindowInsets(
    JNIEnv* env, jclass clazz, jint top, jint bottom, jint left, jint right)
{
    Q_UNUSED(env);
    Q_UNUSED(clazz);
    if (auto* inst = ruby::android::SceneOrientation::instance()) {
        inst->updateInsets(top, bottom, left, right);
    }
}

JNIEXPORT void JNICALL
Java_in_aevora_ruby_RubyActivity_nativeOnFileOpened(
    JNIEnv* env, jclass clazz, jstring path)
{
    Q_UNUSED(clazz);
    if (!path) return;
    const char* str = env->GetStringUTFChars(path, nullptr);
    QString qpath = QString::fromUtf8(str);
    env->ReleaseStringUTFChars(path, str);

    QMetaObject::invokeMethod(qApp, [qpath]() {
        if (auto* inst = ruby::android::SceneOrientation::instance()) {
            inst->handleFileOpen(qpath);
        }
    }, Qt::QueuedConnection);
}

} // extern "C"
#endif

