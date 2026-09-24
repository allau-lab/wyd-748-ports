/* Assinaturas compartilhadas. As funcoes *_C exportadas pelo Go ficam
 * declaradas no _cgo_export.h gerado — este header NAO as repete (evita
 * conflito de tipos com o cgo). gui_impl.c inclui _cgo_export.h primeiro. */
#ifndef WYD_GUI_IMPL_H
#define WYD_GUI_IMPL_H

int WydGuiMain(int argc, char **argv);
int wydgui_start(const char *hostport, const char *password);

#endif
