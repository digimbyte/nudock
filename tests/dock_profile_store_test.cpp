#include "dock_profile_store.hpp"
#include "dock_restore_coordinator.hpp"
#include "dock_state_restorer.hpp"

#include <QApplication>
#include <QDir>
#include <QDockWidget>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QMainWindow>
#include <QMap>
#include <QTemporaryDir>

#include <functional>
#include <iostream>
#include <stdexcept>
#include <utility>
#include <vector>

namespace {
struct ScheduledRestore {
  int delay = 0;
  std::function<void()> callback;
};

void require(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}

QDockWidget *addTestDock(QMainWindow &window, const QString &id,
                         Qt::DockWidgetArea area) {
  auto *dock = new QDockWidget(id, &window);
  dock->setObjectName(id);
  dock->setWidget(new QLabel(id, dock));
  window.addDockWidget(area, dock);
  return dock;
}

void runScheduled(std::vector<ScheduledRestore> &scheduled) {
  std::vector<ScheduledRestore> pending = std::move(scheduled);
  scheduled.clear();
  for (ScheduledRestore &task : pending)
    task.callback();
}

void writeJson(const QString &path, const QJsonObject &object) {
  QFile file(path);
  require(file.open(QIODevice::WriteOnly), "JSON fixture open failed");
  require(file.write(QJsonDocument(object).toJson()) > 0,
          "JSON fixture write failed");
}

void crudRoundTripAndSharedMappings() {
  QTemporaryDir directory(
      QDir::current().filePath(QStringLiteral("nudock-tests-XXXXXX")));
  require(directory.isValid(), "temporary directory creation failed");
  DockProfileStore store(directory.path());
  QString error;
  require(store.load(&error), qPrintable(QStringLiteral("initial load: %1").arg(error)));
  const QString mainId =
      store.createProfile(QStringLiteral("Main"), QByteArray("state-main"),
                          QStringLiteral("32.1.0"),
                          {QStringLiteral("core"), QStringLiteral("custom")},
                          &error);
  const QString editId =
      store.createProfile(QStringLiteral("Editing"), QByteArray("state-edit"),
                          QStringLiteral("32.1.0"), {QStringLiteral("edit")},
                          &error);
  require(!mainId.isEmpty(), qPrintable(error));
  require(!editId.isEmpty(), qPrintable(error));
  store.setMapping(QStringLiteral("Streaming"), mainId);
  store.setMapping(QStringLiteral("Recording"), mainId);
  require(store.commit(&error), qPrintable(QStringLiteral("first commit: %1").arg(error)));
  QFile configFile(QDir(directory.path()).filePath(QStringLiteral("config.json")));
  require(configFile.open(QIODevice::ReadOnly), "schema v2 config was not written");
  const QJsonObject config =
      QJsonDocument::fromJson(configFile.readAll()).object();
  configFile.close();
  require(config.value(QStringLiteral("version")).toInt() == 2,
          "config schema version is not 2");
  require(config.value(QStringLiteral("profileIds")).toArray().size() == 2,
          "config profile manifest was not committed");

  DockProfileStore loaded(directory.path());
  require(loaded.load(&error), qPrintable(QStringLiteral("round-trip load: %1").arg(error)));
  require(loaded.profiles().size() == 2, "profile count did not round-trip");
  require(loaded.mappingFor(QStringLiteral("Streaming")) == mainId,
          "first shared mapping did not round-trip");
  require(loaded.mappingFor(QStringLiteral("Recording")) == mainId,
          "second shared mapping did not round-trip");
  require(loaded.profile(editId) &&
              loaded.profile(editId)->state == QByteArray("state-edit"),
          "profile state did not round-trip");
  require(loaded.renameProfile(editId, QStringLiteral("Production"), &error),
          qPrintable(error));
  require(loaded.updateProfileState(editId, QByteArray("state-new"),
                                    QStringLiteral("32.2.2"),
                                    {QStringLiteral("z"), QStringLiteral("a"),
                                     QStringLiteral("a")},
                                    &error),
          qPrintable(error));
  require(loaded.commit(&error), qPrintable(QStringLiteral("update commit: %1").arg(error)));

  DockProfileStore reloaded(directory.path());
  require(reloaded.load(&error), qPrintable(QStringLiteral("updated load: %1").arg(error)));
  require(reloaded.profile(editId) &&
              reloaded.profile(editId)->name == QStringLiteral("Production"),
          "renamed profile did not round-trip");
  require(reloaded.profile(editId)->state == QByteArray("state-new"),
          "updated profile state did not round-trip");
  require(reloaded.profile(editId)->dockIds ==
              QStringList({QStringLiteral("a"), QStringLiteral("z")}),
          "sorted unique dock inventory did not round-trip");
  require(QDir(directory.path())
              .entryList({QStringLiteral("*.tmp")}, QDir::Files)
              .isEmpty(),
          "atomic writes left temporary files behind");
}

void duplicateAndEmptyStatesAreRejected() {
  QTemporaryDir directory(
      QDir::current().filePath(QStringLiteral("nudock-tests-XXXXXX")));
  DockProfileStore store(directory.path());
  QString error;
  require(store.load(&error), qPrintable(error));
  require(!store
               .createProfile(QStringLiteral("Main"), QByteArray("state"),
                              QStringLiteral("32.1.0"), {}, &error)
               .isEmpty(),
          qPrintable(error));
  require(store
              .createProfile(QStringLiteral("main"), QByteArray("other"),
                             QStringLiteral("32.1.0"), {}, &error)
              .isEmpty(),
          "case-insensitive duplicate name was accepted");
  require(!error.isEmpty(), "duplicate name did not report an error");
  require(store
              .createProfile(QStringLiteral("Empty"), {},
                             QStringLiteral("32.1.0"), {}, &error)
              .isEmpty(),
          "empty dock state was accepted");
}

void deletionClearsMappingsAndFiles() {
  QTemporaryDir directory(
      QDir::current().filePath(QStringLiteral("nudock-tests-XXXXXX")));
  DockProfileStore store(directory.path());
  QString error;
  require(store.load(&error), qPrintable(error));
  const QString id =
      store.createProfile(QStringLiteral("Temporary"), QByteArray("state"),
                          QStringLiteral("32.1.0"), {}, &error);
  store.setMapping(QStringLiteral("One"), id);
  store.setMapping(QStringLiteral("Two"), id);
  require(store.commit(&error), qPrintable(error));
  const QString filePath =
      QDir(store.profilesDirectory()).filePath(id + QStringLiteral(".json"));
  require(QFileInfo::exists(filePath), "profile file was not written");

  store.deleteProfile(id);
  require(store.mappings().isEmpty(), "deletion left mappings behind");
  require(store.commit(&error), qPrintable(error));
  require(!QFileInfo::exists(filePath), "deleted profile file remained");
}

void renameTransferIsDistinctFromDeletion() {
  QTemporaryDir directory(
      QDir::current().filePath(QStringLiteral("nudock-tests-XXXXXX")));
  DockProfileStore store(directory.path());
  QString error;
  require(store.load(&error), qPrintable(error));
  const QString id = store.createProfile(
      QStringLiteral("Main"), QByteArray("state"), QStringLiteral("32.1.0"),
      {});
  store.setMapping(QStringLiteral("Old Name"), id);
  require(store.reconcileObsProfiles({QStringLiteral("New Name")},
                                     QStringLiteral("Old Name"),
                                     QStringLiteral("New Name"), true),
          "rename did not change mappings");
  require(store.mappingFor(QStringLiteral("New Name")) == id,
          "rename did not transfer mapping");

  store.setMapping(QStringLiteral("Deleted"), id);
  require(store.reconcileObsProfiles({QStringLiteral("New Name")},
                                     QStringLiteral("Deleted"),
                                     QStringLiteral("New Name"), false),
          "deletion did not change mappings");
  require(store.mappingFor(QStringLiteral("Deleted")).isEmpty(),
          "deleted OBS profile mapping remained");
  require(store.mappingFor(QStringLiteral("New Name")) == id,
          "unrelated mapping changed during deletion");
}

void schemaV1IsRejectedWithoutImport() {
  QTemporaryDir directory(
      QDir::current().filePath(QStringLiteral("nudock-tests-XXXXXX")));
  QJsonObject config;
  config.insert(QStringLiteral("format"), QStringLiteral("nudock.config"));
  config.insert(QStringLiteral("version"), 1);
  config.insert(QStringLiteral("obsProfileBindings"), QJsonObject{});
  writeJson(QDir(directory.path()).filePath("config.json"), config);

  DockProfileStore store(directory.path());
  QString error;
  require(!store.load(&error), "schema v1 configuration was imported");
  require(store.profiles().isEmpty(), "schema v1 profiles were imported");
  require(!error.isEmpty(), "schema v1 rejection was not reported");
}

void manifestControlsVisibilityAndOrphanCleanup() {
  QTemporaryDir directory(
      QDir::current().filePath(QStringLiteral("nudock-tests-XXXXXX")));
  DockProfileStore store(directory.path());
  QString error;
  require(store.load(&error), qPrintable(error));
  const QString id = store.createProfile(
      QStringLiteral("Main"), QByteArray("state"), QStringLiteral("32.1.0"),
      {QStringLiteral("coreDock")}, &error);
  store.setMapping(QStringLiteral("OBS"), id);
  require(store.commit(&error), qPrintable(error));

  const QString orphanId = QStringLiteral("22222222-2222-2222-2222-222222222222");
  const QString orphanPath =
      QDir(store.profilesDirectory()).filePath(orphanId + QStringLiteral(".json"));
  QFile::copy(QDir(store.profilesDirectory())
                  .filePath(id + QStringLiteral(".json")),
              orphanPath);

  DockProfileStore loaded(directory.path());
  require(loaded.load(&error), qPrintable(error));
  require(loaded.profiles().size() == 1,
          "unmanifested interrupted-create file was loaded");
  require(loaded.commit(&error), qPrintable(error));
  require(!QFileInfo::exists(orphanPath), "orphan profile file was not cleaned");

  require(QFile::remove(QDir(store.profilesDirectory())
                            .filePath(id + QStringLiteral(".json"))),
          "manifested profile fixture removal failed");
  DockProfileStore missing(directory.path());
  require(missing.load(&error), qPrintable(error));
  require(missing.profiles().isEmpty(), "missing manifested profile was loaded");
  require(missing.mappings().isEmpty(),
          "mapping to missing manifested profile was loaded");
  require(!missing.issues().isEmpty(),
          "missing manifested profile was not reported");
}

void corruptManifestedProfileIsRejected() {
  QTemporaryDir directory(
      QDir::current().filePath(QStringLiteral("nudock-tests-XXXXXX")));
  DockProfileStore store(directory.path());
  QString error;
  require(store.load(&error), qPrintable(error));
  const QString id = store.createProfile(
      QStringLiteral("Corrupt Me"), QByteArray("state"),
      QStringLiteral("32.1.0"), {QStringLiteral("dock")}, &error);
  store.setMapping(QStringLiteral("OBS"), id);
  require(store.commit(&error), qPrintable(error));
  writeJson(QDir(store.profilesDirectory())
                .filePath(id + QStringLiteral(".json")),
            QJsonObject{{QStringLiteral("format"),
                         QStringLiteral("nudock.profile")},
                        {QStringLiteral("version"), 2}});

  DockProfileStore loaded(directory.path());
  require(loaded.load(&error), qPrintable(error));
  require(loaded.profiles().isEmpty(), "corrupt manifested profile was loaded");
  require(loaded.mappings().isEmpty(),
          "mapping to corrupt manifested profile was loaded");
  require(!loaded.issues().isEmpty(),
          "corrupt manifested profile was not reported");
}

void immediateProfileMutationsOutliveUnappliedAssignments() {
  QTemporaryDir directory(
      QDir::current().filePath(QStringLiteral("nudock-tests-XXXXXX")));
  DockProfileStore persisted(directory.path());
  QString error;
  require(persisted.load(&error), qPrintable(error));
  const QString firstId = persisted.createProfile(
      QStringLiteral("First"), QByteArray("first"), QStringLiteral("32.1.0"),
      {QStringLiteral("core")}, &error);
  persisted.setMapping(QStringLiteral("OBS"), firstId);
  require(persisted.commit(&error), qPrintable(error));

  DockProfileStore working = persisted;
  working.setMapping(QStringLiteral("OBS"), {});
  const QMap<QString, QString> stagedAssignments = working.mappings();
  const QString secondId = working.createProfile(
      QStringLiteral("Second"), QByteArray("second"),
      QStringLiteral("32.1.0"), {QStringLiteral("custom")}, &error);
  working.replaceMappings(persisted.mappings());
  require(working.commit(&error), qPrintable(error));
  persisted = working;
  working.replaceMappings(stagedAssignments);

  DockProfileStore afterCancel(directory.path());
  require(afterCancel.load(&error), qPrintable(error));
  require(afterCancel.profile(secondId),
          "immediate Create did not survive assignment Cancel");
  require(afterCancel.mappingFor(QStringLiteral("OBS")) == firstId,
          "unapplied assignment edit survived Cancel");

  require(persisted.renameProfile(secondId, QStringLiteral("Renamed"), &error),
          qPrintable(error));
  require(persisted.updateProfileState(
              secondId, QByteArray("updated"), QStringLiteral("32.2.2"),
              {QStringLiteral("late"), QStringLiteral("core")}, &error),
          qPrintable(error));
  require(persisted.commit(&error), qPrintable(error));
  persisted.deleteProfile(firstId);
  require(persisted.commit(&error), qPrintable(error));

  DockProfileStore finalStore(directory.path());
  require(finalStore.load(&error), qPrintable(error));
  require(!finalStore.profile(firstId), "immediate Delete was not durable");
  require(finalStore.mappings().isEmpty(),
          "immediate Delete did not clear persisted assignments");
  require(finalStore.profile(secondId) &&
              finalStore.profile(secondId)->name == QStringLiteral("Renamed") &&
              finalStore.profile(secondId)->state == QByteArray("updated"),
          "immediate Rename or Save Current was not durable");
}

void mappedProfileTransitionsUseCurrentSnapshot() {
  QString currentProfile = QStringLiteral("Profile A");
  QMap<QString, QString> mappings{
      {QStringLiteral("Profile A"), QStringLiteral("dock-a")},
      {QStringLiteral("Profile B"), QStringLiteral("dock-b")},
  };
  QMap<QString, DockRestoreSnapshot> snapshots{
      {QStringLiteral("dock-a"),
       {QStringLiteral("dock-a"), QStringLiteral("Layout A"), 1,
        QByteArray("state-a"), {QStringLiteral("core")}}},
      {QStringLiteral("dock-b"),
       {QStringLiteral("dock-b"), QStringLiteral("Layout B"), 1,
        QByteArray("state-b"), {QStringLiteral("core")}}},
  };
  std::vector<ScheduledRestore> scheduled;
  std::vector<QByteArray> restored;
  QStringList errors;

  DockRestoreCoordinator coordinator(
      [&]() { return currentProfile; },
      [&](const QString &profile) { return mappings.value(profile); },
      [&](const QString &id) -> std::optional<DockRestoreSnapshot> {
        const auto it = snapshots.constFind(id);
        return it == snapshots.constEnd()
                   ? std::nullopt
                   : std::optional<DockRestoreSnapshot>(*it);
      },
      [&](const QByteArray &state, int version, QString *) {
        require(version == 1, "unexpected Qt state version");
        restored.push_back(state);
        return true;
      },
      []() { return QStringList{QStringLiteral("core")}; },
      [](const QStringList &, QString *) { return true; },
      [&](int delay, std::function<void()> callback) {
        scheduled.push_back({delay, std::move(callback)});
      },
      [&](const QString &error) { errors.push_back(error); });

  coordinator.scheduleForCurrentProfile();
  require(scheduled.size() == 1, "assigned profile did not schedule one restore");
  require(scheduled[0].delay == 0, "initial restore was not immediate");
  runScheduled(scheduled);
  require(restored.size() == 1, "complete snapshot restored more than once");
  for (const QByteArray &state : restored)
    require(state == QByteArray("state-a"),
            "Profile A restored the wrong Dock Profile");

  restored.clear();
  mappings.remove(QStringLiteral("Profile A"));
  coordinator.scheduleForCurrentProfile();
  require(scheduled.empty(), "Keep Current scheduled a restoration");
  require(restored.empty(), "Keep Current changed the layout");

  mappings.insert(QStringLiteral("Profile A"), QStringLiteral("dock-b"));
  coordinator.scheduleForCurrentProfile();
  runScheduled(scheduled);
  require(restored.size() == 1, "active mapping change was not restored once");
  for (const QByteArray &state : restored)
    require(state == QByteArray("state-b"),
            "active mapping change restored the old snapshot");

  restored.clear();
  mappings.insert(QStringLiteral("Profile A"), QStringLiteral("dock-a"));
  coordinator.scheduleForCurrentProfile();
  currentProfile = QStringLiteral("Profile B");
  coordinator.scheduleForCurrentProfile();
  runScheduled(scheduled);
  require(restored.size() == 1,
          "rapid switch did not cancel exactly the stale restore");
  for (const QByteArray &state : restored)
    require(state == QByteArray("state-b"),
            "stale Profile A retry overwrote Profile B");
  require(errors.isEmpty(), "valid mapped transitions reported an error");
}

void missingSnapshotIsReportedOnce() {
  std::vector<ScheduledRestore> scheduled;
  QStringList errors;
  DockRestoreCoordinator coordinator(
      []() { return QStringLiteral("OBS"); },
      [](const QString &) { return QStringLiteral("missing"); },
      [](const QString &) -> std::optional<DockRestoreSnapshot> {
        return std::nullopt;
      },
      [](const QByteArray &, int, QString *) { return true; },
      []() { return QStringList{}; },
      [](const QStringList &, QString *) { return true; },
      [&](int delay, std::function<void()> callback) {
        scheduled.push_back({delay, std::move(callback)});
      },
      [&](const QString &error) { errors.push_back(error); });
  coordinator.scheduleForCurrentProfile();
  runScheduled(scheduled);
  require(errors.size() == 1, "missing snapshot error was not deduplicated");
}

void customDockLayoutRoundTrips() {
  QMainWindow window;
  window.resize(900, 600);
  auto *core =
      addTestDock(window, QStringLiteral("coreDock"), Qt::LeftDockWidgetArea);
  auto *custom = addTestDock(window, QStringLiteral("plugin.custom.primary"),
                             Qt::RightDockWidgetArea);
  auto *tabbed = addTestDock(window, QStringLiteral("plugin.custom.tabbed"),
                             Qt::RightDockWidgetArea);
  auto *hidden = addTestDock(window, QStringLiteral("plugin.custom.hidden"),
                             Qt::BottomDockWidgetArea);
  auto *floating = addTestDock(window, QStringLiteral("plugin.custom.floating"),
                               Qt::BottomDockWidgetArea);
  window.tabifyDockWidget(custom, tabbed);
  window.show();
  hidden->hide();
  floating->setFloating(true);
  floating->show();
  tabbed->raise();
  QApplication::processEvents();
  const QByteArray saved = window.saveState(1);
  require(!saved.isEmpty(), "custom dock snapshot was empty");

  window.addDockWidget(Qt::BottomDockWidgetArea, core);
  window.addDockWidget(Qt::LeftDockWidgetArea, custom);
  window.addDockWidget(Qt::BottomDockWidgetArea, tabbed);
  hidden->show();
  floating->setFloating(false);
  window.addDockWidget(Qt::LeftDockWidgetArea, floating);
  QApplication::processEvents();

  QString error;
  require(restoreDockStateTransactional(window, saved, 1, &error),
          qPrintable(error));
  QApplication::processEvents();
  require(window.dockWidgetArea(core) == Qt::LeftDockWidgetArea,
          "core dock area was not restored");
  require(window.dockWidgetArea(custom) == Qt::RightDockWidgetArea,
          "custom dock area was not restored");
  require(window.tabifiedDockWidgets(custom).contains(tabbed),
          "custom dock tab group was not restored");
  require(!hidden->isVisible(), "custom dock visibility was not restored");
  require(floating->isFloating(),
          "custom dock floating state was not restored");
}

void lateCustomDockIsRestoredByRetry() {
  QMainWindow source;
  source.resize(800, 500);
  addTestDock(source, QStringLiteral("coreDock"), Qt::LeftDockWidgetArea);
  addTestDock(source, QStringLiteral("plugin.custom.late"),
              Qt::RightDockWidgetArea);
  source.show();
  QApplication::processEvents();
  const QByteArray state = source.saveState(1);

  QMainWindow target;
  target.resize(800, 500);
  auto *targetCore =
      addTestDock(target, QStringLiteral("coreDock"), Qt::LeftDockWidgetArea);
  target.show();
  QApplication::processEvents();

  std::vector<ScheduledRestore> scheduled;
  int fullRestores = 0;
  int targetedRestores = 0;
  DockRestoreCoordinator coordinator(
      []() { return QStringLiteral("OBS"); },
      [](const QString &) { return QStringLiteral("layout"); },
      [&](const QString &) -> std::optional<DockRestoreSnapshot> {
        return DockRestoreSnapshot{QStringLiteral("layout"),
                                   QStringLiteral("Layout"), 1, state,
                                   {QStringLiteral("coreDock"),
                                    QStringLiteral("plugin.custom.late")}};
      },
      [&](const QByteArray &restoreState, int version, QString *error) {
        ++fullRestores;
        return restoreDockStateTransactional(target, restoreState, version,
                                             error);
      },
      [&]() {
        QStringList ids;
        for (QDockWidget *dock : target.findChildren<QDockWidget *>())
          ids.push_back(dock->objectName());
        return ids;
      },
      [&](const QStringList &ids, QString *error) {
        ++targetedRestores;
        for (const QString &id : ids) {
          QDockWidget *dock = target.findChild<QDockWidget *>(id);
          if (!dock || !target.restoreDockWidget(dock)) {
            if (error)
              *error = QStringLiteral("targeted restore failed");
            return false;
          }
        }
        return true;
      },
      [&](int delay, std::function<void()> callback) {
        scheduled.push_back({delay, std::move(callback)});
      },
      [](const QString &) {});

  coordinator.scheduleForCurrentProfile();
  require(scheduled.size() == 1, "initial restore was not singular");
  runScheduled(scheduled);
  require(scheduled.size() == 1 && scheduled[0].delay == 250,
          "missing dock did not start inventory polling");
  target.addDockWidget(Qt::BottomDockWidgetArea, targetCore);
  auto *late = new QDockWidget(QStringLiteral("plugin.custom.late"), &target);
  late->setObjectName(QStringLiteral("plugin.custom.late"));
  late->setWidget(new QLabel(QStringLiteral("late"), late));
  QApplication::processEvents();
  runScheduled(scheduled);
  QApplication::processEvents();
  require(fullRestores == 1, "late dock caused an unnecessary full snap");
  require(targetedRestores == 1, "late dock did not use targeted restoration");
  require(target.dockWidgetArea(late) == Qt::RightDockWidgetArea,
          "late custom dock did not restore during grace polling");
  require(target.dockWidgetArea(targetCore) == Qt::BottomDockWidgetArea,
          "targeted late restore overwrote a temporary existing-dock edit");
}

void targetedFailureUsesOneFullFallback() {
  std::vector<ScheduledRestore> scheduled;
  bool latePresent = false;
  int fullRestores = 0;
  int targetedRestores = 0;
  DockRestoreCoordinator coordinator(
      []() { return QStringLiteral("OBS"); },
      [](const QString &) { return QStringLiteral("layout"); },
      [](const QString &) -> std::optional<DockRestoreSnapshot> {
        return DockRestoreSnapshot{QStringLiteral("layout"),
                                   QStringLiteral("Layout"), 1,
                                   QByteArray("state"),
                                   {QStringLiteral("late")}};
      },
      [&](const QByteArray &, int, QString *) {
        ++fullRestores;
        return true;
      },
      [&]() {
        return latePresent ? QStringList{QStringLiteral("late")}
                           : QStringList{};
      },
      [&](const QStringList &, QString *error) {
        ++targetedRestores;
        if (error)
          *error = QStringLiteral("forced targeted failure");
        return false;
      },
      [&](int delay, std::function<void()> callback) {
        scheduled.push_back({delay, std::move(callback)});
      },
      [](const QString &) {});

  coordinator.scheduleForCurrentProfile();
  runScheduled(scheduled);
  latePresent = true;
  runScheduled(scheduled);
  require(targetedRestores == 1, "late dock targeted path was not attempted");
  require(fullRestores == 2,
          "targeted failure did not perform exactly one full fallback");
  require(scheduled.empty(), "resolved fallback left polling active");
}

void unresolvedDocksExpireAndReportOnce() {
  std::vector<ScheduledRestore> scheduled;
  QStringList errors;
  int fullRestores = 0;
  DockRestoreCoordinator coordinator(
      []() { return QStringLiteral("OBS"); },
      [](const QString &) { return QStringLiteral("layout"); },
      [](const QString &) -> std::optional<DockRestoreSnapshot> {
        return DockRestoreSnapshot{QStringLiteral("layout"),
                                   QStringLiteral("Layout"), 1,
                                   QByteArray("state"),
                                   {QStringLiteral("never-registered")}};
      },
      [&](const QByteArray &, int, QString *) {
        ++fullRestores;
        return true;
      },
      []() { return QStringList{}; },
      [](const QStringList &, QString *) { return true; },
      [&](int delay, std::function<void()> callback) {
        scheduled.push_back({delay, std::move(callback)});
      },
      [&](const QString &error) { errors.push_back(error); });

  coordinator.scheduleForCurrentProfile();
  int batches = 0;
  while (!scheduled.empty() && batches < 30) {
    runScheduled(scheduled);
    ++batches;
  }
  require(batches == 21, "grace polling did not stop at five seconds");
  require(fullRestores == 1, "inventory polling changed the layout");
  require(errors.size() == 1, "unresolved docks were not reported once");
  require(errors.front().contains(QStringLiteral("never-registered")),
          "unresolved report omitted the dock ID");
  require(scheduled.empty(), "grace polling continued after expiration");
}

void rapidSwitchCancelsStaleInventoryPolling() {
  QString current = QStringLiteral("A");
  std::vector<ScheduledRestore> scheduled;
  std::vector<QByteArray> restored;
  int targeted = 0;
  DockRestoreCoordinator coordinator(
      [&]() { return current; },
      [](const QString &profile) { return profile.toLower(); },
      [](const QString &id) -> std::optional<DockRestoreSnapshot> {
        return DockRestoreSnapshot{id, id, 1, id.toUtf8(),
                                   id == QStringLiteral("a")
                                       ? QStringList{QStringLiteral("late-a")}
                                       : QStringList{}};
      },
      [&](const QByteArray &state, int, QString *) {
        restored.push_back(state);
        return true;
      },
      []() { return QStringList{}; },
      [&](const QStringList &, QString *) {
        ++targeted;
        return true;
      },
      [&](int delay, std::function<void()> callback) {
        scheduled.push_back({delay, std::move(callback)});
      },
      [](const QString &) {});

  coordinator.scheduleForCurrentProfile();
  runScheduled(scheduled);
  require(restored == std::vector<QByteArray>{QByteArray("a")},
          "Profile A initial restore failed");
  current = QStringLiteral("B");
  coordinator.scheduleForCurrentProfile();
  runScheduled(scheduled);
  require(restored ==
              std::vector<QByteArray>{QByteArray("a"), QByteArray("b")},
          "stale polling restored the wrong profile");
  require(targeted == 0, "stale Profile A polling used targeted restore");
  require(scheduled.empty(), "stale polling survived rapid switch");
}

void transactionalRestoreRollsBackInvalidState() {
  QMainWindow window;
  auto *left = new QDockWidget(QStringLiteral("Left"), &window);
  left->setObjectName(QStringLiteral("leftDock"));
  left->setWidget(new QLabel(QStringLiteral("Left"), left));
  auto *right = new QDockWidget(QStringLiteral("Right"), &window);
  right->setObjectName(QStringLiteral("rightDock"));
  right->setWidget(new QLabel(QStringLiteral("Right"), right));
  window.addDockWidget(Qt::LeftDockWidgetArea, left);
  window.addDockWidget(Qt::RightDockWidgetArea, right);
  const QByteArray baseline = window.saveState(1);
  require(!baseline.isEmpty(), "baseline state was empty");

  window.addDockWidget(Qt::BottomDockWidgetArea, left);
  const QByteArray alternate = window.saveState(1);
  require(window.restoreState(baseline, 1), "baseline restore failed");
  QString error;
  require(restoreDockStateTransactional(window, alternate, 1, &error),
          qPrintable(error));

  const QByteArray beforeInvalid = window.saveState(1);
  require(
      !restoreDockStateTransactional(window, QByteArray("invalid"), 1, &error),
      "invalid state was accepted");
  require(window.saveState(1) == beforeInvalid,
          "invalid restore did not roll back the layout");
  require(!error.isEmpty(), "invalid restore did not report an error");
}
} // namespace

