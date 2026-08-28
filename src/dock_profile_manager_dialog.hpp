#pragma once

#include "dock_profile_store.hpp"

#include <QDialog>

#include <functional>

class QDialogButtonBox;
class QLabel;
class QListWidget;
class QPushButton;
class QTableWidget;

class DockProfileManagerDialog final : public QDialog {
public:
  using ObsVersionProvider = std::function<QString()>;
  using StateProvider = std::function<QByteArray()>;
  using LoadCallback = std::function<bool(const QByteArray &, QString *)>;
  using ApplyCallback =
      std::function<bool(const DockProfileStore &, QString *)>;

  DockProfileManagerDialog(QWidget *parent, const DockProfileStore &store,
                           const QStringList &obsProfiles,
                           const QString &currentObsProfile,
                           ObsVersionProvider obsVersionProvider,
                           StateProvider stateProvider,
                           LoadCallback loadCallback,
                           ApplyCallback applyCallback);

  void refreshObsProfiles(const QStringList &obsProfiles,
                          const QString &currentObsProfile,
                          const QString &previousCurrent,
                          bool allowRenameTransfer = true);

private:
  DockProfileStore workingStore;
  QStringList obsProfiles;
  QString currentObsProfile;
  ObsVersionProvider obsVersionProvider;
  StateProvider stateProvider;
  LoadCallback loadCallback;
  ApplyCallback applyCallback;

  QListWidget *profileList = nullptr;
  QTableWidget *mappingTable = nullptr;
  QLabel *issueLabel = nullptr;
  QPushButton *renameButton = nullptr;
  QPushButton *deleteButton = nullptr;
  QPushButton *loadButton = nullptr;
  QPushButton *saveButton = nullptr;
  QDialogButtonBox *buttonBox = nullptr;

  QString selectedProfileId() const;
  void buildUi();
  void rebuildProfileList(const QString &preferredId = {});
  void rebuildMappingTable();
  void syncMappingsFromTable();
  void updateActionState();
  void createProfile();
  void renameProfile();
  void deleteProfile();
  void loadProfileNow();
  void saveCurrentToProfile();
  bool applyChanges(bool closeAfter);
  void showError(const QString &message);
};
