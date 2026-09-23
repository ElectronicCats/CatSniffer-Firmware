#!/usr/bin/env bash
#
# compile_cc1352p7.sh — Compila los firmwares del CC1352P7 de la CatSniffer v3
#
# El CC1352P7 es un ARM Cortex-M4F que se compila con el toolchain de TI
# (SimpleLink CC13xx/CC26xx SDK + SysConfig + tiarmclang), no con arduino-cli.
# Las reglas de build ya viven en CC1352P7/linux-build/; este script las
# envuelve: verifica el toolchain, lo instala si falta y lanza make.
#
# Uso:
#   ./compile_cc1352p7.sh                        # menú interactivo
#   ./compile_cc1352p7.sh -f airtag_scanner      # compila un firmware
#   ./compile_cc1352p7.sh -a                     # compila todos
#   ./compile_cc1352p7.sh -l                     # lista los firmwares disponibles
#   ./compile_cc1352p7.sh --check                # solo verifica el toolchain
#   ./compile_cc1352p7.sh --setup                # instala/prepara el toolchain
#   ./compile_cc1352p7.sh -f airtag_scanner -o ~/builds    # copia el .hex a ~/builds
#   ./compile_cc1352p7.sh -a -o ~/builds -j 8              # todos, en paralelo
#
# Opciones:
#   -f, --firmware <nombre>  Firmware a compilar (proyecto de linux-build/projects)
#   -a, --all                Compila todos los firmwares
#   -l, --list               Lista los firmwares disponibles y termina
#   -o, --out <ruta>         Copia el .hex resultante a <ruta> (archivo o directorio).
#                            Con -a la ruta debe ser un directorio.
#   -j, --jobs <n>           Trabajos paralelos de make (por defecto: 1)
#       --clean              Borra out/ antes de compilar. Sin -f ni -a, solo limpia.
#       --check              Verifica el toolchain sin instalar nada y termina
#       --setup              Instala/verifica el toolchain y termina
#   -y, --yes                No pide confirmación
#   -h, --help               Muestra esta ayuda

set -euo pipefail

# --------------------------------------------------------------------------- #
# Configuración
# --------------------------------------------------------------------------- #
# Rutas: este script vive en la raíz del repositorio; el build del CC1352P7
# está en CC1352P7/linux-build/ (Makefile + common.mak + projects/*.mak).
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$SCRIPT_DIR/CC1352P7/linux-build"
PROJECTS_DIR="$BUILD_DIR/projects"
SETUP_SH="$BUILD_DIR/setup.sh"
# REPO_DIR es lo que common.mak calcula como raíz de las fuentes del CC1352P7.
REPO_DIR="$SCRIPT_DIR/CC1352P7"

# --------------------------------------------------------------------------- #
# Colores / logging
# --------------------------------------------------------------------------- #
if [[ -t 1 ]]; then
  C_RED=$'\033[31m'; C_GRN=$'\033[32m'; C_YEL=$'\033[33m'
  C_BLU=$'\033[34m'; C_MAG=$'\033[35m'; C_CYN=$'\033[36m'
  C_BLD=$'\033[1m';  C_DIM=$'\033[2m';  C_RST=$'\033[0m'
else
  C_RED=""; C_GRN=""; C_YEL=""; C_BLU=""; C_MAG=""; C_CYN=""
  C_BLD=""; C_DIM=""; C_RST=""
fi

# Glifos Unicode si el locale es UTF-8; si no, equivalentes ASCII.
if [[ "${LC_ALL:-${LC_CTYPE:-${LANG:-}}}" == *[Uu][Tt][Ff]* ]]; then
  G_INFO="•"; G_OK="✓"; G_WARN="▲"; G_ERR="✗"; G_ARROW="➜"
  BX_H="─"; BX_TL="╭"; BX_TR="╮"; BX_BL="╰"; BX_BR="╯"; BX_V="│"
else
  G_INFO="*"; G_OK="+"; G_WARN="!"; G_ERR="x"; G_ARROW=">"
  BX_H="-"; BX_TL="+"; BX_TR="+"; BX_BL="+"; BX_BR="+"; BX_V="|"
fi

# Ancho útil de la terminal (acotado para que las cajas no queden enormes).
term_width() {
  local w="${COLUMNS:-0}"
  [[ "$w" -gt 0 ]] 2>/dev/null || w="$(tput cols 2>/dev/null || echo 80)"
  (( w > 72 )) && w=72
  (( w < 40 )) && w=40
  echo "$w"
}

