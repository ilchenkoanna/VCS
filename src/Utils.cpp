#include "Utils.h"
#include <QMessageBox>
#include <QDialogButtonBox>

///////////////////////////////////////////////////////////////////////////////
QMessageBox::StandardButton DialogQuery(QWidget *parent, const QString &title, const QString &query, QMessageBox::StandardButtons buttons)
{
	QMessageBox mb(QMessageBox::Question, title, query, buttons, parent, Qt::Dialog | Qt::MSWindowsFixedSizeDialogHint | Qt::Sheet );
	mb.setDefaultButton(QMessageBox::No);
	mb.setWindowModality(Qt::WindowModal);
	mb.setModal(true);
	mb.exec();
	QMessageBox::StandardButton res = mb.standardButton(mb.clickedButton());
	return res;
}

//-----------------------------------------------------------------------------

#ifdef Q_WS_WIN
// Explorer File Context Menu support. Based on http://www.microsoft.com/msj/0497/wicked/wicked0497.aspx
#include <shlobj.h>

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
//
//  FUNCTION:       DoExplorerMenu
//
//  DESCRIPTION:    Given a path name to a file or folder object, displays
//                  the shell's context menu for that object and executes
//                  the menu command (if any) selected by the user.
//
//  INPUT:          hwnd    = Handle of the window in which the menu will be
//                            displayed.
//
//                  pszPath = Pointer to an ANSI or Unicode string
//                            specifying the path to the object.
//
//                  point   = x and y coordinates of the point where the
//                            menu's upper left corner should be located, in
//                            client coordinates relative to hwnd.
//
//  RETURNS:        TRUE if successful, FALSE if not.
//
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
bool ShowExplorerMenu(HWND hwnd, const QString &path, const QPoint &qpoint)
{
	struct Util
	{
		//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
		//  FUNCTION:       GetNextItem
		//  DESCRIPTION:    Finds the next item in an item ID list.
		//  INPUT:          pidl = Pointer to an item ID list.
		//  RETURNS:        Pointer to the next item.
		//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
		static LPITEMIDLIST GetNextItem (LPITEMIDLIST pidl)
		{
			USHORT nLen;

			if ((nLen = pidl->mkid.cb) == 0)
				return NULL;

			return (LPITEMIDLIST) (((LPBYTE) pidl) + nLen);
		}

		//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
		//  FUNCTION:       GetItemCount
		//  DESCRIPTION:    Computes the number of item IDs in an item ID list.
		//  INPUT:          pidl = Pointer to an item ID list.
		//  RETURNS:        Number of item IDs in the list.
		//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
		static UINT GetItemCount (LPITEMIDLIST pidl)
		{
			USHORT nLen;
			UINT nCount;

			nCount = 0;
			while ((nLen = pidl->mkid.cb) != 0) {
				pidl = GetNextItem (pidl);
				nCount++;
			}
			return nCount;
		}

		static LPITEMIDLIST DuplicateItem (LPMALLOC pMalloc, LPITEMIDLIST pidl)
		{
			USHORT nLen;
			LPITEMIDLIST pidlNew;

			nLen = pidl->mkid.cb;
			if (nLen == 0)
				return NULL;

			pidlNew = (LPITEMIDLIST) pMalloc->Alloc (
				nLen + sizeof (USHORT));
			if (pidlNew == NULL)
				return NULL;

			CopyMemory (pidlNew, pidl, nLen);
			*((USHORT*) (((LPBYTE) pidlNew) + nLen)) = 0;

			return pidlNew;
		}
	};


	LPITEMIDLIST pidlMain, pidlItem, pidlNextItem, *ppidl;
	UINT nCount;
	POINT point;
	point.x = qpoint.x();
	point.y = qpoint.y();

	WCHAR wchPath[MAX_PATH];
	memset(wchPath, 0, sizeof(wchPath));
	Q_ASSERT(path.length()<MAX_PATH);
	path.toWCharArray(wchPath);

	//
	// Get pointers to the shell's IMalloc interface and the desktop's
	// IShellFolder interface.
	//
	bool bResult = false;
	
	LPMALLOC pMalloc;
	if (!SUCCEEDED (SHGetMalloc (&pMalloc)))
		return bResult;

	LPSHELLFOLDER psfFolder;
	if (!SUCCEEDED (SHGetDesktopFolder (&psfFolder))) {
		pMalloc->Release();
		return bResult;
	}

	//
	// Convert the path name into a pointer to an item ID list (pidl).
	//
	ULONG ulCount, ulAttr;
	if (SUCCEEDED (psfFolder->ParseDisplayName (hwnd,
		NULL, wchPath, &ulCount, &pidlMain, &ulAttr)) && (pidlMain != NULL)) {

		if ( (nCount = Util::GetItemCount (pidlMain))>0) { // nCount must be > 0

			pidlItem = pidlMain;

			while (--nCount) {
				//
				// Create a 1-item item ID list for the next item in pidlMain.
				//
				pidlNextItem = Util::DuplicateItem (pMalloc, pidlItem);
				if (pidlNextItem == NULL) {
					pMalloc->Free(pidlMain);
					psfFolder->Release ();
					pMalloc->Release ();
					return bResult;
				}

				//
				// Bind to the folder specified in the new item ID list.
				//
				LPSHELLFOLDER psfNextFolder;
				if (!SUCCEEDED (psfFolder->BindToObject (
					pidlNextItem, NULL, IID_IShellFolder, (void**)&psfNextFolder))) {
					pMalloc->Free (pidlNextItem);
					pMalloc->Free (pidlMain);
					psfFolder->Release ();
					pMalloc->Release ();
					return bResult;
				}

				psfFolder->Release ();
				psfFolder = psfNextFolder;

				pMalloc->Free(pidlNextItem);
				pidlItem = Util::GetNextItem (pidlItem);
			}

			ppidl = &pidlItem;
			LPCONTEXTMENU pContextMenu;
			if (SUCCEEDED (psfFolder->GetUIObjectOf (
				hwnd, 1, (const ITEMIDLIST **)ppidl, IID_IContextMenu, NULL, (void**)&pContextMenu))) {

				HMENU hMenu = CreatePopupMenu ();
				if (SUCCEEDED (pContextMenu->QueryContextMenu (
					hMenu, 0, 1, 0x7FFF, CMF_EXPLORE))) {


					//
					// Display the context menu.
					//
					UINT nCmd = TrackPopupMenu (hMenu, TPM_LEFTALIGN |
						TPM_LEFTBUTTON | TPM_RIGHTBUTTON | TPM_RETURNCMD,
						point.x, point.y, 0, hwnd, NULL);


					//
					// If a command was selected from the menu, execute it.
					//
					if (nCmd) {
						CMINVOKECOMMANDINFO ici;
						memset(&ici, 0, sizeof(ici) );
						ici.cbSize          = sizeof (CMINVOKECOMMANDINFO);
						ici.fMask           = 0;
						ici.hwnd            = hwnd;
						ici.lpVerb          = MAKEINTRESOURCEA (nCmd - 1);
						ici.lpParameters    = NULL;
						ici.lpDirectory     = NULL;
						ici.nShow           = SW_SHOWNORMAL;
						ici.dwHotKey        = 0;
						ici.hIcon           = NULL;

						if (SUCCEEDED (
							pContextMenu->InvokeCommand (
							(CMINVOKECOMMANDINFO*)&ici)))
							bResult = true;
					}
				}
				DestroyMenu (hMenu);
				pContextMenu->Release ();
			}
		}
		pMalloc->Free (pidlMain);
	}

	//
	// Clean up and return.
	//
	psfFolder->Release ();
	pMalloc->Release ();

	return bResult;
}

#endif

