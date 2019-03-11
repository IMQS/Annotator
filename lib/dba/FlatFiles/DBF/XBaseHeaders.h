#pragma once

#pragma pack(push)
#pragma pack(1)

namespace imqs {
namespace dba {
namespace xbase {

enum OpenFlags {
	OpenFlagCreate                  = 1,
	OpenFlagTolerateMissingMemoFile = 2,
};

/* Affects how the DB interprets fields.
*/
enum CompatibilityMode {
	/* 'Original', which was initially used by Albion.
	FieldTypeNumber fields are represented as PublicFieldReal if their width is greater than 8
	or their decimal width is positive. Otherwise, as PublicFieldInteger.
	*/
	CompatModeOriginal = 1,

	/* BDE Compatible.
	FieldTypeNumber fields are represented as PublicFieldReal if their width is greater than 4
	or their decimal width is positive. Otherwise, as PublicFieldInteger.

	I suspect the origin of this is because they had no 64-bit integers at the time,
	so doubles were the next best thing.
	*/
	CompatModeBDE,

	/* Default.
	FieldTypeNumber fields are represented as PublicFieldReal if their decimal width is positive.
	Otherwise, as PublicFieldInteger.
	*/
	CompatModeDefault
};

enum Constants {
	/* The clicketyclick specification says that a maximum of 128 fields are allowed.
	I have found DBF files in the wild with 160 odd fields, so I raised this number.
	*/
	MaximumFields            = 250,
	MaximumFieldWidth        = 255,
	MaximumDecimalWidth      = 15,
	MaximumFieldName         = 10,
	MaximumTextFieldLength   = 254,
	DefaultIntegerFieldWidth = 12,
	DefaultDoubleFieldWidth  = 16
};

enum DataBytes {
	DataEmptySpace = 0x20
};

enum RecordDeletedFlag {
	RecordDeleted = 0x2A,
	RecordValid   = DataEmptySpace
};

enum TransactionFlag {
	TransactionEnded   = 0,
	TransactionStarted = 1
};

enum EncryptionFlag {
	EncryptionNone      = 0,
	EncryptionEncrypted = 1
};

enum CodePage {
	CodePageUSA = 1,
	CodePageMultilingual,
	CodePageANSI,
	CodePageMacintosh,
	CodePageEEDOS     = 0x64,
	CodePageEEWindows = 0xC8
};

enum PublicFieldType {
	PublicFieldNull,
	PublicFieldText,
	PublicFieldInteger,
	PublicFieldLogical,
	PublicFieldDate,
	PublicFieldDateTime,
	PublicFieldReal
};

enum FieldType {
	FieldTypeNull          = '!',
	FieldTypeText          = 'C',
	FieldTypeNumber        = 'N',
	FieldTypeLogical       = 'L',
	FieldTypeDate          = 'D',
	FieldTypeMemo          = 'M',
	FieldTypeFloat         = 'F',
	FieldTypeBinary        = 'B',
	FieldTypeGeneral       = 'G',
	FieldTypePicture       = 'P',
	FieldTypeCurrency      = 'Y',
	FieldTypeDateTime      = 'T',
	FieldTypeInteger       = 'I',
	FieldTypeVariField     = 'V',
	FieldTypeVariant       = 'X',
	FieldTypeTimeStamp     = '@',
	FieldTypeDouble        = 'O',
	FieldTypeAutoIncrement = '+'
};

enum IndexFieldFlag {
	IndexFieldNone  = 0,
	IndexFieldInMDX = 1
};

struct MemoHeader_V4 {
	uint32_t NextBlock;
	uint32_t Reserved1[3];
	uint8_t  Version;
};

inline void MakeHeaderDate(time::Time date, uint8_t into[3]) {
	int         year;
	time::Month month;
	int         day;
	int         yday;
	date.DateComponents(year, month, day, yday);
	into[0] = (uint8_t)(year - 1900);
	into[1] = (uint8_t)(int) month;
	into[2] = (uint8_t) day;
}

struct Header_V4 {
	uint8_t  Version;
	uint8_t  LastModified[3];
	uint32_t RecordCount;
	uint16_t HeadSize;
	uint16_t RecordSize;
	uint8_t  Reserved1[2];
	uint8_t  IncompleteTransaction; // TransactionFlag
	uint8_t  Encryption;            // EncryptionFlag
	uint8_t  FreeRecordThread[4];
	uint8_t  ReservedMultiUser[8];
	uint8_t  MDXFlag;
	uint8_t  CodePage; // CodePage
	uint8_t  Reserved2[2];
};

struct Header_V7 {
	uint8_t  Version;
	uint8_t  LastModified[3];
	uint32_t RecordCount;
	uint16_t HeadSize;
	uint16_t RecordSize;
	uint8_t  Reserved1[2];
	uint8_t  IncompleteTransaction; // TransactionFlag
	uint8_t  Encryption;            // EncryptionFlag
	uint8_t  ReservedMultiUser[12];
	uint8_t  MDXFlag;
	uint8_t  CodePage; // CodePage
	uint8_t  Reserved2[2];
	uint8_t  LanguageDriverName[32];
	uint8_t  Reserved3[4];
};

struct Field_V4 {
	static const int MaxNameLen = 10;
	uint8_t          Name[11];
	uint8_t          Type; // FieldType
	union {
		uint32_t Address;
		struct
		{
			uint16_t OffsetFoxPro;
			uint16_t OffsetFoxPro_Unused;
		};
	};
	union {
		uint16_t FieldLengthCombined;
		struct
		{
			uint8_t FieldLength;
			uint8_t DecimalCount;
		};
	};
	uint8_t ReservedMultiUser1[2];
	uint8_t WorkAreaId;
	uint8_t ReservedMultiUser2[2];
	uint8_t SetFieldsFlag;
	uint8_t Reserved[7];
	uint8_t IndexField; // IndexFieldFlag
};

struct Field_V7 {
	static const int MaxNameLen = 31;
	uint8_t          Name[32];
	uint8_t          Type; // FieldType
	union {
		uint16_t FieldLengthCombined;
		struct
		{
			uint8_t FieldLength;
			uint8_t DecimalCount;
		};
	};
	uint8_t  Reserved1[2];
	uint8_t  MDXFlag;
	uint8_t  Reserved2[2];
	uint32_t NextAutoIncrement;
	uint8_t  Reserved3[4];
};

} // namespace xbase
} // namespace dba
} // namespace imqs

#pragma pack(pop)
