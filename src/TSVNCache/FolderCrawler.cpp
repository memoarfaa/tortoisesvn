// TortoiseSVN - a Windows shell extension for easy version control

// External Cache Copyright (C) 2005-2010 - TortoiseSVN

// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.

// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software Foundation,
// 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
//

#include "StdAfx.h"
#include ".\foldercrawler.h"
#include "SVNStatusCache.h"
#include "registry.h"
#include "TSVNCache.h"
#include "shlobj.h"
#include "SysInfo.h"


CFolderCrawler::CFolderCrawler(void)
{
    m_hWakeEvent = CreateEvent(NULL,FALSE,FALSE,NULL);
    m_hTerminationEvent = CreateEvent(NULL,TRUE,FALSE,NULL);
    m_lCrawlInhibitSet = 0;
    m_crawlHoldoffReleasesAt = (long)GetTickCount();
    m_bRun = false;
    m_bPathsAddedSinceLastCrawl = false;
    m_bItemsAddedSinceLastCrawl = false;
}

CFolderCrawler::~CFolderCrawler(void)
{
    Stop();
}

void CFolderCrawler::Stop()
{
    m_bRun = false;
    if (m_hTerminationEvent)
    {
        SetEvent(m_hTerminationEvent);
        if(WaitForSingleObject(m_hThread, 4000) != WAIT_OBJECT_0)
        {
            ATLTRACE("Error terminating crawler thread\n");
        }
        m_hThread.CloseHandle();
        m_hTerminationEvent.CloseHandle();
        m_hWakeEvent.CloseHandle();
    }
}

void CFolderCrawler::Initialise()
{
    // Don't call Initialize more than once
    ATLASSERT(!m_hThread);

    // Just start the worker thread.
    // It will wait for event being signaled.
    // If m_hWakeEvent is already signaled the worker thread
    // will behave properly (with normal priority at worst).

    m_bRun = true;
    unsigned int threadId = 0;
    m_hThread = (HANDLE)_beginthreadex(NULL,0,ThreadEntry,this,0,&threadId);
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_IDLE);
}

void CFolderCrawler::AddDirectoryForUpdate(const CTSVNPath& path)
{
    if (!CSVNStatusCache::Instance().IsPathGood(path))
        return;
    {
        AutoLocker lock(m_critSec);

        m_foldersToUpdate.Push(path);
        ATLASSERT(path.IsDirectory() || !path.Exists());
        // set this flag while we are sync'ed
        // with the worker thread
        m_bItemsAddedSinceLastCrawl = true;
    }
    SetEvent(m_hWakeEvent);
}

void CFolderCrawler::AddPathForUpdate(const CTSVNPath& path)
{
    if (!CSVNStatusCache::Instance().IsPathGood(path))
        return;
    {
        AutoLocker lock(m_critSec);
        m_pathsToUpdate.Push(path);
        m_bPathsAddedSinceLastCrawl = true;
    }
    SetEvent(m_hWakeEvent);
}

unsigned int CFolderCrawler::ThreadEntry(void* pContext)
{
    ((CFolderCrawler*)pContext)->WorkerThread();
    return 0;
}

