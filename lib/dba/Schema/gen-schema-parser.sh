#!/bin/bash

if ! type -P leg ; then
	set -e

	source /etc/os-release
	if [ "$NAME" == "Arch Linux" ]; then
		sudo pacman --noconfirm -Sy peg
	fi
	sudo ldconfig
fi


# peg -o SchemaParserLeg.h SchemaParser.leg

# This doesn't actually work -- I'm guessing \"clang-format off\" is local to a function scope or something.
# I leave it in here because someday if we have a \"format-all\" script, then that script can do special parsing
# of a file such as this. ie.. if a file starts with // clang-format off and ends with // clang-format on, then
# ignore the entire file.
rm -f SchemaParserLeg.h
leg -P -o tmp.h SchemaParser.leg
echo // clang-format off >> SchemaParserLeg.h
cat tmp.h >> SchemaParserLeg.h
echo // clang-format on >> SchemaParserLeg.h
rm -f tmp.h
