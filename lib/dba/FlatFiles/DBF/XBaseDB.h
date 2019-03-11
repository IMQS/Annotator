#pragma once

#include "XBaseHeaders.h"

namespace imqs {
namespace dba {
namespace xbase {

/* XBase (aka DBF) file.

This was ported from the Albion code, which supported reading, creating, and modifying
xbase files. During the port, we removed the ability to modify files, since that's not
of interest to us anymore.
*/
class DB {
public:
	static StaticError ErrReadOnly;
	static StaticError ErrNull; // Returned by Read___ functions when they encounter null values
	static StaticError ErrInvalidRecord;
	static StaticError ErrInvalidField;
	static StaticError ErrInvalidData;
	static StaticError ErrTruncated;
	static StaticError ErrMemoTruncated;
	static StaticError ErrNoData;
	static StaticError ErrWrongMethod;
	static StaticError ErrNeedMoreSpace;
	static StaticError ErrUnableToOpenMemo;

	struct Field {
		std::string      Name;
		xbase::FieldType Type;
		PublicFieldType  PublicType;
		int              Length;
		int              PublicLength;
		int              Decimals;
		int              Offset;
	};

	DB();
	~DB();

	Error Open(std::string filename, int openFlags = 0);
	Error Close();

	// Flush any pending writes, as well as write an updated header.
	Error Flush();

	std::string GetFilename() { return Filename; }

	int RecordCount() { return Head.RecordCount; }
	int FieldCount() { return (int) Fields.size(); }

	Error AddField(std::string title, xbase::PublicFieldType type, int len1, int len2);

	const Field* GetField(int field) { return Fields[field]; }

	int FieldIndexByName(const std::string& name);

	Error SetRecord(int record);                  // Sets the current record.
	Error AddRecord(int& record);                 // Adds a new record to the database and implicitly sets the current record.
	Error ClearRecord();                          // Clears the current record and marks it as unerased.
	Error EraseRecord();                          // Mark the record as erased.
	Error ReadIsErased(bool& isErased);           // Returns true if the current record is erased.
	Error ReadTextA(int field, std::string& str); // Use this to read FieldTypeText and FieldTypeMemo.

	/* Returns the string in an allocated buffer, if possible.
	buffer: The destination buffer, typically existing on the stack.
	buffer_size: Contains the number of characters allocated to the buffer (including the necessary null terminator).
	If the buffer size is not large enough, return ErrNeedMoreSpace.
	ErrNeedMoreSpace is always returned for memo fields.
	There is no way of querying exactly how much space is needed. The intention is that if ReadTextA_Fast fails, you
	will simply resort to ReadTextA.
	*/
	Error ReadTextA_Fast(int field, char* buffer, int buffer_size);

	Error ReadDate(int field, time::Time& date);
	Error ReadInteger(int field, int64_t& val);
	Error ReadLogical(int field, bool& val);
	Error ReadReal(int field, double& val);
	Error ReadIsAttributeEmpty(int field, bool& isEmpty);

	Error WriteTextA(int field, const std::string& str);
	Error WriteDate(int field, time::Time date);
	Error WriteInteger(int field, int64_t val);
	Error WriteLogical(int field, bool val);
	Error WriteReal(int field, double val);
	Error WriteEmpty(int field);

	bool IsOpen() { return File.IsOpen(); }
	bool IsOpenForCreate() { return File.IsOpen() && (FileOpenFlags & OpenFlagCreate) != 0; }

	PublicFieldType    ToPublic(xbase::FieldType ft, int width1, int width2);
	static const char* Describe(PublicFieldType ft);
	static bool        IsValidType(uint8_t type);

	CompatibilityMode CompatMode;

protected:
	int          FileOpenFlags = 0;
	std::string  Filename;
	bool         HeaderDirty = false;
	os::MMapFile File;
	os::MMapFile MemoFile;
	int          MemoFileBlocks = 0;

	uint8_t* CurrentBuff   = nullptr;
	int      CurrentRecord = -1;
	bool     CurrentLoaded = false;
	bool     CurrentDirty  = false;
	uint8_t  EmptySpace    = DataEmptySpace;

	uint8_t* MemoBuff = nullptr;

	struct Header {
		bool HasMemoFile;
		bool IsFoxPro;
		bool MissingMemo;
		bool IsIII;
		bool IsIV;
		bool Seven;
		// The database is not legal, but we have made allowances for it.
		bool    ToleratedBadFields;
		int     MemoBlockSize;
		int     RecordSize;
		int     RecordCount;
		int     HeadTotalSize;
		int64_t RecordStart;
	};

	Header              Head;
	std::vector<Field*> Fields;

	std::string MemoFilePath();
	void        SetupNewHeader(int openFlags);
	Error       OpenMemoFile();
	void        CommitNewFile();
	Error       WriteHeader();

	FieldType ToPrivate(xbase::PublicFieldType ft, int& width1, int& width2);

	bool CheckField(int field);
	bool CheckCurrent() { return CurrentRecord >= 0 && CurrentRecord < Head.RecordCount; }
	int  ToPublicLength(FieldType type, int len);

	Error ReadHeader();
	Error ReadFields();
	Error ReadField(Field* f, Field_V4& desc);
	Error ReadField(Field* f, Field_V7& desc);

	void FixupTitle(std::string& title);

	int64_t RecordPos(int record) const;
	Error   ReadCurrent();
	Error   WriteCurrent();
	Error   FlushWrites();

	Error PrepareRead(int field);
	Error PrepareWrite(int field);

	void  LocateData(int field, int& start, int& len);
	Error WriteTextual(int field, const char* str);

	void Swap(double& v);
	void Swap(int32_t& v);
	void Swap(int64_t& v);

	Error ReadMemoA(int field, std::string& str);
	Error ReadMemoA_III(int field, std::string& str);
	Error ReadMemoA_IV(int field, std::string& str);

	bool IsMemoOpen() { return MemoFile.IsOpen(); }
};

} // namespace xbase
} // namespace dba
} // namespace imqs