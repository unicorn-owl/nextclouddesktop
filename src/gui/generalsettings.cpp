/*
 * Copyright (C) by Daniel Molkentin <danimo@owncloud.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
 * for more details.
 */

#include "generalsettings.h"
#include "ui_generalsettings.h"

#include "theme.h"
#include "configfile.h"
#include "application.h"
#include "configfile.h"
#include "owncloudsetupwizard.h"
#include "accountmanager.h"
#include "synclogdialog.h"

#include "updater/updater.h"
#include "updater/ocupdater.h"
#include "ignorelisteditor.h"
#include "common/utility.h"
#include "logger.h"

#include "config.h"

#include "legalnotice.h"

#include <QFileDialog>
#include <QMessageBox>
#include <QNetworkProxy>
#include <QDir>
#include <QScopedValueRollback>
#include <QStandardPaths>

#include <private/qzipwriter_p.h>

#define QTLEGACY (QT_VERSION < QT_VERSION_CHECK(5,9,0))

#if !(QTLEGACY)
#include <QOperatingSystemVersion>
#endif

namespace {
struct ZipEntry {
    QString localFilename;
    QString zipFilename;
};

ZipEntry fileInfoToZipEntry(const QFileInfo &info)
{
    return {
        info.absoluteFilePath(),
        info.fileName()
    };
}

ZipEntry fileInfoToLogZipEntry(const QFileInfo &info)
{
    auto entry = fileInfoToZipEntry(info);
    entry.zipFilename.prepend(QStringLiteral("logs/"));
    return entry;
}

ZipEntry syncFolderToZipEntry(OCC::Folder *f)
{
    const auto journalPath = f->journalDb()->databaseFilePath();
    const auto journalInfo = QFileInfo(journalPath);
    return fileInfoToZipEntry(journalInfo);
}

QVector<ZipEntry> createFileList()
{
    auto list = QVector<ZipEntry>();
    OCC::ConfigFile cfg;

    list.append(fileInfoToZipEntry(QFileInfo(cfg.configFile())));

    const auto logger = OCC::Logger::instance();

    if (!logger->logDir().isEmpty()) {
        list.append({QString(), QStringLiteral("logs")});

        QDir dir(logger->logDir());
        const auto infoList = dir.entryInfoList(QDir::Files);
        std::transform(std::cbegin(infoList), std::cend(infoList),
                       std::back_inserter(list),
                       fileInfoToLogZipEntry);
    } else if (!logger->logFile().isEmpty()) {
        list.append(fileInfoToZipEntry(QFileInfo(logger->logFile())));
    }

    const auto folders = OCC::FolderMan::instance()->map().values();
    std::transform(std::cbegin(folders), std::cend(folders),
                   std::back_inserter(list),
                   syncFolderToZipEntry);

    return list;
}

void createDebugArchive(const QString &filename)
{
    const auto entries = createFileList();

    QZipWriter zip(filename);
    for (const auto &entry : entries) {
        if (entry.localFilename.isEmpty()) {
            zip.addDirectory(entry.zipFilename);
        } else {
            QFile file(entry.localFilename);
            if (!file.open(QFile::ReadOnly)) {
                continue;
            }
            zip.addFile(entry.zipFilename, &file);
        }
    }

    zip.addFile("__nextcloud_client_parameters.txt", QCoreApplication::arguments().join(' ').toUtf8());

    const auto buildInfo = QString(OCC::Theme::instance()->about() + "\n\n" + OCC::Theme::instance()->aboutDetails());
    zip.addFile("__nextcloud_client_buildinfo.txt", buildInfo.toUtf8());
}
}

