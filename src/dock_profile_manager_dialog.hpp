#pragma once

#include "dock_profile_store.hpp"

#include <QDialog>

#include <functional>

class QDialogButtonBox;
class QCloseEvent;
class QLabel;
class QListWidget;
class QPushButton;
class QTableWidget;

class DockProfileManagerDialog final : public QDialog {
public:
  using ObsVersionProvider = std::function<QString()>;
  using StateProvider = std::function<QByteArray()>;
  using DockIdsProvider = std::function<QStringList()>;
  using LoadCallback = std::function<bool(const QByteArray &, QString *)>;
  using ApplyCallback =
      std::function<bool(const DockProfileStore &, QString *)>;
  using ProfileCommitCallback =
      std::function<bool(DockProfileStore *, QString *)>;

  DockProfileManagerDialog(QWidget *parent, const DockProfileStore &store,
                           const QStringList &obsProfiles,
                           const QString &currentObsProfile,
                           ObsVersionProvider obsVersionProvider,
                           StateProvider stateProvider,
                           DockIdsProvider dockIdsProvider,
                           LoadCallback loadCallback,
                           ApplyCallback applyCallback,
                           ProfileCommitCallback profileCommitCallback);

  void refreshObsProfiles(const QStringList &obsProfiles,
                          const QString &currentObsProfile,
                          const QString &previousCurrent,
                          bool allowRenameTransfer = true);

private:
  DockProfileStore workingStore;
  QMap<QString, QString> appliedMappings;
  QStringList obsProfiles;
  QString currentObsProfile;
  ObsVersionProvider obsVersionProvider;
  StateProvider stateProvider;
  DockIdsProvider dockIdsProvider;
  LoadCallback loadCallback;
  ApplyCallback applyCallback;
  ProfileCommitCallback profileCommitCallback;

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
  bool hasPendingAssignments() const;
  void updatePendingState(const QString &status = {});
  bool commitProfileMutation(const DockProfileStore &before,
                             const QMap<QString, QString> &stagedMappings,
                             const QString &preferredId,
                             const QString &successStatus);
  bool confirmDiscardAssignments();
  void updateActionState();
  void createProfile();
  void renameProfile();
  void deleteProfile();
  void loadProfileNow();
  void saveCurrentToProfile();
  bool applyChanges(bool closeAfter);
  void showError(const QString &message);

protected:
  void reject() override;
  void closeEvent(QCloseEvent *event) override;
};
