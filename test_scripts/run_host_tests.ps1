docker run --rm -v "${PWD}:/p" -w /p/host_test espressif/idf:release-v5.2 `
  bash -c "idf.py --preview set-target linux && idf.py build && ./build/host_test.elf"
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
