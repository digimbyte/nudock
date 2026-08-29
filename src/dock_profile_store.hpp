#pragma once

#include <QByteArray>
#include <QList>
#include <QMap>
#include <QString>
#include <QStringList>

struct DockProfile {
  QString id;
  QString name;
  QString createdAtUtc;
  QString updatedAtUtc;
  QString obsVersion;
  int qtStateVersion = 1;
  QByteArray state;
  QStringList dockIds;
};

class DockProfileStore {
public:
  explicit DockProfileStore(QString rootDirectory = {});

  bool load(QString *error = nullptr);
  bool commit(QString *error = nullptr) const;
  bool commitMappings(const QMap<QString, QString> &mappings,
                      QString *error = nullptr);

  const QMap<QString, DockProfile> &profiles() const { return profiles_; }
  const QMap<QString, QString> &mappings() const { return mappings_; }
  const QStringList &issues() const { return issues_; }
  QList<DockProfile> sortedProfiles() const;
  const DockProfile *profile(const QString &id) const;

  QString createProfile(const QString &name, const QByteArray &state,
                        const QString &obsVersion, const QStringList &dockIds,
                        QString *error = nullptr);
  bool renameProfile(const QString &id, const QString &name,
                     QString *error = nullptr);
  bool updateProfileState(const QString &id, const QByteArray &state,
                          const QString &obsVersion, const QStringList &dockIds,
                          QString *error = nullptr);
  void deleteProfile(const QString &id);

  QString mappingFor(const QString &obsProfileName) const;
  void setMapping(const QString &obsProfileName,
                  const QString &dockProfileId);
  void replaceMappings(const QMap<QString, QString> &mappings);
  bool reconcileObsProfiles(const QStringList &obsProfiles,
                            const QString &previousCurrent,
                            const QString &current,
                            bool allowRenameTransfer = true);

  QString rootDirectory() const { return rootDirectory_; }
  QString profilesDirectory() const;
  static bool isValidId(const QString &id);

private:
  QString rootDirectory_;
  QMap<QString, DockProfile> profiles_;
  QMap<QString, QString> mappings_;
  QStringList issues_;

  bool validateName(const QString &name, const QString &exceptId,
                    QString *error) const;
  bool readProfileFile(const QString &path, DockProfile *profile,
                       QString *error) const;
  bool writeProfileFile(const DockProfile &profile, QString *error) const;
  bool writeConfigFile(const QMap<QString, QString> &mappings,
                       QString *error) const;
  void cleanOrphanFiles() const;
  static QStringList normalizedDockIds(const QStringList &dockIds);
  static void setError(QString *target, const QString &message);
};
