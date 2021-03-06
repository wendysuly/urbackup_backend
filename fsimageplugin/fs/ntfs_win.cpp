#include "ntfs_win.h"
#include <Windows.h>
#include "../../Interface/Server.h"
#include "../../stringtools.h"

FSNTFSWIN::FSNTFSWIN(const std::wstring &pDev, bool read_ahead, bool background_priority)
	: Filesystem(pDev, read_ahead, background_priority), bitmap(NULL)
{
	HANDLE hDev=CreateFileW( pDev.c_str(), GENERIC_READ, FILE_SHARE_READ|FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL|FILE_FLAG_NO_BUFFERING, NULL );
	if(hDev==INVALID_HANDLE_VALUE)
	{
		Server->Log("Error opening device -2", LL_ERROR);
		has_error=true;
		return;
	}

	DWORD sectors_per_cluster;
	DWORD bytes_per_sector;
	DWORD NumberOfFreeClusters;
	DWORD TotalNumberOfClusters;
	BOOL b=GetDiskFreeSpaceW((pDev+L"\\").c_str(), &sectors_per_cluster, &bytes_per_sector, &NumberOfFreeClusters, &TotalNumberOfClusters);
	if(!b)
	{
		Server->Log("Error in GetDiskFreeSpaceW", LL_ERROR);
		has_error=true;
		CloseHandle(hDev);
		return;
	}

	DWORD r_bytes;
	NTFS_VOLUME_DATA_BUFFER vdb;
	b=DeviceIoControl(hDev, FSCTL_GET_NTFS_VOLUME_DATA,  NULL,  0, &vdb,  sizeof(NTFS_VOLUME_DATA_BUFFER), &r_bytes, NULL);
	if(!b)
	{
		Server->Log("Error in DeviceIoControl(FSCTL_GET_NTFS_VOLUME_DATA)", LL_ERROR);
		has_error=true;
		CloseHandle(hDev);
		return;
	}

	sectorsize=bytes_per_sector;
	clustersize=sectorsize*sectors_per_cluster;
	drivesize=vdb.NumberSectors.QuadPart*sectorsize;

	uint64 numberOfClusters=drivesize/clustersize;

	STARTING_LCN_INPUT_BUFFER start;
	LARGE_INTEGER li;
	li.QuadPart=0;
	start.StartingLcn=li;

	size_t new_size=(size_t)(numberOfClusters/8);
	if(numberOfClusters%8!=0)
		++new_size;
			
	size_t n_clusters=new_size;

	new_size+=sizeof(VOLUME_BITMAP_BUFFER);

	VOLUME_BITMAP_BUFFER *vbb=(VOLUME_BITMAP_BUFFER*)(new char[new_size]);

	
	b=DeviceIoControl(hDev, FSCTL_GET_VOLUME_BITMAP, &start, sizeof(STARTING_LCN_INPUT_BUFFER), vbb, (DWORD)new_size, &r_bytes, NULL);
	if(!b)
	{
		Server->Log("DeviceIoControl(FSCTL_GET_VOLUME_BITMAP) failed", LL_ERROR);
		has_error=true;
		CloseHandle(hDev);
		return;
	}

	Server->Log("TotalNumberOfClusters="+nconvert((size_t)TotalNumberOfClusters)+" numberOfClusters="+nconvert(numberOfClusters)+" n_clusters="+nconvert(n_clusters)+" StartingLcn="+nconvert(vbb->StartingLcn.QuadPart)+" BitmapSize="+nconvert(vbb->BitmapSize.QuadPart)+" r_bytes="+nconvert((size_t)r_bytes), LL_DEBUG);

	bitmap=new unsigned char[(unsigned int)(n_clusters)];
	memset(bitmap, 0xFF, n_clusters);
	memcpy(bitmap+(unsigned int)(vbb->StartingLcn.QuadPart/8), vbb->Buffer, (size_t)(vbb->BitmapSize.QuadPart/8));

	delete []vbb;

	CloseHandle(hDev);
}

FSNTFSWIN::~FSNTFSWIN(void)
{
	delete [] bitmap;
}

int64 FSNTFSWIN::getBlocksize(void)
{
	return clustersize;
}

int64 FSNTFSWIN::getSize(void)
{
	return drivesize;
}

