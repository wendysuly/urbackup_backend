#ifndef CHANGEJOURNALWATCHER_H
#define CHANGEJOURNALWATCHER_H

#include <string>
#include <vector>
#include <windows.h>
#include <WinIoCtl.h>

#include "../Interface/Database.h"
#include "../Interface/Query.h"
#include "PersistentOpenFiles.h"

#include "watchdir/JournalDAO.h"

class DirectoryWatcherThread;

struct SChangeJournal
{
	std::vector<std::wstring> path;
	USN last_record;
	DWORDLONG journal_id;
	HANDLE hVolume;
	_i64 rid;
	std::wstring vol_str;
};

struct SDeviceInfo
{
	bool has_info;
	DWORDLONG journal_id;
	USN last_record;
	bool index_done;
};

class uint128
{
public:
	uint128()
		:lowPart(0), highPart(0) 
	{

	}

	explicit uint128(BYTE data[16])
	{
		memcpy(&lowPart, data, sizeof(lowPart));
		memcpy(&highPart, data+sizeof(lowPart), sizeof(highPart));
	}

	explicit uint128(uint64 lowPart)
		: lowPart(lowPart), highPart(0)
	{

	}

	uint128(uint64 lowPart, uint64 highPart)
		: lowPart(lowPart), highPart(highPart)
	{

	}

	bool hasHighPart()
	{
		return highPart!=0;
	}

	bool operator==(const uint128& other) 
	{
		return lowPart==other.lowPart &&
			highPart==other.highPart;
	}

	bool operator!=(const uint128& other)
	{
		return !(*this==other);
	}

	void set(uint64 low, uint64 high)
	{
		lowPart = low;
		highPart = high;
	}

	uint64 highPart;
	uint64 lowPart;
};

struct UsnInt
{
	DWORD version;
	uint128 FileReferenceNumber;
    uint128 ParentFileReferenceNumber;
	USN Usn;
	DWORD Reason;
	std::wstring Filename;
	USN NextUsn;
	DWORD attributes;
};

const uint128 c_frn_root((uint64)-1, (uint64)-1);

class IChangeJournalListener;

class ChangeJournalWatcher
{
public:
	ChangeJournalWatcher(DirectoryWatcherThread * dwt, IDatabase *pDB);
	~ChangeJournalWatcher(void);

	void watchDir(const std::wstring &dir);

	void update(std::wstring vol_str=L"");
	void update_longliving(void);

	void set_freeze_open_write_files(bool b);

	void set_last_backup_time(int64 t);

	void add_listener(IChangeJournalListener *pListener);

private:
	std::vector<IChangeJournalListener*> listeners;
	std::map<std::wstring, SChangeJournal> wdirs;

	SDeviceInfo getDeviceInfo(const std::wstring &name);
	_i64 hasRoot(const std::wstring &root);
	int64 addFrn(const std::wstring &name, uint128 parent_id, uint128 frn, _i64 rid);
	void addFrnTmp(const std::wstring &name, uint128 parent_id, uint128 frn, _i64 rid);
	void renameEntry(const std::wstring &name, _i64 id, uint128 pid);
	void resetRoot(_i64 rid);
	_i64 hasEntry( _i64 rid, uint128 frn);
	std::vector<uint128> getChildren(uint128 frn, _i64 rid);
	void deleteEntry(_i64 id);
	void deleteEntry(uint128 frn, _i64 rid);
	void saveJournalData(DWORDLONG journal_id, const std::wstring &vol, const UsnInt& rec, USN nextUsn);
	std::vector<UsnInt> getJournalData( const std::wstring &vol);
	void setIndexDone(const std::wstring &vol, int s);
	void deleteJournalData(const std::wstring &vol);
	void deleteJournalId(const std::wstring &vol);

	void deleteWithChildren( uint128 frn, _i64 rid);
	std::wstring getFilename(const SChangeJournal &cj, uint128 frn, bool fallback_to_mft, bool& filter_error, bool& has_error);

	void indexRootDirs(_i64 rid, const std::wstring &root, uint128 parent, size_t& nDirFrns);
	void indexRootDirs2(const std::wstring &root, SChangeJournal *sj, bool& not_supported);

	uint128 getRootFRN( const std::wstring & root );

	void updateWithUsn(const std::wstring &vol, const SChangeJournal &cj, const UsnInt *UsnRecord, bool fallback_to_mft, std::map<std::wstring, bool>& local_open_write_files);

	void reindex(_i64 rid, std::wstring vol, SChangeJournal *sj);
	void logEntry(const std::wstring &vol, const UsnInt *UsnRecord);

	std::wstring getNameFromMFTByFRN(const SChangeJournal &cj, uint128 frn, uint128& parent_frn, bool& has_error);

	void resetAll(const std::wstring& vol);

	IDatabase *db;

	IQuery *q_add_frn_tmp;

	int64 last_index_update;

	bool has_error;
	bool indexing_in_progress;
	std::wstring indexing_volume;

	bool freeze_open_write_files;

	std::map<std::wstring, bool> open_write_files_frozen;

	std::vector<std::wstring> error_dirs;

	DirectoryWatcherThread * dwt;

	int64 last_backup_time;

	JournalDAO journal_dao;
	
	bool unsupported_usn_version_err;

	std::wstring rename_old_name;
	bool usn_logging_enabled;

	size_t num_changes;
	
	PersistentOpenFiles open_write_files;
};

class IChangeJournalListener
{
public:
	virtual int64 getStartUsn(int64 sequence_id)=0;
	virtual void On_FileNameChanged(const std::wstring & strOldFileName, const std::wstring & strNewFileName, bool closed)=0;
	virtual void On_DirNameChanged(const std::wstring & strOldFileName, const std::wstring & strNewFileName, bool closed)=0;
    virtual void On_FileRemoved(const std::wstring & strFileName, bool closed)=0;
    virtual void On_FileAdded(const std::wstring & strFileName, bool closed)=0;
	virtual void On_DirAdded(const std::wstring & strFileName, bool closed)=0;
    virtual void On_FileModified(const std::wstring & strFileName, bool closed)=0;
	virtual void On_FileOpen(const std::wstring & strFileName)=0;
	virtual void On_ResetAll(const std::wstring & vol)=0;
	virtual void On_DirRemoved(const std::wstring & strDirName, bool closed)=0;
	
	struct SSequence
	{
		int64 id;
		int64 start;
		int64 stop;
	};

	virtual void Commit(const std::vector<SSequence>& sequences)=0;
};

#endif //CHANGEJOURNALWATCHER_H