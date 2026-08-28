#pragma once

#include <QByteArray>
#include <QString>

class QMainWindow;

bool restoreDockStateTransactional(QMainWindow &window,
                                   const QByteArray &desiredState,
                                   int stateVersion,
                                   QString *error = nullptr);
