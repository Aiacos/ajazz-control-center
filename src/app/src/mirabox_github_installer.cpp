// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file mirabox_github_installer.cpp
 * @brief Implementation of @ref ajazz::app::MiraboxGithubInstaller.
 */
#include "mirabox_github_installer.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QUrl>

#include <limits>

namespace ajazz::app {

namespace {
Q_LOGGING_CATEGORY(lcMghInstall, "ajazz.plugins.mirabox-github.install")

constexpr int kPerRequestTimeoutMs = 15000;
constexpr char kManifestName[] = "manifest.json";

/// Parent path of a "Plugins/<name>" repo path ("Plugins/Discord" -> "Plugins").
QString parentPath(QString const& repoPath) {
    qsizetype const slash = repoPath.lastIndexOf(QLatin1Char('/'));
    return slash > 0 ? repoPath.left(slash) : QString{};
}
} // namespace

MiraboxGithubInstaller::MiraboxGithubInstaller(QNetworkAccessManager* networkManager,
                                               QObject* parent)
    : QObject(parent), m_net(networkManager) {}

MiraboxGithubInstaller::~MiraboxGithubInstaller() = default;

// ---------------------------------------------------------------------------
// Pure helpers
// ---------------------------------------------------------------------------

std::optional<QString> MiraboxGithubInstaller::findBundleRoot(QStringList const& blobPaths) {
    QString best;
    bool found = false;
    int bestDepth = std::numeric_limits<int>::max();
    for (QString const& p : blobPaths) {
        QString root;
        bool isManifest = false;
        if (p == QString::fromLatin1(kManifestName)) {
            root = QString{}; // manifest at subtree root
            isManifest = true;
        } else if (p.endsWith(QStringLiteral("/") + QString::fromLatin1(kManifestName))) {
            root = p.left(p.size() - QString::fromLatin1(kManifestName).size() - 1);
            isManifest = true;
        }
        if (!isManifest) {
            continue;
        }
        int const depth = root.isEmpty() ? 0 : static_cast<int>(root.count(QLatin1Char('/')) + 1);
        if (!found || depth < bestDepth) {
            best = root;
            bestDepth = depth;
            found = true;
        }
    }
    if (!found) {
        return std::nullopt;
    }
    return best;
}

bool MiraboxGithubInstaller::isSafeRelPath(QString const& relPath) {
    if (relPath.isEmpty()) {
        return false;
    }
    if (relPath.startsWith(QLatin1Char('/')) || relPath.startsWith(QLatin1Char('\\'))) {
        return false; // absolute
    }
    if (relPath.contains(QLatin1Char('\\'))) {
        return false; // Windows separators / UNC
    }
    if (relPath.contains(QLatin1Char(':'))) {
        return false; // drive prefix / alternate data stream
    }
    for (QString const& comp : relPath.split(QLatin1Char('/'), Qt::SkipEmptyParts)) {
        if (comp == QStringLiteral("..")) {
            return false;
        }
    }
    return true;
}

// ---------------------------------------------------------------------------
// Async flow
// ---------------------------------------------------------------------------

void MiraboxGithubInstaller::fail(QString const& error) {
    if (m_done) {
        return;
    }
    m_done = true;
    qCWarning(lcMghInstall) << "install fetch failed:" << error;
    emit finished(false, QString{}, error);
    deleteLater();
}

void MiraboxGithubInstaller::start(QString const& ownerRepo,
                                   QString const& branch,
                                   QString const& repoPath,
                                   QString const& stagingDir) {
    m_ownerRepo = ownerRepo;
    m_branch = branch;
    m_repoPath = repoPath;
    m_stagingDir = stagingDir;

    if (!m_net) {
        fail(QStringLiteral("No network access manager."));
        return;
    }
    QString const parent = parentPath(repoPath);
    if (parent.isEmpty() || repoPath.isEmpty()) {
        fail(QStringLiteral("Invalid plugin repo path: %1").arg(repoPath));
        return;
    }

    // Step 1: list the parent dir to find the plugin dir's tree SHA.
    QUrl const url(QStringLiteral("https://api.github.com/repos/%1/contents/%2?ref=%3")
                       .arg(m_ownerRepo, parent, m_branch));
    QNetworkRequest req(url);
    req.setRawHeader("Accept", "application/vnd.github+json");
    req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("ajazz-control-center"));
    req.setTransferTimeout(kPerRequestTimeoutMs);
    QNetworkReply* const reply = m_net->get(req);
    QPointer<MiraboxGithubInstaller> self(this);
    QObject::connect(reply, &QNetworkReply::finished, this, [self, reply]() {
        if (self) {
            self->onParentListing(reply);
        } else {
            reply->deleteLater();
        }
    });
}

