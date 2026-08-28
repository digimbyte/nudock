#include "nudock.hpp"

#include "dock_profile_manager_dialog.hpp"
#include "dock_state_restorer.hpp"
#include "pluginInfo.hpp"

#include <obs-module.h>
#include <util/bmem.h>

#include <QAction>
#include <QMainWindow>
#include <QMenu>
#include <QTimer>

#include <memory>
#include <optional>
#include <utility>

OBS_DECLARE_MODULE()
OBS_MODULE_AUTHOR(PLUGIN_AUTHOR)
OBS_MODULE_USE_DEFAULT_LOCALE(PLUGIN_NAME, "en-US")

namespace {
std::unique_ptr<NuDock> moduleInstance;
}

bool obs_module_load() {
  auto *mainWindow = static_cast<QMainWindow *>(obs_frontend_get_main_window());
  if (!mainWindow) {
    blog(LOG_ERROR, "[%s] OBS frontend main window is unavailable",
         PLUGIN_NAME);
    return false;
  }

  moduleInstance = std::make_unique<NuDock>(mainWindow);
  if (!moduleInstance->initialize()) {
    moduleInstance.reset();
    return false;
  }

  blog(LOG_INFO, "[%s] loaded version %s for OBS %s", PLUGIN_NAME,
       PLUGIN_VERSION, obs_get_version_string());
  return true;
}

void obs_module_unload() { moduleInstance.reset(); }

NuDock::NuDock(QMainWindow *mainWindow_)
    : QObject(mainWindow_), mainWindow(mainWindow_),
      store(moduleConfigDirectory()),
      restoreCoordinator(
          []() { return currentObsProfile(); },
          [this](const QString &obsProfileName) {
            return store.mappingFor(obsProfileName);
          },
          [this](const QString &dockProfileId)
              -> std::optional<DockRestoreSnapshot> {
            const DockProfile *profile = store.profile(dockProfileId);
            if (!profile)
              return std::nullopt;
            return DockRestoreSnapshot{profile->id, profile->name,
                                       profile->qtStateVersion, profile->state};
          },
          [this](const QByteArray &state, int stateVersion, QString *error) {
            return restoreStateTransactional(state, stateVersion, error);
          },
          [this](int delay, std::function<void()> callback) {
            QTimer::singleShot(delay, this, std::move(callback));
          },
          [](const QString &message) {
            blog(LOG_ERROR, "[%s] %s", PLUGIN_NAME,
                 message.toUtf8().constData());
          }) {}

NuDock::~NuDock() {
  restoreCoordinator.cancel();
  obs_frontend_remove_event_callback(frontendEvent, this);
  if (managerDialog)
    managerDialog->close();
  delete docksAction;
  docksAction = nullptr;
}

bool NuDock::initialize() {
  QString loadError;
  if (!store.load(&loadError))
    blog(LOG_ERROR, "[%s] could not load configuration: %s", PLUGIN_NAME,
         loadError.toUtf8().constData());
  for (const QString &issue : store.issues())
    blog(LOG_WARNING, "[%s] %s", PLUGIN_NAME, issue.toUtf8().constData());

  knownObsProfiles = obsProfiles();
  knownCurrentObsProfile = currentObsProfile();
  reconcileStoreWithObs(knownObsProfiles, {}, knownCurrentObsProfile);

  auto *docksMenu = mainWindow->findChild<QMenu *>("menuDocks");
  if (!docksMenu) {
    blog(LOG_ERROR, "[%s] OBS Docks menu object 'menuDocks' was not found",
         PLUGIN_NAME);
    return false;
  }

  docksAction = new QAction(obs_module_text("NuDock Profiles..."), docksMenu);
  docksAction->setObjectName(QStringLiteral("nudockProfilesAction"));
  const QList<QAction *> dockActions = docksMenu->actions();
  const int resetIndex =
      dockActions.indexOf(mainWindow->findChild<QAction *>("resetDocks"));
  if (resetIndex >= 0 && resetIndex + 1 < dockActions.size())
    docksMenu->insertAction(dockActions.at(resetIndex + 1), docksAction);
  else
    docksMenu->addAction(docksAction);
  connect(docksAction, &QAction::triggered, this, &NuDock::showManager);
  obs_frontend_add_event_callback(frontendEvent, this);
  return true;
}

QString NuDock::moduleConfigDirectory() {
  char *rawPath = obs_module_config_path("");
  if (!rawPath)
    return {};
  const QString path = QString::fromUtf8(rawPath);
  bfree(rawPath);
  return path;
}

QStringList NuDock::obsProfiles() {
  QStringList result;
  char **profiles = obs_frontend_get_profiles();
  if (!profiles)
    return result;
  for (char **profile = profiles; *profile; ++profile)
    result.push_back(QString::fromUtf8(*profile));
  bfree(profiles);
  result.sort(Qt::CaseInsensitive);
  return result;
}

