/**
* Copyright (c) 2015 Daniel Molkentin <danimo@owncloud.com>. All rights reserved.
*
* This library is free software; you can redistribute it and/or modify it under
* the terms of the GNU Lesser General Public License as published by the Free
* Software Foundation; either version 2.1 of the License, or (at your option)
* any later version.
*
* This library is distributed in the hope that it will be useful, but WITHOUT
* ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
* FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public License for more
* details.
*/

#include "NCContextMenu.h"
#include "NCClientInterface.h"

#include <shobjidl.h>
#include <shlwapi.h>
#include <shellapi.h>
#include <StringUtil.h>
#include <strsafe.h>
#include <assert.h>

extern HINSTANCE g_hInst;
extern long g_cDllRef;

#define IDM_FIRST             0
#define IDM_SHARE             0
#define IDM_DRIVEMENU         1
#define IDM_DRIVEMENU_OFFLINE 2
#define IDM_DRIVEMENU_ONLINE  3
#define IDM_LAST              4

NCContextMenu::NCContextMenu(void) 
    : m_cRef(1)
    , m_pszMenuText(L"&Share")
    , m_pszVerb("ocshare")
    , m_pwszVerb(L"ocshare")
    , m_pszVerbCanonicalName("OCShareViaOC")
    , m_pwszVerbCanonicalName(L"OCShareViaOC")
    , m_pszVerbHelpText("Share via ownCloud")
    , m_pwszVerbHelpText(L"Share via ownCloud")
{
    InterlockedIncrement(&g_cDllRef);
}

NCContextMenu::~NCContextMenu(void)
{
    InterlockedDecrement(&g_cDllRef);
}


void NCContextMenu::OnVerbDisplayFileName(HWND hWnd)
{
    NCClientInterface::ContextMenuInfo info = NCClientInterface::FetchInfo();
    NCClientInterface::ShareObject(std::wstring(m_szSelectedFile));
}


#pragma region IUnknown

// Query to the interface the component supported.
IFACEMETHODIMP NCContextMenu::QueryInterface(REFIID riid, void **ppv)
{
    static const QITAB qit[] =
    {
        QITABENT(NCContextMenu, IContextMenu),
        QITABENT(NCContextMenu, IShellExtInit),
        { 0 },
    };
    return QISearch(this, qit, riid, ppv);
}

// Increase the reference count for an interface on an object.
IFACEMETHODIMP_(ULONG) NCContextMenu::AddRef()
{
    return InterlockedIncrement(&m_cRef);
}

// Decrease the reference count for an interface on an object.
IFACEMETHODIMP_(ULONG) NCContextMenu::Release()
{
    ULONG cRef = InterlockedDecrement(&m_cRef);
    if (0 == cRef) {
        delete this;
    }

    return cRef;
}

#pragma endregion


#pragma region IShellExtInit

// Initialize the context menu handler.
IFACEMETHODIMP NCContextMenu::Initialize(
    LPCITEMIDLIST pidlFolder, LPDATAOBJECT pDataObj, HKEY hKeyProgID)
{
    if (!pDataObj) {
        return E_INVALIDARG;
    }

    HRESULT hr = E_FAIL;

    FORMATETC fe = { CF_HDROP, NULL, DVASPECT_CONTENT, -1, TYMED_HGLOBAL };
    STGMEDIUM stm;

    if (SUCCEEDED(pDataObj->GetData(&fe, &stm))) {
        // Get an HDROP handle.
        HDROP hDrop = static_cast<HDROP>(GlobalLock(stm.hGlobal));
        if (hDrop) {
            // Ignore multi-selections
            UINT nFiles = DragQueryFile(hDrop, 0xFFFFFFFF, NULL, 0);
            if (nFiles == 1) {
                // Get the path of the file.
                if (0 != DragQueryFile(hDrop, 0, m_szSelectedFile,  ARRAYSIZE(m_szSelectedFile)))
                {
                    hr = S_OK;
                }
            }

            GlobalUnlock(stm.hGlobal);
        }

        ReleaseStgMedium(&stm);
    }

    // If any value other than S_OK is returned from the method, the context 
    // menu item is not displayed.
    return hr;
}

#pragma endregion


#pragma region IContextMenu

void InsertSeperator(HMENU hMenu, UINT indexMenu)
{
    // Add a separator.
    MENUITEMINFO sep = { sizeof(sep) };
    sep.fMask = MIIM_TYPE;
    sep.fType = MFT_SEPARATOR;
    InsertMenuItem(hMenu, indexMenu, TRUE, &sep);
}

