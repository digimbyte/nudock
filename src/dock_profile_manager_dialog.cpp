#include "dock_profile_manager_dialog.hpp"

#include <obs-module.h>

#include <QComboBox>
#include <QDialogButtonBox>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

#include <utility>

namespace {
QString text(const char *key) { return QString::fromUtf8(obs_module_text(key)); }
}

DockProfileManagerDialog::DockProfileManagerDialog(
    QWidget *parent, const DockProfileStore &store,
    const QStringList &obsProfiles_, const QString &currentObsProfile_,
    ObsVersionProvider obsVersionProvider_, StateProvider stateProvider_,
    LoadCallback loadCallback_, ApplyCallback applyCallback_)
    : QDialog(parent), workingStore(store), obsProfiles(obsProfiles_),
      currentObsProfile(currentObsProfile_),
      obsVersionProvider(std::move(obsVersionProvider_)),
      stateProvider(std::move(stateProvider_)),
      loadCallback(std::move(loadCallback_)),
      applyCallback(std::move(applyCallback_)) {
  buildUi();
  rebuildProfileList();
  rebuildMappingTable();
}

void DockProfileManagerDialog::buildUi() {
  setWindowTitle(text("NuDock Profiles"));
  setMinimumSize(760, 540);

  auto *root = new QVBoxLayout(this);
  issueLabel = new QLabel(this);
  issueLabel->setWordWrap(true);
  issueLabel->setStyleSheet(QStringLiteral("color: #d98b2b;"));
  if (workingStore.issues().isEmpty()) {
    issueLabel->hide();
  } else {
    issueLabel->setText(workingStore.issues().join(QLatin1Char('\n')));
    root->addWidget(issueLabel);
  }

  auto *profilesGroup = new QGroupBox(text("Dock Profiles"), this);
  auto *profilesLayout = new QHBoxLayout(profilesGroup);
  profileList = new QListWidget(profilesGroup);
  profilesLayout->addWidget(profileList, 1);

  auto *profileButtons = new QVBoxLayout;
  auto *newButton = new QPushButton(text("Create..."), profilesGroup);
  renameButton = new QPushButton(text("Rename..."), profilesGroup);
  deleteButton = new QPushButton(text("Delete"), profilesGroup);
  loadButton = new QPushButton(text("Load Now"), profilesGroup);
  saveButton =
      new QPushButton(text("Save Current to Profile"), profilesGroup);
  profileButtons->addWidget(newButton);
  profileButtons->addWidget(renameButton);
  profileButtons->addWidget(deleteButton);
  profileButtons->addSpacing(12);
  profileButtons->addWidget(loadButton);
  profileButtons->addWidget(saveButton);
  profileButtons->addStretch(1);
  profilesLayout->addLayout(profileButtons);
  root->addWidget(profilesGroup, 1);

  auto *mappingGroup = new QGroupBox(text("OBS Profile Assignments"), this);
  auto *mappingLayout = new QVBoxLayout(mappingGroup);
  mappingTable = new QTableWidget(mappingGroup);
  mappingTable->setColumnCount(2);
  mappingTable->setHorizontalHeaderLabels(
      {text("OBS Profile"), text("Dock Profile")});
  mappingTable->horizontalHeader()->setSectionResizeMode(0,
                                                         QHeaderView::Stretch);
  mappingTable->horizontalHeader()->setSectionResizeMode(1,
                                                         QHeaderView::Stretch);
  mappingTable->verticalHeader()->setVisible(false);
  mappingTable->setSelectionMode(QAbstractItemView::NoSelection);
  mappingLayout->addWidget(mappingTable);
  root->addWidget(mappingGroup, 1);

  buttonBox = new QDialogButtonBox(
      QDialogButtonBox::Ok | QDialogButtonBox::Apply |
          QDialogButtonBox::Cancel,
      this);
  root->addWidget(buttonBox);

  connect(profileList, &QListWidget::currentRowChanged, this,
          [this]() { updateActionState(); });
  connect(newButton, &QPushButton::clicked, this,
          [this]() { createProfile(); });
  connect(renameButton, &QPushButton::clicked, this,
          [this]() { renameProfile(); });
  connect(deleteButton, &QPushButton::clicked, this,
          [this]() { deleteProfile(); });
  connect(loadButton, &QPushButton::clicked, this,
          [this]() { loadProfileNow(); });
  connect(saveButton, &QPushButton::clicked, this,
          [this]() { saveCurrentToProfile(); });
  connect(buttonBox->button(QDialogButtonBox::Apply), &QPushButton::clicked,
          this, [this]() { applyChanges(false); });
  connect(buttonBox->button(QDialogButtonBox::Ok), &QPushButton::clicked, this,
          [this]() { applyChanges(true); });
  connect(buttonBox->button(QDialogButtonBox::Cancel), &QPushButton::clicked,
          this, &QDialog::reject);
}

QString DockProfileManagerDialog::selectedProfileId() const {
  const QListWidgetItem *item = profileList->currentItem();
  return item ? item->data(Qt::UserRole).toString() : QString{};
}

void DockProfileManagerDialog::rebuildProfileList(
    const QString &preferredId) {
  const QString wanted =
      preferredId.isEmpty() ? selectedProfileId() : preferredId;
  profileList->clear();
  int selectedRow = -1;
  const QList<DockProfile> profiles = workingStore.sortedProfiles();
  for (int index = 0; index < profiles.size(); ++index) {
    const DockProfile &profile = profiles.at(index);
    auto *item = new QListWidgetItem(profile.name, profileList);
    item->setData(Qt::UserRole, profile.id);
    if (profile.id == wanted)
      selectedRow = index;
  }
  if (selectedRow < 0 && profileList->count() > 0)
    selectedRow = 0;
  profileList->setCurrentRow(selectedRow);
  updateActionState();
}

