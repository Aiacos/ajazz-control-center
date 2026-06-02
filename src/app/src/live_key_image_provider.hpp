// SPDX-License-Identifier: GPL-3.0-or-later
//
// live_key_image_provider.hpp -- mirrors the live device key render to the QML
// editor canvas (Phase 29, OpenDeck parity).
//
// WHY
// ---
// A plugin paints a key via setImage/setTitle/setState. Those render to the
// physical device through StreamDockControlService::assignKeyImage. OpenDeck's
// update_state pushes the same render to BOTH the device AND the frontend canvas
// so the on-screen key mirrors what the device shows (e.g. live "RAM 43%").
// We replicate that: StreamDockControlService writes the final composited QImage
// for each 0-based key index into a shared store and emits keyImageAssigned();
// the QML editor binds the matching KeyCell's iconSource to
// "image://livekey/<index>?r=<revision>" so it reloads the live frame.
//
// THREADING
// ---------
// QQuickImageProvider::requestImage may run on a Qt image-loading thread, while
// assignKeyImage writes from the GUI thread. The store is therefore mutex-guarded
// and shared via shared_ptr between the control service (writer) and the provider
// (reader, owned by the QML engine) so neither outlives the data.
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

/// Mutex-guarded map of 0-based key index -> latest composited key QImage.
class LiveKeyImageStore {
public:
    void set(int keyIndex, QImage image) {
        QMutexLocker lock(&m_mutex);
        m_images[keyIndex] = std::move(image);
    }

    [[nodiscard]] QImage get(int keyIndex) const {
        QMutexLocker lock(&m_mutex);
        auto const it = m_images.find(keyIndex);
        return it != m_images.end() ? it->second : QImage{};
    }

private:
    mutable QMutex m_mutex;
    std::map<int, QImage> m_images;
};

/// Serves "image://livekey/<0-based-key-index>" from a shared LiveKeyImageStore.
/// The "?r=<revision>" query the QML side appends is ignored here (it only exists
/// to bust QML's image cache) -- we strip everything from '?' onward.
class LiveKeyImageProvider : public QQuickImageProvider {
public:
    explicit LiveKeyImageProvider(std::shared_ptr<LiveKeyImageStore> store)
        : QQuickImageProvider(QQuickImageProvider::Image), m_store(std::move(store)) {}

    QImage requestImage(QString const& id, QSize* size, QSize const& /*requested*/) override {
        int const keyIndex = id.section('?', 0, 0).toInt();
        QImage const img = m_store ? m_store->get(keyIndex) : QImage{};
        if (size != nullptr) {
            *size = img.size();
        }
        return img;
    }

private:
    std::shared_ptr<LiveKeyImageStore> m_store;
};

} // namespace ajazz::app
