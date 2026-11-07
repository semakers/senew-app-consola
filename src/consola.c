/* Consola serie (app de fábrica de senew): lo que llega por la UART 0 (115200 8N1) y
 * enviar texto. Es el banco de pruebas del futuro ESP32-C3: la recepción es por SONDEO cada 40 ms desde un temporizador
 * de la UI (ap_serie_leer no bloquea); sin cable, lo enviado se descarta y no cuelga nada.
 * La UART: mientras la app está abierta es SUYA (solo ella la lee). Al descargarla el sistema tira lo que quedara sin
 * leer, para que el siguiente que escuche («Recibir por serie», en Archivos) no vea la cola de la consola.
 * El historial y la línea a medio escribir vivían en estáticos del firmware y sobrevivían a salir: aquí se guardan en el
 * fichero «historial» al salir (solo si han cambiado) y se recuperan al abrir. */
#include "w5.h"
#include "w5_pantalla.h"                  /* (1.7) la letra nueva */

W5_APP(.api_menor = W5_PANT_MENOR, .nombre = "Consola", .icono_gen = W5_IC_CONSOLA, .version = 1, .caps = W5_CAP_SERIE | W5_CAP_GIRA, .monton = 2048);

#define TAM 2048
#define LINEA 80
static struct { char linea[LINEA]; char hist[TAM]; } g;   /* el fichero: la línea a medio escribir y el historial, en este orden */
static int n_hist, cambiado;
static W5Et ent;
static struct { u32 version, rx, tx; } s;