info()  { printf '%s%s%s %s\n' "$C_BLU" "$G_INFO" "$C_RST" "$*"; }
ok()    { printf '%s%s%s %s\n' "$C_GRN" "$G_OK"   "$C_RST" "$*"; }
warn()  { printf '%s%s%s %s\n' "$C_YEL" "$G_WARN" "$C_RST" "$*" >&2; }
err()   { printf '%s%s%s %s\n' "$C_RED" "$G_ERR"  "$C_RST" "$*" >&2; }
die()   { err "$*"; exit 1; }

# Repite una cadena (segura para glifos multibyte, a diferencia de `tr`).
repeat_str() {
  local n="$1" s="$2" out=""
  while (( n-- > 0 )); do out+="$s"; done
  printf '%s' "$out"
}

# Regla horizontal del ancho de la terminal.
hr() {
  printf '%s%s%s\n' "$C_DIM" "$(repeat_str "$(term_width)" "$BX_H")" "$C_RST"
}

# Imprime una salida ajena (make, setup.sh) sangrada y en tenue.
indent_output() {
  local line
  while IFS= read -r line; do
    printf '%s    %s%s\n' "$C_DIM" "$line" "$C_RST"
  done
}

# Cabecera de sección: [n/N] Título, subrayado tenue.
STEP_CUR=0
STEP_TOTAL=0
step() {
  STEP_CUR=$((STEP_CUR + 1))
  printf '\n%s%s[%d/%d]%s %s%s%s\n' \
    "$C_BLD" "$C_CYN" "$STEP_CUR" "$STEP_TOTAL" "$C_RST" \
    "$C_BLD" "$*" "$C_RST"
}

# Banner de bienvenida en una caja.
banner() {
  local w title sub pad_t pad_s inner
  w="$(term_width)"
  inner=$((w - 2))
  title="CatSniffer · Compilador de firmware"
  sub="CC1352P7 · Cortex-M4F · TI Arm Clang"
  local top bot
  top="$(repeat_str "$inner" "$BX_H")"
  bot="$top"
  printf '%s%s%s%s%s\n' "$C_MAG" "$BX_TL" "$top" "$BX_TR" "$C_RST"
  # Línea de título centrada.
  pad_t=$(( (inner - ${#title}) / 2 ))
  printf '%s%s%s%*s%s%s%s%*s%s%s\n' \
    "$C_MAG" "$BX_V" "$C_RST" \
    "$pad_t" "" "$C_BLD" "$title" "$C_RST" \
    "$(( inner - ${#title} - pad_t ))" "" \
    "$C_MAG" "$BX_V$C_RST"
  # Línea de subtítulo centrada.
  pad_s=$(( (inner - ${#sub}) / 2 ))
  printf '%s%s%s%*s%s%s%s%*s%s%s\n' \
    "$C_MAG" "$BX_V" "$C_RST" \
    "$pad_s" "" "$C_DIM" "$sub" "$C_RST" \
    "$(( inner - ${#sub} - pad_s ))" "" \
    "$C_MAG" "$BX_V$C_RST"
  printf '%s%s%s%s%s\n' "$C_MAG" "$BX_BL" "$bot" "$BX_BR" "$C_RST"
}

# --------------------------------------------------------------------------- #
# Sistema (portabilidad Debian/Ubuntu/Mint/…)
# --------------------------------------------------------------------------- #
have() { command -v "$1" >/dev/null 2>&1; }

# ¿Es una distro de la familia Debian? (Debian, Ubuntu, Mint, Pop!_OS, …)
is_debian_like() {
  [[ -r /etc/os-release ]] || return 1
  local id id_like
  id="$(. /etc/os-release; echo "${ID:-}")"
  id_like="$(. /etc/os-release; echo "${ID_LIKE:-}")"
  [[ "$id" == debian || "$id" == ubuntu ]] && return 0
  [[ " $id_like " == *" debian "* || " $id_like " == *" ubuntu "* ]]
}

# Instala paquetes del sistema con apt (solo en distros Debian-like).
apt_install() {
  is_debian_like || return 1
  have apt-get || return 1
  local sudo=""; [[ "$(id -u)" -ne 0 ]] && sudo="sudo"
  info "Instalando con apt: $*"
  $sudo apt-get update -qq && $sudo apt-get install -y "$@"
}

# Lee una variable del propio paths.mk del build, para no duplicar rutas aquí.
# Incluye paths.local.mk primero, igual que common.mak, para respetar los
# ajustes locales del usuario.
mk_var() {
  local var="$1" tmp out
  tmp="$(mktemp)"
  {
    echo "-include paths.local.mk"
    echo "include paths.mk"
    echo "print:"
    printf '\t@echo $(%s)\n' "$var"
  } > "$tmp"
  out="$(make --no-print-directory -C "$BUILD_DIR" -f "$tmp" \
              REPO_DIR="$REPO_DIR" print 2>/dev/null || true)"
  rm -f "$tmp"
  printf '%s' "$out"
}

# --------------------------------------------------------------------------- #
# Opciones
# --------------------------------------------------------------------------- #
FIRMWARE=""
BUILD_ALL=0
DO_LIST=0
DO_CLEAN=0
CHECK_ONLY=0
SETUP_ONLY=0
ASSUME_YES=0
HEX_OUT=""        # ruta destino del .hex pedida con -o/--out (vacío = no copiar)
JOBS=1

usage() { awk 'NR>1 && /^#/ {sub(/^# ?/, ""); print; next} NR>1 {exit}' "${BASH_SOURCE[0]}"; }

while [[ $# -gt 0 ]]; do
  case "$1" in
    -f|--firmware) FIRMWARE="${2:-}"
                   [[ -n "$FIRMWARE" ]] || die "-f/--firmware necesita un nombre (usa -l para listar)."
                   shift 2 ;;
    -a|--all)      BUILD_ALL=1; shift ;;
    -l|--list)     DO_LIST=1; shift ;;
    -o|--out)      HEX_OUT="${2:-}"
                   [[ -n "$HEX_OUT" ]] \
                     || die "-o/--out necesita una ruta (archivo .hex o directorio destino)."
                   shift 2 ;;
    -j|--jobs)     JOBS="${2:-}"
                   [[ "$JOBS" =~ ^[0-9]+$ && "$JOBS" -ge 1 ]] \
                     || die "-j/--jobs necesita un número mayor o igual que 1."
                   shift 2 ;;
    --clean)       DO_CLEAN=1; shift ;;
    --check)       CHECK_ONLY=1; shift ;;
    --setup)       SETUP_ONLY=1; shift ;;
    -y|--yes)      ASSUME_YES=1; shift ;;
    -h|--help)     usage; exit 0 ;;
    *) die "Opción desconocida: $1 (usa -h para ayuda)" ;;
  esac