IFACEMETHODIMP NCContextMenu::QueryContextMenu(HMENU hMenu, UINT indexMenu, UINT idCmdFirst, UINT idCmdLast, UINT uFlags)
{
    //< Comment for file streaming test.
    /*
    // If uFlags include CMF_DEFAULTONLY then we should not do anything.
    if (CMF_DEFAULTONLY & uFlags)
    {
        return MAKE_HRESULT(SEVERITY_SUCCESS, 0, USHORT(0));
    }
	*/

    NCClientInterface::ContextMenuInfo info = NCClientInterface::FetchInfo();

    bool skip = true;
    size_t selectedFileLength = wcslen(m_szSelectedFile);
    for (const std::wstring path : info.watchedDirectories) {
        if (StringUtil::isDescendantOf(m_szSelectedFile, selectedFileLength, path)) {
            skip = false;
            break;
        }
    }

    if (skip) {
        return MAKE_HRESULT(SEVERITY_SUCCESS, 0, USHORT(0));
    }

    InsertSeperator(hMenu, indexMenu);
    indexMenu++;

    // Query the download mode
    std::wstring downloadMode = NCClientInterface::GetDownloadMode(m_szSelectedFile);
    bool checkOnlineItem = downloadMode == L"ONLINE";
    bool checkOfflineItem = downloadMode == L"OFFLINE";

    // Insert the drive Online|Offline submenu
    {
        // Create the submenu
        HMENU hDriveSubMenu = CreateMenu();
        if (!hDriveSubMenu)
            return HRESULT_FROM_WIN32(GetLastError());
        // Setup the "Online" item
        MENUITEMINFO menuInfoDriveOnline { 0 };
        menuInfoDriveOnline.cbSize = sizeof(MENUITEMINFO);
        menuInfoDriveOnline.fMask = MIIM_STRING;
        menuInfoDriveOnline.dwTypeData = &info.streamOnlineItemTitle[0];
        menuInfoDriveOnline.fMask |= MIIM_ID;
        menuInfoDriveOnline.wID = idCmdFirst + IDM_DRIVEMENU_ONLINE;
        menuInfoDriveOnline.fMask |= MIIM_STATE;
        menuInfoDriveOnline.fState = MFS_ENABLED;
        if (checkOnlineItem)
            menuInfoDriveOnline.fState |= MFS_CHECKED;
        // Insert it into the submenu
        if (!InsertMenuItem(hDriveSubMenu,
                0, // At position zero
                TRUE, //  indicates the existing item by using its zero-based position. (For example, the first item in the menu has a position of 0.)
                &menuInfoDriveOnline)) {
            return HRESULT_FROM_WIN32(GetLastError());
        }

        // Setup the "Online" item
        MENUITEMINFO menuInfoDriveOffline { 0 };
        menuInfoDriveOffline.cbSize = sizeof(MENUITEMINFO);
        menuInfoDriveOffline.fMask = MIIM_STRING;
        menuInfoDriveOffline.dwTypeData = &info.streamOfflineItemTitle[0];
        menuInfoDriveOffline.fMask |= MIIM_ID;
        menuInfoDriveOffline.wID = idCmdFirst + IDM_DRIVEMENU_OFFLINE;
        menuInfoDriveOffline.fMask |= MIIM_STATE;
        menuInfoDriveOffline.fState = MFS_ENABLED;
        if (checkOfflineItem)
            menuInfoDriveOffline.fState |= MFS_CHECKED;
        // Insert it into the submenu
        if (!InsertMenuItem(hDriveSubMenu,
                1, // At position one
                TRUE, //  indicates the existing item by using its zero-based position. (For example, the first item in the menu has a position of 0.)
                &menuInfoDriveOffline))
            return HRESULT_FROM_WIN32(GetLastError());

        // Setup the "Share" item
        MENUITEMINFO menuInfoDriveShare { 0 };
        menuInfoDriveShare.cbSize = sizeof(MENUITEMINFO);
        menuInfoDriveShare.fMask = MIIM_STRING;
        menuInfoDriveShare.dwTypeData = &info.shareMenuTitle[0];
        menuInfoDriveShare.fMask |= MIIM_ID;
        menuInfoDriveShare.wID = idCmdFirst + IDM_SHARE;
        menuInfoDriveShare.fMask |= MIIM_STATE;
        menuInfoDriveShare.fState = MFS_ENABLED;

        //if (checkOfflineItem)
        //menuInfoDriveShare.fState |= MFS_CHECKED;

        // Insert it into the submenu
        if (!InsertMenuItem(hDriveSubMenu,
                2, // At position one
                TRUE, //  indicates the existing item by using its zero-based position. (For example, the first item in the menu has a position of 0.)
                &menuInfoDriveShare))
            return HRESULT_FROM_WIN32(GetLastError());

        // Insert the submenu below the "share" item
        MENUITEMINFO hDriveSubMenuInfo;
        hDriveSubMenuInfo.cbSize = sizeof(MENUITEMINFO);
        hDriveSubMenuInfo.fMask = MIIM_SUBMENU | MIIM_STATE | MIIM_STRING;
        hDriveSubMenuInfo.fState = MFS_ENABLED;
        // TODO: obtener el texto del cliente/gui
        hDriveSubMenuInfo.dwTypeData = &info.streamSubMenuTitle[0];
        hDriveSubMenuInfo.hSubMenu = hDriveSubMenu;

        // Insert the subitem into the
        if (!InsertMenuItem(hMenu,
                indexMenu++,
                TRUE,
                &hDriveSubMenuInfo))
            return HRESULT_FROM_WIN32(GetLastError());
    }

    indexMenu++;
    InsertSeperator(hMenu, indexMenu);

    // Return an HRESULT value with the severity set to SEVERITY_SUCCESS.
    // Set the code value to the offset of the largest command identifier
    // that was assigned, plus one (1).

    //< Comment for file streaming test.
    //return MAKE_HRESULT(SEVERITY_SUCCESS, 0, USHORT(IDM_SHARE + 1));

    //< Append for file streaming test.
    return MAKE_HRESULT(SEVERITY_SUCCESS, 0, USHORT(IDM_LAST));
}

