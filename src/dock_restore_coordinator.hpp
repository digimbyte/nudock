#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>

#include <cstdint>
#include <functional>
#include <optional>

struct DockRestoreSnapshot {
  QString id;
  QString name;
  int qtStateVersion = 1;
  QByteArray state;
  QStringList dockIds;
};

class DockRestoreCoordinator {
public:
  using CurrentProfileProvider = std::function<QString()>;
  using MappingProvider = std::function<QString(const QString &)>;
  using SnapshotProvider =
      std::function<std::optional<DockRestoreSnapshot>(const QString &)>;
  using RestoreCallback =
      std::function<bool(const QByteArray &, int, QString *)>;
  using DockInventoryProvider = std::function<QStringList()>;
  using RestoreLateDocksCallback =
      std::function<bool(const QStringList &, QString *)>;
  using ScheduleCallback = std::function<void(int, std::function<void()>)>;
  using ErrorCallback = std::function<void(const QString &)>;

  DockRestoreCoordinator(CurrentProfileProvider currentProfileProvider,
                         MappingProvider mappingProvider,
                         SnapshotProvider snapshotProvider,
                         RestoreCallback restoreCallback,
                         DockInventoryProvider dockInventoryProvider,
                         RestoreLateDocksCallback restoreLateDocksCallback,
                         ScheduleCallback scheduleCallback,
                         ErrorCallback errorCallback);

  void scheduleForCurrentProfile();
  void cancel();

private:
  CurrentProfileProvider currentProfileProvider_;
  MappingProvider mappingProvider_;
  SnapshotProvider snapshotProvider_;
  RestoreCallback restoreCallback_;
  DockInventoryProvider dockInventoryProvider_;
  RestoreLateDocksCallback restoreLateDocksCallback_;
  ScheduleCallback scheduleCallback_;
  ErrorCallback errorCallback_;
  std::uint64_t generation_ = 0;
  std::uint64_t reportedErrorGeneration_ = 0;

  void restoreInitial(std::uint64_t generation, const QString &obsProfileName,
                      const QString &dockProfileId);
  void pollInventory(std::uint64_t generation,
                     const QString &obsProfileName,
                     const QString &dockProfileId,
                     DockRestoreSnapshot snapshot,
                     QStringList unresolvedDockIds, int pollNumber,
                     bool fallbackUsed);
  bool isCurrent(std::uint64_t generation, const QString &obsProfileName,
                 const QString &dockProfileId) const;
  void reportOnce(std::uint64_t generation, const QString &message);
};
