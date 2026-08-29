#include "dock_profile_store.hpp"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QUuid>

#include <algorithm>
#include <utility>

namespace {
constexpr auto ConfigFormat = "nudock.config";
constexpr auto ProfileFormat = "nudock.profile";
constexpr int SchemaVersion = 2;
constexpr int DockStateVersion = 1;

QString utcNow() {
  return QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
}

bool isUtcTimestamp(const QString &value) {
  const QDateTime parsed = QDateTime::fromString(value, Qt::ISODateWithMs);
  return parsed.isValid() && parsed.offsetFromUtc() == 0;
}

bool writeJsonAtomically(const QString &path, const QJsonObject &object,
                         QString *error) {
  QSaveFile file(path);
  if (!file.open(QIODevice::WriteOnly)) {
    if (error)
      *error = QStringLiteral("Could not open %1 for writing: %2")
                   .arg(path, file.errorString());
    return false;
  }
  const QByteArray bytes = QJsonDocument(object).toJson(QJsonDocument::Indented);
  if (file.write(bytes) != bytes.size() || !file.commit()) {
    if (error)
      *error = QStringLiteral("Could not atomically write %1: %2")
                   .arg(path, file.errorString());
    return false;
  }
  return true;
}
} // namespace

DockProfileStore::DockProfileStore(QString rootDirectory)
    : rootDirectory_(QDir::cleanPath(std::move(rootDirectory))) {}

void DockProfileStore::setError(QString *target, const QString &message) {
  if (target)
    *target = message;
}

QString DockProfileStore::profilesDirectory() const {
  return QDir(rootDirectory_).filePath(QStringLiteral("profiles"));
}

bool DockProfileStore::isValidId(const QString &id) {
  const QUuid uuid(id);
  return !uuid.isNull() &&
         uuid.toString(QUuid::WithoutBraces) == id;
}

bool DockProfileStore::load(QString *error) {
  profiles_.clear();
  mappings_.clear();
  issues_.clear();

  if (rootDirectory_.isEmpty()) {
    setError(error, QStringLiteral("NuDock configuration path is empty."));
    return false;
  }
  if (!QDir().mkpath(profilesDirectory())) {
    setError(error,
             QStringLiteral("Could not create %1.").arg(profilesDirectory()));
    return false;
  }

  const QString configPath =
      QDir(rootDirectory_).filePath(QStringLiteral("config.json"));
  if (!QFileInfo::exists(configPath))
    return true;

  QFile configFile(configPath);
  if (!configFile.open(QIODevice::ReadOnly)) {
    setError(error, QStringLiteral("Could not open %1: %2")
                        .arg(configPath, configFile.errorString()));
    return false;
  }

  QJsonParseError parseError;
  const QJsonDocument document =
      QJsonDocument::fromJson(configFile.readAll(), &parseError);
  const QJsonObject root = document.object();
  if (parseError.error != QJsonParseError::NoError || !document.isObject() ||
      root.value(QStringLiteral("format")).toString() != ConfigFormat ||
      root.value(QStringLiteral("version")).toInt(-1) != SchemaVersion ||
      !root.value(QStringLiteral("profileIds")).isArray() ||
      !root.value(QStringLiteral("obsProfileBindings")).isObject()) {
    setError(error, QStringLiteral("%1 is not a valid NuDock configuration.")
                        .arg(configPath));
    return false;
  }

  QSet<QString> manifestIds;
  QStringList manifestOrder;
  const QJsonArray profileIds =
      root.value(QStringLiteral("profileIds")).toArray();
  for (const QJsonValue &value : profileIds) {
    const QString id = value.isString() ? value.toString() : QString();
    if (!isValidId(id) || manifestIds.contains(id)) {
      setError(error,
               QStringLiteral("%1 has an invalid Dock Profile manifest.")
                   .arg(configPath));
      profiles_.clear();
      mappings_.clear();
      return false;
    }
    manifestIds.insert(id);
    manifestOrder.push_back(id);
  }
  QStringList sortedManifest = manifestOrder;
  sortedManifest.sort(Qt::CaseSensitive);
  if (manifestOrder != sortedManifest) {
    setError(error,
             QStringLiteral("%1 has an unsorted Dock Profile manifest.")
                 .arg(configPath));
    return false;
  }

  QSet<QString> names;
  QStringList orderedIds = manifestIds.values();
  orderedIds.sort(Qt::CaseInsensitive);
  for (const QString &id : orderedIds) {
    const QString path =
        QDir(profilesDirectory()).filePath(id + QStringLiteral(".json"));
    DockProfile loaded;
    QString profileError;
    if (!readProfileFile(path, &loaded, &profileError)) {
      issues_.push_back(
          QStringLiteral("Manifested Dock Profile %1 is missing or invalid: %2")
              .arg(id, profileError));
      continue;
    }
    if (loaded.id != id) {
      issues_.push_back(
          QStringLiteral("Manifested Dock Profile %1 has a mismatched ID.")
              .arg(id));
      continue;
    }
    const QString foldedName = loaded.name.toCaseFolded();
    if (names.contains(foldedName)) {
      issues_.push_back(
          QStringLiteral("Ignored duplicate manifested Dock Profile %1.")
              .arg(id));
      continue;
    }
    profiles_.insert(loaded.id, loaded);
    names.insert(foldedName);
  }

  const QJsonObject bindings =
      root.value(QStringLiteral("obsProfileBindings")).toObject();
  for (auto it = bindings.constBegin(); it != bindings.constEnd(); ++it) {
    if (it.key().trimmed().isEmpty() || !it.value().isString()) {
      issues_.push_back(QStringLiteral("Ignored an invalid OBS profile binding."));
      continue;
    }
    const QString id = it.value().toString();
    if (!isValidId(id) || !profiles_.contains(id)) {
      issues_.push_back(QStringLiteral(
                            "Ignored the missing Dock Profile mapped to OBS profile '%1'.")
                            .arg(it.key()));
      continue;
    }
    mappings_.insert(it.key(), id);
  }
  return true;
}

