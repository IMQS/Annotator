#include "pch.h"
#include "XBaseDB.h"

#ifdef _MSC_VER
#define gcvt _gcvt
#endif

namespace imqs {
namespace dba {
namespace xbase {

StaticError DB::ErrReadOnly("Cannot write, DBF is opened for read only");
StaticError DB::ErrNull("Null Data");
StaticError DB::ErrInvalidRecord("Invalid record number");
StaticError DB::ErrInvalidField("Invalid record number");
StaticError DB::ErrInvalidData("Invalid dbf data");
StaticError DB::ErrTruncated("File is truncated");
StaticError DB::ErrMemoTruncated("Memo file is truncated");
StaticError DB::ErrNoData("No data");
StaticError DB::ErrWrongMethod("Method not applicable to this field type");
StaticError DB::ErrNeedMoreSpace("Need more buffer space");
StaticError DB::ErrUnableToOpenMemo("Unable to open memo file");

DB::DB() {
	CompatMode         = CompatModeDefault;
	Head.MemoBlockSize = 512;
	MemoBuff           = new uint8_t[Head.MemoBlockSize];
}

DB::~DB() {
	Close();
	delete[] MemoBuff;
	MemoBuff = nullptr;
}

Error DB::Flush() {
	if (!IsOpen())
		return Error();

	Error err;
	err |= FlushWrites();
	err |= WriteHeader();

	return err;
}

Error DB::Close() {
	if (!IsOpen())
		return Error();

	if (IsOpenForCreate()) {
		auto err = Flush();
		if (!err.OK())
			return err;
	}

	MemoFile.Close();
	MemoFileBlocks = 0;

	for (auto f : Fields)
		delete f;
	Fields.clear();
	CurrentRecord = -1;
	HeaderDirty   = false;
	delete[] CurrentBuff;
	CurrentBuff   = nullptr;
	CurrentLoaded = false;
	CurrentDirty  = false;
	Filename      = "";

	return Error();
}

Error DB::Open(std::string filename, int openFlags) {
	Close();

	bool create   = !!(openFlags & OpenFlagCreate);
	Filename      = filename;
	FileOpenFlags = openFlags;

	if (create) {
		auto err = File.Create(filename);
		if (!err.OK())
			return err;
	} else {
		auto err = File.Open(filename);
		if (!err.OK())
			return err;
	}

	if (create) {
		SetupNewHeader(openFlags);
	} else {
		auto err = ReadHeader();
		if (!err.OK()) {
			Close();
			return err;
		}
	}
	return Error();
}

void DB::SetupNewHeader(int openFlags) {
	FileOpenFlags = openFlags;

	memset(&Head, 0, sizeof(Head));
	Head.HasMemoFile        = false;
	Head.IsFoxPro           = false;
	Head.MissingMemo        = false;
	Head.IsIII              = true;
	Head.IsIV               = false;
	Head.Seven              = false;
	Head.ToleratedBadFields = false;
	Head.MemoBlockSize      = 0;
	Head.RecordSize         = 0;
	Head.RecordCount        = 0;
	Head.HeadTotalSize      = 0;
	Head.RecordStart        = 0;
}

Error DB::ReadHeader() {
	Header_V4 head;

	auto err = File.Seek(0, io::SeekWhence::Begin);
	err |= File.ReadExactly(&head, sizeof(head));
	if (!err.OK())
		return err;

	static_assert(sizeof(Header_V4) == 32, "header size");
	static_assert(sizeof(Field_V4) == 32, "header size");
	static_assert(sizeof(Header_V7) == 68, "header size");
	static_assert(sizeof(Field_V7) == 48, "header size");

	Head.HasMemoFile        = false;
	Head.IsIII              = false;
	Head.IsIV               = false;
	Head.IsFoxPro           = false;
	Head.Seven              = false;
	Head.ToleratedBadFields = false;

	if (head.Version == 0x03) {
		Head.IsIII = true;
	} else if (head.Version == 0x04) {
		Head.IsIV = true;
	} else if (head.Version == 0x30 || head.Version == 0x31) {
		Head.IsFoxPro = true;
	} else if (head.Version == 0x7B) {
		Head.IsIV        = true;
		Head.HasMemoFile = true;
	} else if (head.Version == 0x83) {
		Head.IsIII       = true;
		Head.HasMemoFile = true;
	} else if (head.Version == 0x8B) {
		Head.IsIV        = true;
		Head.HasMemoFile = true;
	} else {
		int ver = head.Version & 7;
		if (ver != 3 && ver != 4) {
			return Error::Fmt("Unsupported DBF version %v", ver);
		}
		Head.IsIII       = ver == 3;
		Head.IsIV        = ver == 4;
		Head.HasMemoFile = (head.Version & 0x80) != 0;
	}

	Head.RecordCount = head.RecordCount;
	Head.RecordSize  = head.RecordSize;
	Head.MissingMemo = false;

	Head.HeadTotalSize = head.HeadSize;

	// open DBT memo file
	if (Head.HasMemoFile) {
		err = OpenMemoFile();
		if (!err.OK()) {
			if (FileOpenFlags & OpenFlagTolerateMissingMemoFile)
				Head.MissingMemo = true;
			else
				return Error::Fmt("Unable to load memo file %v", MemoFilePath());
		}
	}

	if (!ReadFields().OK()) {
		// try version 7.
		Head.Seven = true;
		err        = ReadFields();
		if (!err.OK())
			return err;
	}

	if (Head.IsFoxPro) {
		// skip over Database Container
		err |= File.Seek(263, io::SeekWhence::Current);
	}

	Head.RecordStart = File.Position();

	// had to do this for III files created by paradox
	Head.RecordStart++;

	int64_t remaining = File.Length() - Head.RecordStart;
	int64_t required  = Head.RecordCount * Head.RecordSize; // could add + 1 here for the terminator, but who needs it..
	if (remaining < required)
		return ErrTruncated;

	CurrentBuff = new uint8_t[Head.RecordSize];

	return err;
}

std::string DB::MemoFilePath() {
	std::string base, ext;
	path::SplitExt(Filename, base, ext);
	return base + ".dbt";
}

Error DB::OpenMemoFile() {
	auto err = MemoFile.Open(MemoFilePath());
	if (!err.OK())
		return err;

	MemoHeader_V4 mhead;
	err = MemoFile.ReadExactly(&mhead, sizeof(mhead));
	if (!err.OK())
		return err;

	MemoFileBlocks = int(MemoFile.Length() / Head.MemoBlockSize);

	return Error();
}

Error DB::WriteHeader() {
	if (!IsOpenForCreate() || !HeaderDirty)
		return Error();

	CommitNewFile();

	Header_V4 head;
	memset(&head, 0, sizeof(head));

	bool fresh = IsOpenForCreate();

	if (fresh) {
		head.CodePage = CodePageANSI;
		if (Head.IsIII)
			head.Version = 3;
		else
			head.Version = 4;
		head.HeadSize   = (uint16_t) Head.RecordStart;
		head.RecordSize = Head.RecordSize;
	} else {
		auto err = File.ReadExactlyAt(0, &head, sizeof(head));
		if (!err.OK())
			return err;
	}

	MakeHeaderDate(time::Now(), head.LastModified);
	head.RecordCount = Head.RecordCount;

	auto err = File.Seek(0, io::SeekWhence::Begin);
	err |= File.Write(&head, sizeof(head));

	if (!err.OK())
		return err;

	for (int i = 0; i < Fields.size(); i++) {
		Field*   f = Fields[i];
		Field_V4 field;
		memset(&field, 0, sizeof(field));

		if (fresh) {
			memcpy(field.Name, f->Name.c_str(), f->Name.size());
			field.Name[f->Name.size()] = 0;
			field.Type                 = f->Type;
			field.FieldLength          = f->Length;
			field.DecimalCount         = f->Decimals;
			err |= File.Write(&field, sizeof(field));
		} else {
			// do nothing. We don't support field modifications (but we could conceivably do renames here).
		}
	}

	if (fresh) {
		uint8_t term = 0x0D;
		err |= File.Write(&term, 1);
	}

	if (!err.OK())
		return err;

	HeaderDirty = false;

	return Error();
}

int64_t DB::RecordPos(int record) const {
	return Head.RecordStart + (int64_t) record * (int64_t) Head.RecordSize;
}

Error DB::ReadFields() {
	for (auto f : Fields)
		delete f;
	Fields.clear();

	Error err;
	if (Head.Seven)
		err = File.Seek(sizeof(Header_V7), io::SeekWhence::Begin);
	else
		err = File.Seek(sizeof(Header_V4), io::SeekWhence::Begin);
	if (!err.OK())
		return err;

	// start offset at 1 to skip the record deletion flag
	int offset = 1;
	while (err.OK()) {
		int8_t term = 0;
		err |= File.ReadExactly(&term, sizeof(term));
		err |= File.Seek(-1, io::SeekWhence::Current);
		if (term == 0x0D)
			break;
		if (term == 0x20 && File.Position() >= Head.HeadTotalSize)
			break;

		if (Fields.size() >= MaximumFields)
			return Error::Fmt("Too many fields (max %v)", MaximumFields);

		Field_V4 field4;
		Field_V7 field7;
		if (Head.Seven) {
			err |= File.ReadExactly(&field7, sizeof(field7));

			if (!IsValidType(field7.Type))
				return Error::Fmt("Invalid field type %v", field7.Type);
		} else {
			err |= File.ReadExactly(&field4, sizeof(field4));

			if (!IsValidType(field4.Type))
				return Error::Fmt("Invalid field type %v", field4.Type);

			if (field4.FieldLength == 0 || field4.Name[0] == 0) {
				// probably version 7.
				return Error("Invalid field width or name");
			}
		}

		// ensure null terminator
		field4.Name[Field_V4::MaxNameLen] = 0;
		field7.Name[Field_V7::MaxNameLen] = 0;

		Field* f  = new Field;
		f->Offset = offset;

		if (Head.Seven)
			err |= ReadField(f, field7);
		else
			err |= ReadField(f, field4);

		Fields.push_back(f);
		offset += f->Length;

		// Provides a nice dump of DB fields
		//TRACE( "%03d %011s %03d(%c)\n", f->Length, (LPCSTR) f->Name, f->Type, (char) f->Type );
	}
	if (!err.OK())
		return err;

	// Witness this in sample data\strange\ettsezn.dbf
	// I believe it is an error- but we shall tolerate it.
	// The records seem to have padding on them.
	//if ( offset == Head.RecordSize + 1 && Fields[0]->Type == FieldTypeNumber )
	if (Fields.size() > 1 && offset == Head.RecordSize + Fields.size() - 1) {
		//TRACE("Tolerating DBF with illegal field lengths\n");
		int shift = 0;
		for (int i = 0; i < Fields.size(); i++) {
			Fields[i]->Offset += shift;
			Fields[i]->Length--;
			shift--;
		}
		offset = Head.RecordSize;
	}

	if (!Head.IsFoxPro) {
		// The sum of field lengths may not exceed Head.RecordSize
		if (offset > Head.RecordSize) {
			return Error::Fmt("Inconsistent record size and field width sum (%v vs %v)", offset, Head.RecordSize);
		} else if (offset < Head.RecordSize) {
			// [2013-Feb-05 BMH] I have added tolerance of this condition, because it seems to be benign (ie merely wasted space inside each record).
			// The offset here was 929, and the record size 930. Perhaps the program thought it was being smart and rounded the record size up to a
			// 2 byte multiple?
			//TRACE("Tolerating DBF with less field data than Head.RecordSize. %d bytes are wasted at the end of each record\n", (int) (Head.RecordSize - offset));
		}
	}

	return err;
}

int DB::ToPublicLength(FieldType type, int len) {
	if (type == FieldTypeInteger && len == 4)
		return DefaultIntegerFieldWidth;
	return len;
}

Error DB::ReadField(Field* f, Field_V4& desc) {
	f->Decimals     = desc.DecimalCount;
	f->Length       = desc.FieldLength;
	f->PublicLength = f->Length;
	f->Name         = (char*) desc.Name;
	f->Type         = (FieldType) desc.Type;
	f->PublicType   = ToPublic(f->Type, f->Length, f->Decimals);
	if (Head.IsFoxPro) {
		f->Offset = desc.OffsetFoxPro;
	}
	return Error();
}

Error DB::ReadField(Field* f, Field_V7& desc) {
	f->Decimals     = desc.DecimalCount;
	f->Length       = desc.FieldLength;
	f->Name         = (char*) desc.Name;
	f->Type         = (FieldType) desc.Type;
	f->PublicType   = ToPublic(f->Type, f->Length, f->Decimals);
	f->PublicLength = ToPublicLength(f->Type, f->Length);
	return Error();
}

Error DB::AddField(std::string title, xbase::PublicFieldType type, int len1, int len2) {
	if (!IsOpenForCreate())
		return ErrReadOnly;

	if (RecordCount() > 0)
		return Error("You may only add fields to an empty table");

	if (FieldCount() >= MaximumFields)
		return Error::Fmt("Maximum field count exceeded. Limit is %v", MaximumFields);

	if (type == PublicFieldNull)
		return Error("Invalid field type");

	if (len1 >= MaximumFieldWidth)
		len1 = MaximumFieldWidth;

	if (len2 >= MaximumDecimalWidth)
		len2 = MaximumDecimalWidth;

	FixupTitle(title);
	if (title == "")
		return Error("Empty field name");

	HeaderDirty = true;

	Field* f        = new Field();
	f->Type         = ToPrivate(type, len1, len2);
	f->Name         = title;
	f->Length       = len1;
	f->Decimals     = len2;
	f->Offset       = 0;
	f->PublicType   = type;
	f->PublicLength = ToPublicLength(f->Type, f->Length);

	Fields.push_back(f);

	return Error();
}

FieldType DB::ToPrivate(xbase::PublicFieldType ft, int& width1, int& width2) {
	switch (ft) {
	case PublicFieldNull: return FieldTypeNull;
	case PublicFieldText:
		if (width1 == 0 || width1 > MaximumTextFieldLength) {
			width1 = MaximumTextFieldLength;
			width2 = 0;
			return FieldTypeText;
		} else {
			width2 = 0;
			return FieldTypeText;
		}
	case PublicFieldInteger:
		if (width1 == 0) {
			// use a default width
			width1 = DefaultIntegerFieldWidth;
		} else {
			width1 = math::Clamp(width1, 1, 17);
		}
		width2 = 0;
		return FieldTypeNumber;
	case PublicFieldLogical:
		width1 = 1;
		width2 = 0;
		return FieldTypeLogical;
	case PublicFieldDate:
		width1 = 8;
		width2 = 0;
		return FieldTypeDate;
	case PublicFieldDateTime:
		// no support yet for DateTime
		width1 = 8;
		width2 = 0;
		return FieldTypeDate;
	case PublicFieldReal:
		// Haven't figured FieldTypeDouble out.
		// A real field must have a width2 of at least 1. In CompatModeDefault, this is how we use to determine
		// whether a field is real or integer.
		if (width1 == 0 && width2 == 0) {
			width1 = DefaultDoubleFieldWidth;
			width2 = 8;
		} else if (width2 == 0) {
			width2 = 4;
		}
		return FieldTypeNumber;
	default:
		return FieldTypeNull;
	}
}

void DB::FixupTitle(std::string& title) {
	if (title.size() > MaximumFieldName) {
		title = title.substr(MaximumFieldName);
	}
	for (int i = 0; i < title.size(); i++) {
		char ch = title[i];
		if ((ch >= 'A' && ch <= 'Z') ||
		    (ch >= 'a' && ch <= 'z') ||
		    (ch >= '0' && ch <= '9') ||
		    ch == '_') {
			// ok
		} else {
			title[i] = '_';
		}
	}
	int         digits = 1;
	int         inc    = 0;
	std::string org    = title;
	while (FieldIndexByName(title) >= 0) {
		title = org.substr(MaximumFieldName - digits);
		title += ItoA(inc);
		inc++;
		if (inc == 10 || inc == 100 || inc == 1000)
			digits++;
	}
}

int DB::FieldIndexByName(const std::string& name) {
	for (size_t i = 0; i < Fields.size(); i++) {
		if (strings::eqnocase(Fields[i]->Name, name))
			return (int) i;
	}
	return -1;
}

Error DB::AddRecord(int& record) {
	if (!IsOpenForCreate())
		return ErrReadOnly;

	CommitNewFile();

	auto err = FlushWrites();
	if (!err.OK())
		return err;

	HeaderDirty = true;

	record = Head.RecordCount;
	Head.RecordCount++;

	// initialize the record
	SetRecord(record);
	ClearRecord();

	return Error();
}

Error DB::ClearRecord() {
	if (!IsOpenForCreate())
		return ErrReadOnly;
	if (!CheckCurrent())
		return ErrInvalidRecord;
	CurrentDirty  = true;
	CurrentLoaded = true;
	// set everything except for the first byte, which is the record deleted flag.
	memset(CurrentBuff + 1, EmptySpace, Head.RecordSize - 1);
	CurrentBuff[0] = RecordValid;
	return Error();
}

Error DB::EraseRecord() {
	if (!IsOpenForCreate())
		return ErrReadOnly;
	if (!CheckCurrent())
		return ErrInvalidRecord;
	auto err = ReadCurrent();
	if (!err.OK())
		return err;
	CurrentDirty   = true;
	CurrentBuff[0] = RecordDeleted;
	return Error();
}

Error DB::SetRecord(int record) {
	if (record == CurrentRecord)
		return Error();

	auto err = FlushWrites();
	if (!err.OK())
		return err;

	if (record < 0 || record >= Head.RecordCount)
		return ErrInvalidRecord;

	CurrentRecord = record;
	CurrentDirty  = false;
	CurrentLoaded = false;

	return Error();
}

Error DB::ReadIsErased(bool& isErased) {
	auto err = ReadCurrent();
	if (!err.OK())
		return err;
	isErased = CurrentBuff[0] == RecordDeleted;
	return Error();
}

Error DB::ReadCurrent() {
	if (CurrentRecord < 0)
		return ErrInvalidRecord;
	if (CurrentLoaded)
		return Error();

	auto err = File.ReadExactlyAt(RecordPos(CurrentRecord), CurrentBuff, Head.RecordSize);
	if (!err.OK())
		return err;

	CurrentLoaded = true;
	CurrentDirty  = false;

	return Error();
}

void DB::CommitNewFile() {
	if (Head.RecordSize != 0)
		return;

	// start offset at 1 to skip the record deletion flag
	int offset = 1;
	for (int i = 0; i < Fields.size(); i++) {
		Fields[i]->Offset = offset;
		offset += Fields[i]->Length;
	}

	Head.RecordSize = offset;

	// extra 1 is for the 0dh terminator that immediately follows the field descriptors
	Head.RecordStart = 1 + sizeof(Header_V4) + Fields.size() * sizeof(Field_V4);

	CurrentBuff = new uint8_t[Head.RecordSize];
}

Error DB::FlushWrites() {
	if (CheckCurrent() && CurrentDirty)
		return WriteCurrent();
	return Error();
}

Error DB::WriteCurrent() {
	if (CurrentRecord < 0)
		return Error("Cannot write current - current is -1");
	if (!IsOpenForCreate())
		return ErrReadOnly;

	auto err = File.WriteAt(RecordPos(CurrentRecord), CurrentBuff, Head.RecordSize);
	if (!err.OK())
		return err;

	CurrentLoaded = true;
	CurrentDirty  = false;

	return Error();
}

Error DB::ReadIsAttributeEmpty(int field, bool& isEmpty) {
	auto err = PrepareRead(field);
	if (!err.OK())
		return err;

	isEmpty = true;

	int off = Fields[field]->Offset;
	for (int i = 0; i < Fields[field]->Length; i++) {
		if (CurrentBuff[i] != EmptySpace) {
			isEmpty = false;
			break;
		}
	}

	return Error();
}

Error DB::ReadTextA(int field, std::string& str) {
	auto err = PrepareRead(field);
	if (!err.OK())
		return err;

	if (Fields[field]->Type == FieldTypeMemo)
		return ReadMemoA(field, str);

	int off, len;
	LocateData(field, off, len);

	if (len == 0) {
		str.resize(0);
		return Error();
	}

	str.resize(len);
	char* sdata = &str[0];

	if (Fields[field]->Type == FieldTypeText)
		memcpy(&str[0], CurrentBuff + off, len);

	return Error();
}

Error DB::ReadTextA_Fast(int field, char* buffer, int buffer_size) {
	auto err = PrepareRead(field);
	if (!err.OK())
		return err;

	IMQS_ASSERT(buffer_size >= 1);
	buffer[0] = 0;
	if (Fields[field]->Type == FieldTypeMemo)
		return ErrNeedMoreSpace;

	if (Fields[field]->Type != FieldTypeText)
		return ErrWrongMethod;

	int off, len;
	LocateData(field, off, len);

	if (len == 0)
		return ErrNull;
	else if (len + 1 > buffer_size)
		return ErrNeedMoreSpace;

	memcpy(buffer, CurrentBuff + off, len);
	buffer[len] = 0;

	return Error();
}

Error DB::ReadMemoA(int field, std::string& str) {
	if (Head.IsIII)
		return ReadMemoA_III(field, str);
	else if (Head.IsIV)
		return ReadMemoA_IV(field, str);
	else
		return ErrUnsupported;
}

Error DB::ReadMemoA_III(int field, std::string& str) {
	auto err = PrepareRead(field);
	if (!err.OK())
		return err;
	if (!IsMemoOpen())
		return ErrUnableToOpenMemo;

	int64_t pos = 0;
	err         = ReadInteger(field, pos);
	if (!err.OK())
		return err;

	if (pos == 0)
		return ErrNull;

	uint8_t memoTerm = 0x1A;

	str.resize(0);

	err |= MemoFile.Seek(pos * Head.MemoBlockSize, io::SeekWhence::Begin);
	while (pos < MemoFileBlocks) {
		err |= MemoFile.ReadExactly(MemoBuff, Head.MemoBlockSize);
		if (!err.OK())
			break;
		for (int i = 0; i < Head.MemoBlockSize; i++) {
			if (MemoBuff[i] == memoTerm) {
				return Error();
			} else {
				str += (char) MemoBuff[i];
			}
		}
	}

	return ErrMemoTruncated;
}

Error DB::ReadMemoA_IV(int field, std::string& str) {
	return ErrNull;
}

Error DB::ReadDate(int field, time::Time& date) {
	auto err = PrepareRead(field);
	if (!err.OK())
		return err;

	int  off = Fields[field]->Offset;
	char cyear[5], cmon[3], cday[3];
	cyear[0] = CurrentBuff[off];
	cyear[1] = CurrentBuff[off + 1];
	cyear[2] = CurrentBuff[off + 2];
	cyear[3] = CurrentBuff[off + 3];
	cyear[4] = 0;
	cmon[0]  = CurrentBuff[off + 4];
	cmon[1]  = CurrentBuff[off + 5];
	cmon[2]  = 0;
	cday[0]  = CurrentBuff[off + 6];
	cday[1]  = CurrentBuff[off + 7];
	cday[2]  = 0;

	int year = atoi(cyear);
	int mon  = atoi(cmon);
	int day  = atoi(cday);
	if (year == 0)
		return ErrNull;

	// We blindly assume that dates in DBFs are UTC.
	time::Month m;
	if (mon >= 1 && mon <= 12)
		m = time::Month(mon);
	else
		m = time::Month::January;
	date = time::Time(year, m, day, 0, 0, 0, 0);

	return Error();
}

Error DB::ReadInteger(int field, int64_t& val) {
	auto err = PrepareRead(field);
	if (!err.OK())
		return err;

	if (Fields[field]->Type == FieldTypeInteger) {
		int32_t* p32 = (int32_t*) (CurrentBuff + Fields[field]->Offset);
		int32_t  i32 = *p32;
		Swap(i32);
		// i don't understand, but this is taken from BDE
		if (i32 == 0x7FFFFFFF)
			i32 = 0xFFFFFFFF;
		else if (i32 != 0xFFFFFFFF)
			i32 &= 0x00FFFFFF;
		val = i32;
	} else {
		int start, len;
		LocateData(field, start, len);

		if (len == 0)
			return ErrNull;
		if (len > 63)
			return ErrInvalidData;
		char buff[64];
		memcpy(buff, CurrentBuff + start, len);
		buff[len] = 0;

		val = AtoI64(buff);
	}

	return Error();
}

Error DB::ReadLogical(int field, bool& val) {
	auto err = PrepareRead(field);
	if (!err.OK())
		return err;

	if (Fields[field]->Type != FieldTypeLogical)
		return ErrWrongMethod;

	char cb = CurrentBuff[Fields[field]->Offset];
	if (cb == '?' || cb == ' ') {
		val = false;
		return ErrNull;
	}

	val = cb == 'T' || cb == 't' || cb == 'Y' || cb == 'y';

	return Error();
}

Error DB::ReadReal(int field, double& val) {
	auto err = PrepareRead(field);
	if (!err.OK())
		return err;

	if (Fields[field]->Type == FieldTypeDouble) {
		double* pv = (double*) (CurrentBuff + Fields[field]->Offset);
		val        = *pv;
		Swap(val);
		// Don't know why this is so.
		val = -val;
	} else if (Fields[field]->Type == FieldTypeInteger) {
		int64_t ival;
		err = ReadInteger(field, ival);
		val = (double) ival;
		return err;
	} else {
		int start, len;
		LocateData(field, start, len);

		if (len == 0)
			return ErrNull;
		if (len > 63)
			return ErrInvalidData;

		char buff[64];
		memcpy(buff, CurrentBuff + start, len);
		buff[len] = 0;

		// I have found that a string of asterisks represents empty.
		int asterisks = 0;
		for (int i = 0; i < len; i++)
			asterisks += buff[i] == '*' ? 1 : 0;
		if (asterisks == len)
			return ErrNull;

		val = atof(buff);
	}

	return Error();
}

Error DB::WriteEmpty(int field) {
	auto err = PrepareWrite(field);
	if (!err.OK())
		return err;
	CurrentDirty = true;

	uint8_t* start = (CurrentBuff + Fields[field]->Offset);
	for (int i = 0; i < Fields[field]->Length; i++) {
		start[i] = EmptySpace;
	}

	return Error();
}

Error DB::WriteTextual(int field, const char* str) {
	// DBF padding (always with spaces) is as follows:
	// Text:	Back
	// Numeric: Front
	// If necessary, we chop trailing characters.
	uint8_t* start = (uint8_t*) (CurrentBuff + Fields[field]->Offset);
	int      len   = Fields[field]->Length;

	bool backPad = Fields[field]->Type == FieldTypeText;

	int slen    = (int) strlen(str);
	int padding = len - slen;
	padding     = std::max(padding, 0);

	int i = 0;
	if (!backPad) {
		// front pad
		for (; i < padding; i++)
			start[i] = EmptySpace;
	}

	for (int j = 0; i < len && str[j] != 0; i++, j++)
		start[i] = str[j];

	// back pad
	for (; i < len; i++)
		start[i] = EmptySpace;

	return Error();
}

Error DB::WriteTextA(int field, const std::string& str) {
	auto err = PrepareWrite(field);
	if (!err.OK())
		return err;
	CurrentDirty = true;

	return WriteTextual(field, str.c_str());
}

Error DB::WriteDate(int field, time::Time date) {
	auto err = PrepareWrite(field);
	if (!err.OK())
		return err;
	CurrentDirty = true;

	int off = Fields[field]->Offset;

	int year = 0, month = 0, day = 0;

	if (!date.IsNull()) {
		// We blindly assume that dates in DBFs are UTC.
		int         yday;
		time::Month mon;
		date.DateComponents(year, mon, day, yday);
		month = (int) mon;
	}

	char cyear[34], cmon[34], cday[34];
	ItoA(year, cyear, 10);
	ItoA(month, cmon, 10);
	ItoA(day, cday, 10);
	cyear[arraysize(cyear) - 1] = 0;
	cmon[arraysize(cmon) - 1]   = 0;
	cday[arraysize(cday) - 1]   = 0;
	int yearlen                 = (int) strlen(cyear);
	int monlen                  = (int) strlen(cmon);
	int daylen                  = (int) strlen(cday);
	for (int i = 0; i < 4 - yearlen; i++) {
		cyear[4] = cyear[3];
		cyear[3] = cyear[2];
		cyear[2] = cyear[1];
		cyear[1] = cyear[0];
		cyear[0] = '0';
	}
	if (monlen == 1) {
		cmon[2] = cmon[1];
		cmon[1] = cmon[0];
		cmon[0] = '0';
	}
	if (daylen == 1) {
		cday[2] = cday[1];
		cday[1] = cday[0];
		cday[0] = '0';
	}

	CurrentBuff[off]     = cyear[0];
	CurrentBuff[off + 1] = cyear[1];
	CurrentBuff[off + 2] = cyear[2];
	CurrentBuff[off + 3] = cyear[3];

	CurrentBuff[off + 4] = cmon[0];
	CurrentBuff[off + 5] = cmon[1];

	CurrentBuff[off + 6] = cday[0];
	CurrentBuff[off + 7] = cday[1];

	return Error();
}

Error DB::WriteInteger(int field, int64_t val) {
	auto err = PrepareWrite(field);
	if (!err.OK())
		return err;
	CurrentDirty = true;

#pragma warning(push)
#pragma warning(disable : 4996) // CRT security

	static const int BUFFLEN       = 64;
	char             buff[BUFFLEN] = "";
	int              off           = Fields[field]->Offset;
	int32_t          v32           = (int32_t) val;

	switch (Fields[field]->Type) {
	case FieldTypeAutoIncrement:
	case FieldTypeInteger: {
		int32_t* p32 = (int32_t*) (CurrentBuff + off);
		*p32         = v32;
		break;
	}
	case FieldTypeNumber:
	case FieldTypeFloat:
		I64toA(val, buff, 10);
		buff[BUFFLEN - 1] = 0;
		WriteTextual(field, buff);
		break;
	default:
		return ErrWrongMethod;
	}
#pragma warning(pop)

	return Error();
}

Error DB::WriteLogical(int field, bool val) {
	auto err = PrepareWrite(field);
	if (!err.OK())
		return err;
	CurrentDirty = true;

	char* obj = (char*) (CurrentBuff + Fields[field]->Offset);
	*obj      = val ? 'T' : 'F';

	return Error();
}

Error DB::WriteReal(int field, double val) {
	auto err = PrepareWrite(field);
	if (!err.OK())
		return err;
	CurrentDirty = true;

	const int BUFFLEN       = 64;
	char      buff[BUFFLEN] = "";
	int       off           = Fields[field]->Offset;
	int       len           = Fields[field]->Length;
	len                     = std::min(len, BUFFLEN - 1);
	int32_t v32             = (int32_t) val;

	switch (Fields[field]->Type) {
	case FieldTypeAutoIncrement:
	case FieldTypeInteger: {
		int32_t* p32 = (int32_t*) (CurrentBuff + off);
		*p32         = v32;
		break;
	}
	case FieldTypeDouble: {
		double* pd = (double*) (CurrentBuff + off);
		*pd        = val;
		Swap(*pd);
		break;
	}
	case FieldTypeNumber:
	case FieldTypeFloat:
		// This was first noticed when writing 0.097000000000000003 into a 20 character field. gcvt(v, 20) yields 9.7000000000000003e-002.
		for (int tlen = len; tlen > 0; tlen--) {
			gcvt(val, tlen, buff);
			if (strlen(buff) <= len)
				break;
		}
		WriteTextual(field, buff);
		break;
	default:
		return ErrWrongMethod;
	}

	return Error();
}

void DB::LocateData(int field, int& start, int& len) {
	int foff = Fields[field]->Offset;
	int flen = Fields[field]->Length;
	start    = foff;
	len      = 0;
	int  i;
	int  firstTok  = -1;
	int  lastTok   = -1;
	int  tok       = 0;
	int  zeros     = 0;
	bool spaceEnds = false;

	switch (Fields[field]->Type) {
	case FieldTypeLogical:
		len = 1;
		return;
	case FieldTypeDate:
		len = 8;
		return;
	case FieldTypeText:
		// text may have spaces on the left side, but not on the right (daft!)
		for (i = foff; i < foff + flen; i++) {
			zeros += CurrentBuff[i] == 0;
			if (CurrentBuff[i] != 0x20) {
				if (firstTok == -1)
					firstTok = i;
				lastTok = i;
			}
		}
		if (zeros == flen)
			len = 0;
		else {
			if (firstTok == -1)
				len = 0;
			else
				len = 1 + lastTok - start;
		}
		return;
	case FieldTypeFloat:
	case FieldTypeNumber:
	case FieldTypeMemo: // for reading memo location
		spaceEnds = true;
		for (i = foff; i < foff + flen; i++) {
			if (CurrentBuff[i] != 0x20)
				lastTok = i;

			if (tok == 0) {
				if (CurrentBuff[i] != 0x20) {
					start = i;
					tok++;
				}
			} else {
				if ((CurrentBuff[i] == 0x20 && spaceEnds) || (CurrentBuff[i] == 0)) {
					len = i - start;
					return;
				} else if (CurrentBuff[i] != 0x20)
					tok++;
			}
		}
		if (tok == 0)
			len = 0;
		else
			len = 1 + lastTok - start;
		return;
	case FieldTypeInteger: len = 4; return;
	case FieldTypeDouble: len = 8; return;
	default:
		len = 0;
		return;
	}
}

Error DB::PrepareRead(int field) {
	if (!CheckField(field))
		return ErrInvalidField;
	return ReadCurrent();
}

Error DB::PrepareWrite(int field) {
	if (!IsOpenForCreate())
		return ErrReadOnly;
	if (!CheckField(field))
		return ErrInvalidField;
	return ReadCurrent();
}

bool DB::CheckField(int field) {
	if (field < 0 || field >= Fields.size())
		return false;
	return true;
}

const char* DB::Describe(PublicFieldType ft) {
	switch (ft) {
	case PublicFieldNull: return "Null";
	case PublicFieldText: return "Text";
	case PublicFieldInteger: return "Integer";
	case PublicFieldLogical: return "Logical";
	case PublicFieldDate: return "Date";
	case PublicFieldDateTime: return "DateTime";
	case PublicFieldReal: return "Real";
	default: return "Unknown";
	}
}

PublicFieldType DB::ToPublic(xbase::FieldType ft, int width1, int width2) {
	int realCut = CompatMode == CompatModeBDE ? 4 : 8;

	switch (ft) {
	case FieldTypeNull: return PublicFieldNull;
	case FieldTypeText: return PublicFieldText;
	case FieldTypeNumber:
		if (CompatMode != CompatModeDefault) {
			// for width1 > 8, width2 = 0, we could use Int64--
			// but for compatibility with BDE we leave it at real
			if (width1 > realCut)
				return PublicFieldReal;
			if (width2 == 0)
				return PublicFieldInteger;
			else
				return PublicFieldReal;
		} else {
			return width2 == 0 ? PublicFieldInteger : PublicFieldReal;
		}
	case FieldTypeLogical: return PublicFieldLogical;
	case FieldTypeDate: return PublicFieldDate;
	case FieldTypeMemo: return PublicFieldText;
	case FieldTypeFloat: return PublicFieldReal;
	case FieldTypeBinary: return PublicFieldNull;
	case FieldTypeGeneral: return PublicFieldNull;
	case FieldTypePicture: return PublicFieldNull;
	case FieldTypeCurrency: return PublicFieldNull;
	case FieldTypeDateTime: return PublicFieldDateTime;
	case FieldTypeInteger: return PublicFieldInteger;
	case FieldTypeVariField: return PublicFieldNull;
	case FieldTypeVariant: return PublicFieldNull;
	case FieldTypeTimeStamp: return PublicFieldNull;
	case FieldTypeDouble: return PublicFieldReal;
	case FieldTypeAutoIncrement: return PublicFieldNull;
	default: return PublicFieldNull;
	}
}

bool DB::IsValidType(uint8_t type) {
	return type == FieldTypeNull ||
	       type == FieldTypeText ||
	       type == FieldTypeNumber ||
	       type == FieldTypeLogical ||
	       type == FieldTypeDate ||
	       type == FieldTypeMemo ||
	       type == FieldTypeFloat ||
	       type == FieldTypeBinary ||
	       type == FieldTypeGeneral ||
	       type == FieldTypePicture ||
	       type == FieldTypeCurrency ||
	       type == FieldTypeDateTime ||
	       type == FieldTypeInteger ||
	       type == FieldTypeVariField ||
	       type == FieldTypeVariant ||
	       type == FieldTypeTimeStamp ||
	       type == FieldTypeDouble ||
	       type == FieldTypeAutoIncrement;
}

void DB::Swap(int64_t& v) {
	int64_t  n = 0;
	uint8_t* a = (uint8_t*) &v;
	uint8_t* b = (uint8_t*) &n;
	b[0]       = a[7];
	b[1]       = a[6];
	b[2]       = a[5];
	b[3]       = a[4];
	b[4]       = a[3];
	b[5]       = a[2];
	b[6]       = a[1];
	b[7]       = a[0];
	v          = n;
}

void DB::Swap(double& v) {
	double   n = 0;
	uint8_t* a = (uint8_t*) &v;
	uint8_t* b = (uint8_t*) &n;
	b[0]       = a[7];
	b[1]       = a[6];
	b[2]       = a[5];
	b[3]       = a[4];
	b[4]       = a[3];
	b[5]       = a[2];
	b[6]       = a[1];
	b[7]       = a[0];
	v          = n;
}

void DB::Swap(int32_t& v) {
	int32_t  n = 0;
	uint8_t* a = (uint8_t*) &v;
	uint8_t* b = (uint8_t*) &n;
	b[0]       = a[3];
	b[1]       = a[2];
	b[2]       = a[1];
	b[3]       = a[0];
	v          = n;
}

} // namespace xbase
} // namespace dba
} // namespace imqs
