#include "jrdockie.hpp"

#include "pluginInfo.hpp"

#include <obs-module.h>
#include <util/bmem.h>

#include <obs-websocket-api.h>

#include <QAction>
#include <QCursor>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMainWindow>
#include <QMenu>
#include <QMessageBox>
#include <QSaveFile>
#include <QUrl>

#include <memory>

OBS_DECLARE_MODULE()
OBS_MODULE_AUTHOR(PLUGIN_AUTHOR)
OBS_MODULE_USE_DEFAULT_LOCALE(PLUGIN_NAME, "en-US")

namespace {
std::unique_ptr<JrDockie> moduleInstance;

constexpr auto DocksetExtension = ".dockset";
constexpr auto DocksetFormat = "jrdockie.dockset";
constexpr int DocksetFileVersion = 1;

QString normalizedDocksetPath(QString path) {
  if (!path.endsWith(DocksetExtension, Qt::CaseInsensitive))
    path += DocksetExtension;
  return QDir::cleanPath(path);
}
} // namespace

bool obs_module_load() {
  auto *mainWindow = static_cast<QMainWindow *>(obs_frontend_get_main_window());
  if (!mainWindow) {
    blog(LOG_ERROR, "[%s] OBS frontend main window is unavailable",
         PLUGIN_NAME);
    return false;
  }

  moduleInstance = std::make_unique<JrDockie>(mainWindow);
  if (!moduleInstance->initialize()) {
    moduleInstance.reset();
    return false;
  }

  blog(LOG_INFO, "[%s] loaded version %s for OBS %s", PLUGIN_NAME,
       PLUGIN_VERSION, obs_get_version_string());
  return true;
}

void obs_module_post_load() {
  if (moduleInstance)
    moduleInstance->postLoad();
}

void obs_module_unload() { moduleInstance.reset(); }

JrDockie::JrDockie(QMainWindow *mainWindow_)
    : QObject(mainWindow_), mainWindow(mainWindow_) {}

JrDockie::~JrDockie() {
  unregisterWebsocketCommands();
  obs_frontend_remove_save_callback(frontendSave, this);
  obs_frontend_remove_event_callback(frontendEvent, this);

  if (cycleHotkey != OBS_INVALID_HOTKEY_ID)
    obs_hotkey_unregister(cycleHotkey);

  delete dockSetsMenu;
  dockSetsAction = nullptr;
  dockSetsMenu = nullptr;

  if (toolsAction) {
    delete toolsAction;
    toolsAction = nullptr;
  }
}

bool JrDockie::initialize() {
  // OBS 32.x exposes the Docks menu as this stable QObject. The frontend API
  // has no function for adding a Docks submenu, so object identity is used
  // instead of the old locale-dependent visible-label scan.
  auto *docksMenu = mainWindow->findChild<QMenu *>("menuDocks");
  if (!docksMenu) {
    blog(LOG_ERROR, "[%s] OBS Docks menu object 'menuDocks' was not found",
         PLUGIN_NAME);
    return false;
  }

  dockSetsMenu = new QMenu(obs_module_text("Dock Sets"), docksMenu);
  dockSetsMenu->setObjectName("jrdockieDockSetsMenu");

  QAction *insertBefore = nullptr;
  const QList<QAction *> dockActions = docksMenu->actions();
  const int resetIndex =
      dockActions.indexOf(mainWindow->findChild<QAction *>("resetDocks"));
  if (resetIndex >= 0 && resetIndex + 1 < dockActions.size())
    insertBefore = dockActions.at(resetIndex + 1);

  dockSetsAction = insertBefore
                       ? docksMenu->insertMenu(insertBefore, dockSetsMenu)
                       : docksMenu->addMenu(dockSetsMenu);
  connect(dockSetsMenu, &QMenu::aboutToShow, this, &JrDockie::rebuildMenu);

  // This supported frontend entry is a deterministic fallback and opens the
  // same manager menu if OBS changes its Docks menu internals again.
  toolsAction = static_cast<QAction *>(obs_frontend_add_tools_menu_qaction(
      obs_module_text("JrDockie: Dock Sets")));
  if (toolsAction)
    connect(toolsAction, &QAction::triggered, this, &JrDockie::showMenu);

  cycleHotkey = obs_hotkey_register_frontend(
      "jrdockie.cycle", obs_module_text("JrDockie: Cycle Dock Sets"),
      hotkeyCallback, this);
  obs_frontend_add_event_callback(frontendEvent, this);
  obs_frontend_add_save_callback(frontendSave, this);

  rebuildMenu();
  return true;
}

void JrDockie::postLoad() { registerWebsocketCommands(); }

QString JrDockie::docksetDirectory() const {
  char *rawPath = obs_module_config_path("docksets");
  if (!rawPath)
    return {};

  const QString path = QDir::cleanPath(QString::fromUtf8(rawPath));
  bfree(rawPath);
  QDir().mkpath(path);
  return path;
}