IFACEMETHODIMP NCContextMenu::InvokeCommand(LPCMINVOKECOMMANDINFO pici)
{

    // For the Unicode case, if the high-order word is not zero, the 
    // command's verb string is in lpcmi->lpVerbW. 
    if (HIWORD(((CMINVOKECOMMANDINFOEX*)pici)->lpVerbW))
    {
        // Is the verb supported by this context menu extension?
        if (StrCmpIW(((CMINVOKECOMMANDINFOEX*)pici)->lpVerbW, m_pwszVerb) == 0)
        {
            OnVerbDisplayFileName(pici->hwnd);
        }
        else
        {
            // If the verb is not recognized by the context menu handler, it 
            // must return E_FAIL to allow it to be passed on to the other 
            // context menu handlers that might implement that verb.
            return E_FAIL;
        }
    }

    // If the command cannot be identified through the verb string, then 
    // check the identifier offset.
    else
    {
        // Is the command identifier offset supported by this context menu 
        // extension?
        if (LOWORD(pici->lpVerb) == IDM_SHARE)
        {
            OnVerbDisplayFileName(pici->hwnd);
        }
        else if (LOWORD(pici->lpVerb) == IDM_DRIVEMENU_ONLINE)
        {
            OnDriveMenuOnline(pici->hwnd);
        }
        else if (LOWORD(pici->lpVerb) == IDM_DRIVEMENU_OFFLINE)
        {
            OnDriveMenuOffline(pici->hwnd);
        }
        else
        {
            // If the verb is not recognized by the context menu handler, it 
            // must return E_FAIL to allow it to be passed on to the other 
            // context menu handlers that might implement that verb.
            return E_FAIL;
        }
    }

    return S_OK;
}

IFACEMETHODIMP NCContextMenu::GetCommandString(UINT_PTR idCommand,
    UINT uFlags, UINT *pwReserved, LPSTR pszName, UINT cchMax)
{
    HRESULT hr = E_INVALIDARG;

    if (idCommand == IDM_SHARE)
    {
        switch (uFlags)
        {
        case GCS_HELPTEXTW:
            // Only useful for pre-Vista versions of Windows that have a 
            // Status bar.
            hr = StringCchCopy(reinterpret_cast<PWSTR>(pszName), cchMax,
                m_pwszVerbHelpText);
            break;

        case GCS_VERBW:
            // GCS_VERBW is an optional feature that enables a caller to 
            // discover the canonical name for the verb passed in through 
            // idCommand.
            hr = StringCchCopy(reinterpret_cast<PWSTR>(pszName), cchMax,
                m_pwszVerbCanonicalName);
            break;

        default:
            hr = S_OK;
        }
    }

    // If the command (idCommand) is not supported by this context menu 
    // extension handler, return E_INVALIDARG.

    return hr;
}

void NCContextMenu::OnDriveMenuOffline(HWND hWnd)
{
    NCClientInterface::SetDownloadMode(std::wstring(m_szSelectedFile), false);
}

void NCContextMenu::OnDriveMenuOnline(HWND hWnd)
{
    NCClientInterface::SetDownloadMode(std::wstring(m_szSelectedFile), true);
}


#pragma endregion