void DockProfileManagerDialog::syncMappingsFromTable() {
  for (int row = 0; row < mappingTable->rowCount(); ++row) {
    const auto *profileItem = mappingTable->item(row, 0);
    const auto *combo =
        qobject_cast<QComboBox *>(mappingTable->cellWidget(row, 1));
    if (profileItem && combo)
      workingStore.setMapping(profileItem->data(Qt::UserRole).toString(),
                              combo->currentData().toString());
  }
}

void DockProfileManagerDialog::rebuildMappingTable() {
  mappingTable->setRowCount(obsProfiles.size());
  const QList<DockProfile> profiles = workingStore.sortedProfiles();
  for (int row = 0; row < obsProfiles.size(); ++row) {
    const QString &obsProfile = obsProfiles.at(row);
    auto *item = new QTableWidgetItem(obsProfile);
    item->setData(Qt::UserRole, obsProfile);
    item->setFlags(item->flags() & ~Qt::ItemIsEditable);
    if (obsProfile == currentObsProfile)
      item->setText(obsProfile + text(" (Current)"));
    mappingTable->setItem(row, 0, item);

    auto *combo = new QComboBox(mappingTable);
    combo->addItem(text("Keep Current"), QString{});
    for (const DockProfile &profile : profiles)
      combo->addItem(profile.name, profile.id);
    const int mappedIndex =
        combo->findData(workingStore.mappingFor(obsProfile));
    combo->setCurrentIndex(mappedIndex >= 0 ? mappedIndex : 0);
    mappingTable->setCellWidget(row, 1, combo);
  }
}

void DockProfileManagerDialog::updateActionState() {
  const bool hasSelection = !selectedProfileId().isEmpty();
  renameButton->setEnabled(hasSelection);
  deleteButton->setEnabled(hasSelection);
  loadButton->setEnabled(hasSelection);
  saveButton->setEnabled(hasSelection);
}

void DockProfileManagerDialog::createProfile() {
  bool accepted = false;
  const QString name = QInputDialog::getText(
      this, text("Create Dock Profile"), text("Profile name:"),
      QLineEdit::Normal, {}, &accepted);
  if (!accepted)
    return;

  QString error;
  const QString id = workingStore.createProfile(
      name, stateProvider(), obsVersionProvider(), &error);
  if (id.isEmpty()) {
    showError(error);
    return;
  }
  syncMappingsFromTable();
  rebuildProfileList(id);
  rebuildMappingTable();
}

void DockProfileManagerDialog::renameProfile() {
  const QString id = selectedProfileId();
  const DockProfile *profile = workingStore.profile(id);
  if (!profile)
    return;

  bool accepted = false;
  const QString name = QInputDialog::getText(
      this, text("Rename Dock Profile"), text("Profile name:"),
      QLineEdit::Normal, profile->name, &accepted);
  if (!accepted)
    return;

  QString error;
  if (!workingStore.renameProfile(id, name, &error)) {
    showError(error);
    return;
  }
  syncMappingsFromTable();
  rebuildProfileList(id);
  rebuildMappingTable();
}

void DockProfileManagerDialog::deleteProfile() {
  const QString id = selectedProfileId();
  const DockProfile *profile = workingStore.profile(id);
  if (!profile)
    return;
  if (QMessageBox::question(
          this, text("Delete Dock Profile"),
          text("Delete '%1' and clear every OBS profile assignment?")
              .arg(profile->name)) != QMessageBox::Yes)
    return;

  syncMappingsFromTable();
  workingStore.deleteProfile(id);
  rebuildProfileList();
  rebuildMappingTable();
}

void DockProfileManagerDialog::loadProfileNow() {
  const DockProfile *profile = workingStore.profile(selectedProfileId());
  if (!profile)
    return;
  QString error;
  if (!loadCallback(profile->state, &error))
    showError(error);
}

void DockProfileManagerDialog::saveCurrentToProfile() {
  const QString id = selectedProfileId();
  const DockProfile *profile = workingStore.profile(id);
  if (!profile)
    return;
  if (QMessageBox::question(
          this, text("Save Current Dock Layout"),
          text("Replace the saved layout in '%1' with the current OBS layout?")
              .arg(profile->name)) != QMessageBox::Yes)
    return;

  QString error;
  if (!workingStore.updateProfileState(id, stateProvider(),
                                       obsVersionProvider(), &error))
    showError(error);
}

bool DockProfileManagerDialog::applyChanges(bool closeAfter) {
  syncMappingsFromTable();
  QString error;
  if (!applyCallback(workingStore, &error)) {
    showError(error);
    return false;
  }
  if (closeAfter)
    QDialog::accept();
  return true;
}

void DockProfileManagerDialog::refreshObsProfiles(
    const QStringList &obsProfiles_, const QString &currentObsProfile_,
    const QString &previousCurrent, bool allowRenameTransfer) {
  syncMappingsFromTable();
  workingStore.reconcileObsProfiles(obsProfiles_, previousCurrent,
                                    currentObsProfile_, allowRenameTransfer);
  obsProfiles = obsProfiles_;
  currentObsProfile = currentObsProfile_;
  rebuildMappingTable();
}

void DockProfileManagerDialog::showError(const QString &message) {
  QMessageBox::critical(this, text("NuDock"), message);
}