namespace OCC {

GeneralSettings::GeneralSettings(QWidget *parent)
    : QWidget(parent)
    , _ui(new Ui::GeneralSettings)
    , _currentlyLoading(false)
{
    _ui->setupUi(this);

    connect(_ui->serverNotificationsCheckBox, &QAbstractButton::toggled,
        this, &GeneralSettings::slotToggleOptionalServerNotifications);
    _ui->serverNotificationsCheckBox->setToolTip(tr("Server notifications that require attention."));

    #ifdef defined(Q_OS_MAC) || defined(Q_OS_WIN)
    _ui->virtualFileSystemCheckBox->show();
    connect(_ui->virtualFileSystemCheckBox, &QAbstractButton::toggled,
        this, &GeneralSettings::slotToggleOptionalVirtualFileSystem);
    _ui->virtualFileSystemCheckBox->setToolTip(tr("Sync files on demand."));
    #endif

    connect(_ui->showInExplorerNavigationPaneCheckBox, &QAbstractButton::toggled, this, &GeneralSettings::slotShowInExplorerNavigationPane);

    _ui->autostartCheckBox->setChecked(Utility::hasLaunchOnStartup(Theme::instance()->appName()));
    connect(_ui->autostartCheckBox, &QAbstractButton::toggled, this, &GeneralSettings::slotToggleLaunchOnStartup);

    // setup about section
    QString about = Theme::instance()->about();
    _ui->aboutLabel->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextBrowserInteraction);
    _ui->aboutLabel->setText(about);
    _ui->aboutLabel->setOpenExternalLinks(true);

    // About legal notice
    connect(_ui->legalNoticeButton, &QPushButton::clicked, this, &GeneralSettings::slotShowLegalNotice);

    loadMiscSettings();
    slotUpdateInfo();

    // misc
    connect(_ui->monoIconsCheckBox, &QAbstractButton::toggled, this, &GeneralSettings::saveMiscSettings);
    connect(_ui->crashreporterCheckBox, &QAbstractButton::toggled, this, &GeneralSettings::saveMiscSettings);
    connect(_ui->newFolderLimitCheckBox, &QAbstractButton::toggled, this, &GeneralSettings::saveMiscSettings);
    connect(_ui->newFolderLimitSpinBox, static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged), this, &GeneralSettings::saveMiscSettings);
    connect(_ui->newExternalStorage, &QAbstractButton::toggled, this, &GeneralSettings::saveMiscSettings);

#ifndef WITH_CRASHREPORTER
    _ui->crashreporterCheckBox->setVisible(false);
#endif

    // Hide on non-Windows, or WindowsVersion < 10.
    // The condition should match the default value of ConfigFile::showInExplorerNavigationPane.
#ifdef Q_OS_WIN
    #if QTLEGACY
        if (QSysInfo::windowsVersion() < QSysInfo::WV_WINDOWS10)
    #else
        if (QOperatingSystemVersion::current() < QOperatingSystemVersion::Windows10)
    #endif
            _ui->showInExplorerNavigationPaneCheckBox->setVisible(false);
#endif

    /* Set the left contents margin of the layout to zero to make the checkboxes
     * align properly vertically , fixes bug #3758
     */
    int m0, m1, m2, m3;
    _ui->horizontalLayout_3->getContentsMargins(&m0, &m1, &m2, &m3);
    _ui->horizontalLayout_3->setContentsMargins(0, m1, m2, m3);

    // OEM themes are not obliged to ship mono icons, so there
    // is no point in offering an option
    _ui->monoIconsCheckBox->setVisible(Theme::instance()->monoIconsAvailable());

    connect(_ui->ignoredFilesButton, &QAbstractButton::clicked, this, &GeneralSettings::slotIgnoreFilesEditor);
    connect(_ui->debugArchiveButton, &QAbstractButton::clicked, this, &GeneralSettings::slotCreateDebugArchive);

    // accountAdded means the wizard was finished and the wizard might change some options.
    connect(AccountManager::instance(), &AccountManager::accountAdded, this, &GeneralSettings::loadMiscSettings);
}

GeneralSettings::~GeneralSettings()
{
    delete _ui;
    delete _syncLogDialog;
}

QSize GeneralSettings::sizeHint() const
{
    return QSize(ownCloudGui::settingsDialogSize().width(), QWidget::sizeHint().height());
}

