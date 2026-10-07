#!/bin/bash
# Fix im_client deployment: copy missing non-system DLL imports next to the exe.
# Scan BOTH the exe(s) and all deployed DLLs (windeployqt skips compiler runtime
# and protobuf/abseil DLLs), iterating the transitive closure until it converges.
set -u
cd /c/Users/Mai/.zcode/workspace/default/im-system/client/cmake-build-win || exit 1

for round in 1 2 3 4; do
  added=0
  while IFS= read -r f; do
    for d in $(/mingw64/bin/objdump.exe -p "$f" | awk '/DLL Name:/ {print $3}'); do
      if [ ! -f "./$d" ] && [ -f "/mingw64/bin/$d" ]; then
        cp /mingw64/bin/"$d" . && echo "ADDED $d" && added=1
      fi
    done
  done < <(find . -maxdepth 2 \( -name '*.dll' -o -name '*.exe' \) -not -path './CMakeFiles/*')
  [ "$added" -eq 0 ] && break
done

echo '--- VERIFY: unresolved imports (exe dir + System32 only) ---'
while IFS= read -r f; do
  for d in $(/mingw64/bin/objdump.exe -p "$f" | awk '/DLL Name:/ {print $3}'); do
    if [ -f "./$d" ]; then continue; fi
    if [ -f "/c/Windows/System32/$d" ]; then continue; fi
    echo "UNRESOLVED $d  (imported by $f)"
  done
done < <(find . -maxdepth 2 \( -name '*.dll' -o -name '*.exe' \) -not -path './CMakeFiles/*')
echo SCAN_DONE
