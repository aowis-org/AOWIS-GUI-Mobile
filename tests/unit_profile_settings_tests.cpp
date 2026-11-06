#include "unit_profile_settings.h"

#include <QCoreApplication>
#include <QTemporaryDir>
#include <QVariantMap>
#ifdef NDEBUG
#undef NDEBUG // Keep regression assertions enabled in RelWithDebInfo builds.
#endif
#include <cassert>
#include <iostream>

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QTemporaryDir dir;
    assert(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("units.ini"));
    QString customId;
    {
        UnitProfileSettings prefs(nullptr, path);
        assert(prefs.profiles().size() == 12);
        assert(prefs.currentIndex() == 0);
        assert(!prefs.editable());
        assert(!prefs.setUnit(QStringLiteral("length"), QStringLiteral("ft")));
        assert(!prefs.createProfile(QStringLiteral("")));
        assert(!prefs.selectProfile(-1));
        assert(!prefs.selectProfile(99));
        assert(prefs.selectProfile(7)); // CFS: US/Imperial length settings
        assert(prefs.formatAltitude(1,prefs.revision()).contains(QStringLiteral("ft")));
        assert(prefs.createProfile(QStringLiteral("Field US")));
        assert(prefs.editable());
        assert(prefs.profiles().size() == 13);
        customId = prefs.profiles().last().toMap().value(QStringLiteral("id")).toString();
        assert(!customId.isEmpty());
        assert(!prefs.createProfile(QStringLiteral("Field US")));
        assert(!prefs.setUnit(QStringLiteral("length"), QStringLiteral("bogus")));
        assert(prefs.setUnit(QStringLiteral("altitude"), QStringLiteral("m")));
        assert(prefs.formatAltitude(1,prefs.revision()).contains(QStringLiteral(" m")));
        assert(prefs.formatDistance(1,prefs.revision()).contains(QStringLiteral("ft")));
        assert(prefs.renameProfile(QStringLiteral("Survey")));
        assert(prefs.activeName() == QStringLiteral("Survey"));
        const QVariantList fields = prefs.fields(QStringLiteral("pressure"), prefs.revision());
        assert(!fields.isEmpty());
        assert(fields.first().toMap().contains(QStringLiteral("choices")));
        assert(prefs.fields(QStringLiteral("never-a-unit"), prefs.revision()).isEmpty());
    }
    {
        UnitProfileSettings prefs(nullptr, path);
        assert(prefs.editable());
        assert(prefs.activeName() == QStringLiteral("Survey"));
        assert(prefs.profiles().at(prefs.currentIndex()).toMap().value(QStringLiteral("id")).toString() == customId);
        assert(prefs.formatAltitude(1,prefs.revision()).contains(QStringLiteral(" m")));
        assert(prefs.removeProfile());
        assert(!prefs.editable());
        assert(prefs.currentIndex() == 0);
        assert(!prefs.removeProfile());
        assert(!prefs.renameProfile(QStringLiteral("No")));
    }
    {
        UnitProfileSettings prefs(nullptr, path);
        assert(prefs.profiles().size() == 12);
        assert(prefs.currentIndex() == 0);
        assert(prefs.formatSpeed(1,prefs.revision()).contains(QStringLiteral("m/s")));
    }
    std::cout << "Unit profile settings tests passed\n";
    return 0;
}