bool DockProfileStore::readProfileFile(const QString &path,
                                       DockProfile *profile,
                                       QString *error) const {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) {
    setError(error,
             QStringLiteral("Ignored %1: %2").arg(path, file.errorString()));
    return false;
  }

  QJsonParseError parseError;
  const QJsonDocument document =
      QJsonDocument::fromJson(file.readAll(), &parseError);
  const QJsonObject root = document.object();
  if (parseError.error != QJsonParseError::NoError || !document.isObject() ||
      root.value(QStringLiteral("format")).toString() != ProfileFormat ||
      root.value(QStringLiteral("version")).toInt(-1) != SchemaVersion ||
      !root.value(QStringLiteral("id")).isString() ||
      !root.value(QStringLiteral("name")).isString() ||
      !root.value(QStringLiteral("createdAtUtc")).isString() ||
      !root.value(QStringLiteral("updatedAtUtc")).isString() ||
      !root.value(QStringLiteral("obsVersion")).isString() ||
      root.value(QStringLiteral("qtStateVersion")).toInt(-1) !=
          DockStateVersion ||
      !root.value(QStringLiteral("dockIds")).isArray() ||
      !root.value(QStringLiteral("state")).isString()) {
    setError(error,
             QStringLiteral("Ignored invalid Dock Profile file %1.").arg(path));
    return false;
  }

  DockProfile result;
  result.id = root.value(QStringLiteral("id")).toString();
  const QString storedName = root.value(QStringLiteral("name")).toString();
  result.name = storedName.trimmed();
  result.createdAtUtc = root.value(QStringLiteral("createdAtUtc")).toString();
  result.updatedAtUtc = root.value(QStringLiteral("updatedAtUtc")).toString();
  result.obsVersion = root.value(QStringLiteral("obsVersion")).toString();
  result.qtStateVersion = root.value(QStringLiteral("qtStateVersion")).toInt();
  const QJsonArray dockIds = root.value(QStringLiteral("dockIds")).toArray();
  for (const QJsonValue &value : dockIds) {
    if (!value.isString() || value.toString().trimmed().isEmpty()) {
      setError(error,
               QStringLiteral("Ignored invalid Dock Profile file %1.").arg(path));
      return false;
    }
    result.dockIds.push_back(value.toString());
  }

  const auto decoded = QByteArray::fromBase64Encoding(
      root.value(QStringLiteral("state")).toString().toLatin1(),
      QByteArray::AbortOnBase64DecodingErrors);
  if (!isValidId(result.id) || result.name.isEmpty() ||
      result.name != storedName ||
      result.obsVersion.trimmed().isEmpty() ||
      !isUtcTimestamp(result.createdAtUtc) ||
      !isUtcTimestamp(result.updatedAtUtc) || !decoded ||
      decoded.decoded.isEmpty() ||
      result.dockIds != normalizedDockIds(result.dockIds)) {
    setError(error,
             QStringLiteral("Ignored invalid Dock Profile file %1.").arg(path));
    return false;
  }
  result.state = decoded.decoded;
  *profile = result;
  return true;
}