done

# Validación de combinaciones
if [[ $BUILD_ALL -eq 1 && -n "$FIRMWARE" ]]; then
  die "-a/--all y -f/--firmware son incompatibles."
fi
if [[ $CHECK_ONLY -eq 1 && $SETUP_ONLY -eq 1 ]]; then
  die "--check y --setup son incompatibles (--check no instala nada)."
fi

# El build tiene que existir: sin él no hay nada que envolver.
[[ -d "$BUILD_DIR" ]]    || die "No se encontró el build del CC1352P7 en $BUILD_DIR."
[[ -d "$PROJECTS_DIR" ]] || die "No se encontró $PROJECTS_DIR (¿repositorio incompleto?)."
[[ -x "$SETUP_SH" ]]     || die "No se encontró (o no es ejecutable) $SETUP_SH."

# --------------------------------------------------------------------------- #
# Firmwares disponibles (un projects/<nombre>.mak por firmware)
# --------------------------------------------------------------------------- #
list_firmwares() {
  local f name
  for f in "$PROJECTS_DIR"/*.mak; do
    [[ -e "$f" ]] || continue
    name="$(basename "$f" .mak)"
    echo "$name"
  done | sort
}

# Primera línea de comentario de projects/<nombre>.mak: sirve de descripción.
firmware_desc() {
  local line
  while IFS= read -r line; do
    [[ "$line" == "#"* ]] || break
    line="${line#\#}"
    line="${line# }"
    [[ -n "$line" ]] && { printf '%s' "$line"; return; }
  done < "$PROJECTS_DIR/$1.mak"
}

# Un nombre mal escrito se detecta antes de verificar el toolchain, que es lento.
if [[ -n "$FIRMWARE" && ! -f "$PROJECTS_DIR/$FIRMWARE.mak" ]]; then
  err "No existe el firmware '$FIRMWARE' (falta $PROJECTS_DIR/$FIRMWARE.mak)."
  printf '      Disponibles: %s\n' "$(list_firmwares | tr '\n' ' ')" >&2
  exit 1
fi

if [[ $DO_LIST -eq 1 ]]; then
  printf '%sFirmwares disponibles%s %s(%s)%s\n' \
    "$C_BLD" "$C_RST" "$C_DIM" "$PROJECTS_DIR" "$C_RST"
  hr
  while IFS= read -r fw; do
    printf '  %s%s%s %-22s %s%s%s\n' \
      "$C_CYN" "$G_ARROW" "$C_RST" "$fw" "$C_DIM" "$(firmware_desc "$fw")" "$C_RST"
  done < <(list_firmwares)
  hr
  exit 0
fi

# --------------------------------------------------------------------------- #
# Herramientas del sistema
# --------------------------------------------------------------------------- #
ensure_build_tools() {
  have make || {
    warn "'make' no está instalado y es imprescindible para compilar."
    apt_install make build-essential \
      || die "Instala make y vuelve a ejecutar: sudo apt-get install build-essential"
  }
  ok "make encontrado: $(make --version | head -n1)"

  # setup.sh descarga el compilador con curl; solo hace falta si no está ya.
  if ! have curl; then
    warn "'curl' no está instalado (setup.sh lo necesita para bajar tiarmclang)."
  fi
}

# --------------------------------------------------------------------------- #
# Toolchain de TI (SDK + SysConfig + tiarmclang)
# --------------------------------------------------------------------------- #
# setup.sh ya sabe comprobar e instalar; aquí solo se envuelve su salida.
# Devuelve 0 si está todo, 1 si falta algo.
run_setup() {
  local status=0 out
  out="$("$SETUP_SH" "$@" 2>&1)" || status=$?
  printf '%s\n' "$out" | indent_output
  return $status
}

ensure_toolchain() {
  info "Verificando SDK, SysConfig y tiarmclang..."
  if run_setup --check; then
    ok "Toolchain completo."
    return 0
  fi

  if [[ $CHECK_ONLY -eq 1 ]]; then
    die "Faltan componentes del toolchain (ver arriba)."
  fi

  # setup.sh instala tiarmclang solo; el SDK es un .run tras el login de ti.com.
  warn "Falta algún componente del toolchain."
  confirm "¿Intentar instalar lo que falte ahora?" \
    || die "Cancelado: el toolchain es necesario para compilar."
  if run_setup; then
    ok "Toolchain listo."
    return 0
  fi

  err "No se pudo completar el toolchain automáticamente."
  printf '\n%s    El SDK de TI no tiene descarga anónima y hay que instalarlo a mano:%s\n' \
    "$C_BLD" "$C_RST" >&2
  printf '      1) Descarga simplelink_cc13xx_cc26xx_sdk 8.30.01.01 o posterior para\n' >&2
  printf '         Linux desde https://www.ti.com/tool/SIMPLELINK-LOWPOWER-F2-SDK\n' >&2
  printf '      2) Instálalo bajo ~/ti (trae también SysConfig); paths.mk detecta\n' >&2
  printf '         la versión instalada, no hace falta que sea una concreta:\n' >&2
  printf '           chmod +x simplelink_cc13xx_cc26xx_sdk_*.run\n' >&2
  printf '           ./simplelink_cc13xx_cc26xx_sdk_*.run --mode unattended \\\n' >&2
  printf '             --prefix ~/ti/simplelink_cc13xx_cc26xx_sdk_8_33_00_16\n' >&2
  printf '      3) Si lo pusiste fuera de ~/ti, créate %s/paths.local.mk:\n' "$BUILD_DIR" >&2
  printf '           SIMPLELINK_CC13XX_CC26XX_SDK_INSTALL_DIR := /tu/ruta\n' >&2
  printf '      Más detalles en %s/README.md\n\n' "$BUILD_DIR" >&2
  exit 1
}

# --------------------------------------------------------------------------- #
# Compilación
# --------------------------------------------------------------------------- #
# Los proyectos *_p1 (CatSniffer v2, CC1352P1) compilan fuentes que no están en
# esta rama, sino en origin/v1.x/v2.x. Avisar antes de que make falle.
uses_v2_sources() { grep -q 'V2_SRC_ROOT' "$PROJECTS_DIR/$1.mak"; }

check_v2_sources() {
  local fw="$1" src
  uses_v2_sources "$fw" || return 0
  src="$(mk_var V2_SRC_ROOT)"
  [[ -n "$src" && -d "$src" ]] && return 0
  warn "'$fw' compila las fuentes de la CatSniffer v2, que no están en esta rama."
  warn "Esperadas en: ${src:-<V2_SRC_ROOT sin definir>}"
  printf '      Para traerlas como worktree:\n' >&2
  printf '        git -C %s worktree add --detach %s origin/v1.x/v2.x\n' \
    "$SCRIPT_DIR" "${src%/CC1352P2}" >&2
  confirm "¿Intentar compilar de todas formas?" \
    || die "Cancelado: faltan las fuentes de la v2."
}

# Borra out/. Pide confirmación porque ahí no solo hay objetos: out/release/
# suele guardar los .hex ya recolectados, y un .hex perdido solo se recupera
# recompilando (con el SDK instalado).
clean_out() {
  local out="$BUILD_DIR/out"
  if [[ ! -d "$out" ]]; then
    info "No hay nada que borrar: $out no existe."
    return 0
  fi
  info "Se borrará $C_DIM$out$C_RST $C_DIM($(du -sh "$out" | cut -f1))$C_RST"
  local hexes
  hexes="$(find "$out" -name '*.hex' | wc -l)"
  (( hexes > 0 )) && warn "Contiene $hexes archivo(s) .hex que se perderán."
  confirm "¿Borrar out/?" || die "Cancelado por el usuario."
  make --no-print-directory -C "$BUILD_DIR" clean | indent_output
}

# Normaliza la ruta pedida con -o/--out y prepara su directorio.
#   - directorio existente (o terminado en /) -> <dir>/<nombre>.hex
#   - con -a/--all siempre se trata como directorio
#   - ruta sin extensión                      -> se le añade .hex
# Devuelve la ruta absoluta del archivo destino.
resolve_hex_out() {
  local out="$1" base="$2" dir
  if [[ -d "$out" || "$out" == */ || $BUILD_ALL -eq 1 ]]; then
    out="${out%/}/$base"
  fi
  [[ "$out" == *.hex ]] || out+=".hex"
  dir="$(dirname "$out")"
  base="$(basename "$out")"
  mkdir -p "$dir" || die "No se pudo crear el directorio destino: $dir"
  dir="$(cd "$dir" && pwd)" || die "No se pudo acceder al directorio destino: $dir"
  echo "$dir/$base"
}

