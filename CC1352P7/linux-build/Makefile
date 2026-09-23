# Build en Linux de los firmwares del CC1352P7, sin Code Composer Studio.
#
#   make list              lista los firmwares disponibles
#   make airtag_scanner    compila uno
#   make all               compila todos
#   make clean             borra out/
#
# Las rutas de las herramientas están en paths.mk. Si falta el compilador,
# ejecuta ./setup.sh primero.

PROJECTS := $(sort $(basename $(notdir $(wildcard projects/*.mak))))

.PHONY: all list clean $(PROJECTS)

all: $(PROJECTS)

list:
	@echo "Firmwares disponibles:"
	@printf '  %s\n' $(PROJECTS)

$(PROJECTS):
	@$(MAKE) --no-print-directory -f common.mak PROJECT=$@

clean:
	@rm -rf out
	@echo "out/ borrado"