void MiraboxGithubInstaller::onParentListing(QNetworkReply* reply) {
    reply->deleteLater();
    if (reply->error() != QNetworkReply::NoError) {
        fail(QStringLiteral("GitHub listing failed: %1").arg(reply->errorString()));
        return;
    }
    QJsonDocument const doc = QJsonDocument::fromJson(reply->readAll());
    if (!doc.isArray()) {
        fail(QStringLiteral("Unexpected GitHub listing shape."));
        return;
    }
    QString sha;
    for (QJsonValue const& v : doc.array()) {
        QJsonObject const o = v.toObject();
        if (o.value(QStringLiteral("path")).toString() == m_repoPath &&
            o.value(QStringLiteral("type")).toString() == QStringLiteral("dir")) {
            sha = o.value(QStringLiteral("sha")).toString();
            break;
        }
    }
    if (sha.isEmpty()) {
        fail(QStringLiteral("Plugin directory not found in repo: %1").arg(m_repoPath));
        return;
    }

    // Step 2: recursive tree of just this plugin's subtree (no truncation risk).
    QUrl const url(QStringLiteral("https://api.github.com/repos/%1/git/trees/%2?recursive=1")
                       .arg(m_ownerRepo, sha));
    QNetworkRequest req(url);
    req.setRawHeader("Accept", "application/vnd.github+json");
    req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("ajazz-control-center"));
    req.setTransferTimeout(kPerRequestTimeoutMs);
    QNetworkReply* const treeReply = m_net->get(req);
    QPointer<MiraboxGithubInstaller> self(this);
    QObject::connect(treeReply, &QNetworkReply::finished, this, [self, treeReply]() {
        if (self) {
            self->onTree(treeReply);
        } else {
            treeReply->deleteLater();
        }
    });
}

void MiraboxGithubInstaller::onTree(QNetworkReply* reply) {
    reply->deleteLater();
    if (reply->error() != QNetworkReply::NoError) {
        fail(QStringLiteral("GitHub tree fetch failed: %1").arg(reply->errorString()));
        return;
    }
    QJsonObject const root = QJsonDocument::fromJson(reply->readAll()).object();
    QJsonArray const tree = root.value(QStringLiteral("tree")).toArray();

    QStringList blobPaths;
    blobPaths.reserve(static_cast<int>(tree.size()));
    for (QJsonValue const& v : tree) {
        QJsonObject const o = v.toObject();
        if (o.value(QStringLiteral("type")).toString() == QStringLiteral("blob")) {
            blobPaths.append(o.value(QStringLiteral("path")).toString());
        }
    }

    std::optional<QString> const bundleRoot = findBundleRoot(blobPaths);
    if (!bundleRoot) {
        fail(QStringLiteral("This plugin is distributed as source (no manifest.json) and "
                            "cannot be installed directly."));
        return;
    }
    QString const prefix = bundleRoot->isEmpty() ? QString{} : (*bundleRoot + QLatin1Char('/'));

    // Build the fetch list: blobs under the bundle root, mapped to staging-rel
    // paths, bounded by the file-count and total-byte caps.
    m_pending.clear();
    qint64 declaredTotal = 0;
    for (QJsonValue const& v : tree) {
        QJsonObject const o = v.toObject();
        if (o.value(QStringLiteral("type")).toString() != QStringLiteral("blob")) {
            continue;
        }
        QString const path = o.value(QStringLiteral("path")).toString();
        if (!bundleRoot->isEmpty() && !path.startsWith(prefix)) {
            continue;
        }
        QString const destRel = bundleRoot->isEmpty() ? path : path.mid(prefix.size());
        if (!isSafeRelPath(destRel)) {
            fail(QStringLiteral("Unsafe path in plugin bundle: %1").arg(path));
            return;
        }
        declaredTotal += static_cast<qint64>(o.value(QStringLiteral("size")).toDouble(0));
        if (declaredTotal > kMaxTotalBytes) {
            fail(QStringLiteral("Plugin bundle exceeds the %1 MB limit.")
                     .arg(kMaxTotalBytes / (1024 * 1024)));
            return;
        }
        QString const rawUrl = QStringLiteral("https://raw.githubusercontent.com/%1/%2/%3/%4")
                                   .arg(m_ownerRepo, m_branch, m_repoPath, path);
        m_pending.push_back({rawUrl, destRel});
        if (static_cast<int>(m_pending.size()) > kMaxFiles) {
            fail(QStringLiteral("Plugin bundle has too many files (> %1).").arg(kMaxFiles));
            return;
        }
    }
    if (m_pending.empty()) {
        fail(QStringLiteral("Plugin bundle is empty."));
        return;
    }

    QDir().mkpath(m_stagingDir);
    qCInfo(lcMghInstall) << "assembling" << m_pending.size() << "file(s) for" << m_repoPath
                         << "(bundle root:" << (bundleRoot->isEmpty() ? "<root>" : *bundleRoot)
                         << ")";
    m_idx = 0;
    fetchNextBlob();
}