# Compila un firmware y deja en HEX_FILE la ruta del .hex generado.
HEX_FILE=""
build_firmware() {
  local fw="$1" out_dir="$BUILD_DIR/out/$fw"
  check_v2_sources "$fw"
  info "Proyecto: $C_DIM$PROJECTS_DIR/$fw.mak$C_RST"
  # make ya imprime una línea por fuente compilada; se deja pasar tal cual.
  make --no-print-directory -C "$BUILD_DIR" -j "$JOBS" "$fw" \
    || die "Falló la compilación de '$fw'."
  HEX_FILE="$(find "$out_dir" -maxdepth 1 -name '*.hex' | sort | head -n1)"
  [[ -n "$HEX_FILE" ]] || die "No se encontró ningún .hex en $out_dir tras compilar."
  ok "Compilado: $C_BLD$HEX_FILE$C_RST $C_DIM($(du -h "$HEX_FILE" | cut -f1))$C_RST"
  info "Junto al .hex quedan el .out con símbolos y el .map."
}

# --------------------------------------------------------------------------- #
# Utilidades
# --------------------------------------------------------------------------- #
confirm() {
  [[ $ASSUME_YES -eq 1 ]] && return 0
  local reply
  printf '%s%s%s %s %s[s/N]%s ' \
    "$C_YEL" "$G_ARROW" "$C_RST" "$1" "$C_DIM" "$C_RST" >&2
  read -r reply
  [[ "$reply" =~ ^([sS]|[yY])$ ]]
}