void GeneralSettings::loadMiscSettings()
{
    QScopedValueRollback<bool> scope(_currentlyLoading, true);
    ConfigFile cfgFile;
    _ui->monoIconsCheckBox->setChecked(cfgFile.monoIcons());
    _ui->serverNotificationsCheckBox->setChecked(cfgFile.optionalServerNotifications());
    _ui->showInExplorerNavigationPaneCheckBox->setChecked(cfgFile.showInExplorerNavigationPane());
    _ui->crashreporterCheckBox->setChecked(cfgFile.crashReporter());
    auto newFolderLimit = cfgFile.newBigFolderSizeLimit();
    _ui->newFolderLimitCheckBox->setChecked(newFolderLimit.first);
    _ui->newFolderLimitSpinBox->setValue(newFolderLimit.second);
    _ui->newExternalStorage->setChecked(cfgFile.confirmExternalStorage());
    _ui->monoIconsCheckBox->setChecked(cfgFile.monoIcons());
}

void GeneralSettings::slotUpdateInfo()
{
    // Note: the sparkle-updater is not an OCUpdater
    OCUpdater *updater = qobject_cast<OCUpdater *>(Updater::instance());
    if (ConfigFile().skipUpdateCheck()) {
        updater = nullptr; // don't show update info if updates are disabled
    }

    if (updater) {
        connect(updater, &OCUpdater::downloadStateChanged, this, &GeneralSettings::slotUpdateInfo, Qt::UniqueConnection);
        connect(_ui->restartButton, &QAbstractButton::clicked, updater, &OCUpdater::slotStartInstaller, Qt::UniqueConnection);
        connect(_ui->restartButton, &QAbstractButton::clicked, qApp, &QApplication::quit, Qt::UniqueConnection);
        _ui->updateStateLabel->setText(updater->statusString());
        _ui->restartButton->setVisible(updater->downloadState() == OCUpdater::DownloadComplete);
    } else {
        // can't have those infos from sparkle currently
        _ui->updatesGroupBox->setVisible(false);
    }
}

void GeneralSettings::saveMiscSettings()
{
    if (_currentlyLoading)
        return;
    ConfigFile cfgFile;
    bool isChecked = _ui->monoIconsCheckBox->isChecked();
    cfgFile.setMonoIcons(isChecked);
    Theme::instance()->setSystrayUseMonoIcons(isChecked);
    cfgFile.setCrashReporter(_ui->crashreporterCheckBox->isChecked());

    cfgFile.setNewBigFolderSizeLimit(_ui->newFolderLimitCheckBox->isChecked(),
        _ui->newFolderLimitSpinBox->value());
    cfgFile.setConfirmExternalStorage(_ui->newExternalStorage->isChecked());
}

void GeneralSettings::slotToggleLaunchOnStartup(bool enable)
{
    Theme *theme = Theme::instance();
    Utility::setLaunchOnStartup(theme->appName(), theme->appNameGUI(), enable);
}

void GeneralSettings::slotToggleOptionalServerNotifications(bool enable)
{
    ConfigFile cfgFile;
    cfgFile.setOptionalServerNotifications(enable);

    #if defined(Q_OS_MAC)
        QString defaultFileStreamSyncPath = cfgFile.defaultFileStreamSyncPath();
        QString defaultFileStreamMirrorPath = cfgFile.defaultFileStreamMirrorPath();

        if (defaultFileStreamSyncPath.isEmpty() || defaultFileStreamSyncPath.compare(QString("")) == 0)
            cfgFile.setDefaultFileStreamSyncPath(QString("/Volumes/" + Theme::instance()->appName() + "fs"));

        if (defaultFileStreamMirrorPath.isEmpty() || defaultFileStreamMirrorPath.compare(QString("")) == 0)
            cfgFile.setDefaultFileStreamMirrorPath(QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation) + "/.cachedFiles");
    #endif

