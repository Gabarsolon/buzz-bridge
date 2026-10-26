$ErrorActionPreference = 'Stop'
Push-Location $PSScriptRoot
$savedTaskPath = $env:PATH
try {
  $compilerPath = 'C:/Program Files/LLVM/bin/clang.exe'
  $env:PATH = 'C:/Program Files/LLVM/lib/clang/23/lib/windows;' + $env:PATH
  & $compilerPath -std=c11 -g -O1 '-fsanitize=address,undefined' -fno-sanitize-recover=all -Wall -Wextra -Wpedantic -Werror -Icore core/buzz.c core/websocket.c core/mailbox.c tests/fuzz_parsers.c -o build/fuzz_parsers.exe
  if ($LASTEXITCODE -ne 0) { throw 'Sanitizer compilation failed' }
  & ./build/fuzz_parsers.exe
  if ($LASTEXITCODE -ne 0) { throw 'Sanitizer run failed' }
  & $compilerPath -std=c11 -g -O1 '-fsanitize=address,undefined' -fno-sanitize-recover=all -Wall -Wextra -Wpedantic -Werror -Icore -Igame core/buzz.c core/usb_model.c game/cellusbd_adapter.c game/import_hook.c tests/test_game_sequence.c -o build/test_game_sequence_sanitize.exe
  if ($LASTEXITCODE -ne 0) { throw 'Game adapter sanitizer compilation failed' }
  & ./build/test_game_sequence_sanitize.exe
  if ($LASTEXITCODE -ne 0) { throw 'Game adapter sanitizer run failed' }
} finally { $env:PATH = $savedTaskPath; Pop-Location }