void CFolderCrawler::WorkerThread()
{
    HANDLE hWaitHandles[2];
    hWaitHandles[0] = m_hTerminationEvent;
    hWaitHandles[1] = m_hWakeEvent;
    CTSVNPath workingPath;
    bool bFirstRunAfterWakeup = false;
    DWORD currentTicks = 0;

    for(;;)
    {
        bool bRecursive = !!(DWORD)CRegStdDWORD(_T("Software\\TortoiseSVN\\RecursiveOverlay"), TRUE);

        if (SysInfo::Instance().IsVistaOrLater())
        {
            SetThreadPriority(GetCurrentThread(), THREAD_MODE_BACKGROUND_END);
        }
        DWORD waitResult = WaitForMultipleObjects(_countof(hWaitHandles), hWaitHandles, FALSE, INFINITE);

        // exit event/working loop if the first event (m_hTerminationEvent)
        // has been signaled or if one of the events has been abandoned
        // (i.e. ~CFolderCrawler() is being executed)
        if(m_bRun == false || waitResult == WAIT_OBJECT_0 || waitResult == WAIT_ABANDONED_0 || waitResult == WAIT_ABANDONED_0+1)
        {
            // Termination event
            break;
        }
        CTraceToOutputDebugString::Instance()(_T("FolderCrawler.cpp: waking up crawler\n"));

        // If we get here, we've been woken up by something being added to the queue.
        // However, it's important that we don't do our crawling while
        // the shell is still asking for items
        //
        bFirstRunAfterWakeup = true;
        for(;;)
        {
            if (!m_bRun)
                break;
            if (IsHoldOff())
            {
                Sleep(2000);
                continue;
            }
            // Any locks today?
            if (CSVNStatusCache::Instance().m_bClearMemory)
            {
                CAutoWriteLock writeLock(CSVNStatusCache::Instance().GetGuard());
                CSVNStatusCache::Instance().ClearCache();
                CSVNStatusCache::Instance().m_bClearMemory = false;
            }
            if(m_lCrawlInhibitSet > 0)
            {
                // We're in crawl hold-off
                Sleep(200);
                continue;
            }
            if (bFirstRunAfterWakeup)
            {
                Sleep(20);
                bFirstRunAfterWakeup = false;
                continue;
            }
            if ((m_blockReleasesAt < GetTickCount())&&(!m_blockedPath.IsEmpty()))
            {
                m_blockedPath.Reset();
            }
            CSVNStatusCache::Instance().RemoveTimedoutBlocks();

            if ((m_foldersToUpdate.size() == 0)&&(m_pathsToUpdate.size() == 0))
            {
                // Nothing left to do
                break;
            }
            currentTicks = GetTickCount();
            if (m_pathsToUpdate.size())
            {
                {
                    AutoLocker lock(m_critSec);

                    m_bPathsAddedSinceLastCrawl = false;

                    workingPath = m_pathsToUpdate.Pop();
                    if ((!m_blockedPath.IsEmpty())&&(m_blockedPath.IsAncestorOf(workingPath)))
                    {
                        // move the path to the end of the list
                        m_pathsToUpdate.Push(workingPath);
                        if (m_pathsToUpdate.size() < 3)
                            Sleep(200);
                        continue;
                    }
                }
                // don't crawl paths that are excluded
                if (!CSVNStatusCache::Instance().IsPathAllowed(workingPath))
                    continue;
                if (!CSVNStatusCache::Instance().IsPathGood(workingPath))
                    continue;
                // check if the changed path is inside an .svn folder
                if ((workingPath.IsDirectory()&&(SVNHelper::IsVersioned(workingPath)))||workingPath.IsAdminDir())
                {
                    // we don't crawl for paths changed in a tmp folder inside an .svn folder.
                    // Because we also get notifications for those even if we just ask for the status!
                    // And changes there don't affect the file status at all, so it's safe
                    // to ignore notifications on those paths.
                    if (workingPath.IsAdminDir())
                    {
                        CString lowerpath = workingPath.GetWinPathString();
                        lowerpath.MakeLower();
                        if (lowerpath.Find(_T("\\wc.db-journal"))>0)
                            continue;
                        if (lowerpath.Find(_T("\\wc.db"))>0)
                        {
                            CSVNStatusCache::Instance().WCRoots()->NotifyChange(workingPath);
                        }
                    }
                    else if (!workingPath.Exists())
                    {
                        CAutoWriteLock writeLock(CSVNStatusCache::Instance().GetGuard());
                        CSVNStatusCache::Instance().RemoveCacheForPath(workingPath);
                        continue;
                    }

                    do
                    {
                        workingPath = workingPath.GetContainingDirectory();
                    } while(workingPath.IsAdminDir());

                    {
                        AutoLocker print(critSec);
                        _stprintf_s(szCurrentCrawledPath[nCurrentCrawledpathIndex], MAX_CRAWLEDPATHSLEN, _T("Invalidating and refreshing folder: %s"), workingPath.GetWinPath());
                        nCurrentCrawledpathIndex++;
                        if (nCurrentCrawledpathIndex >= MAX_CRAWLEDPATHS)
                            nCurrentCrawledpathIndex = 0;
                        CTraceToOutputDebugString::Instance()(_T("FolderCrawler.cpp: Invalidating/refreshing folder %s\n"), workingPath.GetWinPath());
                    }
                    InvalidateRect(hWnd, NULL, FALSE);
                    {
                        CAutoReadLock readLock(CSVNStatusCache::Instance().GetGuard());
                        // Invalidate the cache of this folder, to make sure its status is fetched again.
                        CCachedDirectory * pCachedDir = CSVNStatusCache::Instance().GetDirectoryCacheEntry(workingPath);
                        if (pCachedDir)
                        {
                            svn_wc_status_kind status = pCachedDir->GetCurrentFullStatus();
                            pCachedDir->Invalidate();
                            if (workingPath.Exists())
                            {
                                pCachedDir->RefreshStatus(bRecursive);
                                // if the previous status wasn't normal and now it is, then
                                // send a notification too.
                                // We do this here because GetCurrentFullStatus() doesn't send
                                // notifications for 'normal' status - if it would, we'd get tons
                                // of notifications when crawling a working copy not yet in the cache.
                                if ((status != svn_wc_status_normal)&&(pCachedDir->GetCurrentFullStatus() != status))
                                {
                                    CSVNStatusCache::Instance().UpdateShell(workingPath);
                                }
                            }
                            else
                            {
                                CAutoWriteLock writeLock(CSVNStatusCache::Instance().GetGuard());
                                CSVNStatusCache::Instance().RemoveCacheForPath(workingPath);
                            }
                        }
                    }
                    //In case that svn_client_stat() modified a file and we got
                    //a notification about that in the directory watcher,
                    //remove that here again - this is to prevent an endless loop
                    AutoLocker lock(m_critSec);
                    m_pathsToUpdate.erase(workingPath);
                }
                else if (SVNHelper::IsVersioned(workingPath))
                {
                    if (!workingPath.Exists())
                    {
                        CAutoWriteLock writeLock(CSVNStatusCache::Instance().GetGuard());
                        CSVNStatusCache::Instance().RemoveCacheForPath(workingPath);
                        continue;
                    }
                    {
                        AutoLocker print(critSec);
                        _stprintf_s(szCurrentCrawledPath[nCurrentCrawledpathIndex], MAX_CRAWLEDPATHSLEN, _T("Updating path: %s"), workingPath.GetWinPath());
                        nCurrentCrawledpathIndex++;
                        if (nCurrentCrawledpathIndex >= MAX_CRAWLEDPATHS)
                            nCurrentCrawledpathIndex = 0;
                        CTraceToOutputDebugString::Instance()(_T("FolderCrawler.cpp: updating path %s\n"), workingPath.GetWinPath());
                    }
                    InvalidateRect(hWnd, NULL, FALSE);
                    // HasAdminDir() already checks if the path points to a dir
                    DWORD flags = TSVNCACHE_FLAGS_FOLDERISKNOWN;
                    flags |= (workingPath.IsDirectory() ? TSVNCACHE_FLAGS_ISFOLDER : 0);
                    flags |= (bRecursive ? TSVNCACHE_FLAGS_RECUSIVE_STATUS : 0);
                    {
                        CAutoReadLock readLock(CSVNStatusCache::Instance().GetGuard());
                        // Invalidate the cache of folders manually. The cache of files is invalidated
                        // automatically if the status is asked for it and the file times don't match
                        // anymore, so we don't need to manually invalidate those.
                        CCachedDirectory * cachedDir = CSVNStatusCache::Instance().GetDirectoryCacheEntry(workingPath.GetDirectory());
                        if (cachedDir && workingPath.IsDirectory())
                        {
                            cachedDir->Invalidate();
                        }
                        if (cachedDir && cachedDir->GetStatusForMember(workingPath, bRecursive).GetEffectiveStatus() > svn_wc_status_unversioned)
                        {
                            CSVNStatusCache::Instance().UpdateShell(workingPath);
                        }
                    }
                    AutoLocker lock(m_critSec);
                    m_pathsToUpdate.erase(workingPath);
                }
                else
                {
                    if (!workingPath.Exists())
                    {
                        CAutoWriteLock writeLock(CSVNStatusCache::Instance().GetGuard());
                        CSVNStatusCache::Instance().RemoveCacheForPath(workingPath);
                    }
                }
            }
            else if (m_foldersToUpdate.size())
            {
                if (SysInfo::Instance().IsVistaOrLater())
                {
                    SetThreadPriority(GetCurrentThread(), THREAD_MODE_BACKGROUND_BEGIN);
                }
                {
                    AutoLocker lock(m_critSec);

                    m_bItemsAddedSinceLastCrawl = false;

                    // create a new CTSVNPath object to make sure the cached flags are requested again.
                    // without this, a missing file/folder is still treated as missing even if it is available
                    // now when crawling.
                    workingPath = CTSVNPath(m_foldersToUpdate.Pop().GetWinPath());

                    if ((!m_blockedPath.IsEmpty())&&(m_blockedPath.IsAncestorOf(workingPath)))
                    {
                        // move the path to the end of the list
                        m_foldersToUpdate.Push (workingPath);
                        if (m_foldersToUpdate.size() < 3)
                            Sleep(200);
                        continue;
                    }
                }
                if (!CSVNStatusCache::Instance().IsPathAllowed(workingPath))
                    continue;
                if (!CSVNStatusCache::Instance().IsPathGood(workingPath))
                    continue;

                {
                    AutoLocker print(critSec);
                    _stprintf_s(szCurrentCrawledPath[nCurrentCrawledpathIndex], MAX_CRAWLEDPATHSLEN, _T("Crawling folder: %s"), workingPath.GetWinPath());
                    nCurrentCrawledpathIndex++;
                    if (nCurrentCrawledpathIndex >= MAX_CRAWLEDPATHS)
                        nCurrentCrawledpathIndex = 0;
                    CTraceToOutputDebugString::Instance()(_T("FolderCrawler.cpp: Crawling folder %s\n"), workingPath.GetWinPath());
                }
                InvalidateRect(hWnd, NULL, FALSE);
                {
                    CAutoReadLock readLock(CSVNStatusCache::Instance().GetGuard());
                    // Now, we need to visit this folder, to make sure that we know its 'most important' status
                    CCachedDirectory * cachedDir = CSVNStatusCache::Instance().GetDirectoryCacheEntry(workingPath.GetDirectory());
                    // check if the path is monitored by the watcher. If it isn't, then we have to invalidate the cache
                    // for that path and add it to the watcher.
                    if (!CSVNStatusCache::Instance().IsPathWatched(workingPath))
                    {
                        if (SVNHelper::IsVersioned(workingPath))
                            CSVNStatusCache::Instance().AddPathToWatch(workingPath);
                        if (cachedDir)
                            cachedDir->Invalidate();
                        else
                        {
                            CAutoWriteLock writeLock(CSVNStatusCache::Instance().GetGuard());
                            CSVNStatusCache::Instance().RemoveCacheForPath(workingPath);
                        }
                    }
                    if (cachedDir)
                        cachedDir->RefreshStatus(bRecursive);
                }

                // While refreshing the status, we could get another crawl request for the same folder.
                // This can happen if the crawled folder has a lower status than one of the child folders
                // (recursively). To avoid double crawlings, remove such a crawl request here
                AutoLocker lock(m_critSec);
                if (m_bItemsAddedSinceLastCrawl)
                {
                    m_foldersToUpdate.erase (workingPath);
                }
            }
        }
    }
    _endthread();
}

bool CFolderCrawler::SetHoldoff(DWORD milliseconds /* = 500*/)
{
    long tick = (long)GetTickCount();
    bool ret = ((tick - m_crawlHoldoffReleasesAt) > 0);
    m_crawlHoldoffReleasesAt = tick + milliseconds;
    return ret;
}

bool CFolderCrawler::IsHoldOff()
{
    return (((long)GetTickCount() - m_crawlHoldoffReleasesAt) < 0);
}

void CFolderCrawler::BlockPath(const CTSVNPath& path, DWORD ticks)
{
    AutoLocker lock(m_critSec);
    m_blockedPath = path;
    if (ticks == 0)
        m_blockReleasesAt = GetTickCount()+10000;
    else
        m_blockReleasesAt = GetTickCount()+ticks;
}