QStringList JrDockie::docksetFiles() const {
  const QDir directory(docksetDirectory());
  const QFileInfoList entries = directory.entryInfoList(
      {QStringLiteral("*.dockset")}, QDir::Files | QDir::Readable,
      QDir::Name | QDir::IgnoreCase);

  QStringList files;
  files.reserve(entries.size());
  for (const QFileInfo &entry : entries)
    files.push_back(entry.absoluteFilePath());
  return files;
}

QString JrDockie::resolveDocksetPath(const QString &requestedPath) const {
  if (requestedPath.trimmed().isEmpty())
    return {};

  const QFileInfo direct(normalizedDocksetPath(requestedPath));
  if (direct.isAbsolute() && direct.isFile())
    return direct.absoluteFilePath();

  const QFileInfo managed(
      QDir(docksetDirectory()).filePath(normalizedDocksetPath(requestedPath)));
  return managed.isFile() ? managed.absoluteFilePath() : QString{};
}

void JrDockie::rebuildMenu() {
  if (!dockSetsMenu)
    return;

  dockSetsMenu->clear();

  auto *saveAction =
      dockSetsMenu->addAction(obs_module_text("Save Current Dock Set As..."));
  connect(saveAction, &QAction::triggered, this, &JrDockie::saveDocksetAs);

  auto *loadAction =
      dockSetsMenu->addAction(obs_module_text("Load Dock Set From File..."));
  connect(loadAction, &QAction::triggered, this,
          &JrDockie::loadDocksetFromDialog);

  dockSetsMenu->addSeparator();
  const QStringList files = docksetFiles();
  if (files.isEmpty()) {
    auto *emptyAction =
        dockSetsMenu->addAction(obs_module_text("No Saved Dock Sets"));
    emptyAction->setEnabled(false);
  } else {
    for (const QString &path : files) {
      auto *action =
          dockSetsMenu->addAction(QFileInfo(path).completeBaseName());
      action->setCheckable(true);
      action->setChecked(QDir::cleanPath(path) ==
                         QDir::cleanPath(lastLoadedPath));
      connect(action, &QAction::triggered, this,
              [this, path]() { loadDockset(path); });
    }
  }

  dockSetsMenu->addSeparator();
  auto *browseAction =
      dockSetsMenu->addAction(obs_module_text("Open Dock Set Folder"));
  connect(browseAction, &QAction::triggered, this, &JrDockie::browseDocksets);
}

void JrDockie::saveDocksetAs() {
  const QString defaultPath =
      QDir(docksetDirectory()).filePath(QStringLiteral("My Dock Set.dockset"));
  const QString path = QFileDialog::getSaveFileName(
      mainWindow, obs_module_text("Save Dock Set"), defaultPath,
      obs_module_text("JrDockie Dock Sets (*.dockset)"));
  if (!path.isEmpty())
    saveDockset(normalizedDocksetPath(path));
}

void JrDockie::loadDocksetFromDialog() {
  const QString path = QFileDialog::getOpenFileName(
      mainWindow, obs_module_text("Load Dock Set"), docksetDirectory(),
      obs_module_text("JrDockie Dock Sets (*.dockset)"));
  if (!path.isEmpty())
    loadDockset(path);
}

bool JrDockie::saveDockset(const QString &path) {
  const QByteArray state = mainWindow->saveState(DockStateVersion);
  if (state.isEmpty()) {
    showError(obs_module_text("OBS returned an empty dock state."));
    return false;
  }

  QJsonObject root;
  root.insert("format", DocksetFormat);
  root.insert("version", DocksetFileVersion);
  root.insert("obsVersion", QString::fromUtf8(obs_get_version_string()));
  root.insert("savedAtUtc",
              QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
  root.insert("qtStateVersion", DockStateVersion);
  root.insert("state", QString::fromLatin1(state.toBase64()));

  QSaveFile file(path);
  if (!file.open(QIODevice::WriteOnly) ||
      file.write(QJsonDocument(root).toJson(QJsonDocument::Indented)) < 0 ||
      !file.commit()) {
    showError(obs_module_text("Could not write the dock set file."));
    return false;
  }

  lastLoadedPath = QDir::cleanPath(QFileInfo(path).absoluteFilePath());
  blog(LOG_INFO, "[%s] saved dock set '%s'", PLUGIN_NAME,
       path.toUtf8().constData());
  return true;
}

bool JrDockie::loadDockset(const QString &path) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) {
    showError(obs_module_text("Could not open the dock set file."));
    return false;
  }

  QJsonParseError parseError;
  const QJsonDocument document =
      QJsonDocument::fromJson(file.readAll(), &parseError);
  const QJsonObject root = document.object();
  if (parseError.error != QJsonParseError::NoError ||
      root.value("format").toString() != DocksetFormat ||
      root.value("version").toInt() != DocksetFileVersion ||
      root.value("qtStateVersion").toInt() != DockStateVersion) {
    showError(
        obs_module_text("This is not a valid current JrDockie dock set."));
    return false;
  }

  const QByteArray state =
      QByteArray::fromBase64(root.value("state").toString().toLatin1());
  if (state.isEmpty() || !mainWindow->restoreState(state, DockStateVersion)) {
    showError(obs_module_text("OBS could not restore this dock set."));
    return false;
  }

  lastLoadedPath = QDir::cleanPath(QFileInfo(path).absoluteFilePath());
  blog(LOG_INFO, "[%s] loaded dock set '%s'", PLUGIN_NAME,
       path.toUtf8().constData());
  return true;
}

