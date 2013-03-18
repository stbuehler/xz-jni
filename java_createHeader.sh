#/bin/bash

self=$(readlink -f "$0")
base=$(dirname "${self}")

cd "${base}"

(cd java && ant) || exit

cd lib
javah -classpath ../java/build/ de.unistuttgart.informatik.OfflineToureNPlaner.xz.XZInputStream

