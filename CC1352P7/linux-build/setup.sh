#!/usr/bin/env bash
# Prepara el toolchain de TI necesario para compilar los firmwares del
# CC1352P7 en Linux. Es idempotente: lo que ya esté instalado no se toca.
#
#   ./setup.sh          instala lo que falte
#   ./setup.sh --check  sólo informa, no instala
set -euo pipefail

CGT_VERSION="3.2.2.LTS"
CGT_URL="https://dr-download.ti.com/software-development/ide-configuration-compiler-or-debugger/MD-ayxs93eZNN/${CGT_VERSION}/ti_cgt_armllvm_${CGT_VERSION}_linux-x64_installer.bin"

cd "$(dirname "$0")"
CHECK_ONLY=0
[[ "${1:-}" == "--check" ]] && CHECK_ONLY=1

# Lee del mismo paths.mk que usa el build, para no duplicar las rutas.
# paths.local.mk primero, igual que common.mak, para respetar rutas propias.
path_of() { make -f - <<<"-include paths.local.mk
include paths.mk
print:
	@echo \$($1)" print; }

SDK=$(path_of SIMPLELINK_CC13XX_CC26XX_SDK_INSTALL_DIR)
SYSCFG=$(path_of SYSCONFIG_TOOL)
CGT=$(path_of TICLANG_ARMCOMPILER)

status=0

if [[ -d "$SDK/source/ti/ble5stack" ]]; then
	echo "[ok]    SDK            $SDK"
else
	echo "[FALTA] SDK            ${SDK:-<no se encontró ninguno en \$TI_ROOT>}"
	echo "        Descarga simplelink_cc13xx_cc26xx_sdk 8.30.01.01 o posterior para"
	echo "        Linux desde ti.com e instálalo bajo ~/ti (paths.mk detecta la"
	echo "        versión), o fija la ruta en paths.local.mk"
	status=1
fi

if [[ -x "$SYSCFG" ]]; then
	echo "[ok]    SysConfig      $SYSCFG"
else
	echo "[FALTA] SysConfig      $SYSCFG"
	echo "        Viene con el instalador del SDK"
	status=1
fi

if [[ -x "$CGT/bin/tiarmclang" ]]; then
	echo "[ok]    tiarmclang     $CGT"
elif [[ $CHECK_ONLY -eq 1 ]]; then
	echo "[FALTA] tiarmclang     $CGT"
	status=1
else
	echo "[...]   tiarmclang     instalando $CGT_VERSION"
	tmp=$(mktemp -d)
	trap 'rm -rf "$tmp"' EXIT
	curl -fL --progress-bar -o "$tmp/cgt.bin" "$CGT_URL"
	chmod +x "$tmp/cgt.bin"
	# El instalador crea un subdirectorio con el nombre del producto dentro
	# de --prefix, así que se le pasa el directorio padre del que espera
	# paths.mk.
	"$tmp/cgt.bin" --mode unattended --prefix "$(dirname "$CGT")"
	if [[ -x "$CGT/bin/tiarmclang" ]]; then
		echo "[ok]    tiarmclang     $CGT"
	else
		echo "[ERROR] tiarmclang no quedó en $CGT"
		status=1
	fi
fi

echo
if [[ $status -eq 0 ]]; then
	echo "Todo listo. Prueba: make list && make airtag_scanner"
else
	echo "Faltan dependencias (ver arriba)."
fi
exit $status
