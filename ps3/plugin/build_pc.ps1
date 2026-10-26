$ErrorActionPreference = 'Stop'
$savedTemp=$env:TEMP; $savedTmp=$env:TMP
$env:TEMP=Join-Path (Split-Path $PSScriptRoot -Parent) 'build\tmp'; $env:TMP=$env:TEMP
New-Item -ItemType Directory -Force -Path $env:TEMP | Out-Null
Push-Location $PSScriptRoot
try {
  New-Item -ItemType Directory -Force build | Out-Null
  $compilerPath = 'C:/Program Files/LLVM/bin/clang.exe'
  if (-not (Test-Path -LiteralPath $compilerPath)) { $compilerPath = (Get-Command clang -ErrorAction Stop).Source }
  & $compilerPath -std=c11 -O2 -Wall -Wextra -Wpedantic -Werror -Icore -Ikernel core/buzz.c core/websocket.c core/usb_model.c core/mailbox.c kernel/engine.c tests/test_core.c -o build/test_core.exe
  if ($LASTEXITCODE -ne 0) { throw 'C compilation failed' }
  & ./build/test_core.exe
  if ($LASTEXITCODE -ne 0) { throw 'C tests failed' }
  & $compilerPath -std=c11 -O2 -Wall -Wextra -Wpedantic -Werror -Icore -Igame core/buzz.c core/usb_model.c game/cellusbd_adapter.c game/import_hook.c tests/test_game_sequence.c -o build/test_game_sequence.exe
  if ($LASTEXITCODE -ne 0) { throw 'Game adapter compilation failed' }
  & ./build/test_game_sequence.exe
  if ($LASTEXITCODE -ne 0) { throw 'Game adapter tests failed' }
  & $compilerPath -DBUZZ_HOST_CHECK=1 -D_CRT_SECURE_NO_WARNINGS -std=c11 -Wall -Wextra -Wpedantic -Werror -Icore -Igame -Inet -fsyntax-only game/native.c
  if ($LASTEXITCODE -ne 0) { throw 'Game SPRX source syntax check failed' }
  & $compilerPath -std=c11 -O2 -Wall -Wextra -Wpedantic -Werror game/format.c tests/test_format.c -o build/test_format.exe
  if ($LASTEXITCODE -ne 0) { throw 'PRX formatter compilation failed' }
  & ./build/test_format.exe
  if ($LASTEXITCODE -ne 0) { throw 'PRX formatter tests failed' }
  python tools/embed_web.py
  if ($LASTEXITCODE -ne 0) { throw 'Asset generation failed' }
  & $compilerPath -D_CRT_SECURE_NO_WARNINGS -DBUZZ_NATIVE_FORMAT=1 -std=c11 -O2 -Wall -Wextra -Wpedantic -Werror -Icore -Inet core/buzz.c core/websocket.c net/server.c game/format.c tests/pc_server.c -lws2_32 -o build/pc_server.exe
  if ($LASTEXITCODE -ne 0) { throw 'Server compilation failed' }
  & $compilerPath -DBUZZ_HOST_CHECK=1 -DBUZZ_NATIVE_FORMAT=1 -D_CRT_SECURE_NO_WARNINGS -std=c11 -O2 -Wall -Wextra -Wpedantic -Werror -Icore -Igame -Inet core/buzz.c core/usb_model.c core/websocket.c game/cellusbd_adapter.c game/import_hook.c game/resolve_game.c game/format.c net/server.c tests/test_worker_liveness.c -lws2_32 -o build/test_worker_liveness.exe
  if ($LASTEXITCODE -ne 0) { throw 'Service liveness harness compilation failed' }
} finally { Pop-Location; $env:TEMP=$savedTemp; $env:TMP=$savedTmp }
