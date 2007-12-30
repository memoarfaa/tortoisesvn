#include "stdafx.h"
#include "ProfilingInfo.h"

//////////////////////////////////////////////////////////////////////
// construction / destruction
//////////////////////////////////////////////////////////////////////

CProfilingRecord::CProfilingRecord ( const char* name
                                   , const char* file
                                   , int line)
    : name (name)
    , file (file)
    , line (line)
    , count (0)
    , sum (0)
    , minValue (ULLONG_MAX)
    , maxValue (0)
{
}

//////////////////////////////////////////////////////////////////////
// record values
//////////////////////////////////////////////////////////////////////

void CProfilingRecord::Add (unsigned __int64 value)
{
    ++count;
    sum += value;

    if (value < minValue)
        minValue = value;
    if (value > maxValue)
        maxValue = value;
}

//////////////////////////////////////////////////////////////////////
// modification
//////////////////////////////////////////////////////////////////////

void CProfilingRecord::Reset()
{
    count = 0;
    sum = 0;

    minValue = LLONG_MAX;
    maxValue = 0;
}

//////////////////////////////////////////////////////////////////////
// construction / destruction
//////////////////////////////////////////////////////////////////////

CProfilingInfo::CProfilingInfo()
{
}

CProfilingInfo::~CProfilingInfo(void)
{
	if (records.size() > 0)
	{
		// write profile to file

		TCHAR buffer [MAX_PATH];
		if (GetModuleFileNameEx (GetCurrentProcess(), NULL, buffer, MAX_PATH) > 0)
			try
			{
				std::wstring fileName (buffer);
				fileName += L".profile";

				std::string report = GetInstance()->GetReport();

				CFile file (fileName.c_str(), CFile::modeCreate | CFile::modeWrite );
				file.Write (report.c_str(), (UINT)report.size());
			}
			catch (...)
			{
				// ignore all file errors etc.
			}


		// free data

		for (size_t i = 0; i < records.size(); ++i)
			delete records[i];
	}
}

//////////////////////////////////////////////////////////////////////
// access to default instance
//////////////////////////////////////////////////////////////////////

CProfilingInfo* CProfilingInfo::GetInstance()
{
    static CProfilingInfo instance;
    return &instance;
}

//////////////////////////////////////////////////////////////////////
// create a report
//////////////////////////////////////////////////////////////////////

static std::string IntToStr (unsigned __int64 value)
{
	char buffer[100];
	_ui64toa_s (value, buffer, 100, 10);

	std::string result = buffer;
	for (size_t i = 3; i < result.length(); i += 4)
		result.insert (result.length() - i, 1, ',');

	return result;
};

std::string CProfilingInfo::GetReport() const
{
    enum { LINE_LENGTH = 500 };

    char lineBuffer [LINE_LENGTH];
    const char * const format ="%10s%17s%17s%17s%6s %s\t%s\n";

    std::string result;
    result.reserve (LINE_LENGTH * records.size());
    sprintf_s ( lineBuffer, format
              , "count", "sum", "min", "max"
              , "line", "name", "file");
    result += lineBuffer;

    for ( TRecords::const_iterator iter = records.begin(), end = records.end()
        ; iter != end
        ; ++iter)
    {
        unsigned __int64 minValue = (*iter)->GetMinValue();
        if (minValue == ULLONG_MAX)
            minValue = 0;

        sprintf_s ( lineBuffer, format

                  , IntToStr ((*iter)->GetCount()).c_str()
                  , IntToStr ((*iter)->GetSum()).c_str()
                  , IntToStr (minValue).c_str()
                  , IntToStr ((*iter)->GetMaxValue()).c_str()

                  , IntToStr ((*iter)->GetLine()).c_str()
                  , (*iter)->GetName()
                  , (*iter)->GetFile());

        result += lineBuffer;
    }

    return result;
}

//////////////////////////////////////////////////////////////////////
// add a new record
//////////////////////////////////////////////////////////////////////

CProfilingRecord* CProfilingInfo::Create ( const char* name
                                         , const char* file
                                         , int line)
{
    CProfilingRecord* record = new CProfilingRecord (name, file, line);
    records.push_back (record);

    return record;
}

