@echo off
rem ..\..\..\build\codegen\leg -o SchemaParserLeg.h SchemaParser.leg

rem This doesn't actually work -- I'm guessing "clang-format off" is local to a function scope or something.
rem I leave it in here because someday if we have a "format-all" script, then that script can do special parsing
rem of a file such as this. ie.. if a file starts with // clang-format off and ends with // clang-format on, then
rem ignore the entire file.
del SchemaParserLeg.h
..\..\..\build\codegen\leg -P -o tmp.h SchemaParser.leg
echo // clang-format off >> SchemaParserLeg.h
type tmp.h >> SchemaParserLeg.h
echo // clang-format on >> SchemaParserLeg.h
del tmp.h

