#!/bin/bash

# Copyright (c) 2015 Valentin D'Emmanuele
# Distributed under the GNU GPL v2. For full terms see the file LICENSE

echo "xSDM AutoMatic Script"
if [ ! -f src/xsdm ]; then
./configure
make
fi

if [ -f $1 ]; then
export get="$(cat $1 | cut -d'=' -f 1-)"
export domain="$(cat $1 | cut -d'/' -f 3)"
export oipu="$(wget $get -O- | grep "oiopu1" | cut -d"'" -f6)"
export fileID="$(wget $get -O- | grep "fileID1" | cut -d"'" -f6)"
export oiop="$(wget $get -O- | grep "oiop1" | cut -d"'" -f6)"
export dlSelect="$(wget $get -O- | grep "dlSelect1" | cut -d"'" -f6)"
export addr="http://$domain/WebStore/Account/SDMAuthorize.ashx?oiopu=$oipu&f=$fileID&oiop=$oiop&dl=$dlSelect"

export edv="$(wget $addr -O- | xmllint --format - | grep edv | cut -d">" -f2 | cut -d"<" -f1)"

echo $edv > "$1.key"
export fileUrl="$(wget $addr -O- | xmllint --format - | grep CDATA | cut -d"[" -f3 | cut -d"]" -f1)"

wget $fileUrl
src/xsdm $1
else
printf "Welcome on xSDM AutoMatic Script \nIt allows to download and extract files from MSDNAA/Dreamspark. You just need to pass the sdc file like ./xSDM-Auto.sh file.sdx\n"
fi