choose_firmware() {
  local fws=() i choice
  mapfile -t fws < <(list_firmwares)
  [[ ${#fws[@]} -eq 0 ]] && die "No se encontraron firmwares en $PROJECTS_DIR"
  printf '\n%s%sSelecciona el firmware a compilar%s\n' "$C_BLD" "$C_CYN" "$C_RST" >&2
  hr >&2
  for i in "${!fws[@]}"; do
    printf '  %s%2d%s%s)%s %-22s %s%s%s\n' \
      "$C_BLD$C_CYN" "$((i+1))" "$C_RST" "$C_DIM" "$C_RST" "${fws[$i]}" \
      "$C_DIM" "$(firmware_desc "${fws[$i]}")" "$C_RST" >&2
  done
  hr >&2
  printf '%s%s%s Número %s[1-%d]%s: ' \
    "$C_YEL" "$G_ARROW" "$C_RST" "$C_DIM" "${#fws[@]}" "$C_RST" >&2
  read -r choice
  [[ "$choice" =~ ^[0-9]+$ ]] && (( choice >= 1 && choice <= ${#fws[@]} )) \
    || die "Selección inválida."
  echo "${fws[$((choice-1))]}"
}

# --------------------------------------------------------------------------- #
# Flujo principal
# --------------------------------------------------------------------------- #
banner

# --clean sin firmware: solo limpiar y salir.
CLEAN_ONLY=0
if [[ $DO_CLEAN -eq 1 && $BUILD_ALL -eq 0 && -z "$FIRMWARE" ]]; then
  CLEAN_ONLY=1
fi

# Número total de pasos para los indicadores [n/N].
# Base: herramientas del sistema + toolchain de TI.
STEP_TOTAL=2
if [[ $CLEAN_ONLY -eq 1 ]]; then
  STEP_TOTAL=1                                         # solo limpiar
elif [[ $CHECK_ONLY -eq 0 && $SETUP_ONLY -eq 0 ]]; then
  [[ $DO_CLEAN -eq 1 ]] && STEP_TOTAL=$((STEP_TOTAL + 1))   # limpiar
  STEP_TOTAL=$((STEP_TOTAL + 1))                            # compilar
  [[ -n "$HEX_OUT" ]] && STEP_TOTAL=$((STEP_TOTAL + 1))     # exportar el .hex
fi

if [[ $CLEAN_ONLY -eq 1 ]]; then
  step "Limpiando out/"
  clean_out
  ok "Listo."
  exit 0
fi

step "Comprobando herramientas del sistema"; ensure_build_tools
step "Preparando el toolchain de TI";        ensure_toolchain

if [[ $CHECK_ONLY -eq 1 || $SETUP_ONLY -eq 1 ]]; then
  echo
  ok "Toolchain listo. Ejecuta el script de nuevo con -f <firmware> para compilar."
  exit 0
fi

# Elegir firmware
FIRMWARES=()
if [[ $BUILD_ALL -eq 1 ]]; then
  mapfile -t FIRMWARES < <(list_firmwares)
  [[ ${#FIRMWARES[@]} -eq 0 ]] && die "No se encontraron firmwares en $PROJECTS_DIR"
else
  [[ -z "$FIRMWARE" ]] && FIRMWARE="$(choose_firmware)"
  [[ -f "$PROJECTS_DIR/$FIRMWARE.mak" ]] \
    || die "No existe el firmware '$FIRMWARE' (falta $PROJECTS_DIR/$FIRMWARE.mak). Usa -l para listar."
  FIRMWARES=("$FIRMWARE")
fi

# Con -a/--all el destino de -o tiene que ser un directorio: hay varios .hex.
if [[ -n "$HEX_OUT" && $BUILD_ALL -eq 1 && -e "$HEX_OUT" && ! -d "$HEX_OUT" ]]; then
  die "Con -a/--all, -o/--out debe ser un directorio (recibido: $HEX_OUT)."
fi

if [[ $DO_CLEAN -eq 1 ]]; then
  step "Limpiando out/"
  clean_out
fi

# Compilar
if [[ ${#FIRMWARES[@]} -eq 1 ]]; then
  step "Compilando '${FIRMWARES[0]}'"
else
  step "Compilando ${#FIRMWARES[@]} firmwares"
fi
[[ "$JOBS" -gt 1 ]] && info "make -j$JOBS"
BUILT_HEX=()
for fw in "${FIRMWARES[@]}"; do
  build_firmware "$fw"
  BUILT_HEX+=("$HEX_FILE")
done
ok "Compilación correcta."

# Exportar el .hex a la ruta pedida con -o/--out
if [[ -n "$HEX_OUT" ]]; then
  step "Exportando el .hex"
  for hex in "${BUILT_HEX[@]}"; do
    dest="$(resolve_hex_out "$HEX_OUT" "$(basename "$hex")")"
    cp "$hex" "$dest" || die "No se pudo copiar $hex a $dest."
    ok "Exportado: $C_BLD$dest$C_RST"
  done
fi

echo
hr
info "Para flashear un .hex en la CatSniffer usa cc2538-bsl o catnip:"
info "  catnip_uploader load <archivo.hex>"
warn "Compilar no es validar: prueba en hardware antes de publicar un .hex."
