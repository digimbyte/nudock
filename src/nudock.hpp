#pragma once

#include "dock_profile_store.hpp"
#include "dock_restore_coordinator.hpp"

#include <obs-frontend-api.h>

#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>

class QAction;
class DockProfileManagerDialog;
class QMainWindow;

class NuDock final : public QObject {
public:
  explicit NuDock(QMainWindow *mainWindow);
  ~NuDock() override;

  NuDock(const NuDock &) = delete;
  NuDock &operator=(const NuDock &) = delete;

  bool initialize();

private:
  static constexpr int DockStateVersion = 1;

  QMainWindow *mainWindow = nullptr;
  QAction *docksAction = nullptr;
  QPointer<DockProfileManagerDialog> managerDialog;
  DockProfileStore store;
  DockRestoreCoordinator restoreCoordinator;
  QStringList knownObsProfiles;
  QString knownCurrentObsProfile;
  bool profileChangingEventSeen = false;

  void showManager();
  void handleObsProfileEvent(enum obs_frontend_event event);
  void scheduleRestoreForCurrentProfile();
  bool restoreStateTransactional(const QByteArray &state, int stateVersion,
                                 QString *error = nullptr);
  bool applyStore(const DockProfileStore &candidate, QString *error);
  bool reconcileStoreWithObs(const QStringList &profiles,
                             const QString &previousCurrent,
                             const QString &current,
                             bool allowRenameTransfer = true);

  static QStringList obsProfiles();
  static QString currentObsProfile();
  static QString moduleConfigDirectory();
  static void frontendEvent(enum obs_frontend_event event, void *data);
};