const unsigned char * FSNTFSWIN::getBitmap(void)
{
	return bitmap;
}

bool FSNTFSWIN::excludeFiles( const std::wstring& path, const std::wstring& fn_contains )
{
	HANDLE fHandle;
	WIN32_FIND_DATAW wfd;
	std::wstring tpath=path;
	if(!tpath.empty() && tpath[tpath.size()-1]=='\\' ) tpath.erase(path.size()-1, 1);
	fHandle=FindFirstFileW((tpath+L"\\*"+fn_contains+L"*").c_str(),&wfd); 

	if(fHandle==INVALID_HANDLE_VALUE)
	{
		Server->Log(L"Error opening find handle to "+tpath+L" err: "+convert((int)GetLastError()), LL_WARNING);
		return false;
	}

	bool ret=true;
	do
	{
		std::wstring name = wfd.cFileName;
		if(name==L"." || name==L".." )
			continue;

		if(!(wfd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
		{
			if(!excludeFile(tpath+L"\\"+name))
			{
				ret=false;
			}
		}
	}
	while (FindNextFileW(fHandle,&wfd) );
	FindClose(fHandle);

	return ret;
}

bool FSNTFSWIN::excludeSectors( int64 start, int64 count )
{
	for(int64 block=start;block<start+count;++block)
	{
		if(!excludeBlock(block))
		{
			return false;
		}
	}

	return true;
}

bool FSNTFSWIN::excludeBlock( int64 block )
{
	int64 blocksize=getBlocksize();

	size_t bitmap_byte=(size_t)(block/8);
	size_t bitmap_bit=block%8;

	unsigned char b=bitmap[bitmap_byte];

	b=b&(~(1<<(7-bitmap_bit)));

	bitmap[bitmap_byte]=b;

	return true;
}

bool FSNTFSWIN::excludeFile( const std::wstring& path )
{
	Server->Log(L"Trying to exclude contents of file "+path+L" from backup...", LL_DEBUG);

	HANDLE hFile = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_WRITE|FILE_SHARE_READ, NULL,
		OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);

	if(hFile!=INVALID_HANDLE_VALUE)
	{
		STARTING_VCN_INPUT_BUFFER start_vcn = {};
		std::vector<char> ret_buf;
		ret_buf.resize(32768);

		while(true)
		{
			RETRIEVAL_POINTERS_BUFFER* ret_ptrs = reinterpret_cast<RETRIEVAL_POINTERS_BUFFER*>(ret_buf.data());

			DWORD bytesRet = 0;

			BOOL b = DeviceIoControl(hFile, FSCTL_GET_RETRIEVAL_POINTERS,
				&start_vcn, sizeof(start_vcn), ret_ptrs, static_cast<DWORD>(ret_buf.size()), &bytesRet, NULL);

			DWORD err=GetLastError();

			if(b || err==ERROR_MORE_DATA)
			{
				LARGE_INTEGER last_vcn = ret_ptrs->StartingVcn;
				for(DWORD i=0;i<ret_ptrs->ExtentCount;++i)
				{
					//Sparse entries have Lcn -1
					if(ret_ptrs->Extents[i].Lcn.QuadPart!=-1)
					{
						int64 count = ret_ptrs->Extents[i].NextVcn.QuadPart - last_vcn.QuadPart;

						if(!excludeSectors(ret_ptrs->Extents[i].Lcn.QuadPart, count))
						{
							Server->Log(L"Error excluding sectors of file "+path, LL_WARNING);
						}
					}							

					last_vcn = ret_ptrs->Extents[i].NextVcn;
				}
			}

			if(!b)
			{
				if(err==ERROR_MORE_DATA)
				{
					start_vcn.StartingVcn = ret_ptrs->Extents[ret_ptrs->ExtentCount-1].NextVcn;
				}
				else
				{
					Server->Log("Error "+nconvert((int)GetLastError())+" while accessing retrieval points", LL_WARNING);
					CloseHandle(hFile);
					break;
				}
			}
			else
			{
				CloseHandle(hFile);
				break;
			}
		}			
		return true;
	}
	else
	{
		Server->Log(L"Error opening file handle to "+path, LL_WARNING);
		return false;
	}
}
