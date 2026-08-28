#pragma once

#include <obs-frontend-api.h>
#include <obs-hotkey.h>

#include <QObject>
#include <QString>
#include <QStringList>

class QAction;
class QMainWindow;
class QMenu;

typedef void *obs_websocket_vendor;

class JrDockie final : public QObject {
public:
  explicit JrDockie(QMainWindow *mainWindow);
  ~JrDockie() override;

  JrDockie(const JrDockie &) = delete;
  JrDockie &operator=(const JrDockie &) = delete;

  bool initialize();
  void postLoad();

private:
  static constexpr int DockStateVersion = 1;

  QMainWindow *mainWindow = nullptr;
  QMenu *dockSetsMenu = nullptr;
  QAction *dockSetsAction = nullptr;
  QAction *toolsAction = nullptr;
  obs_hotkey_id cycleHotkey = OBS_INVALID_HOTKEY_ID;
  obs_websocket_vendor websocketVendor = nullptr;
  QString lastLoadedPath;

  QString docksetDirectory() const;
  QStringList docksetFiles() const;
  QString resolveDocksetPath(const QString &requestedPath) const;

  void rebuildMenu();
  void saveDocksetAs();
  void loadDocksetFromDialog();
  bool saveDockset(const QString &path);
  bool loadDockset(const QString &path);
  void cycleDocksets();
  void browseDocksets();
  void showMenu();
  void showError(const QString &message) const;

  void registerWebsocketCommands();
  void unregisterWebsocketCommands();

  static void frontendEvent(enum obs_frontend_event event, void *data);
  static void frontendSave(obs_data_t *saveData, bool saving, void *data);
  static void hotkeyCallback(void *data, obs_hotkey_id id, obs_hotkey_t *hotkey,
                             bool pressed);
  static void websocketLoad(obs_data_t *requestData, obs_data_t *responseData,
                            void *data);
  static void websocketCycle(obs_data_t *requestData, obs_data_t *responseData,
                             void *data);
};
