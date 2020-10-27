#include "syncwrapper.h"
#include "socketapi.h"

#include <qdir.h>

namespace OCC {

//enum csync_instructions_e {
//    CSYNC_INSTRUCTION_NONE = 0x00000000, /* Nothing to do (UPDATE|RECONCILE) */
//    CSYNC_INSTRUCTION_EVAL = 0x00000001, /* There was changed compared to the DB (UPDATE) */
//    CSYNC_INSTRUCTION_EVAL_RENAME = 0x00000800, /* The file is new, it is the destination of a rename (UPDATE) */
//    CSYNC_INSTRUCTION_NEW = 0x00000008, /* The file is new compared to the db (UPDATE) */
//    CSYNC_INSTRUCTION_IGNORE = 0x00000020, /* The file is ignored (UPDATE|RECONCILE) */

//    CSYNC_INSTRUCTION_UPDATE_METADATA = 0x00000400, /* If the etag has been updated and need to be writen to the db,
//                                                      but without any propagation (UPDATE|RECONCILE) */

//    CSYNC_INSTRUCTION_SYNC = 0x00000040, /* The file need to be pushed to the other remote (RECONCILE) */
//    CSYNC_INSTRUCTION_STAT_ERROR = 0x00000080,
//    CSYNC_INSTRUCTION_ERROR = 0x00000100,
//    CSYNC_INSTRUCTION_TYPE_CHANGE = 0x00000200, /* Like NEW, but deletes the old entity first (RECONCILE)
//                                                      Used when the type of something changes from directory to file
//                                                      or back. */
//    CSYNC_INSTRUCTION_CONFLICT = 0x00000010, /* The file need to be downloaded because it is a conflict (RECONCILE) */
//    CSYNC_INSTRUCTION_REMOVE = 0x00000002, /* The file need to be removed (RECONCILE) */
//    CSYNC_INSTRUCTION_RENAME = 0x00000004, /* The file need to be renamed (RECONCILE) */
//};

Q_LOGGING_CATEGORY(lcSyncWrapper, "nextcloud.gui.wrapper", QtInfoMsg);

SyncWrapper *SyncWrapper::instance()
{
    static SyncWrapper instance;
    return &instance;
}

QString SyncWrapper::getRelativePath(QString path)
{
    QString localPath = QDir::cleanPath(path);
    if (localPath.endsWith('/'))
        localPath.chop(1);

    if (localPath.startsWith('/'))
        localPath.remove(0, 1);

    Folder *folderForPath = FolderMan::instance()->folderForPath(localPath);

    QString folderRelativePath("");
    if (folderForPath)
        folderRelativePath = localPath.mid(folderForPath->cleanPath().length() + 1);

    qDebug() << "Path: " << path;
    qDebug() << "Local Path: " << localPath;
    qDebug() << "Folder Relative Path: " << folderRelativePath;

    return folderRelativePath;
}

//void SyncWrapper::updateSyncQueue(const QString path, bool syncing)
//{
//    _syncDone.insert(path, syncing);
//}

void SyncWrapper::updateFileTree(int type, const QString path)
{
	csync_instructions_e instruction = (type == 2) ? CSYNC_INSTRUCTION_NEW : CSYNC_INSTRUCTION_IGNORE;
    if (SyncJournalDb::instance()->getSyncMode(getRelativePath(path)) != SyncJournalDb::SyncMode::SYNCMODE_OFFLINE) {
        FolderMan::instance()->currentSyncFolder()->updateLocalFileTree(getRelativePath(path), instruction);
    } else {
        FolderMan::instance()->currentSyncFolder()->updateLocalFileTree(getRelativePath(path), CSYNC_INSTRUCTION_NEW);
	}

	FolderMan::instance()->currentSyncFolder()->updateFuseCreatedFile(getRelativePath(path), true);
}

void SyncWrapper::createItemAtPath(const QString path)
{
	FolderMan::instance()->currentSyncFolder()->updateFuseCreatedFile(getRelativePath(path), false);
    sync(path, false, CSYNC_INSTRUCTION_NEW);
}

void SyncWrapper::openFileAtPath(const QString path)
{
    sync(path, true, CSYNC_INSTRUCTION_NEW);
}

void SyncWrapper::writeFileAtPath(const QString path)
{
	FolderMan::instance()->currentSyncFolder()->updateFuseCreatedFile(getRelativePath(path), false);
    sync(path, false, CSYNC_INSTRUCTION_NEW);
}

void SyncWrapper::releaseFileAtPath(const QString path)
{
	FolderMan::instance()->currentSyncFolder()->updateFuseCreatedFile(getRelativePath(path), false);
    sync(path, false, CSYNC_INSTRUCTION_NEW);
}

void SyncWrapper::deleteItemAtPath(const QString path)
{
	FolderMan::instance()->currentSyncFolder()->updateFuseCreatedFile(getRelativePath(path), false);
    sync(path, false, CSYNC_INSTRUCTION_NEW);
}

void SyncWrapper::moveItemAtPath(const QString path)
{
	FolderMan::instance()->currentSyncFolder()->updateFuseCreatedFile(getRelativePath(path), false);
    sync(path, false, CSYNC_INSTRUCTION_NEW);
}

void SyncWrapper::sync(const QString path, bool is_fuse_created_file, csync_instructions_e instruction)
{
    QString folderRelativePath = getRelativePath(path);
    if (!folderRelativePath.isEmpty()) {

			//Prepare to sync
			qCWarning(lcSyncWrapper) << "SyncWrapper::sync #########################################";
            qCWarning(lcSyncWrapper) << "path: " << path;
            qCWarning(lcSyncWrapper) << "relative path: " << folderRelativePath;
            qCWarning(lcSyncWrapper) << "#########################################";

            if (SyncJournalDb::instance()->updateLastAccess(folderRelativePath) == 0)
                qCWarning(lcSyncWrapper) << "Couldn't update file to last access.";

            if (SyncJournalDb::instance()->setSyncModeDownload(folderRelativePath, SyncJournalDb::SyncModeDownload::SYNCMODE_DOWNLOADED_NO) == 0)
                qCWarning(lcSyncWrapper) << "Couldn't set file to SYNCMODE_DOWNLOADED_NO.";

            FolderMan::instance()->currentSyncFolder()->updateLocalFileTree(folderRelativePath, CSYNC_INSTRUCTION_NEW);
            //_syncDone.insert(folderRelativePath, false);
			FolderMan::instance()->scheduleFolder();

    } else {
        emit syncFinish();
	}
}

bool SyncWrapper::shouldSync(const QString path)
{
    //FolderMan::instance()->currentSyncFolder()->updateLocalFileTree(path, CSYNC_INSTRUCTION_NEW);
    // Checks sync mode
    //if (SyncJournalDb::instance()->getSyncMode(path) != SyncJournalDb::SyncMode::SYNCMODE_OFFLINE)
    //    return false;

    // checks if file is cached
    // checks last access

    return true;
}
}