QString NuDock::currentObsProfile() {
  char *rawName = obs_frontend_get_current_profile();
  if (!rawName)
    return {};
  const QString name = QString::fromUtf8(rawName);
  bfree(rawName);
  return name;
}

void NuDock::showManager() {
  const QString previousCurrent = knownCurrentObsProfile;
  knownObsProfiles = obsProfiles();
  knownCurrentObsProfile = currentObsProfile();
  reconcileStoreWithObs(knownObsProfiles, previousCurrent,
                        knownCurrentObsProfile);

  if (managerDialog) {
    managerDialog->refreshObsProfiles(knownObsProfiles, knownCurrentObsProfile,
                                      previousCurrent);
    managerDialog->show();
    managerDialog->raise();
    managerDialog->activateWindow();
    return;
  }

  managerDialog = new DockProfileManagerDialog(
      mainWindow, store, knownObsProfiles, knownCurrentObsProfile,
      []() { return QString::fromUtf8(obs_get_version_string()); },
      [this]() { return mainWindow->saveState(DockStateVersion); },
      [this](const QByteArray &state, QString *error) {
        restoreCoordinator.cancel();
        return restoreStateTransactional(state, DockStateVersion, error);
      },
      [this](const DockProfileStore &candidate, QString *error) {
        return applyStore(candidate, error);
      });
  managerDialog->setAttribute(Qt::WA_DeleteOnClose);
  managerDialog->show();
}

bool NuDock::applyStore(const DockProfileStore &candidate, QString *error) {
  const QString activeObsProfile = currentObsProfile();
  const QString oldDockProfile = store.mappingFor(activeObsProfile);
  if (!candidate.commit(error))
    return false;

  store = candidate;
  const QString newDockProfile = store.mappingFor(activeObsProfile);
  if (oldDockProfile != newDockProfile) {
    if (newDockProfile.isEmpty())
      restoreCoordinator.cancel();
    else
      scheduleRestoreForCurrentProfile();
  }
  return true;
}

bool NuDock::reconcileStoreWithObs(const QStringList &profiles,
                                   const QString &previousCurrent,
                                   const QString &current,
                                   bool allowRenameTransfer) {
  if (!store.reconcileObsProfiles(profiles, previousCurrent, current,
                                  allowRenameTransfer))
    return false;
  QString error;
  if (!store.commit(&error)) {
    blog(LOG_ERROR, "[%s] could not reconcile OBS profile mappings: %s",
         PLUGIN_NAME, error.toUtf8().constData());
    return false;
  }
  return true;
}

void NuDock::handleObsProfileEvent(enum obs_frontend_event event) {
  const QString previousCurrent = knownCurrentObsProfile;
  const bool allowRenameTransfer = !profileChangingEventSeen;
  knownObsProfiles = obsProfiles();
  knownCurrentObsProfile = currentObsProfile();
  reconcileStoreWithObs(knownObsProfiles, previousCurrent,
                        knownCurrentObsProfile, allowRenameTransfer);

  if (managerDialog)
    managerDialog->refreshObsProfiles(knownObsProfiles, knownCurrentObsProfile,
                                      previousCurrent, allowRenameTransfer);

  if (event == OBS_FRONTEND_EVENT_PROFILE_CHANGED) {
    profileChangingEventSeen = false;
    scheduleRestoreForCurrentProfile();
  } else if (event == OBS_FRONTEND_EVENT_FINISHED_LOADING) {
    scheduleRestoreForCurrentProfile();
  }
}

void NuDock::scheduleRestoreForCurrentProfile() {
  restoreCoordinator.scheduleForCurrentProfile();
}

bool NuDock::restoreStateTransactional(const QByteArray &state,
                                       int stateVersion, QString *error) {
  return restoreDockStateTransactional(*mainWindow, state, stateVersion, error);
}

void NuDock::frontendEvent(enum obs_frontend_event event, void *data) {
  auto *self = static_cast<NuDock *>(data);
  if (event == OBS_FRONTEND_EVENT_PROFILE_CHANGING) {
    self->profileChangingEventSeen = true;
    return;
  }
  if (event != OBS_FRONTEND_EVENT_FINISHED_LOADING &&
      event != OBS_FRONTEND_EVENT_PROFILE_CHANGED &&
      event != OBS_FRONTEND_EVENT_PROFILE_LIST_CHANGED &&
      event != OBS_FRONTEND_EVENT_PROFILE_RENAMED)
    return;

  QMetaObject::invokeMethod(
      self, [self, event]() { self->handleObsProfileEvent(event); },
      Qt::QueuedConnection);
}
