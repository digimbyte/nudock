#include "dock_profile_store.hpp"
#include "dock_restore_coordinator.hpp"
#include "dock_state_restorer.hpp"

#include <QApplication>
#include <QDir>
#include <QDockWidget>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
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

void crudRoundTripAndSharedMappings() {
  QTemporaryDir directory(
      QDir::current().filePath(QStringLiteral("nudock-tests-XXXXXX")));
  require(directory.isValid(), "temporary directory creation failed");
  DockProfileStore store(directory.path());
  QString error;
  require(store.load(&error), qPrintable(error));
  const QString mainId =
      store.createProfile(QStringLiteral("Main"), QByteArray("state-main"),
                          QStringLiteral("32.1.0"), &error);
  const QString editId =
      store.createProfile(QStringLiteral("Editing"), QByteArray("state-edit"),
                          QStringLiteral("32.1.0"), &error);
  require(!mainId.isEmpty(), qPrintable(error));
  require(!editId.isEmpty(), qPrintable(error));
  store.setMapping(QStringLiteral("Streaming"), mainId);
  store.setMapping(QStringLiteral("Recording"), mainId);
  require(store.commit(&error), qPrintable(error));

  DockProfileStore loaded(directory.path());
  require(loaded.load(&error), qPrintable(error));
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
                                    QStringLiteral("32.2.2"), &error),
          qPrintable(error));
  require(loaded.commit(&error), qPrintable(error));

  DockProfileStore reloaded(directory.path());
  require(reloaded.load(&error), qPrintable(error));
  require(reloaded.profile(editId) &&
              reloaded.profile(editId)->name == QStringLiteral("Production"),
          "renamed profile did not round-trip");
  require(reloaded.profile(editId)->state == QByteArray("state-new"),
          "updated profile state did not round-trip");
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
                              QStringLiteral("32.1.0"), &error)
               .isEmpty(),
          qPrintable(error));
  require(store
              .createProfile(QStringLiteral("main"), QByteArray("other"),
                             QStringLiteral("32.1.0"), &error)
              .isEmpty(),
          "case-insensitive duplicate name was accepted");
  require(!error.isEmpty(), "duplicate name did not report an error");
  require(store
              .createProfile(QStringLiteral("Empty"), {},
                             QStringLiteral("32.1.0"), &error)
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
                          QStringLiteral("32.1.0"), &error);
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
      QStringLiteral("Main"), QByteArray("state"), QStringLiteral("32.1.0"));
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

void corruptAndMissingProfilesAreReported() {
  QTemporaryDir directory(
      QDir::current().filePath(QStringLiteral("nudock-tests-XXXXXX")));
  require(QDir().mkpath(QDir(directory.path()).filePath("profiles")),
          "profile directory creation failed");

  QFile corrupt(QDir(directory.path()).filePath("profiles/not-a-profile.json"));
  require(corrupt.open(QIODevice::WriteOnly), "corrupt fixture open failed");
  corrupt.write("{not-json");
  corrupt.close();

  QJsonObject bindings;
  bindings.insert(QStringLiteral("OBS"),
                  QStringLiteral("11111111-1111-1111-1111-111111111111"));
  QJsonObject config;
  config.insert(QStringLiteral("format"), QStringLiteral("nudock.config"));
  config.insert(QStringLiteral("version"), 1);
  config.insert(QStringLiteral("obsProfileBindings"), bindings);
  QFile configFile(QDir(directory.path()).filePath("config.json"));
  require(configFile.open(QIODevice::WriteOnly), "config fixture open failed");
  configFile.write(QJsonDocument(config).toJson());
  configFile.close();

  DockProfileStore store(directory.path());
  QString error;
  require(store.load(&error), qPrintable(error));
  require(store.profiles().isEmpty(), "corrupt profile was loaded");
  require(store.mappings().isEmpty(), "missing profile mapping was loaded");
  require(store.issues().size() >= 2,
          "corrupt and missing profile issues were not both reported");
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
        QByteArray("state-a")}},
      {QStringLiteral("dock-b"),
       {QStringLiteral("dock-b"), QStringLiteral("Layout B"), 1,
        QByteArray("state-b")}},
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
      [&](int delay, std::function<void()> callback) {
        scheduled.push_back({delay, std::move(callback)});
      },
      [&](const QString &error) { errors.push_back(error); });

  coordinator.scheduleForCurrentProfile();
  require(scheduled.size() == 3, "assigned profile did not schedule retries");
  require(scheduled[0].delay == 0 && scheduled[1].delay == 250 &&
              scheduled[2].delay == 1000,
          "restore retry delays changed");
  runScheduled(scheduled);
  require(restored.size() == 3, "assigned snapshot was not retried");
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
  require(restored.size() == 3, "active mapping change was not restored");
  for (const QByteArray &state : restored)
    require(state == QByteArray("state-b"),
            "active mapping change restored the old snapshot");

  restored.clear();
  mappings.insert(QStringLiteral("Profile A"), QStringLiteral("dock-a"));
  coordinator.scheduleForCurrentProfile();
  currentProfile = QStringLiteral("Profile B");
  coordinator.scheduleForCurrentProfile();
  runScheduled(scheduled);
  require(restored.size() == 3,
          "rapid switch did not cancel exactly the stale retries");
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
  addTestDock(target, QStringLiteral("coreDock"), Qt::LeftDockWidgetArea);
  target.show();
  QApplication::processEvents();

  std::vector<ScheduledRestore> scheduled;
  DockRestoreCoordinator coordinator(
      []() { return QStringLiteral("OBS"); },
      [](const QString &) { return QStringLiteral("layout"); },
      [&](const QString &) -> std::optional<DockRestoreSnapshot> {
        return DockRestoreSnapshot{QStringLiteral("layout"),
                                   QStringLiteral("Layout"), 1, state};
      },
      [&](const QByteArray &restoreState, int version, QString *error) {
        return restoreDockStateTransactional(target, restoreState, version,
                                             error);
      },
      [&](int delay, std::function<void()> callback) {
        scheduled.push_back({delay, std::move(callback)});
      },
      [](const QString &) {});

  coordinator.scheduleForCurrentProfile();
  require(scheduled.size() == 3, "late dock retries were not scheduled");
  scheduled[0].callback();
  auto *late = addTestDock(target, QStringLiteral("plugin.custom.late"),
                           Qt::LeftDockWidgetArea);
  QApplication::processEvents();
  scheduled[1].callback();
  scheduled[2].callback();
  QApplication::processEvents();
  require(target.dockWidgetArea(late) == Qt::RightDockWidgetArea,
          "late custom dock did not restore on a delayed retry");
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
      {"corrupt and missing profiles", corruptAndMissingProfilesAreReported},
      {"mapped profile transitions and rapid switching",
       mappedProfileTransitionsUseCurrentSnapshot},
      {"missing snapshot error reporting", missingSnapshotIsReportedOnce},
      {"custom dock layout round-trip", customDockLayoutRoundTrips},
      {"late custom dock retry", lateCustomDockIsRestoredByRetry},
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
