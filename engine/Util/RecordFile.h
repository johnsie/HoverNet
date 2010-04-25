// RecordFile.h
//
//
// Copyright (c) 1995-1998 - Richard Langlois and Grokksoft Inc.
//
// Licensed under GrokkSoft HoverRace SourceCode License v1.0(the "License");
// you may not use this file except in compliance with the License.
//
// A copy of the license should have been attached to the package from which
// you have taken this file. If you can not find the license you can not use
// this file.
//
//
// The author makes no representations about the suitability of
// this software for any purpose.  It is provided "as is" "AS IS",
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or
// implied.
//
// See the License for the specific language governing permissions
// and limitations under the License.
//

#ifndef RECORD_FILE_H
#define RECORD_FILE_H

#ifdef _WIN32
#	ifdef MR_ENGINE
#		define MR_DllDeclare   __declspec( dllexport )
#	else
#		define MR_DllDeclare   __declspec( dllimport )
#	endif
#else
#	define MR_DllDeclare
#endif

class MR_RecordFileTable;

class MR_DllDeclare MR_RecordFile : public CFile
{
	private:

		MR_RecordFileTable * mTable;
		int mCurrentRecord;						  // for read and write, -1 = not specified
		BOOL mConstructionMode;

	public:
		// Constructors
		MR_RecordFile();
		virtual ~MR_RecordFile();

		// Creation operations
		BOOL CreateForWrite(const char *pFileName, int pNbRecords, const char *lTitle = NULL);
		BOOL OpenForWrite(const char *pFileName);

		BOOL BeginANewRecord();

		// Read operation
		BOOL OpenForRead(const char *pFileName, BOOL pValidateChkSum = FALSE);
		void SelectRecord(int pRecordNumber);

		// Checksum stuff (Renamed to Reopen for security purpose
#define ApplyChecksum ReOpen
		BOOL ReOpen(const char *pFileName);

#define GetCheckSum GetAlignMode
		DWORD GetAlignMode();

		// File information functions
		int GetNbRecords() const;
		int GetNbRecordsMax() const;
		int GetCurrentRecordNumber() const;

		// Overrided CFile operations
		ULONGLONG GetPosition() const;
		CString GetFileTitle() const;

		BOOL Open(LPCTSTR lpszFileName, UINT nOpenFlags, CFileException * pError = NULL);
		CFile *Duplicate() const;

		LONG Seek(LONG lOff, UINT nFrom);
		void SetLength(DWORD dwNewLen);
		ULONGLONG GetLength() const;

		UINT Read(void *lpBuf, UINT nCount);
		void Write(const void *lpBuf, UINT nCount);

		void LockRange(DWORD dwPos, DWORD dwCount);
		void UnlockRange(DWORD dwPos, DWORD dwCount);

		void Abort();
		void Close();

#ifdef _DEBUG
		void AssertValid() const;
		void Dump(CDumpContext & dc) const;
#endif

};

#undef MR_DllDeclare
#endif