void JrDockie::cycleDocksets() {
  const QStringList files = docksetFiles();
  if (files.isEmpty())
    return;

  int index = files.indexOf(lastLoadedPath);
  index = (index + 1) % files.size();
  loadDockset(files.at(index));
}

void JrDockie::browseDocksets() {
  QDesktopServices::openUrl(QUrl::fromLocalFile(docksetDirectory()));
}

void JrDockie::showMenu() {
  if (!dockSetsMenu)
    return;
  rebuildMenu();
  dockSetsMenu->popup(QCursor::pos());
}

void JrDockie::showError(const QString &message) const {
  blog(LOG_ERROR, "[%s] %s", PLUGIN_NAME, message.toUtf8().constData());
  QMessageBox::critical(mainWindow, obs_module_text("JrDockie"), message);
}

void JrDockie::frontendEvent(enum obs_frontend_event event, void *data) {
  auto *self = static_cast<JrDockie *>(data);
  if (event == OBS_FRONTEND_EVENT_FINISHED_LOADING ||
      event == OBS_FRONTEND_EVENT_PROFILE_CHANGED ||
      event == OBS_FRONTEND_EVENT_SCENE_COLLECTION_CHANGED)
    QMetaObject::invokeMethod(self, &JrDockie::rebuildMenu,
                              Qt::QueuedConnection);
}

void JrDockie::frontendSave(obs_data_t *saveData, bool saving, void *data) {
  auto *self = static_cast<JrDockie *>(data);
  if (self->cycleHotkey == OBS_INVALID_HOTKEY_ID)
    return;

  if (saving) {
    obs_data_array_t *bindings = obs_hotkey_save(self->cycleHotkey);
    obs_data_set_array(saveData, "jrdockie.cycle", bindings);
    obs_data_array_release(bindings);
  } else {
    obs_data_array_t *bindings = obs_data_get_array(saveData, "jrdockie.cycle");
    if (bindings) {
      obs_hotkey_load(self->cycleHotkey, bindings);
      obs_data_array_release(bindings);
    }
  }
}

void JrDockie::hotkeyCallback(void *data, obs_hotkey_id, obs_hotkey_t *,
                              bool pressed) {
  if (!pressed)
    return;
  auto *self = static_cast<JrDockie *>(data);
  QMetaObject::invokeMethod(self, &JrDockie::cycleDocksets,
                            Qt::QueuedConnection);
}

void JrDockie::registerWebsocketCommands() {
  websocketVendor = obs_websocket_register_vendor("jrDockie");
  if (!websocketVendor) {
    blog(LOG_WARNING, "[%s] obs-websocket vendor API is unavailable",
         PLUGIN_NAME);
    return;
  }

  if (!obs_websocket_vendor_register_request(websocketVendor, "LoadDockset",
                                             websocketLoad, this))
    blog(LOG_WARNING, "[%s] failed to register LoadDockset websocket request",
         PLUGIN_NAME);
  if (!obs_websocket_vendor_register_request(websocketVendor, "CycleDockset",
                                             websocketCycle, this))
    blog(LOG_WARNING, "[%s] failed to register CycleDockset websocket request",
         PLUGIN_NAME);
}

void JrDockie::unregisterWebsocketCommands() {
  if (!websocketVendor)
    return;
  obs_websocket_vendor_unregister_request(websocketVendor, "LoadDockset");
  obs_websocket_vendor_unregister_request(websocketVendor, "CycleDockset");
  websocketVendor = nullptr;
}

void JrDockie::websocketLoad(obs_data_t *requestData, obs_data_t *responseData,
                             void *data) {
  auto *self = static_cast<JrDockie *>(data);
  const QString requested =
      QString::fromUtf8(obs_data_get_string(requestData, "filename"));
  const QString path = self->resolveDocksetPath(requested);
  const bool found = !path.isEmpty();
  obs_data_set_bool(responseData, "accepted", found);
  if (!found) {
    obs_data_set_string(responseData, "error", "Dock set not found");
    return;
  }

  obs_data_set_string(responseData, "path", path.toUtf8().constData());
  QMetaObject::invokeMethod(
      self, [self, path]() { self->loadDockset(path); }, Qt::QueuedConnection);
}

void JrDockie::websocketCycle(obs_data_t *, obs_data_t *responseData,
                              void *data) {
  auto *self = static_cast<JrDockie *>(data);
  obs_data_set_bool(responseData, "accepted", true);
  QMetaObject::invokeMethod(self, &JrDockie::cycleDocksets,
                            Qt::QueuedConnection);
}
