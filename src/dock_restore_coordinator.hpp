#pragma once

#include <QByteArray>
#include <QString>

#include <cstdint>
#include <functional>
#include <optional>

struct DockRestoreSnapshot {
  QString id;
  QString name;
  int qtStateVersion = 1;
  QByteArray state;
};

class DockRestoreCoordinator {
public:
  using CurrentProfileProvider = std::function<QString()>;
  using MappingProvider = std::function<QString(const QString &)>;
  using SnapshotProvider =
      std::function<std::optional<DockRestoreSnapshot>(const QString &)>;
  using RestoreCallback =
      std::function<bool(const QByteArray &, int, QString *)>;
  using ScheduleCallback = std::function<void(int, std::function<void()>)>;
  using ErrorCallback = std::function<void(const QString &)>;

  DockRestoreCoordinator(CurrentProfileProvider currentProfileProvider,
                         MappingProvider mappingProvider,
                         SnapshotProvider snapshotProvider,
                         RestoreCallback restoreCallback,
                         ScheduleCallback scheduleCallback,
                         ErrorCallback errorCallback);

  void scheduleForCurrentProfile();
  void cancel();

private:
  CurrentProfileProvider currentProfileProvider_;
  MappingProvider mappingProvider_;
  SnapshotProvider snapshotProvider_;
  RestoreCallback restoreCallback_;
  ScheduleCallback scheduleCallback_;
  ErrorCallback errorCallback_;
  std::uint64_t generation_ = 0;
  std::uint64_t reportedErrorGeneration_ = 0;

  void attempt(std::uint64_t generation, const QString &obsProfileName,
               const QString &dockProfileId);
  void reportOnce(std::uint64_t generation, const QString &message);
};
