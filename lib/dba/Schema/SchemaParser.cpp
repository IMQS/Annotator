#include "pch.h"
#include "DB.h"
#include "Geom.h"

using namespace std;
using namespace tsf;

/*
I modified peg/src/compile.c, to output "ptrdiff_t" instead of "long". [BMH 2016-09-27]
*/

namespace imqs {
namespace dba {
namespace schema {

/*
Parse our special schema format....

Example
-------

CREATE TABLE "" mybase
{
	required	polyline	geometry				""
} INDEX(geometry);

CREATE TABLE "HV/MV Lines" hv_lines : mybase
{
	"Group1"
	required	int64		rowid					""
	optional	uuid		node_start_id			"Start Node"	unit:kV

	"Group2"
	optional	uuid		blah					"blahhh Node"
} PRIMARY KEY(rowid);

CREATE TABLE "AssetCapPhoto" AssetCapPhoto
{
	"Main"
	required	int64		rowid				""
	optional	uuid		AssetID				""
	optional	bin			Photo				""

	flags		explicitpull		# For ORM/RPC, do not synchronize record of this table during a regular sync or pull operation.
}
INDEX(AssetID);


Notes
-----

A table without a quoted name is anonymous, and will get discarded at the end of the parse. These tables are intended to be
used only as base tables.

Use a colon to inherit from a base table.

The unit:kV is an example of arbitrary key:value pairs that you can tag onto fields. They must be supported by the internal structures,
so the key types are fixed. Right now "unit" is the only one.

The 'required' type becomes a NOT NULL constraint. 'optional' is simply the opposite of this.

Valid types are:
* text
* int16
* int32
* int64
* float
* double
* date
* uuid
* bin
* point
* multipoint
* polygon
* polyline
* geometry

*/

enum class FieldXData {
	Null,
	Unit,
	Digits,
	Tags,
	UIOrder,
};

enum class RelationXData {
	Null,
	Order,
	Module,
};

struct SuffixSet {
	std::vector<std::string> Originals;
	std::vector<std::string> Clones;
};

static const char SuffixTableJoinChar = '_';

//static std::mutex              ParserLock;

#define YY_CTX_MEMBERS                        \
	bool                     Success;         \
	const char*              Input;           \
	char                     Err[256];        \
	char                     FieldGroup[256]; \
	DB*                      Out;             \
	Table*                   cTable;          \
	Field*                   cField;          \
	FieldXData               cField_XD;       \
	Index*                   cIndex;          \
	Relation*                cRelation;       \
	RelationXData            cRelation_XD;    \
	bool                     IndexUnique;     \
	std::vector<SuffixSet*>* SuffixSets;

#define WRITEX(dst) dst = yytext
#define ZEQ(a, b) (strcmp(a, b) == 0)

#define YY_INPUT(cx, buf, result, max_size) \
	{                                       \
		int i = 0;                          \
		while (i < max_size && *cx->Input)  \
			buf[i++] = *cx->Input++;        \
		result = i;                         \
	}
//yyprintf((stderr, "<%c>", yyc));

typedef struct _yycontext yycontext;

static void SetError(yycontext* yy, const std::string& err);
static void success(yycontext* yy);
static void parse_type(yycontext* yy, const char* t, Field* f);
static void relation_xdata_value(yycontext* yy, const char* t);
static void relation_xdata_key(yycontext* yy, const char* t);
static void relation_local_field(yycontext* yy, const char* t);
static void relation_foreign_field(yycontext* yy, const char* t);
static void field_xdata_value(yycontext* yy, const char* t);
static void field_xdata_key(yycontext* yy, const char* t);
static void begin_table(yycontext* yy);
static void table_name(yycontext* yy, const char* t);
static void suffix_table_start(yycontext* yy);
static void add_suffix_table_original(yycontext* yy, const char* base);
static void add_suffix_table_clone(yycontext* yy, const char* base);
static void table_flags(yycontext* yy, const char* t);
static void begin_hasmany(yycontext* yy, const char* t);
static void begin_hasone(yycontext* yy, const char* t);
static void begin_group(yycontext* yy, const char* t);
static void begin_field(yycontext* yy);
static void begin_index(yycontext* yy);
static void done_index(yycontext* yy);
static void add_to_index(yycontext* yy, const char* t);
static void done_pk(yycontext* yy);
static void version(yycontext* yy, const char* t);

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 6244 6308 6011 28183)
#pragma warning(disable : 4551) // function call missing argument list.. I dunno why leg injects these, but they are unreachable code
#endif

#define YY_PARSE(T) static T
#define YY_CTX_LOCAL 1
//#define YY_DEBUG
#include "SchemaParserLeg.h"

#ifdef _MSC_VER
#pragma warning(pop)
#endif

static void SetError(yycontext* yy, const std::string& err) {
	if (yy->Err[0] != 0)
		return;
	size_t len = std::min(arraysize(yy->Err) - 1, err.length());
	memcpy(yy->Err, err.c_str(), len);
	yy->Err[len] = 0;
}

static void success(yycontext* yy) {
	yy->Success = true;
}

static void parse_type(yycontext* yy, const char* t, Field* f) {
	// clang-format off
	f->Type = Type::Null;

	// VS 2015 /analyze gets confused here during strstr(), because we compare the output of strcmp() to 0
	IMQS_ANALYSIS_ASSUME(t != nullptr);

	switch(hash::crc32(t)) {
	case "bool"_crc32: f->Type = Type::Bool; break;
	case "text"_crc32: f->Type = Type::Text; break;
	case "int16"_crc32: f->Type = Type::Int16; break;
	case "int32"_crc32: f->Type = Type::Int32; break;
	case "int64"_crc32: f->Type = Type::Int64; break;
	case "float"_crc32: f->Type = Type::Float; break;
	case "double"_crc32: f->Type = Type::Double; break;
	case "date"_crc32: f->Type = Type::Date; break; // We could use a more coarse date representation for these fields (ie calendar day only)
	case "datetime"_crc32: f->Type = Type::Date; break;
	case "uuid"_crc32: f->Type = Type::Guid; break;
	case "bin"_crc32: f->Type = Type::Bin; break;
	case "point"_crc32: f->Type = Type::GeomPoint; break;
	case "multipoint"_crc32: f->Type = Type::GeomMultiPoint; break;
	case "polygon"_crc32: f->Type = Type::GeomPolygon; break;
	case "polyline"_crc32: f->Type = Type::GeomPolyline; break;
	case "geometry"_crc32: f->Type = Type::GeomAny; break;

	default:
		if (strstr(t, "point") == t) f->Type = Type::GeomPoint;
		if (strstr(t, "multipoint") == t) f->Type = Type::GeomMultiPoint;
		if (strstr(t, "polygon") == t) f->Type = Type::GeomPolygon;
		if (strstr(t, "polyline") == t) f->Type = Type::GeomPolyline;
		if (strstr(t, "geometry") == t) f->Type = Type::GeomAny;
		break;
	}
	// clang-format on

	if (f->Type != Type::Null) {
		if (IsTypeGeom(f->Type)) {
			std::string tt = t;
			if (strings::EndsWith(tt, "mz") || strings::EndsWith(tt, "zm")) {
				f->Flags |= TypeFlags::GeomHasM | TypeFlags::GeomHasZ;
			} else if (strings::EndsWith(tt, "m")) {
				f->Flags |= TypeFlags::GeomHasM;
			} else if (strings::EndsWith(tt, "z")) {
				f->Flags |= TypeFlags::GeomHasZ;
			}
		}
	} else {
		SetError(yy, fmt("Unrecognized field type %v", t));
	}
}

static void begin_index(yycontext* yy) {
	yy->cTable->Indexes.push_back(Index());
	yy->cIndex = &yy->cTable->Indexes.back();
}

static void add_to_index(yycontext* yy, const char* t) {
	auto f = yy->cTable->FieldByName(t);
	if (f == nullptr) {
		SetError(yy, fmt("Field '%v' does not exist, for index on table '%v'", t, yy->cTable->GetName()));
	} else {
		yy->cIndex->Fields.push_back(f->Name);
	}
}

static void done_pk(yycontext* yy) {
	yy->cIndex->IsPrimary = true;
	yy->cIndex->IsUnique  = true;
	yy->cIndex            = nullptr;
}

static void done_index(yycontext* yy) {
	yy->cIndex = nullptr;
}

static void begin_field(yycontext* yy) {
	yy->cTable->Fields.push_back(Field());
	yy->cField          = &yy->cTable->Fields.back();
	yy->cField->UIGroup = yy->FieldGroup;
	yy->cField_XD       = FieldXData::Null;
}

static void begin_group(yycontext* yy, const char* t) {
	strncpy(yy->FieldGroup, t, arraysize(yy->FieldGroup));
	yy->FieldGroup[arraysize(yy->FieldGroup) - 1] = 0;
}

static void split_by_dot(yycontext* yy, const char* t, std::string& left, std::string& right) {
	std::string x   = t;
	auto        dot = x.find('.');
	if (dot == -1) {
		SetError(yy, fmt("Internal Schema Parser Error: No dot found in %v", t));
		left  = t;
		right = "";
	} else {
		left  = x.substr(0, dot);
		right = x.substr(dot + 1);
	}
}

static void begin_hasmany(yycontext* yy, const char* t) {
	std::vector<std::string> v;
	yy->cTable->Relations.push_back(Relation());
	yy->cRelation               = &yy->cTable->Relations.back();
	yy->cRelation->Type         = RelationType::OneToMany;
	yy->cRelation->AccessorName = t;
	yy->cRelation_XD            = RelationXData::Null;
}

static void begin_hasone(yycontext* yy, const char* t) {
	yy->cTable->Relations.push_back(Relation());
	yy->cRelation               = &yy->cTable->Relations.back();
	yy->cRelation->Type         = RelationType::OneToOne;
	yy->cRelation->AccessorName = t;
	yy->cRelation_XD            = RelationXData::Null;
}

static void relation_local_field(yycontext* yy, const char* t) {
	yy->cRelation->LocalField = t;
}

static void relation_foreign_field(yycontext* yy, const char* t) {
	split_by_dot(yy, t, yy->cRelation->ForeignTable, yy->cRelation->ForeignField);
}

static void relation_xdata_key(yycontext* yy, const char* t) {
	if (ZEQ(t, "order"))
		yy->cRelation_XD = RelationXData::Order;
	else if (ZEQ(t, "module"))
		yy->cRelation_XD = RelationXData::Module;
	else
		SetError(yy, fmt("Unrecognized relationship data type '%v'", t));
}

static void relation_xdata_value(yycontext* yy, const char* t) {
	switch (yy->cRelation_XD) {
	case RelationXData::Null:
		// we've already emitted an error for this condition, in relation_xdata_key
		break;
	case RelationXData::Order:
		yy->cRelation->UIGroupOrder = t;
		break;
	case RelationXData::Module:
		yy->cRelation->UIModule = t;
		break;
	}
}

static void table_flags(yycontext* yy, const char* t) {
	// clang-format off

	// We're delaying supporting all these flags until we've had a chance
	// to rethink them, and see if we can get rid of some of them. BMH 2016-09-16

	// [BMH 2017-05-15] I am removing public_rowid, and instead making it on by
	// default. I have seen at least two developers waste many hours because they assumed
	// that rowid would be transmitted. Further comments inside Crud.cpp, where
	// you see the word "crud_public_rowid".
	//if (ZEQ(t, "public_rowid"))			yy->cTable->Tags.insert("crud_public_rowid");

	if (ZEQ(t, "hide_rowid"))			yy->cTable->Tags.insert("crud_hide_rowid");
	else if (ZEQ(t, "orm_noclient"))	yy->cTable->Tags.insert("crud_hidden");
	else if (ZEQ(t, "runtime_schema"))	yy->cTable->Tags.insert("runtime_schema");

	//if (ZEQ(t, "clientpkey"))			cTable->Flags |= Schema::TableFlagClientPKey;
	//else if (ZEQ(t, "orm_readonly"))	cTable->Flags |= Schema::TableFlagOrmReadOnly;
	//else if (ZEQ(t, "orm_server"))		cTable->Flags |= Schema::TableFlagOrmServer;
	//else if (ZEQ(t, "orm_skiperased"))	cTable->Flags |= Schema::TableFlagOrmSkipErased;
	//else
	//{
	//	SetError(fmt("Unrecognized table flag %v", t));
	//}
	// clang-format on
}

static void field_xdata_key(yycontext* yy, const char* t) {
	// clang-format off
	if (strcmp(t, "unit") == 0)				yy->cField_XD = FieldXData::Unit;
	else if (strcmp(t, "tags") == 0)		yy->cField_XD = FieldXData::Tags;
	else if (strcmp(t, "digits") == 0)		yy->cField_XD = FieldXData::Digits;
	else if (strcmp(t, "uiorder") == 0)		yy->cField_XD = FieldXData::UIOrder;
	else
	{
		yy->cField_XD = FieldXData::Null;
		SetError(yy, fmt("Unrecognized field xdata '%v'", t));
	}
	// clang-format on
}

static void field_xdata_value(yycontext* yy, const char* value) {
	switch (yy->cField_XD) {
	case FieldXData::Unit: {
		//double   dscale = 1;
		//NumUnits u      = ParseNumUnit(value, dscale);
		//if (u == NumUnitNULL)
		//	SetError(fmt("Unrecognized unit %v", value));
		//else
		//{
		//	cField->NumStyle.Unit         = u;
		//	cField->NumStyle.DisplayScale = dscale;
		//}
		yy->cField->Unit = value;
		break;
	}
	case FieldXData::Digits:
		yy->cField->UIDigits = atoi(value);
		break;
	case FieldXData::Tags: {
		vector<string> tags;
		strings::Split(value, ',', tags);
		for (const auto& tag : tags)
			yy->cField->Tags.insert(tag);
		break;
	}
	case FieldXData::UIOrder:
		yy->cField->UIOrder = atoi(value);
		break;
	case FieldXData::Null:
		break;
	}
}

static void begin_table(yycontext* yy) {
	yy->cTable        = new Table();
	yy->FieldGroup[0] = 0;
}

static void table_name(yycontext* yy, const char* t) {
	yy->cTable->SetName(t);
	yy->Out->InsertOrReplaceTable(yy->cTable);
}

static void suffix_table_start(yycontext* yy) {
	yy->SuffixSets->push_back(new SuffixSet());
}

static void add_suffix_table_original(yycontext* yy, const char* base) {
	yy->SuffixSets->back()->Originals.push_back(base);
}

static void add_suffix_table_clone(yycontext* yy, const char* base) {
	yy->SuffixSets->back()->Clones.push_back(base);
}

static Table* find_table(const std::vector<Table*>& list, const char* tableName) {
	for (Table* t : list) {
		if (t->GetName() == tableName)
			return t;
	}
	return nullptr;
}

// remap relationships for all the tables inside the suffix group.
// This doesn't do anything for relationships from, say, WaterPipe_04 to SpimInspection, which
// is a relationship that goes outside of the "Water" suffix set.
static void suffix_table_remap_relationships(const std::vector<Table*>& orgList, std::vector<Relation>& rels, const std::string& suffixName) {
	for (Relation& rel : rels) {
		Table* other = find_table(orgList, rel.ForeignTable.c_str());
		if (other)
			rel.ForeignTable = (std::string) rel.ForeignTable + "" + SuffixTableJoinChar + suffixName;
	}
}

static void version(yycontext* yy, const char* t) {
	if (ZEQ(t, "?"))
		yy->Out->Version = 0;
	else
		yy->Out->Version = atoi(t);
}

static bool ResolveInheritance(yycontext* yy, Table* parent, Table* child) {
	auto flags = parent->Flags;
	flags      = (TableFlags)((uint32_t) flags & ~(uint32_t) TableFlags::SuffixBase); // 'Base' must not inherit
	child->Flags |= flags;

	auto err = child->AddInheritedSchemaFrom(*parent);
	if (!err.OK()) {
		SetError(yy, err.Message());
		return false;
	}
	child->InheritedFrom = "";
	return true;
}

static void ResolveInheritance(yycontext* yy, DB& db) {
	auto allTables = db.TableNames();
	bool moreWork  = true;
	while (moreWork) {
		moreWork = false;
		for (const auto& childName : allTables) {
			Table* child = db.TableByName(childName);
			if (child->InheritedFrom != "") {
				Table* parent = db.TableByName(child->InheritedFrom);
				if (!parent) {
					SetError(yy, fmt("Parent table '%v' not found for child table '%v'", child->InheritedFrom.c_str(), childName));
					return;
				}

				// only fully populated parents.. this way we walk the tree properly, from the top down, breadth-first
				if (parent->InheritedFrom != "")
					continue;

				moreWork = true;

				if (!ResolveInheritance(yy, parent, child))
					return;
			}
		}
	}
}

static void DeleteAnonymousTables(DB& db) {
	// delete anonymous (abstract base class) tables
	auto all = db.TableNames();
	for (const auto& name : all) {
		auto table = db.TableByName(name);
		if (table->FriendlyName == "")
			db.RemoveTable(name);
	}
}

static void ResolveSuffixSet(yycontext* yy, SuffixSet* set) {
	std::vector<Table*> org;
	for (const auto& orgName : set->Originals) {
		org.push_back(yy->Out->TableByName(orgName));
		if (org.back() == nullptr) {
			SetError(yy, fmt("Cannot find suffix table base: '%v'", orgName));
			return;
		} else {
			org.back()->Flags |= TableFlags::SuffixBase;
		}
	}

	for (const auto& suffixName : set->Clones) {
		for (Table* orgTab : org) {
			Table* clone = new Table();
			if (!ResolveInheritance(yy, orgTab, clone))
				return;
			//clone->SuffixParent = orgTab->Name;
			//clone->SuffixName   = suffixName;
			//orgTab->SuffixLeaves += suffixName;
			clone->SetName(orgTab->GetName() + SuffixTableJoinChar + suffixName);
			clone->FriendlyName = clone->GetName();
			clone->Flags |= TableFlags::SuffixLeaf;
			clone->Relations = orgTab->Relations;
			suffix_table_remap_relationships(org, clone->Relations, suffixName);
			yy->Out->InsertOrReplaceTable(clone);
		}
	}
}

static void ResolveSuffixSets(yycontext* yy) {
	for (auto& ss : *yy->SuffixSets)
		ResolveSuffixSet(yy, ss);
}

static bool CreateInverseRelations(yycontext* yy, DB& db) {
	auto all       = db.TableNames();
	bool anyChange = false;
	for (const auto& name : all) {
		Table* tab = db.TableByName(name);
		if (!!(tab->Flags & schema::TableFlags::SuffixBase))
			continue;

		for (const Relation& rel : tab->Relations) {
			// rel: relation to invert

			// don't add inverse relation if table is related to itself
			if (rel.ForeignTable == tab->GetName())
				continue;

			// Build up the list of inverse tables.
			// Ordinarily, there is only one inverse table (ie rel.ForeignTable). However, in the case
			// of a relationship with a "suffix table" set, there are in fact multiple foreign tables.
			// Imagine the case where you define a relationship like:
			//   SewerGravity -> SpimInspection
			// The SewerGravity table is actually a "suffix base", and will get turned into 10 tables, as
			// SewerGravity_01 ..SewerGravity_09.
			// In this case, we need to make SpimInspection have a relationship with all 10 of those SewerGravity tables.
			std::vector<Table*> invTables;
			invTables.push_back(db.TableByName(rel.ForeignTable));
			if (!invTables[0]) {
				SetError(yy, fmt("Relation table %v does not exist", rel.ForeignTable.c_str()));
				continue;
			}
			if (!!(invTables[0]->Flags & TableFlags::SuffixBase)) {
				bool foundSuffix = false;
				for (const auto& ss : *yy->SuffixSets) {
					for (const auto& org : ss->Originals) {
						if (org == rel.ForeignTable) {
							foundSuffix = true;
							invTables.clear();
							// fill in invTab with "leaf" tables _01 ... _09
							for (const auto& suffix : ss->Clones) {
								auto other = db.TableByName(rel.ForeignTable + SuffixTableJoinChar + suffix);
								if (!other) {
									SetError(yy, fmt("Suffix relation table %v does not exist", rel.ForeignTable + SuffixTableJoinChar + suffix));
									continue;
								}
								invTables.push_back(other);
							}
							break;
						}
					}
					if (foundSuffix)
						break;
				}
			}

			for (auto invTab : invTables) {
				auto invRels = &invTab->Relations;

				// don't add duplicate relation
				if (invTab->FindRelation(tab->GetName().c_str(), rel.LocalField.c_str()) != -1)
					continue;

				// Fix up the optional pipe character, which is used to describe the relationship name on the
				// opposite end of the relationship. For example:
				// CREATE TABLE Mother {
				//   ...
				//   hasone mother|child mother_id childTable.id
				// }
				Relation invRel;
				auto     pipePos = rel.AccessorName.find_first_of('|');
				if (pipePos != -1)
					invRel.AccessorName = rel.AccessorName.substr(pipePos + 1);
				else
					invRel.AccessorName = rel.AccessorName;
				invRel.ForeignField = rel.LocalField;
				invRel.ForeignTable = tab->GetName();
				invRel.LocalField   = rel.ForeignField;
				if (rel.Type == RelationType::ManyToOne)
					invRel.Type = RelationType::OneToMany;
				else if (rel.Type == RelationType::OneToMany)
					invRel.Type = RelationType::ManyToOne;
				else
					invRel.Type = RelationType::OneToOne;
				invRels->push_back(invRel);
				anyChange = true;
			}
		}
	}
	return anyChange;
}

static void ResolveInverseRelations(yycontext* yy, DB& db) {
	bool moreChange = true;
	// We need to call CreateInverseRelations several times (until it stops making any change) because depending
	//on the order the tables are being treated, it might miss some relations and thus need another pass
	while (moreChange) {
		moreChange = CreateInverseRelations(yy, db);
	}
}

// See explanation of this inside CreateInverseRelations, where we search for the '|' character.
// Here we get rid of the "opposite side" name.
static void RemoveOppositeRelationNames(yycontext* yy, DB& db) {
	auto all = db.TableNames();
	for (const auto& name : all) {
		Table* tab = db.TableByName(name);

		for (auto& rel : tab->Relations) {
			auto pipePos = rel.AccessorName.find_first_of('|');
			if (pipePos != -1)
				rel.AccessorName = rel.AccessorName.substr(0, pipePos);
		}
	}
}

static void VerifyRelations(yycontext* yy, DB& db) {
	auto all = db.TableNames();
	for (const auto& name : all) {
		Table* tab = db.TableByName(name);

		// Look for any incorrect relations (that should be defined with scenario based tables but are not)
		// This happens for exemple in the WaterMasterPlanItem table in which we define relations such as:
		// hasmany Water_Pipe_Memo MPItemNo WaterPipeMemo.MP_Item_No
		// What we want to do is create a relation between the WaterMasterPlanItem and all the WaterPipeMemo_$ tables.
		// This is done previsously but we still need to remove the relation between WaterMasterPlanItemand WaterPipeMemo itself
		for (size_t i = tab->Relations.size() - 1; i != -1; i--) {
			auto& rel = tab->Relations[i];
			for (const auto& ss : *yy->SuffixSets) {
				for (const auto& org : ss->Originals) {
					if (org == rel.ForeignTable) {
						// If we find such incorrect relation, we put it at the end of goodRels and then we remove it
						auto it = std::find(tab->Relations.begin(), tab->Relations.end(), rel);
						if (it != tab->Relations.end()) {
							std::swap(*it, tab->Relations.back());
							tab->Relations.pop_back();
						}
					}
				}
			}
		}

		// This verifies that we do not end up with something like
		// A (has many) B
		// A (has one) B

		// It would be good to also detect erroneous conditions like
		// A (has many) B
		// B (has one) A       (This ought to be B (belongs to) A)
		// hmm.. ahem! I subsequently realized that this belong to business is just confusing
		// We should stick to OneToOne, OneToMany, ManyToOne

		// We DO allow more than one relationship between two tables. This first occurred when we had this:
		// Transaction: OldMeterID, NewMeterID
		// The transaction table records transactions on a customer's water connection. Sometimes a water meter
		// is replaced, in which case the transaction records both the old meter, and the new meter.
		// This means that we have two relationships between the same pair of tables.

		for (size_t i = 0; i < tab->Relations.size(); i++) {
			for (size_t j = 0; j < i; j++) {
				const auto& ri = tab->Relations[i];
				const auto& rj = tab->Relations[j];
				if (ri.ForeignTable == rj.ForeignTable && ri.Type != rj.Type) {
					SetError(yy, fmt("Conflicting or duplicate relations between tables: %v and %v", tab->GetName(), ri.ForeignTable.c_str()));
				}
			}
		}
	}
}

static Error ValidateSchema(DB& db) {
	for (const auto& tname : db.TableNames()) {
		auto tab = db.TableByName(tname);
		// I tested tables up to about 150 fields wide, and I could not justify using a hash table to
		// perform these checks. Even up to 150 fields, it was faster to just use a naive NxN check.
		for (size_t i = 0; i < tab->Fields.size(); i++) {
			for (size_t j = i + 1; j < tab->Fields.size(); j++) {
				if (strings::eqnocase(tab->Fields[i].Name, tab->Fields[j].Name))
					return Error::Fmt("Duplicate field '%v' in table '%v'", tab->Fields[i].Name, tname);
			}
		}
	}
	return Error();
}

// We don't explicitly specify a field as autoincrement, but de-facto, we have made integer primary keys
// autoincrement inside IMQS's database, so here we enforce that behaviour.
static Error PromoteIntPKeysToAutoincrement(DB& db) {
	for (const auto& name : db.TableNames()) {
		auto tab  = db.TableByName(name);
		auto pkey = tab->PrimaryKey();
		if (pkey && pkey->Fields.size() == 1) {
			auto ifield = tab->FieldIndex(pkey->Fields[0].c_str());
			if (ifield == -1)
				return Error(fmt("Indexed field %v not found in table %v", pkey->Fields[0], name));
			tab->Fields[ifield].Flags |= TypeFlags::AutoIncrement;
		}
	}
	return Error();
}

static void MakeAllGeomLatLon(DB& db) {
	for (const auto& name : db.TableNames()) {
		auto tab = db.TableByName(name);
		for (auto& f : tab->Fields) {
			if (IsTypeGeom(f.Type))
				f.SRID = geom::SRID_WGS84LatLonDegrees;
		}
	}
}

static void MarkGeometryIndexesAsSpatial(DB& db) {
	for (const auto& name : db.TableNames()) {
		auto tab = db.TableByName(name);
		for (auto& idx : tab->Indexes) {
			if (idx.Fields.size() != 1)
				continue;
			auto f = tab->FieldByName(idx.Fields[0]);
			if (f->IsTypeGeom())
				idx.IsSpatial = true;
		}
	}
}

static void DeleteSuffixBaseTables(DB& db) {
	auto names = db.TableNames();
	for (const auto& name : names) {
		auto tab = db.TableByName(name);
		if (!!(tab->Flags & TableFlags::SuffixBase)) {
			db.RemoveTable(name);
		}
	}
}

IMQS_DBA_API Error SchemaParse(const char* txt, DB& db) {
	yycontext yy;
	memset(&yy, 0, sizeof(yy));

	yy.Success       = false;
	yy.Input         = txt;
	yy.cTable        = nullptr;
	yy.cField        = nullptr;
	yy.cRelation     = nullptr;
	yy.FieldGroup[0] = 0;
	yy.Err[0]        = 0;
	yy.Out           = &db;
	yy.SuffixSets    = new std::vector<SuffixSet*>();

	Error err = Error();

	while (yyparse(&yy)) {
	}

	char errBuf[100] = {0};
	int  errLen      = std::min(yy.__limit - yy.__begin, (int) arraysize(errBuf) - 1);
	memcpy(errBuf, yy.__buf + yy.__begin, errLen);
	errBuf[errLen] = 0;

	std::string errMsg;

	if (yy.Err[0] != 0)
		errMsg = fmt("Error '%v'. ", yy.Err);
	else if (!yy.Success)
		errMsg = "Error. ";

	if (errMsg != "")
		errMsg += fmt("Possible issue around '%.30s'", errBuf);

	if (errMsg == "") {
		// In the original Albion code we would first do DeleteAnonymousTables, and then CreateInverseRelations.
		// Here we swap those two around, because when we delete anonymous tables, we're more aggressive. We delete
		// ALL anonymous tables, but the original Albion code preserved the "SuffixBase" tables. Basically it's
		// an issue for WaterMasterPlanItem, that has references to WaterPipeMemo, when it fact it ought to
		// reference WaterPipeMemo_$(water_scenario). But that's a concept that we haven't really fleshed out yet,
		// so in the interest of compatibility, we're just sticking with this for now.
		ResolveInheritance(&yy, db);
		ResolveSuffixSets(&yy);
		ResolveInverseRelations(&yy, db);
		RemoveOppositeRelationNames(&yy, db);
		DeleteAnonymousTables(db);
		VerifyRelations(&yy, db);
		auto errSecondary = PromoteIntPKeysToAutoincrement(db);
		errSecondary |= ValidateSchema(db);
		MakeAllGeomLatLon(db);
		MarkGeometryIndexesAsSpatial(db);
		DeleteSuffixBaseTables(db);
		if (yy.Err[0] != 0)
			errMsg = fmt("Error %v", yy.Err);
		else if (!errSecondary.OK())
			errMsg = errSecondary.Message();
	}
	for (SuffixSet* ss : *yy.SuffixSets)
		delete ss;
	delete yy.SuffixSets;
	yyrelease(&yy);
	if (errMsg == "")
		return Error();
	else
		return Error(errMsg);
}

IMQS_DBA_API const char* FieldTypeToSchemaFileType(const Field& f) {
	bool m  = !!(f.Flags & TypeFlags::GeomHasM);
	bool z  = !!(f.Flags & TypeFlags::GeomHasZ);
	bool zm = z && m;

	switch (f.Type) {
	case Type::Bool: return "bool";
	case Type::Int16: return "int16";
	case Type::Int32: return "int32";
	case Type::Int64: return "int64";
	case Type::Float: return "float";
	case Type::Double: return "double";
	case Type::Text: return "text";
	case Type::Guid: return "uuid";
	case Type::Date: return "datetime";
	case Type::Time: return "time";
	case Type::Bin: return "bin";
	case Type::GeomPoint: return zm ? "pointzm" : (z ? "pointz" : (m ? "pointm" : "point"));
	case Type::GeomMultiPoint: return zm ? "multipointzm" : (z ? "multipointz" : (m ? "multipointm" : "multipoint"));
	case Type::GeomPolyline: return zm ? "polylinezm" : (z ? "polylinez" : (m ? "polylinem" : "polyline"));
	case Type::GeomPolygon: return zm ? "polygonzm" : (z ? "polygonz" : (m ? "polygonm" : "polygon"));
	case Type::GeomAny: return zm ? "geometryzm" : (z ? "geometryz" : (m ? "geometrym" : "geometry"));
	default:
		IMQS_DIE();
		return "unknown field type";
	}
}
} // namespace schema
} // namespace dba
} // namespace imqs