bool DockProfileStore::validateName(const QString &name,
                                    const QString &exceptId,
                                    QString *error) const {
  const QString trimmed = name.trimmed();
  static const QRegularExpression controls(
      QStringLiteral("[\\x{0000}-\\x{001F}\\x{007F}]"));
  if (trimmed.isEmpty() || trimmed.size() > 80 || trimmed.contains(controls)) {
    setError(error, QStringLiteral(
                        "Dock Profile names must be 1-80 printable characters."));
    return false;
  }
  for (auto it = profiles_.constBegin(); it != profiles_.constEnd(); ++it) {
    if (it.key() != exceptId &&
        it.value().name.compare(trimmed, Qt::CaseInsensitive) == 0) {
      setError(error, QStringLiteral("A Dock Profile named '%1' already exists.")
                          .arg(trimmed));
      return false;
    }
  }
  return true;
}

QString DockProfileStore::createProfile(const QString &name,
                                        const QByteArray &state,
                                        const QString &obsVersion,
                                        const QStringList &dockIds,
                                        QString *error) {
  if (!validateName(name, {}, error))
    return {};
  if (state.isEmpty()) {
    setError(error, QStringLiteral("OBS returned an empty dock state."));
    return {};
  }
  if (obsVersion.trimmed().isEmpty()) {
    setError(error, QStringLiteral("OBS version is unavailable."));
    return {};
  }

  DockProfile profile;
  profile.id = QUuid::createUuid().toString(QUuid::WithoutBraces).toLower();
  profile.name = name.trimmed();
  profile.createdAtUtc = utcNow();
  profile.updatedAtUtc = profile.createdAtUtc;
  profile.obsVersion = obsVersion;
  profile.qtStateVersion = DockStateVersion;
  profile.state = state;
  profile.dockIds = normalizedDockIds(dockIds);
  profiles_.insert(profile.id, profile);
  return profile.id;
}

bool DockProfileStore::renameProfile(const QString &id, const QString &name,
                                     QString *error) {
  auto it = profiles_.find(id);
  if (it == profiles_.end()) {
    setError(error,
             QStringLiteral("The selected Dock Profile no longer exists."));
    return false;
  }
  if (!validateName(name, id, error))
    return false;
  it->name = name.trimmed();
  it->updatedAtUtc = utcNow();
  return true;
}

bool DockProfileStore::updateProfileState(const QString &id,
                                          const QByteArray &state,
                                          const QString &obsVersion,
                                          const QStringList &dockIds,
                                          QString *error) {
  auto it = profiles_.find(id);
  if (it == profiles_.end()) {
    setError(error,
             QStringLiteral("The selected Dock Profile no longer exists."));
    return false;
  }
  if (state.isEmpty()) {
    setError(error, QStringLiteral("OBS returned an empty dock state."));
    return false;
  }
  if (obsVersion.trimmed().isEmpty()) {
    setError(error, QStringLiteral("OBS version is unavailable."));
    return false;
  }
  it->state = state;
  it->obsVersion = obsVersion;
  it->qtStateVersion = DockStateVersion;
  it->dockIds = normalizedDockIds(dockIds);
  it->updatedAtUtc = utcNow();
  return true;
}

void DockProfileStore::deleteProfile(const QString &id) {
  profiles_.remove(id);
  for (auto it = mappings_.begin(); it != mappings_.end();) {
    if (it.value() == id)
      it = mappings_.erase(it);
    else
      ++it;
  }
}

QList<DockProfile> DockProfileStore::sortedProfiles() const {
  QList<DockProfile> result = profiles_.values();
  std::sort(result.begin(), result.end(),
            [](const DockProfile &left, const DockProfile &right) {
              return left.name.compare(right.name, Qt::CaseInsensitive) < 0;
            });
  return result;
}

const DockProfile *DockProfileStore::profile(const QString &id) const {
  const auto it = profiles_.constFind(id);
  return it == profiles_.constEnd() ? nullptr : &it.value();
}

QString DockProfileStore::mappingFor(const QString &obsProfileName) const {
  return mappings_.value(obsProfileName);
}

void DockProfileStore::setMapping(const QString &obsProfileName,
                                  const QString &dockProfileId) {
  if (obsProfileName.trimmed().isEmpty())
    return;
  if (dockProfileId.isEmpty() || !profiles_.contains(dockProfileId))
    mappings_.remove(obsProfileName);
  else
    mappings_.insert(obsProfileName, dockProfileId);
}

void DockProfileStore::replaceMappings(
    const QMap<QString, QString> &mappings) {
  mappings_.clear();
  for (auto it = mappings.constBegin(); it != mappings.constEnd(); ++it)
    setMapping(it.key(), it.value());
}

QStringList DockProfileStore::normalizedDockIds(const QStringList &dockIds) {
  QSet<QString> unique;
  for (const QString &id : dockIds) {
    if (!id.trimmed().isEmpty())
      unique.insert(id);
  }
  QStringList result = unique.values();
  result.sort(Qt::CaseSensitive);
  return result;
}