int main(int argc, char **argv) {
  QApplication application(argc, argv);
  const std::vector<std::pair<const char *, std::function<void()>>> tests = {
      {"CRUD, serialization, atomic commit, shared mappings",
       crudRoundTripAndSharedMappings},
      {"duplicate names and empty states", duplicateAndEmptyStatesAreRejected},
      {"profile deletion", deletionClearsMappingsAndFiles},
      {"OBS profile rename and deletion reconciliation",
       renameTransferIsDistinctFromDeletion},
      {"schema v1 rejection", schemaV1IsRejectedWithoutImport},
      {"manifest visibility, missing profiles, and orphan cleanup",
       manifestControlsVisibilityAndOrphanCleanup},
      {"corrupt manifested profile rejection",
       corruptManifestedProfileIsRejected},
      {"immediate profile persistence with canceled assignments",
       immediateProfileMutationsOutliveUnappliedAssignments},
      {"mapped profile transitions and rapid switching",
       mappedProfileTransitionsUseCurrentSnapshot},
      {"missing snapshot error reporting", missingSnapshotIsReportedOnce},
      {"custom dock layout round-trip", customDockLayoutRoundTrips},
      {"late custom dock retry", lateCustomDockIsRestoredByRetry},
      {"targeted failure full fallback", targetedFailureUsesOneFullFallback},
      {"unresolved dock grace expiration", unresolvedDocksExpireAndReportOnce},
      {"rapid switch cancels inventory polling",
       rapidSwitchCancelsStaleInventoryPolling},
      {"transactional restore rollback",
       transactionalRestoreRollsBackInvalidState},
  };

  int failures = 0;
  for (const auto &[name, test] : tests) {
    std::cout << "RUN: " << name << std::endl;
    try {
      test();
      std::cout << "PASS: " << name << std::endl;
    } catch (const std::exception &exception) {
      ++failures;
      std::cerr << "FAIL: " << name << ": " << exception.what() << std::endl;
    }
  }
  return failures == 0 ? 0 : 1;
}
