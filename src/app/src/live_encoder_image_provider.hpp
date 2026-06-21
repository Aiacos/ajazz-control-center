// SPDX-License-Identifier: GPL-3.0-or-later
//
// live_encoder_image_provider.hpp -- mirrors the live device dial/encoder render
// to the QML editor canvas (Elgato-parity Phase 1 / Delta B).
//
// WHY
// ---
// A plugin paints a dial's touch-strip segment via setFeedback/setFeedbackLayout
// (or an encoder setState). Those render to the physical device through
// StreamDockControlService::assignEncoderImage (the encoder layout renderer
// composes the Elgato $X1/$A0/$A1/$B1/$B2/$C1 layout into a QImage). This is the
// dial analogue of the key path (LiveKeyImageStore + image://livekey): we write
// the final composited encoder QImage for each 0-based encoder index into a
// shared store and emit encoderImageAssigned(); the QML editor binds the matching
// EncoderDial / touch-strip segment's iconSource to
// "image://liveencoder/<index>?r=<revision>" so it reloads the live frame.
//
// THREADING
// ---------
// Identical to LiveKeyImageStore: QQuickImageProvider::requestImage may run on a
// Qt image-loading thread while assignEncoderImage writes from the GUI thread,
// so the store is mutex-guarded and shared via shared_ptr between the control
// service (writer) and the provider (reader, owned by the QML engine).
#pragma once

#include <QImage>
#include <QMutex>
#include <QMutexLocker>
#include <QQuickImageProvider>
#include <QSize>
#include <QString>

#include <map>
#include <memory>

namespace ajazz::app {

/// Mutex-guarded map of 0-based encoder index -> latest composited encoder QImage.
class LiveEncoderImageStore {
public:
    void set(int encoderIndex, QImage image) {
        QMutexLocker lock(&m_mutex);
        m_images[encoderIndex] = std::move(image);
    }

    [[nodiscard]] QImage get(int encoderIndex) const {
        QMutexLocker lock(&m_mutex);
        auto const it = m_images.find(encoderIndex);
        return it != m_images.end() ? it->second : QImage{};
    }

    /// Whether a live frame is cached for @p encoderIndex. Lets the editor model
    /// re-point a segment at image://liveencoder after a profile-driven rebuild
    /// wiped its iconSource (the frame itself survives in this store).
    [[nodiscard]] bool has(int encoderIndex) const {
        QMutexLocker lock(&m_mutex);
        return m_images.find(encoderIndex) != m_images.end();
    }

    /// Drop the cached frame for an encoder (e.g. when its action is moved away).
    void clear(int encoderIndex) {
        QMutexLocker lock(&m_mutex);
        m_images.erase(encoderIndex);
    }

private:
    mutable QMutex m_mutex;
    std::map<int, QImage> m_images;
};

/// Serves "image://liveencoder/<0-based-encoder-index>" from a shared
/// LiveEncoderImageStore. The "?r=<revision>" query the QML side appends is
/// ignored here (it only exists to bust QML's image cache) -- we strip everything
/// from '?' onward.
class LiveEncoderImageProvider : public QQuickImageProvider {
public:
    explicit LiveEncoderImageProvider(std::shared_ptr<LiveEncoderImageStore> store)
        : QQuickImageProvider(QQuickImageProvider::Image), m_store(std::move(store)) {}

    QImage requestImage(QString const& id, QSize* size, QSize const& /*requested*/) override {
        int const encoderIndex = id.section('?', 0, 0).toInt();
        QImage const img = m_store ? m_store->get(encoderIndex) : QImage{};
        if (size != nullptr) {
            *size = img.size();
        }
        return img;
    }

private:
    std::shared_ptr<LiveEncoderImageStore> m_store;
};

} // namespace ajazz::app
