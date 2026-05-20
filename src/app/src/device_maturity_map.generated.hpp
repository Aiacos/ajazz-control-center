// SPDX-License-Identifier: GPL-3.0-or-later
// AUTO-GENERATED FILE - DO NOT EDIT.
//
// Regenerated from docs/_data/devices.yaml by
// scripts/generate-docs.py. Re-run that script (or commit with
// the pre-commit hook installed) after editing devices.yaml.
//
// The map's only consumer is src/app/src/device_model.cpp;
// callers there fall back to "scaffolded" for unknown codenames
// so a stale generated header degrades gracefully but loudly
// (the QML sidebar shows the device as scaffolded instead of
// its real maturity).
#pragma once

#include <QHash>
#include <QString>

namespace ajazz::app::generated {

inline QHash<QString, QString> deviceMaturityByCodename() {
    return QHash<QString, QString>{
        {QStringLiteral("akp153"), QStringLiteral("functional")},
        {QStringLiteral("akp153_v1"), QStringLiteral("functional")},
        {QStringLiteral("akp153e"), QStringLiteral("functional")},
        {QStringLiteral("akp153e_v2"), QStringLiteral("functional")},
        {QStringLiteral("akp153r"), QStringLiteral("scaffolded")},
        {QStringLiteral("akp815"), QStringLiteral("probed")},
        {QStringLiteral("akp03"), QStringLiteral("functional")},
        {QStringLiteral("akp03_legacy"), QStringLiteral("functional")},
        {QStringLiteral("akp03e"), QStringLiteral("functional")},
        {QStringLiteral("akp03r"), QStringLiteral("functional")},
        {QStringLiteral("akp03r_rev2"), QStringLiteral("scaffolded")},
        {QStringLiteral("mirabox_n3"), QStringLiteral("partial")},
        {QStringLiteral("mirabox_n3_rev3"), QStringLiteral("scaffolded")},
        {QStringLiteral("mirabox_n3en"), QStringLiteral("scaffolded")},
        {QStringLiteral("akp05"), QStringLiteral("scaffolded")},
        {QStringLiteral("mirabox_n4"), QStringLiteral("scaffolded")},
        {QStringLiteral("akp05e"), QStringLiteral("scaffolded")},
        {QStringLiteral("via_generic"), QStringLiteral("functional")},
        {QStringLiteral("proprietary"), QStringLiteral("functional")},
        {QStringLiteral("ak980pro"), QStringLiteral("partial")},
        {QStringLiteral("aj_series_wired_primary"), QStringLiteral("scaffolded")},
        {QStringLiteral("aj_series_wired_alt"), QStringLiteral("scaffolded")},
        {QStringLiteral("aj_series_wired_alt2"), QStringLiteral("scaffolded")},
        {QStringLiteral("aj_series_dongle"), QStringLiteral("scaffolded")},
        {QStringLiteral("aj159_apex_wired"), QStringLiteral("scaffolded")},
        {QStringLiteral("aj159_apex_24g"), QStringLiteral("scaffolded")},
        {QStringLiteral("aj159_apex_dongle"), QStringLiteral("scaffolded")},
        {QStringLiteral("ajazz_24g_8k"), QStringLiteral("partial")},
        {QStringLiteral("aj199_family"), QStringLiteral("scaffolded")},
        {QStringLiteral("aj199_family_dongle"), QStringLiteral("scaffolded")},
        {QStringLiteral("ak980pro_dongle_24g"), QStringLiteral("probed")},
    };
}

} // namespace ajazz::app::generated
