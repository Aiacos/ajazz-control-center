// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file mirabox_github_installer.hpp
 * @brief Fetches a Mirabox plugin's files from the open StreamDock-Plugins
 *        GitHub repo and assembles its `.sdPlugin` bundle into a staging dir.
 *
 * GitHub serves no per-subdirectory archive, so a "mirabox-github" catalogue
 * row can't go through the single-archive download path. This installer fetches
 * the plugin's subtree (GitHub Git-Trees API on the plugin dir's tree SHA →
 * flat blob list) and downloads each blob from raw.githubusercontent.com,
 * writing it into a staging directory at its path RELATIVE to the bundle root
 * (the directory that actually contains `manifest.json` — sometimes the plugin
 * dir itself, sometimes a nested `*.sdPlugin/` subdir). The caller then runs the
 * existing verify → promote stages on the assembled staging dir.
 *
 * Security: every written path is validated (@ref isSafeRelPath — no `..`,
 * absolute or drive-prefixed paths) and the transfer is bounded by a file-count
 * and a total-byte cap. Plugins distributed as SOURCE only (no `manifest.json`
 * anywhere in the subtree, e.g. an un-built Vite/C++ project) are rejected with
 * a clear error rather than installed half-baked.
 */
#pragma once

#include <QObject>
#include <QString>
#include <QStringList>

#include <cstdint>
#include <optional>
#include <vector>

class QNetworkAccessManager;
class QNetworkReply;

namespace ajazz::app {

class MiraboxGithubInstaller : public QObject {
    Q_OBJECT

public:
    /// File-count + total-byte caps for one plugin fetch (DoS bound).
    static constexpr int kMaxFiles = 512;
    static constexpr qint64 kMaxTotalBytes = 64LL * 1024 * 1024;

    /// @p networkManager is borrowed (owned by the caller); the installer
    /// parents itself to @p parent and deletes itself after emitting @ref finished.
    explicit MiraboxGithubInstaller(QNetworkAccessManager* networkManager,
                                    QObject* parent = nullptr);
    ~MiraboxGithubInstaller() override;

    /**
     * @brief Begin fetching @p repoPath (e.g. "Plugins/Discord") into
     *        @p stagingDir (created if missing).
     *
     * On success @ref finished(true, stagingDir, "") fires with @p stagingDir
     * containing `manifest.json` at its root. On any failure
     * @ref finished(false, "", error) fires. Exactly one @ref finished per call.
     */
    void start(QString const& ownerRepo,
               QString const& branch,
               QString const& repoPath,
               QString const& stagingDir);

    // ---- Pure helpers (unit-tested) ---------------------------------------

    /**
     * @brief The bundle root within a plugin subtree: the directory holding the
     *        shallowest `manifest.json`.
     *
     * @return `""` when `manifest.json` sits at the subtree root; the containing
     *         directory path (no trailing slash) when it is nested; `nullopt`
     *         when the subtree has no `manifest.json` at all (source-only).
     */
    [[nodiscard]] static std::optional<QString> findBundleRoot(QStringList const& blobPaths);

    /// True when @p relPath is safe to write under a base dir: non-empty, not
    /// absolute, no drive prefix, and no `..` component.
    [[nodiscard]] static bool isSafeRelPath(QString const& relPath);

signals:
    /// Exactly one per @ref start. @p stagingDir is populated only when @p ok.
    void finished(bool ok, QString stagingDir, QString error);

private:
    void fail(QString const& error);
    void onParentListing(QNetworkReply* reply);
    void onTree(QNetworkReply* reply);
    void fetchNextBlob();
    void onBlob(QNetworkReply* reply, QString destRel);

    QNetworkAccessManager* m_net = nullptr;
    QString m_ownerRepo;
    QString m_branch;
    QString m_repoPath;
    QString m_stagingDir;

    struct Blob {
        QString rawUrl;  ///< raw.githubusercontent.com URL of the blob.
        QString destRel; ///< destination path relative to the staging dir.
    };
    std::vector<Blob> m_pending;
    std::size_t m_idx = 0;
    qint64 m_totalBytes = 0;
    bool m_done = false; ///< guards against a double finished() emission.
};

} // namespace ajazz::app
