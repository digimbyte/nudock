#include "dock_profile_store.hpp"
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
#include <QTemporaryDir>

#include <functional>
#include <iostream>
#include <stdexcept>
#include <utility>
#include <vector>

namespace {
void require(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}

void crudRoundTripAndSharedMappings() {
  QTemporaryDir directory(
      QDir::current().filePath(QStringLiteral("nudock-tests-XXXXXX")));
  require(directory.isValid(), "temporary directory creation failed");
  DockProfileStore store(directory.path());
  QString error;
  require(store.load(&error), qPrintable(error));
  const QString mainId = store.createProfile(
      QStringLiteral("Main"), QByteArray("state-main"),
      QStringLiteral("32.1.0"), &error);
  const QString editId = store.createProfile(
      QStringLiteral("Editing"), QByteArray("state-edit"),
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
  require(!store.createProfile(QStringLiteral("Main"), QByteArray("state"),
                               QStringLiteral("32.1.0"), &error)
               .isEmpty(),
          qPrintable(error));
  require(store.createProfile(QStringLiteral("main"), QByteArray("other"),
                              QStringLiteral("32.1.0"), &error)
              .isEmpty(),
          "case-insensitive duplicate name was accepted");
  require(!error.isEmpty(), "duplicate name did not report an error");
  require(store.createProfile(QStringLiteral("Empty"), {},
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
  const QString id = store.createProfile(QStringLiteral("Temporary"),
                                         QByteArray("state"),
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
  const QString id = store.createProfile(QStringLiteral("Main"),
                                         QByteArray("state"),
                                         QStringLiteral("32.1.0"));
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

  QFile corrupt(
      QDir(directory.path()).filePath("profiles/not-a-profile.json"));
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
  require(!restoreDockStateTransactional(window, QByteArray("invalid"), 1,
                                         &error),
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
      std::cerr << "FAIL: " << name << ": " << exception.what()
                << std::endl;
    }
  }
  return failures == 0 ? 0 : 1;
}
