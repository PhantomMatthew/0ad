/* Copyright (C) 2010 Wildfire Games.
 * This file is part of 0 A.D.
 *
 * 0 A.D. is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * 0 A.D. is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with 0 A.D.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "precompiled.h"
#include "Filesystem.h"

#include "gui/GUIManager.h"
#include "ps/CLogger.h"

#include "lib/res/h_mgr.h"	// h_reload
#include "lib/sysdep/dir_watch.h"


PIVFS g_VFS;

static std::vector<std::pair<FileReloadFunc, void*> > g_ReloadFuncs;

bool VfsFileExists(const PIVFS& vfs, const VfsPath& pathname)
{
	return vfs->GetFileInfo(pathname, 0) == INFO::OK;
}

bool VfsFileExists(const VfsPath& pathname)
{
	return VfsFileExists(g_VFS, pathname);
}

void RegisterFileReloadFunc(FileReloadFunc func, void* obj)
{
	g_ReloadFuncs.push_back(std::make_pair(func, obj));
}

void UnregisterFileReloadFunc(FileReloadFunc func, void* obj)
{
	g_ReloadFuncs.erase(std::remove(g_ReloadFuncs.begin(), g_ReloadFuncs.end(), std::make_pair(func, obj)));
}

// try to skip unnecessary work by ignoring uninteresting notifications.
static bool CanIgnore(const DirWatchNotification& notification)
{
	// ignore directories
	const OsPath& pathname = notification.Pathname();
	if(pathname.IsDirectory())
		return true;

	// ignore uninteresting file types (e.g. temp files, or the
	// hundreds of XMB files that are generated from XML)
	const OsPath extension = pathname.Extension();
	const wchar_t* extensionsToIgnore[] = { L".xmb", L".tmp" };
	for(size_t i = 0; i < ARRAY_SIZE(extensionsToIgnore); i++)
	{
		if(extension == extensionsToIgnore[i])
			return true;
	}

	return false;
}

Status ReloadChangedFiles()
{
	std::vector<DirWatchNotification> notifications;
	RETURN_STATUS_IF_ERR(dir_watch_Poll(notifications));
	for(size_t i = 0; i < notifications.size(); i++)
	{
		if(!CanIgnore(notifications[i]))
		{
			VfsPath pathname;
			RETURN_STATUS_IF_ERR(g_VFS->GetVirtualPath(notifications[i].Pathname(), pathname));
			RETURN_STATUS_IF_ERR(g_VFS->Invalidate(pathname));

			// Tell each hotloadable system about this file change:

			RETURN_STATUS_IF_ERR(g_GUI->ReloadChangedFiles(pathname));

			for (size_t j = 0; j < g_ReloadFuncs.size(); ++j)
				g_ReloadFuncs[j].first(g_ReloadFuncs[j].second, pathname);

			RETURN_STATUS_IF_ERR(h_reload(g_VFS, pathname));
		}
	}
	return INFO::OK;
}


CVFSFile::CVFSFile()
{
}

CVFSFile::~CVFSFile()
{
}

PSRETURN CVFSFile::Load(const PIVFS& vfs, const VfsPath& filename)
{
	// Load should never be called more than once, so complain
	if (m_Buffer)
	{
		ENSURE(0);
		return PSRETURN_CVFSFile_AlreadyLoaded;
	}

	Status ret = vfs->LoadFile(filename, m_Buffer, m_BufferSize);
	if (ret != INFO::OK)
	{
		LOGERROR(L"CVFSFile: file %ls couldn't be opened (vfs_load: %lld)", filename.string().c_str(), ret);
		return PSRETURN_CVFSFile_LoadFailed;
	}

	return PSRETURN_OK;
}

const u8* CVFSFile::GetBuffer() const
{
	// Die in a very obvious way, to avoid subtle problems caused by
	// accidentally forgetting to check that the open succeeded
	if (!m_Buffer)
	{
		debug_warn(L"GetBuffer() called with no file loaded");
		throw PSERROR_CVFSFile_InvalidBufferAccess();
	}

	return m_Buffer.get();
}

size_t CVFSFile::GetBufferSize() const
{
	return m_BufferSize;
}

CStr CVFSFile::GetAsString() const
{
	return std::string((char*)GetBuffer(), GetBufferSize());
}