static unsigned largo(const char *t) { unsigned n = 0; while (t[n]) n++; return n; }
static void anotar(const char *t, int n)
{
    int i;
    for (i = 0; i < n; i++) {
        char ch = t[i];
        if (ch == '\r') continue;
        if ((u8)ch < 32 && ch != '\n') ch = '.';
        if (n_hist == TAM - 1) { memmove(g.hist, g.hist + TAM / 4, (size_t)(TAM - TAM / 4)); n_hist -= TAM / 4; }
        g.hist[n_hist++] = ch;
    }
    g.hist[n_hist] = 0;
    cambiado = 1;
}
static void sondeo(W5Ctx c)
{
    u8 b[64]; int n, algo = 0;
    while ((n = ap_serie_leer(b, sizeof b)) > 0) { anotar((const char *)b, n); s.rx += (u32)n; algo = 1; if (n < (int)sizeof b) break; }
    if (algo) { s.version++; ui_sucio(c); }
}
static void iniciar(W5Ctx c, void *dato)
{
    (void)dato;
    s.rx = s.tx = 0;
    if (!ent && (ent = et_nueva(LINEA)) != NULL) et_poner(ent, g.linea);
    ui_cada(c, 40, sondeo);
}
static void desechar(W5Ctx c, void *dato)
{
    (void)c; (void)dato;
    if (ent) { const char *t = et_texto(ent); unsigned n = largo(t); if (n >= LINEA) n = LINEA - 1;
               if (memcmp(g.linea, t, n + 1)) { memcpy(g.linea, t, n); g.linea[n] = 0; cambiado = 1; } }
}
static void enviar(W5Ctx c, const char *t)
{
    int n = (int)largo(t), m, a, b;
    a = ap_serie_escribir(t, (unsigned)n); b = ap_serie_escribir("\r\n", 2);
    m = (a > 0 ? a : 0) + (b > 0 ? b : 0);
    anotar("> ", 2); anotar(t, n);
    anotar(m ? "\n" : "  [sin cable: descartado]\n", m ? 1 : 26);
    s.tx += (u32)m; s.version++;
    ui_sucio(c);
}
static void opcion(int i, void *d)
{
    W5Ctx c = d; W5Fecha f;
    if (i == 0) enviar(c, "AT");
    else if (i == 1) { ap_fecha(&f); enviar(c, ui_fmt("HORA %02d:%02d:%02d", f.hora, f.min, f.seg)); }
    else if (i == 2) { n_hist = 0; g.hist[0] = 0; cambiado = 1; s.version++; ui_sucio(c); }
}
static int tecla(W5Ctx t, int k, void *dato)
{
    static const char *const ops[] = { "Enviar «AT»", "Enviar la hora", "Limpiar la pantalla" };
    W5Ctx c = ui_mi_pantalla(t);
    (void)dato;
    if (k == W5K_OK) {
        if (ent && et_len(ent)) {                    /* copia: enviar() no debe leer del búfer que se va a vaciar */
            char l[LINEA]; const char *e = et_texto(ent); unsigned n = largo(e);
            if (n >= LINEA) n = LINEA - 1;
            memcpy(l, e, n); l[n] = 0;
            enviar(c, l); et_poner(ent, "");
        }
        return 1;
    }
    if (k == W5K_SEL_IZQ) { ui_opciones("Consola", ops, 3, opcion, c); return 1; }
    if (k == W5K_SEL_DER) return ui_teclas_defecto(t, W5K_SEL_DER);
    return 0;
}
static void pintar(W5Lienzo l, const W5Rect *rr, void *dato)
{
    W5Rect r = *rr;
    unsigned acento = ui_color(W5_COL_ACENTO), texto = ui_color(W5_COL_TEXTO);
    int alto = ui_paso_linea(W5_F_MONO), caben, n = 0, p = 0, i, ini[64];   /* (1.7) el paso del 5x7 monoespaciado en uso */
    (void)dato;
    if (alto <= 0) alto = 10;
    caben = r.h / alto;
    while (p < n_hist) {                                     /* parte todo y se queda con las últimas líneas */
        int paso = li_partir(g.hist + p, r.w - 6, 1, W5_F_MONO);
        ini[n % 64] = p; n++;
        p += paso > 0 ? paso : 1;
    }
    if (caben > 60) caben = 60;
    for (i = n > caben ? n - caben : 0; i < n; i++) {
        int a = ini[i % 64], b = i + 1 < n ? ini[(i + 1) % 64] : n_hist;
        while (b > a && (g.hist[b - 1] == '\n' || g.hist[b - 1] == ' ')) b--;
        li_texto_n(l, r.x + 3, r.y + (i - (n > caben ? n - caben : 0)) * alto, g.hist + a, b - a,
                   g.hist[a] == '>' ? acento : texto, 1, W5_F_MONO);
    }
    if (!n_hist) li_texto(l, r.x + 3, r.y + 2, "(nada recibido todavía)", ui_color(W5_COL_TENUE), 1, 0);
}
static void cambio(W5Ctx x, void *d) { (void)x; (void)d; }
static W5W construir(W5Ctx c, void *dato)
{
    int con = ap_serie_conectado() == 1;
    (void)c; (void)dato;
    return Teclas(tecla, ui_andamio("Consola serie", Columna(A_CRUZADO(W5P_ESTIRAR), HIJOS(
        Caja(A_AN(W5_INF), A_COLOR(ui_color(W5_COL_BARRA)), A_HIJO(RellenoXY(4, 2, Fila(HIJOS(
            Texto(con ? "UART 0 · con cable" : "UART 0 · SIN CABLE", A_COLOR(ui_color(con ? W5_COL_TEXTO : W5_COL_ACENTO)), A_MARCAS(W5_F_ESTILO(W5_T_PEQUENA))),
            Hueco(), Texto(ui_fmt("↓%u ↑%u", s.rx, s.tx), A_COLOR(ui_color(W5_COL_TENUE)), A_MARCAS(W5_F_ESTILO(W5_T_PEQUENA)))))))),
        Expandido(Pintura(pintar, A_VERSION(s.version))),
        ent ? Relleno(2, CampoTexto(ent, A_LINEAS(1), A_AUTOFOCO(1), A_AL_CAMBIAR(cambio))) : NULL)),
        "Opciones", "Atrás"));
}
static void cargar(void)
{
    int t = ap_fich_tam("historial");
    if (t < LINEA || t > (int)sizeof g) return;
    if (ap_fich_leer("historial", 0, &g, (unsigned)t) != t) { memset(&g, 0, sizeof g); return; }
    g.linea[LINEA - 1] = 0;
    n_hist = t - LINEA;
    if (n_hist >= TAM) n_hist = TAM - 1;
    g.hist[n_hist] = 0;
}
W5Pal app_suceso(int suceso, W5Pal a, W5Pal b)
{
    (void)a; (void)b;
    if (suceso == W5_ABRIR) { cargar(); return (W5Pal)ui_pantalla(construir, iniciar, desechar, NULL); }
    if (suceso == W5_SALIR && cambiado) { ap_fich_escribir("historial", &g, (unsigned)(LINEA + n_hist)); cambiado = 0; }
    return 0;
}