bool DockProfileStore::reconcileObsProfiles(const QStringList &obsProfiles,
                                            const QString &previousCurrent,
                                            const QString &current,
                                            bool allowRenameTransfer) {
  const QSet<QString> live(obsProfiles.begin(), obsProfiles.end());
  bool changed = false;
  if (allowRenameTransfer && !previousCurrent.isEmpty() &&
      previousCurrent != current &&
      !live.contains(previousCurrent) && live.contains(current) &&
      mappings_.contains(previousCurrent)) {
    if (!mappings_.contains(current))
      mappings_.insert(current, mappings_.value(previousCurrent));
    mappings_.remove(previousCurrent);
    changed = true;
  }
  for (auto it = mappings_.begin(); it != mappings_.end();) {
    if (!live.contains(it.key())) {
      it = mappings_.erase(it);
      changed = true;
    } else {
      ++it;
    }
  }
  return changed;
}

bool DockProfileStore::writeProfileFile(const DockProfile &profile,
                                        QString *error) const {
  QJsonObject root;
  root.insert(QStringLiteral("format"), ProfileFormat);
  root.insert(QStringLiteral("version"), SchemaVersion);
  root.insert(QStringLiteral("id"), profile.id);
  root.insert(QStringLiteral("name"), profile.name);
  root.insert(QStringLiteral("createdAtUtc"), profile.createdAtUtc);
  root.insert(QStringLiteral("updatedAtUtc"), profile.updatedAtUtc);
  root.insert(QStringLiteral("obsVersion"), profile.obsVersion);
  root.insert(QStringLiteral("qtStateVersion"), profile.qtStateVersion);
  QJsonArray dockIds;
  for (const QString &id : normalizedDockIds(profile.dockIds))
    dockIds.push_back(id);
  root.insert(QStringLiteral("dockIds"), dockIds);
  root.insert(QStringLiteral("state"),
              QString::fromLatin1(profile.state.toBase64()));
  return writeJsonAtomically(
      QDir(profilesDirectory()).filePath(profile.id + QStringLiteral(".json")),
      root, error);
}

bool DockProfileStore::writeConfigFile(
    const QMap<QString, QString> &mappings, QString *error) const {
  QJsonObject bindings;
  for (auto it = mappings.constBegin(); it != mappings.constEnd(); ++it) {
    if (profiles_.contains(it.value()))
      bindings.insert(it.key(), it.value());
  }
  QJsonArray profileIds;
  for (auto it = profiles_.constBegin(); it != profiles_.constEnd(); ++it)
    profileIds.push_back(it.key());
  QJsonObject root;
  root.insert(QStringLiteral("format"), ConfigFormat);
  root.insert(QStringLiteral("version"), SchemaVersion);
  root.insert(QStringLiteral("profileIds"), profileIds);
  root.insert(QStringLiteral("obsProfileBindings"), bindings);
  return writeJsonAtomically(
      QDir(rootDirectory_).filePath(QStringLiteral("config.json")), root,
      error);
}

bool DockProfileStore::commit(QString *error) const {
  if (rootDirectory_.isEmpty() || !QDir().mkpath(profilesDirectory())) {
    setError(error,
             QStringLiteral("Could not create the NuDock profile directory."));
    return false;
  }
  for (const DockProfile &profile : profiles_) {
    if (!writeProfileFile(profile, error))
      return false;
  }
  if (!writeConfigFile(mappings_, error))
    return false;

  cleanOrphanFiles();
  return true;
}

void DockProfileStore::cleanOrphanFiles() const {
  const QFileInfoList existing = QDir(profilesDirectory())
                                     .entryInfoList({QStringLiteral("*.json")},
                                                    QDir::Files, QDir::Name);
  for (const QFileInfo &file : existing) {
    const QString id = file.completeBaseName();
    if (!profiles_.contains(id))
      QFile::remove(file.absoluteFilePath());
  }
}

bool DockProfileStore::commitMappings(
    const QMap<QString, QString> &mappings, QString *error) {
  QMap<QString, QString> valid;
  for (auto it = mappings.constBegin(); it != mappings.constEnd(); ++it) {
    if (!it.key().trimmed().isEmpty() && profiles_.contains(it.value()))
      valid.insert(it.key(), it.value());
  }
  if (rootDirectory_.isEmpty() || !QDir().mkpath(profilesDirectory())) {
    setError(error,
             QStringLiteral("Could not create the NuDock profile directory."));
    return false;
  }
  if (!writeConfigFile(valid, error))
    return false;
  mappings_ = valid;
  cleanOrphanFiles();
  return true;
}