void MiraboxGithubInstaller::fetchNextBlob() {
    if (m_idx >= m_pending.size()) {
        if (m_done) {
            return;
        }
        m_done = true;
        qCInfo(lcMghInstall) << "assembled" << m_pending.size() << "file(s) into" << m_stagingDir;
        emit finished(true, m_stagingDir, QString{});
        deleteLater();
        return;
    }
    Blob const& b = m_pending[m_idx];
    QNetworkRequest req{QUrl(b.rawUrl)};
    req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("ajazz-control-center"));
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);
    req.setTransferTimeout(kPerRequestTimeoutMs);
    QNetworkReply* const reply = m_net->get(req);
    QPointer<MiraboxGithubInstaller> self(this);
    QString const destRel = b.destRel;
    QObject::connect(reply, &QNetworkReply::finished, this, [self, reply, destRel]() {
        if (self) {
            self->onBlob(reply, destRel);
        } else {
            reply->deleteLater();
        }
    });
}

void MiraboxGithubInstaller::onBlob(QNetworkReply* reply, QString destRel) {
    reply->deleteLater();
    if (reply->error() != QNetworkReply::NoError) {
        fail(QStringLiteral("Download failed for %1: %2").arg(destRel, reply->errorString()));
        return;
    }
    QByteArray const body = reply->readAll();
    m_totalBytes += body.size();
    if (m_totalBytes > kMaxTotalBytes) {
        fail(QStringLiteral("Plugin bundle exceeds the %1 MB limit.")
                 .arg(kMaxTotalBytes / (1024 * 1024)));
        return;
    }

    QString const destPath = QDir(m_stagingDir).filePath(destRel);
    // Defence in depth: the resolved path must stay inside the staging dir even
    // after canonicalisation (isSafeRelPath already rejected `..`, but verify the
    // final location too).
    QString const canonicalBase = QDir(m_stagingDir).absolutePath();
    QString const absDest = QFileInfo(destPath).absoluteFilePath();
    if (!absDest.startsWith(canonicalBase + QLatin1Char('/'))) {
        fail(QStringLiteral("Resolved path escapes the staging directory: %1").arg(destRel));
        return;
    }
    QDir().mkpath(QFileInfo(destPath).absolutePath());
    QFile out(destPath);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        fail(QStringLiteral("Cannot write %1: %2").arg(destPath, out.errorString()));
        return;
    }
    if (out.write(body) != body.size()) {
        out.close();
        out.remove();
        fail(QStringLiteral("Short write to %1").arg(destPath));
        return;
    }
    out.close();

    ++m_idx;
    fetchNextBlob();
}

} // namespace ajazz::app
