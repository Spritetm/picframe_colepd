
#define ICON_NONE 0
//note: icons are bottom to top in bmp
#define ICON_BAT_EMPTY 1
#define ICON_WIFI 2
#define ICON_SERVER 3

void epd_send(const uint8_t *epddata, int icon);
void epd_shutdown();
