#include "config.h"
#include "system.h"
#include "lang.h"
#include "menu.h"
#include "list.h"
#include "action.h"
#include "screens.h"
#include "yesno.h"
#include "settings.h"
#include "splash.h"
#include "root_menu.h"

#ifdef INNIOASIS_Y1
/* wrapper for the system fm radio */
static int fm_radio_app_func(void)
{
    system("am start -n com.mediatek.FMRadio/.FMRadioActivity");

    return 0;
}

/* FM Radio app menu item — hooked into the shared root-menu callback so
 * the "Other Items" hide/unhide flow can manage it like every other root
 * entry. */
MENUITEM_FUNCTION(fm_radio_app_item, 0, ID2P(LANG_FM_RADIO),
                  fm_radio_app_func, item_callback, Icon_Radio_screen);
#endif