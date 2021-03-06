/*************************************************************************
*    UrBackup - Client/Server backup system
*    Copyright (C) 2011-2014 Martin Raiber
*
*    This program is free software: you can redistribute it and/or modify
*    it under the terms of the GNU General Public License as published by
*    the Free Software Foundation, either version 3 of the License, or
*    (at your option) any later version.
*
*    This program is distributed in the hope that it will be useful,
*    but WITHOUT ANY WARRANTY; without even the implied warranty of
*    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*    GNU General Public License for more details.
*
*    You should have received a copy of the GNU General Public License
*    along with this program.  If not, see <http://www.gnu.org/licenses/>.
**************************************************************************/

#include "FileSettingsReader.h"
#include "stringtools.h"
#include "Server.h"
#include <iostream>

std::map<std::string, SCachedSettings*>* CFileSettingsReader::settings=new std::map<std::string, SCachedSettings*>;
IMutex* CFileSettingsReader::settings_mutex=NULL;

CFileSettingsReader::CFileSettingsReader(std::string pFile)
{
	std::map<std::string, SCachedSettings*>::iterator iter;
	{
		IScopedLock lock(settings_mutex);
		iter=settings->find(pFile);
	}
	if( iter==settings->end() )
	{
		std::string fdata=getFile(pFile);

		std::vector<SSetting> mSettings;
		int num_lines=linecount(fdata);
		for(int i=0;i<num_lines;++i)
		{
			std::string line=getline(i,fdata);

			if(line.size()<2 || line[0]=='#' )
				continue;

			SSetting setting;
			setting.key=getuntil("=",line);

			if(setting.key=="")
				setting.value=line;
			else
			{
				line.erase(0,setting.key.size()+1);
				setting.value=line;
			}		
			
			mSettings.push_back(setting);
		}

		cached_settings=new SCachedSettings;
		cached_settings->smutex=Server->createMutex();

		for(size_t i=0;i<mSettings.size();++i)
		{
			cached_settings->mSettingsMap[Server->ConvertToUnicode(mSettings[i].key)]=Server->ConvertToUnicode(mSettings[i].value);
		}

		cached_settings->refcount=1;
		cached_settings->key=pFile;

		IScopedLock lock(settings_mutex);
		settings->insert(std::pair<std::string, SCachedSettings*>(pFile, cached_settings) );
	}
	else
	{
		IScopedLock lock(settings_mutex);
		cached_settings=iter->second;
		++cached_settings->refcount;
	}

}

CFileSettingsReader::~CFileSettingsReader()
{
	IScopedLock lock(settings_mutex);
	--cached_settings->refcount;
	if(cached_settings->refcount<=0)
	{
		std::map<std::string, SCachedSettings*>::iterator it=settings->find(cached_settings->key);
		if(it!=settings->end())
		{
			settings->erase(it);
		}
		Server->destroy(cached_settings->smutex);
		delete cached_settings;
	}
}

bool CFileSettingsReader::getValue(std::string key, std::string *value)
{
	std::wstring s_value;
	bool b=getValue( widen(key), &s_value);

	if(b==true)
	{
		std::string nvalue=wnarrow(s_value);
		*value=nvalue;		
		return true;
	}

	return false;
}

bool CFileSettingsReader::getValue(std::wstring key, std::wstring *value)
{
	IScopedLock lock(cached_settings->smutex);

	std::map<std::wstring,std::wstring>::iterator i=cached_settings->mSettingsMap.find(key);
	if( i!= cached_settings->mSettingsMap.end() )
	{
		*value=i->second;
		return true;
	}
	return false;
}

void CFileSettingsReader::cleanup()
{
	{
		IScopedLock lock(settings_mutex);
		for(std::map<std::string, SCachedSettings*>::iterator iter=settings->begin();iter!=settings->end();++iter)
		{
			Server->destroy(iter->second->smutex);
			delete iter->second;
		}
	}
	Server->destroy(settings_mutex);
	delete settings;
}

void CFileSettingsReader::setup()
{
	settings_mutex=Server->createMutex();
}

std::vector<std::wstring> CFileSettingsReader::getKeys()
{
	IScopedLock lock(cached_settings->smutex);

	std::vector<std::wstring> ret;
	for(std::map<std::wstring,std::wstring>::iterator i=cached_settings->mSettingsMap.begin();
		i!=cached_settings->mSettingsMap.end();++i)
	{
		ret.push_back(i->first);
	}
	return ret;
}