#ifdef Q_OS_WIN
    //< Set configuration paths.
    QString m_defaultFileStreamSyncPath = cfgFile.defaultFileStreamSyncPath();
    QString m_defaultFileStreamMirrorPath = cfgFile.defaultFileStreamMirrorPath();
    QString m_defaultFileStreamLetterDrive = cfgFile.defaultFileStreamLetterDrive();

    if (m_defaultFileStreamSyncPath.isEmpty() || m_defaultFileStreamSyncPath.compare(QString("")) == 0)
        cfgFile.setDefaultFileStreamSyncPath(QString("X:/Mi unidad"));

    if (m_defaultFileStreamMirrorPath.isEmpty() || m_defaultFileStreamMirrorPath.compare(QString("")) == 0)
        cfgFile.setDefaultFileStreamMirrorPath(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/cachedFiles");

    if (m_defaultFileStreamLetterDrive.isEmpty() || m_defaultFileStreamLetterDrive.compare(QString("")) == 0)
        cfgFile.setDefaultFileStreamLetterDrive(QString("x"));
#endif
}

void GeneralSettings::slotToggleOptionalVirtualFileSystem(bool enable)
{
    ConfigFile cfgFile;
    cfgFile.setEnableVirtualFileSystem(enable);

    #if defined(Q_OS_MAC)
        QString defaultFileStreamSyncPath = cfgFile.defaultFileStreamSyncPath();
        QString defaultFileStreamMirrorPath = cfgFile.defaultFileStreamMirrorPath();

        if (defaultFileStreamSyncPath.isEmpty() || defaultFileStreamSyncPath.compare(QString("")) == 0)
            cfgFile.setDefaultFileStreamSyncPath(QString("/Volumes/" + Theme::instance()->appName() + "fs"));

        if (defaultFileStreamMirrorPath.isEmpty() || defaultFileStreamMirrorPath.compare(QString("")) == 0)
            cfgFile.setDefaultFileStreamMirrorPath(QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation) + "/.cachedFiles");
    #endif

#ifdef Q_OS_WIN
    //< Set configuration paths.
    QString m_defaultFileStreamSyncPath = cfgFile.defaultFileStreamSyncPath();
    QString m_defaultFileStreamMirrorPath = cfgFile.defaultFileStreamMirrorPath();
    QString m_defaultFileStreamLetterDrive = cfgFile.defaultFileStreamLetterDrive();

    if (m_defaultFileStreamSyncPath.isEmpty() || m_defaultFileStreamSyncPath.compare(QString("")) == 0)
        cfgFile.setDefaultFileStreamSyncPath(QString("X:/Mi unidad"));

    if (m_defaultFileStreamMirrorPath.isEmpty() || m_defaultFileStreamMirrorPath.compare(QString("")) == 0)
        cfgFile.setDefaultFileStreamMirrorPath(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/cachedFiles");

    if (m_defaultFileStreamLetterDrive.isEmpty() || m_defaultFileStreamLetterDrive.compare(QString("")) == 0)
        cfgFile.setDefaultFileStreamLetterDrive(QString("x"));
#endif
}


void GeneralSettings::slotShowInExplorerNavigationPane(bool checked)
{
    ConfigFile cfgFile;
    cfgFile.setShowInExplorerNavigationPane(checked);
    // Now update the registry with the change.
    FolderMan::instance()->navigationPaneHelper().setShowInExplorerNavigationPane(checked);
}

void GeneralSettings::slotIgnoreFilesEditor()
{
    if (_ignoreEditor.isNull()) {
        ConfigFile cfgFile;
        _ignoreEditor = new IgnoreListEditor(this);
        _ignoreEditor->setAttribute(Qt::WA_DeleteOnClose, true);
        _ignoreEditor->open();
    } else {
        ownCloudGui::raiseDialog(_ignoreEditor);
    }
}

void GeneralSettings::slotCreateDebugArchive()
{
    const auto filename = QFileDialog::getSaveFileName(this, tr("Create Debug Archive"), QString(), tr("Zip Archives") + " (*.zip)");
    if (filename.isEmpty()) {
        return;
    }

    createDebugArchive(filename);
    QMessageBox::information(this, tr("Debug Archive Created"), tr("Debug archive is created at %1").arg(filename));
}

void GeneralSettings::slotShowLegalNotice()
{
    auto notice = new LegalNotice();
    notice->exec();
    delete notice;
}

} // namespace OCC
